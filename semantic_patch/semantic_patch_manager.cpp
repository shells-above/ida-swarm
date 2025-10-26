#include "semantic_patch_manager.h"
#include "core/ida_utils.h"
#include "core/logger.h"
#include <hexrays.hpp>
#include <ida.hpp>
#include <funcs.hpp>
#include <sstream>
#include <iomanip>
#include <random>
#include <filesystem>
#include <fstream>

namespace llm_re::semantic {

// Forward declaration
static std::string find_llvm_objcopy();

SemanticPatchManager::SemanticPatchManager(
    PatchManager* patch_manager,
    CodeInjectionManager* code_injection_manager)
    : patch_manager_(patch_manager)
    , code_injection_manager_(code_injection_manager)
    , compiler_(std::make_unique<CCompiler>())
    , convention_analyzer_(std::make_unique<CallingConventionAnalyzer>()) {
}

SemanticPatchManager::~SemanticPatchManager() = default;

// Stage 1: Start semantic patch session
StartPatchResult SemanticPatchManager::start_semantic_patch(ea_t function_address) {
    return IDAUtils::execute_sync_wrapper([&]() -> StartPatchResult {
        StartPatchResult result;

        // Verify it's a function
        func_t* func = get_func(function_address);
        if (!func) {
            result.success = false;
            result.error_message = "Address 0x" + std::to_string(function_address) +
                                  " is not a function";
            return result;
        }

        // Decompile function
        std::string decompiled;
        try {
            decompiled = decompile_function(function_address);
        } catch (const std::exception& e) {
            result.success = false;
            result.error_message = std::string("Decompilation failed: ") + e.what();
            return result;
        }

        if (decompiled.empty()) {
            result.success = false;
            result.error_message = "Decompilation produced no output";
            return result;
        }

        // Detect calling convention
        CallingConvention conv = convention_analyzer_->analyze_function(function_address);

        // Create session with deterministic ID based on function address
        SemanticPatchSession session;
        session.session_id = generate_session_id(function_address);
        session.original_function = function_address;
        session.decompiled_code = decompiled;
        session.detected_convention = conv;
        session.created_at = std::chrono::system_clock::now();
        session.last_updated = session.created_at;

        // Store session
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            sessions_[session.session_id] = session;
        }

        // Build result
        result.success = true;
        result.session_id = session.session_id;
        result.function_address = function_address;
        result.decompiled_code = decompiled;
        result.detected_convention = conv;

        return result;
    }, MFF_READ);
}

// Stage 2: Compile replacement code
CompileResult SemanticPatchManager::compile_replacement(const std::string& session_id, const std::string& c_code, int max_iterations) {
    CompileResult result;

    // Get session
    SemanticPatchSession* session = get_session(session_id);
    if (!session) {
        result.success = false;
        result.error_message = "Invalid session ID: " + session_id;
        return result;
    }

    // Get architecture from analyzer (already cached)
    std::string arch = convention_analyzer_->get_architecture();

    // Compile with symbol and type resolution
    std::vector<ResolvedSymbol> resolved_symbols;
    std::vector<ResolvedType> resolved_types;
    std::string final_c_code;

    CompilationAttempt attempt = compiler_->compile_with_symbol_resolution(
        c_code,
        arch,
        session->detected_convention,
        resolved_symbols,
        resolved_types,
        final_c_code,
        max_iterations
    );

    if (!attempt.success) {
        result.success = false;
        result.error_message = attempt.output;
        return result;
    }

    // Also compile to object file for machine code extraction
    // This uses the final_c_code with all symbols resolved
    std::string object_error;
    std::string object_path = compiler_->compile_to_object_file(
        final_c_code,
        arch,
        session->detected_convention,
        object_error
    );

    if (object_path.empty()) {
        result.success = false;
        result.error_message = "Assembly compilation succeeded but object file generation failed:\n" + object_error;
        return result;
    }

    LOG("Generated object file for semantic patching: %s\n", object_path.c_str());

    // Update session
    session->compiled = true;
    session->compiled_assembly = attempt.output;
    session->compiled_object_path = object_path;
    session->final_c_code = final_c_code;
    session->resolved_symbols.clear();
    for (const auto& sym : resolved_symbols) {
        session->resolved_symbols.push_back(sym.name);
    }
    // Also store resolved type names (optional - for debugging/info)
    for (const auto& type : resolved_types) {
        session->resolved_symbols.push_back("type:" + type.name);
    }
    session->last_updated = std::chrono::system_clock::now();

    // Build result
    result.success = true;
    result.compiled_assembly = attempt.output;
    result.resolved_symbols = session->resolved_symbols;
    result.final_c_code = final_c_code;

    return result;
}

// Stage 3: Preview replacement
PreviewResult SemanticPatchManager::preview_semantic_patch(const std::string& session_id) {
    return IDAUtils::execute_sync_wrapper([&]() -> PreviewResult {
        PreviewResult result;

        // Get session
        SemanticPatchSession* session = get_session(session_id);
        if (!session) {
            result.success = false;
            result.error_message = "Invalid session ID: " + session_id;
            return result;
        }

        if (!session->compiled) {
            result.success = false;
            result.error_message = "Must call compile_replacement before preview";
            return result;
        }

        // Get original function's assembly
        func_t* func = get_func(session->original_function);
        if (!func) {
            result.success = false;
            result.error_message = "Original function no longer exists";
            return result;
        }

        // Generate disassembly of original function
        std::stringstream original_asm;
        ea_t ea = func->start_ea;

        while (ea < func->end_ea) {
            qstring line;
            if (!generate_disasm_line(&line, ea, 0)) break;

            original_asm << "0x" << std::hex << ea << std::dec << ": " << line.c_str() << "\n";

            ea = next_head(ea, func->end_ea);
        }

        // Analyze compiled code's calling convention
        std::string arch = inf_is_64bit() ? "x86_64" : "x86";
        CallingConvention compiled_conv = convention_analyzer_->analyze_assembly(
            session->compiled_assembly,
            arch
        );

        // Check compatibility
        bool compatible = session->detected_convention.is_compatible_with(compiled_conv);

        // Generate warnings
        std::vector<std::string> warnings;
        if (!compatible) {
            warnings.emplace_back("WARNING: Calling conventions are INCOMPATIBLE!");
            warnings.emplace_back("Original: " + session->detected_convention.to_string());
            warnings.emplace_back("Compiled: " + compiled_conv.to_string());
            warnings.emplace_back("DO NOT FINALIZE - this will break the program!");
        }

        // Build analysis
        std::stringstream analysis;
        analysis << "Original Function Convention: " << session->detected_convention.to_string() << "\n";
        analysis << "Compiled Code Convention: " << compiled_conv.to_string() << "\n";
        analysis << "ABI Compatible: " << (compatible ? "YES" : "NO") << "\n";
        analysis << "\n";
        if (!session->resolved_symbols.empty()) {
            analysis << "Resolved Symbols:\n";
            for (const auto& sym : session->resolved_symbols) {
                analysis << "  - " << sym << "\n";
            }
        }

        // Update session
        session->previewed = true;
        session->compiled_convention = compiled_conv;
        session->abi_compatible = compatible;
        session->warnings = warnings;
        session->last_updated = std::chrono::system_clock::now();

        // Build result
        result.success = true;
        result.original_assembly = original_asm.str();
        result.new_assembly = session->compiled_assembly;
        result.original_convention = session->detected_convention;
        result.new_convention = compiled_conv;
        result.abi_compatible = compatible;
        result.warnings = warnings;
        result.analysis = analysis.str();

        return result;
    }, MFF_READ);
}

// Helper: Clean up object file from session
static void cleanup_object_file(SemanticPatchSession* session) {
    if (session && !session->compiled_object_path.empty() &&
        std::filesystem::exists(session->compiled_object_path)) {
        std::filesystem::remove(session->compiled_object_path);
        LOG("Cleaned up object file: %s\n", session->compiled_object_path.c_str());
        session->compiled_object_path.clear();
    }
}

// Stage 4: Finalize replacement
FinalizeResult SemanticPatchManager::finalize_semantic_patch(const std::string& session_id) {
    return IDAUtils::execute_sync_wrapper([&]() -> FinalizeResult {
        FinalizeResult result;

        // Get session
        SemanticPatchSession* session = get_session(session_id);
        if (!session) {
            result.success = false;
            result.error_message = "Invalid session ID: " + session_id;
            return result;
        }

        if (!session->compiled) {
            result.success = false;
            result.error_message = "Must compile_replacement before finalizing";
            return result;
        }

        if (!session->previewed) {
            cleanup_object_file(session);
            result.success = false;
            result.error_message = "Must preview_semantic_patch before finalizing";
            return result;
        }

        if (!session->abi_compatible) {
            cleanup_object_file(session);
            result.success = false;
            result.error_message = "Cannot finalize: ABI is INCOMPATIBLE. "
                                  "This would break the program.";
            return result;
        }

        // Step 1: Extract machine code from object file
        // Object file was generated during compile_replacement() with all symbols resolved
        AssembleResult assemble_result = extract_machine_code_from_object(
            session->compiled_object_path
        );

        if (!assemble_result.success) {
            cleanup_object_file(session);
            result.success = false;
            result.error_message = "Assembly failed: " + assemble_result.error_message;
            return result;
        }

        size_t code_size = assemble_result.machine_code.size();

        // Step 2: Allocate temporary workspace for the assembled code
        WorkspaceAllocation workspace = code_injection_manager_->allocate_code_workspace(code_size);
        if (!workspace.success) {
            cleanup_object_file(session);
            result.success = false;
            result.error_message = "Failed to allocate temp workspace: " + workspace.error_message;
            return result;
        }

        // Step 3: Write assembled code to temporary workspace
        if (!patch_manager_->write_bytes(workspace.temp_segment_ea, assemble_result.machine_code)) {
            cleanup_object_file(session);
            result.success = false;
            result.error_message = "Failed to write assembled code to temp workspace";
            return result;
        }

        ea_t temp_end = workspace.temp_segment_ea + code_size;

        // Step 4: Preview the code injection (required by finalize_code_injection)
        CodePreviewResult preview = code_injection_manager_->preview_code_injection(
            workspace.temp_segment_ea,
            temp_end
        );

        if (!preview.success) {
            cleanup_object_file(session);
            result.success = false;
            result.error_message = "Preview failed: " + preview.error_message;
            return result;
        }

        // Step 5: Finalize injection - relocates to permanent location (code cave or new segment)
        CodeFinalizationResult finalize = code_injection_manager_->finalize_code_injection(
            workspace.temp_segment_ea,
            temp_end
        );

        if (!finalize.success) {
            cleanup_object_file(session);
            result.success = false;
            result.error_message = "Finalization failed: " + finalize.error_message;
            return result;
        }

        ea_t permanent_address = finalize.new_permanent_address;

        // Step 6: Patch original function with JMP to PERMANENT address
        bool patch_success = patch_function_with_jump(session->original_function, permanent_address);
        if (!patch_success) {
            cleanup_object_file(session);
            result.success = false;
            result.error_message = "Failed to patch JMP redirect. "
                                  "Code was injected at 0x" + std::to_string(permanent_address) +
                                  " but original function was not redirected.";
            // Note: Code injection is already tracked by PatchManager, can be reverted
            return result;
        }

        // Step 7: Create function at permanent address
        // Now that code is analyzed (by CIM), create it as a function so agents can analyze it
        LOG("SemanticPatchManager: Creating function at permanent address 0x%llX\n", (uint64_t)permanent_address);

        if (add_func(permanent_address, BADADDR)) {
            LOG("SemanticPatchManager: Successfully created function at 0x%llX\n", (uint64_t)permanent_address);
        } else {
            // Not a critical failure - function creation is best-effort
            // Code is still injected and working
            LOG("WARNING: Failed to auto-create function at 0x%llX\n", (uint64_t)permanent_address);
            LOG("         Agent may need to press 'p' manually or use IDA's function analysis\n");
        }

        // Wait for IDA to finish processing
        auto_wait();

        // Update session
        session->finalized = true;
        session->injected_address = permanent_address;
        session->last_updated = std::chrono::system_clock::now();

        // Clean up object file (no longer needed after successful injection)
        cleanup_object_file(session);

        // Build result
        std::string patch_instruction = generate_jump_instruction(
            session->original_function,
            permanent_address
        );

        result.success = true;
        result.original_function = session->original_function;
        result.new_function_address = permanent_address;
        result.patch_instruction = patch_instruction;

        LOG("SemanticPatchManager: Successfully patched function at 0x%llX to jump to 0x%llX\n",
            (uint64_t)session->original_function, (uint64_t)permanent_address);
        LOG("SemanticPatchManager: Method: %s, Code size: %zu bytes\n",
            finalize.relocation_method.c_str(), code_size);

        return result;
    }, MFF_WRITE);
}

// Session management

bool SemanticPatchManager::has_session(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.find(session_id) != sessions_.end();
}

void SemanticPatchManager::cancel_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    // Clean up object file if it exists
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        cleanup_object_file(&it->second);
    }

    sessions_.erase(session_id);
}

std::vector<std::string> SemanticPatchManager::get_active_sessions() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::vector<std::string> ids;
    for (const auto& [id, session] : sessions_) {
        ids.push_back(id);
    }
    return ids;
}

// Private helpers

std::string SemanticPatchManager::generate_session_id(ea_t function_address) {
    // Generate deterministic session ID based on function address
    // This ensures that replaying tool calls from agents to main database
    // will create the same session ID for the same function
    std::stringstream ss;
    ss << "semantic_patch_0x" << std::hex << function_address;
    return ss.str();
}

SemanticPatchSession* SemanticPatchManager::get_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::string SemanticPatchManager::decompile_function(ea_t func_addr) {
    // Initialize Hex-Rays if needed
    if (!init_hexrays_plugin()) {
        throw std::runtime_error("Hex-Rays decompiler not available");
    }

    // Get function pointer - REQUIRED by decompile() API
    func_t *func = get_func(func_addr);
    if (!func) {
        throw std::runtime_error("Cannot get function at address 0x" +
                                std::to_string(func_addr));
    }

    // Decompile function - MUST pass func_t* not ea_t
    hexrays_failure_t hf;
    cfuncptr_t cfunc = decompile(func, &hf, DECOMP_NO_WAIT | DECOMP_NO_CACHE);

    if (!cfunc) {
        std::string error = "Decompilation failed";
        if (hf.str.length() > 0) {
            error += ": " + std::string(hf.str.c_str());
        }
        throw std::runtime_error(error);
    }

    // Get pseudocode using qstring_printer_t - CORRECT IDA SDK API
    qstring pseudocode;
    qstring_printer_t printer(cfunc, pseudocode, false);  // false = no tags
    cfunc->print_func(printer);

    return pseudocode.c_str();
}

std::string SemanticPatchManager::generate_far_jump_for_architecture(ea_t from, ea_t to) {
    std::stringstream ss;

    if (PH.id == PLFM_386) {
        // x86/x64
        if (inf_is_64bit()) {
            // x86-64: mov rax, ADDRESS; jmp rax (12 bytes)
            ss << "mov rax, 0x" << std::hex << to << "; jmp rax";
        } else {
            // x86-32: mov eax, ADDRESS; jmp eax (7 bytes)
            ss << "mov eax, 0x" << std::hex << to << "; jmp eax";
        }
    }
    else if (PH.id == PLFM_ARM) {
        if (inf_is_64bit()) {
            // ARM64: movz/movk sequence + br (20 bytes total, needs optimization)
            // For simplicity, use ldr + literal pool approach
            uint64_t addr = to;
            ss << "movz x16, #0x" << std::hex << (addr & 0xFFFF) << "; ";
            ss << "movk x16, #0x" << std::hex << ((addr >> 16) & 0xFFFF) << ", lsl #16; ";
            ss << "movk x16, #0x" << std::hex << ((addr >> 32) & 0xFFFF) << ", lsl #32; ";
            ss << "movk x16, #0x" << std::hex << ((addr >> 48) & 0xFFFF) << ", lsl #48; ";
            ss << "br x16";
        } else {
            // ARM32: Use pseudo-instruction that Keystone will expand
            ss << "ldr r12, =" << std::hex << to << "; bx r12";
        }
    }
    else if (PH.id == PLFM_PPC) {
        // PowerPC: lis/ori/mtctr/bctr (16 bytes)
        uint32_t addr = (uint32_t)to;
        uint32_t high = (addr >> 16) & 0xFFFF;
        uint32_t low = addr & 0xFFFF;
        ss << "lis r12, 0x" << std::hex << high << "; ";
        ss << "ori r12, r12, 0x" << std::hex << low << "; ";
        ss << "mtctr r12; bctr";
    }
    else if (PH.id == PLFM_MIPS) {
        // MIPS: lui/ori/jr/nop (16 bytes)
        uint32_t addr = (uint32_t)to;
        uint32_t high = (addr >> 16) & 0xFFFF;
        uint32_t low = addr & 0xFFFF;
        ss << "lui $t0, 0x" << std::hex << high << "; ";
        ss << "ori $t0, $t0, 0x" << std::hex << low << "; ";
        ss << "jr $t0; nop";
    }
    else if (PH.id == PLFM_SPARC) {
        // SPARC: sethi/or/jmpl/nop (16 bytes)
        uint32_t addr = (uint32_t)to;
        uint32_t high = (addr >> 10) & 0x3FFFFF;
        uint32_t low = addr & 0x3FF;
        ss << "sethi 0x" << std::hex << high << ", %g1; ";
        ss << "or %g1, 0x" << std::hex << low << ", %g1; ";
        ss << "jmpl %g1, %g0; nop";
    }
    else {
        LOG("ERROR: Unsupported architecture (PH.id=%d) for far jump generation\n", PH.id);
        const char *proc_name = (PH.plnames && PH.plnames[0]) ? PH.plnames[0] : "unknown";
        LOG("       Processor: %s\n", proc_name);
        return "";
    }

    return ss.str();
}

bool SemanticPatchManager::patch_function_with_jump(ea_t original_func, ea_t new_func) {
    LOG("Creating function redirect from 0x%llX to 0x%llX\n", (uint64_t)original_func, (uint64_t)new_func);

    // Step 1: Generate architecture-specific far jump
    std::string jump_asm = generate_far_jump_for_architecture(original_func, new_func);
    if (jump_asm.empty()) {
        LOG("ERROR: Failed to generate jump for architecture\n");
        return false;
    }

    LOG("Generated jump instruction: %s\n", jump_asm.c_str());

    // Step 2: Assemble the jump instruction
    auto [success, jump_bytes] = patch_manager_->assemble_instruction(jump_asm, original_func);
    if (!success || jump_bytes.empty()) {
        LOG("ERROR: Failed to assemble jump instruction: %s\n", jump_asm.c_str());
        return false;
    }

    LOG("Assembled jump to %zu bytes\n", jump_bytes.size());

    // Step 3: Pad to 16 bytes with NOPs if needed
    const size_t REDIRECT_SIZE = 16;
    if (jump_bytes.size() > REDIRECT_SIZE) {
        LOG("ERROR: Jump instruction too large: %zu bytes (max: %zu)\n",
            jump_bytes.size(), REDIRECT_SIZE);
        return false;
    }

    // Get architecture-appropriate NOPs from PatchManager
    if (jump_bytes.size() < REDIRECT_SIZE) {
        size_t nop_count = REDIRECT_SIZE - jump_bytes.size();
        std::vector<uint8_t> nops = patch_manager_->get_nop_bytes(nop_count);
        jump_bytes.insert(jump_bytes.end(), nops.begin(), nops.end());
        LOG("Padded with %zu NOP bytes to reach %zu bytes total\n", nop_count, REDIRECT_SIZE);
    }

    // Step 4: Read original 16 bytes
    std::vector<uint8_t> original_bytes(REDIRECT_SIZE);
    bool read_success = IDAUtils::execute_sync_wrapper([&]() -> bool {
        for (size_t i = 0; i < REDIRECT_SIZE; i++) {
            original_bytes[i] = get_byte(original_func + i);
        }
        return true;
    }, MFF_READ);

    if (!read_success) {
        LOG("ERROR: Failed to read original bytes at 0x%llX\n", (uint64_t)original_func);
        return false;
    }

    // Step 5: Apply byte patch
    BytePatchResult result = patch_manager_->apply_byte_patch(
        original_func,
        PatchManager::bytes_to_hex_string(original_bytes),
        PatchManager::bytes_to_hex_string(jump_bytes),
        "Semantic patch: redirect to compiled replacement at 0x" +
        std::to_string(new_func)
    );

    if (!result.success) {
        LOG("ERROR: Failed to apply redirect patch: %s\n", result.error_message.c_str());
        return false;
    }

    LOG("Successfully redirected function at 0x%llX to 0x%llX (%zu bytes patched)\n",
        (uint64_t)original_func, (uint64_t)new_func, REDIRECT_SIZE);
    return true;
}

std::string SemanticPatchManager::generate_jump_instruction(ea_t from, ea_t to) {
    if (inf_is_64bit()) {
        // x86-64: JMP rel32
        int64_t offset = (int64_t)to - ((int64_t)from + 5);

        if (offset >= INT32_MIN && offset <= INT32_MAX) {
            // Can use 5-byte relative JMP
            std::stringstream ss;
            ss << "jmp 0x" << std::hex << to;
            return ss.str();
        } else {
            // Need indirect JMP through register
            // This is more complex - would need multiple instructions
            return ""; // Not supported yet
        }
    } else {
        // x86-32: JMP rel32
        std::stringstream ss;
        ss << "jmp 0x" << std::hex << to;
        return ss.str();
    }
}

SemanticPatchManager::AssembleResult SemanticPatchManager::extract_machine_code_from_object(const std::string& object_path) {
    AssembleResult result;
    result.success = false;

    // Verify object file exists
    if (!std::filesystem::exists(object_path)) {
        result.error_message = "Object file not found: " + object_path;
        LOG("ERROR: Object file not found: %s\n", object_path.c_str());
        return result;
    }

    // Find llvm-objcopy executable
    std::string llvm_objcopy = find_llvm_objcopy();
    if (llvm_objcopy.empty()) {
        result.error_message = "llvm-objcopy not found. Semantic patching requires LLVM tools.\n\n"
                              "Install LLVM:\n"
                              "  macOS:   brew install llvm\n"
                              "  Linux:   apt install llvm  (or yum install llvm-toolset)\n"
                              "  Windows: Download from llvm.org\n\n"
                              "After installation, llvm-objcopy should be in your PATH or at:\n"
                              "  /opt/homebrew/opt/llvm/bin/llvm-objcopy (macOS Homebrew)\n"
                              "  /usr/bin/llvm-objcopy (Linux)";
        LOG("ERROR: llvm-objcopy not found. Cannot extract machine code from object file.\n");
        return result;
    }

    // Create temporary output file for extracted .text section
    std::string temp_bin = object_path + ".text.bin";

    // Build llvm-objcopy command
    // --dump-section extracts a specific section to a binary file
    std::stringstream cmd;
    cmd << "\"" << llvm_objcopy << "\" --dump-section=.text=\"" << temp_bin << "\" \"" << object_path << "\" 2>&1";

    LOG("Executing: %s\n", cmd.str().c_str());

    // Execute llvm-objcopy
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        result.error_message = "Failed to execute llvm-objcopy";
        LOG("ERROR: Failed to execute llvm-objcopy\n");
        return result;
    }

    // Read output (for error messages)
    std::string output;
    char buffer[256];
    while (qfgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    int exit_code = pclose(pipe);

    if (exit_code != 0) {
        result.error_message = "llvm-objcopy failed with exit code " + std::to_string(exit_code);
        if (!output.empty()) {
            result.error_message += ":\n" + output;
        }
        LOG("ERROR: llvm-objcopy failed: %s\n", output.c_str());
        return result;
    }

    // Verify output file was created
    if (!std::filesystem::exists(temp_bin)) {
        result.error_message = "llvm-objcopy did not create output file: " + temp_bin;
        LOG("ERROR: llvm-objcopy did not create output file\n");
        return result;
    }

    // Read the binary file containing .text section
    std::ifstream file(temp_bin, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.error_message = "Failed to open extracted binary file: " + temp_bin;
        LOG("ERROR: Failed to open extracted binary file\n");
        std::filesystem::remove(temp_bin);  // Clean up
        return result;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size == 0) {
        result.error_message = "Extracted .text section is empty";
        LOG("ERROR: Extracted .text section is empty\n");
        file.close();
        std::filesystem::remove(temp_bin);
        return result;
    }

    // Read machine code bytes
    result.machine_code.resize(size);
    if (!file.read(reinterpret_cast<char*>(result.machine_code.data()), size)) {
        result.error_message = "Failed to read machine code from extracted binary";
        LOG("ERROR: Failed to read machine code bytes\n");
        file.close();
        std::filesystem::remove(temp_bin);
        return result;
    }

    file.close();

    // Clean up temporary file
    std::filesystem::remove(temp_bin);

    LOG("Successfully extracted %zu bytes of machine code from .text section\n", result.machine_code.size());

    result.success = true;
    return result;
}

// Helper function to find llvm-objcopy executable
static std::string find_llvm_objcopy() {
    // List of potential paths for llvm-objcopy
    std::vector<std::string> paths = {
        "/opt/homebrew/opt/llvm/bin/llvm-objcopy",   // macOS Homebrew (ARM)
        "/usr/local/opt/llvm/bin/llvm-objcopy",      // macOS Homebrew (Intel)
        "/usr/bin/llvm-objcopy",                     // Linux system install
        "/usr/local/bin/llvm-objcopy",               // Linux local install
        "llvm-objcopy"                               // In PATH
    };

    // Check each path
    for (const auto& path : paths) {
        // First check if file exists at absolute path
        if (std::filesystem::exists(path)) {
            LOG("Found llvm-objcopy at: %s\n", path.c_str());
            return path;
        }
    }

    // Try to execute llvm-objcopy --version to check if it's in PATH
    FILE* pipe = popen("llvm-objcopy --version 2>&1", "r");
    if (pipe) {
        char buffer[128];
        bool found = false;
        while (qfgets(buffer, sizeof(buffer), pipe) != nullptr) {
            if (strstr(buffer, "llvm") || strstr(buffer, "LLVM")) {
                found = true;
                break;
            }
        }
        pclose(pipe);
        if (found) {
            LOG("Found llvm-objcopy in PATH\n");
            return "llvm-objcopy";
        }
    }

    LOG("ERROR: llvm-objcopy not found\n");
    return "";
}

} // namespace llm_re::semantic
