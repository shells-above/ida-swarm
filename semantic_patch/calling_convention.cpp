#include "calling_convention.h"
#include "core/ida_utils.h"
#include <ida.hpp>
#include <idp.hpp>
#include <typeinf.hpp>

namespace llm_re::semantic {

// CallingConvention methods
std::string CallingConvention::to_string() const {
    std::stringstream ss;
    ss << name << " (";

    if (!arg_registers.empty()) {
        ss << "args: ";
        for (size_t i = 0; i < arg_registers.size(); i++) {
            if (i > 0) ss << ", ";
            ss << arg_registers[i];
        }
    }

    if (!return_register.empty()) {
        if (!arg_registers.empty()) ss << "; ";
        ss << "ret: " << return_register;
    }

    ss << ")";
    return ss.str();
}

bool CallingConvention::is_compatible_with(const CallingConvention& other) const {
    // Must be same type
    if (type != other.type) {
        return false;
    }

    // Argument registers must match
    if (arg_registers.size() != other.arg_registers.size()) {
        return false;
    }

    for (size_t i = 0; i < arg_registers.size(); i++) {
        if (arg_registers[i] != other.arg_registers[i]) {
            return false;
        }
    }

    // Return register must match
    if (return_register != other.return_register) {
        return false;
    }

    return true;
}

// CallingConventionAnalyzer implementation
CallingConventionAnalyzer::CallingConventionAnalyzer() {
    // Initialize platform detection - must execute on main thread
    IDAUtils::execute_sync_wrapper([this]() -> bool {
        cached_architecture_ = get_architecture();
        cached_is_64bit_ = inf_is_64bit();
        cached_is_windows_ = inf_get_filetype() == f_PE;
        cache_valid_ = true;
        return true;
    }, MFF_READ);
}

CallingConvention CallingConventionAnalyzer::analyze_function(ea_t func_addr) const {
    return IDAUtils::execute_sync_wrapper([&]() -> CallingConvention {
        tinfo_t tif;
        if (!get_tinfo(&tif, func_addr)) {
            return CallingConvention{};  // UNKNOWN
        }

        // Get detailed function information
        func_type_data_t fi;
        if (!tif.get_func_details(&fi)) {
            return CallingConvention{};  // UNKNOWN
        }

        cm_t cc = fi.get_cc();

        // Extract actual register usage from argument locations
        std::vector<std::string> arg_regs;
        for (const funcarg_t& arg : fi) {
            if (arg.argloc.is_reg()) {
                qstring reg_name;
                int reg_num = arg.argloc.reg1();
                if (get_reg_name(&reg_name, reg_num, arg.type.get_size()) > 0) {
                    arg_regs.emplace_back(reg_name.c_str());
                }
            }
        }

        // For x64, distinguish System V vs MS based on actual register usage
        if (cached_is_64bit_) {
            if (!arg_regs.empty()) {
                // Check first argument register to determine convention
                const std::string& first_reg = arg_regs[0];
                if (first_reg == "rdi" || first_reg == "edi") {
                    return build_x64_sysv();
                } else if (first_reg == "rcx" || first_reg == "ecx") {
                    return build_x64_ms();
                }
            }
        }

        // Handle calling conventions by type
        switch (cc) {
            case CM_CC_CDECL:
                return build_x86_cdecl();
            case CM_CC_STDCALL:
                return build_x86_stdcall();
            case CM_CC_FASTCALL:
                return build_x86_fastcall();
            case CM_CC_THISCALL:
                return build_x86_thiscall();
            case CM_CC_SWIFT:
            case CM_CC_GOLANG:
            case CM_CC_SPECIAL:
                // Special calling conventions not fully supported yet
                // Return UNKNOWN so agent knows to handle it
                return CallingConvention{};
            case CM_CC_UNKNOWN:
            case CM_CC_INVALID:
            default:
                // IDA doesn't know the calling convention - return UNKNOWN
                // Don't guess, let the agent handle it
                return CallingConvention{};
        }
    }, MFF_READ);
}

CallingConvention CallingConventionAnalyzer::analyze_assembly(const std::string& assembly, const std::string& architecture) {
    // Parse assembly to detect register usage patterns
    // This is used for verifying compiled code

    // Look for register usage in first few instructions
    std::vector<std::string> used_regs;

    // Simple heuristic: check which registers appear in the first 5 instructions
    std::istringstream asm_stream(assembly);
    std::string line;
    int line_count = 0;

    while (std::getline(asm_stream, line) && line_count < 5) {
        line_count++;

        // Check for common argument registers
        if (architecture == "x86_64" || architecture == "x64") {
            if (line.find("rdi") != std::string::npos) used_regs.emplace_back("rdi");
            if (line.find("rsi") != std::string::npos) used_regs.emplace_back("rsi");
            if (line.find("rdx") != std::string::npos) used_regs.emplace_back("rdx");
            if (line.find("rcx") != std::string::npos) used_regs.emplace_back("rcx");
            if (line.find("r8") != std::string::npos) used_regs.emplace_back("r8");
            if (line.find("r9") != std::string::npos) used_regs.emplace_back("r9");
        }
    }

    // Determine convention based on register usage
    if (architecture == "x86_64" || architecture == "x64") {
        // Check if using System V (rdi, rsi, rdx) or MS (rcx, rdx, r8)
        bool has_rdi = std::find(used_regs.begin(), used_regs.end(), "rdi") != used_regs.end();
        bool has_rcx = std::find(used_regs.begin(), used_regs.end(), "rcx") != used_regs.end();

        if (has_rdi) {
            return build_x64_sysv();
        } else if (has_rcx) {
            return build_x64_ms();
        }
    }

    // Default to platform convention - needs IDA access
    return IDAUtils::execute_sync_wrapper([this]() -> CallingConvention {
        return get_platform_default();
    }, MFF_READ);
}

CallingConvention CallingConventionAnalyzer::get_platform_default() const {
    if (cached_is_64bit_) {
        if (cached_is_windows_) {
            return build_x64_ms();
        } else {
            return build_x64_sysv();
        }
    } else {
        // 32-bit defaults to cdecl
        return build_x86_cdecl();
    }
}

// Private methods

std::string CallingConventionAnalyzer::get_architecture() const {
    // Use processor ID instead of string matching
    switch (PH.id) {
        case PLFM_386:  // x86/x64
            return cached_is_64bit_ ? "x86_64" : "x86";
        case PLFM_ARM:  // ARM
            return cached_is_64bit_ ? "arm64" : "arm";
        default:
            return "unknown";
    }
}

// Convention builders

CallingConvention CallingConventionAnalyzer::build_x64_sysv() {
    CallingConvention conv;
    conv.type = CallingConvention::X64_SYSV;
    conv.name = "System V AMD64 ABI";
    conv.arg_registers = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    conv.return_register = "rax";
    conv.callee_saved = {"rbx", "rbp", "r12", "r13", "r14", "r15"};
    conv.caller_saved = {"rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"};
    conv.uses_stack_args = true;
    conv.stack_alignment = 16;
    conv.compiler_flags = "-mabi=sysv";
    return conv;
}

CallingConvention CallingConventionAnalyzer::build_x64_ms() {
    CallingConvention conv;
    conv.type = CallingConvention::X64_MS;
    conv.name = "Microsoft x64";
    conv.arg_registers = {"rcx", "rdx", "r8", "r9"};
    conv.return_register = "rax";
    conv.callee_saved = {"rbx", "rbp", "rdi", "rsi", "rsp", "r12", "r13", "r14", "r15"};
    conv.caller_saved = {"rax", "rcx", "rdx", "r8", "r9", "r10", "r11"};
    conv.uses_stack_args = true;
    conv.stack_alignment = 16;
    conv.compiler_flags = "-fms-compatibility";
    return conv;
}

CallingConvention CallingConventionAnalyzer::build_x86_cdecl() {
    CallingConvention conv;
    conv.type = CallingConvention::X86_CDECL;
    conv.name = "cdecl";
    conv.arg_registers = {};  // All args on stack
    conv.return_register = "eax";
    conv.callee_saved = {"ebx", "esi", "edi", "ebp"};
    conv.caller_saved = {"eax", "ecx", "edx"};
    conv.uses_stack_args = true;
    conv.stack_alignment = 4;
    conv.compiler_flags = "-m32 -mabi=sysv";
    return conv;
}

CallingConvention CallingConventionAnalyzer::build_x86_stdcall() {
    CallingConvention conv;
    conv.type = CallingConvention::X86_STDCALL;
    conv.name = "stdcall";
    conv.arg_registers = {};  // All args on stack
    conv.return_register = "eax";
    conv.callee_saved = {"ebx", "esi", "edi", "ebp"};
    conv.caller_saved = {"eax", "ecx", "edx"};
    conv.uses_stack_args = true;
    conv.stack_alignment = 4;
    conv.compiler_flags = "-m32 -mrtd";
    return conv;
}

CallingConvention CallingConventionAnalyzer::build_x86_fastcall() {
    CallingConvention conv;
    conv.type = CallingConvention::X86_FASTCALL;
    conv.name = "fastcall";
    conv.arg_registers = {"ecx", "edx"};  // First two args in registers
    conv.return_register = "eax";
    conv.callee_saved = {"ebx", "esi", "edi", "ebp"};
    conv.caller_saved = {"eax", "ecx", "edx"};
    conv.uses_stack_args = true;
    conv.stack_alignment = 4;
    conv.compiler_flags = "-m32 -mregparm=2";
    return conv;
}

CallingConvention CallingConventionAnalyzer::build_x86_thiscall() {
    CallingConvention conv;
    conv.type = CallingConvention::X86_THISCALL;
    conv.name = "thiscall";
    conv.arg_registers = {"ecx"};  // 'this' pointer in ECX, rest on stack
    conv.return_register = "eax";
    conv.callee_saved = {"ebx", "esi", "edi", "ebp"};
    conv.caller_saved = {"eax", "ecx", "edx"};
    conv.uses_stack_args = true;
    conv.stack_alignment = 4;
    conv.compiler_flags = "-m32 -mrtd";  // Callee cleans stack like stdcall
    return conv;
}

CallingConvention CallingConventionAnalyzer::build_arm_aapcs() {
    CallingConvention conv;
    conv.type = CallingConvention::ARM_AAPCS;
    conv.name = "ARM AAPCS";
    conv.arg_registers = {"r0", "r1", "r2", "r3"};
    conv.return_register = "r0";
    conv.callee_saved = {"r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11"};
    conv.caller_saved = {"r0", "r1", "r2", "r3", "r12"};
    conv.uses_stack_args = true;
    conv.stack_alignment = 8;
    conv.compiler_flags = "-target arm";
    return conv;
}

CallingConvention CallingConventionAnalyzer::build_arm64_aapcs() {
    CallingConvention conv;
    conv.type = CallingConvention::ARM64_AAPCS;
    conv.name = "ARM64 AAPCS";
    conv.arg_registers = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
    conv.return_register = "x0";
    conv.callee_saved = {"x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28"};
    conv.caller_saved = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15"};
    conv.uses_stack_args = true;
    conv.stack_alignment = 16;
    conv.compiler_flags = "-target aarch64";
    return conv;
}

} // namespace llm_re::semantic
