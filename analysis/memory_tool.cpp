#include "memory_tool.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace llm_re {

MemoryToolHandler::MemoryToolHandler(const std::string& memory_dir_path)
    : memory_root_(memory_dir_path) {

    // Create memory directory if it doesn't exist
    std::filesystem::create_directories(memory_root_);

    // Ensure memory_root_ is absolute and canonical
    memory_root_ = std::filesystem::canonical(memory_root_);
}

json MemoryToolHandler::execute_command(const json& input) {
    try {
        // Extract command
        if (!input.contains("command")) {
            return {{"success", false}, {"error", "Missing 'command' field"}};
        }

        std::string command = input["command"];

        // Route to appropriate command handler
        if (command == "view") {
            return cmd_view(input);
        } else if (command == "create") {
            return cmd_create(input);
        } else if (command == "str_replace") {
            return cmd_str_replace(input);
        } else if (command == "insert") {
            return cmd_insert(input);
        } else if (command == "delete") {
            return cmd_delete(input);
        } else if (command == "rename") {
            return cmd_rename(input);
        } else {
            return {{"success", false}, {"error", "Unknown command: " + command}};
        }

    } catch (const json::exception& e) {
        return {{"success", false}, {"error", std::string("JSON error: ") + e.what()}};
    } catch (const std::exception& e) {
        return {{"success", false}, {"error", std::string("Error: ") + e.what()}};
    }
}

std::optional<std::filesystem::path> MemoryToolHandler::validate_path(const std::string& path) {
    try {
        // Convert to filesystem path
        std::filesystem::path user_path(path);

        // Strip leading /memories/ if present (Claude's logical namespace)
        std::string path_str = user_path.string();
        if (path_str.starts_with("/memories/")) {
            path_str = path_str.substr(10); // Remove "/memories/"
            user_path = std::filesystem::path(path_str);
        } else if (path_str == "/memories") {
            path_str = "."; // Root of memories becomes current dir
            user_path = std::filesystem::path(path_str);
        }

        // If relative, make it relative to memory_root_
        if (user_path.is_relative()) {
            user_path = memory_root_ / user_path;
        }

        // Resolve to canonical path (this also validates existence for existing paths)
        // For non-existent paths, we need to validate the parent directory
        std::filesystem::path canonical_path;
        if (std::filesystem::exists(user_path)) {
            canonical_path = std::filesystem::canonical(user_path);
        } else {
            // Get parent directory and canonicalize that
            std::filesystem::path parent = user_path.parent_path();
            if (!parent.empty() && std::filesystem::exists(parent)) {
                canonical_path = std::filesystem::canonical(parent) / user_path.filename();
            } else {
                // Parent doesn't exist, build path step by step
                canonical_path = user_path.lexically_normal();
                if (user_path.is_relative()) {
                    canonical_path = memory_root_ / canonical_path;
                }
            }
        }

        // Security check: Ensure path is within memory_root_
        auto canonical_str = canonical_path.string();
        auto root_str = memory_root_.string();

        // Path must start with memory_root_
        if (canonical_str.find(root_str) != 0) {
            return std::nullopt;  // Path traversal attempt
        }

        // Additional check: no ".." components in normalized path
        std::string normalized = canonical_path.lexically_normal().string();
        if (normalized.find("..") != std::string::npos) {
            return std::nullopt;
        }

        return canonical_path;

    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}

json MemoryToolHandler::cmd_view(const json& params) {
    if (!params.contains("path")) {
        return {{"success", false}, {"error", "Missing 'path' parameter"}};
    }

    std::string path_str = params["path"];
    auto validated_path = validate_path(path_str);

    if (!validated_path) {
        return {{"success", false}, {"error", "Invalid or unsafe path"}};
    }

    if (!std::filesystem::exists(*validated_path)) {
        return {{"success", false}, {"error", "Path does not exist: " + path_str}};
    }

    // Check if it's a directory
    if (std::filesystem::is_directory(*validated_path)) {
        // List directory contents
        json entries = json::array();

        for (const auto& entry : std::filesystem::directory_iterator(*validated_path)) {
            std::string rel_path = std::filesystem::relative(entry.path(), memory_root_).string();
            entries.push_back(rel_path);
        }

        return {
            {"success", true},
            {"is_directory", true},
            {"path", path_str},
            {"entries", entries}
        };
    } else {
        // Read file contents
        std::string content = read_file_content(*validated_path);

        // Handle view_range if provided
        if (params.contains("view_range")) {
            auto range = params["view_range"];
            if (range.is_array() && range.size() == 2) {
                int start_line = range[0];
                int end_line = range[1];

                // Split content into lines
                std::istringstream iss(content);
                std::vector<std::string> lines;
                std::string line;
                while (std::getline(iss, line)) {
                    lines.push_back(line);
                }

                // Extract range (1-indexed)
                start_line = std::max(1, start_line) - 1;  // Convert to 0-indexed
                end_line = std::min(static_cast<int>(lines.size()), end_line);

                std::ostringstream oss;
                for (int i = start_line; i < end_line; ++i) {
                    oss << lines[i];
                    if (i < end_line - 1) oss << "\n";
                }
                content = oss.str();
            }
        }

        return {
            {"success", true},
            {"is_directory", false},
            {"path", path_str},
            {"content", content}
        };
    }
}

json MemoryToolHandler::cmd_create(const json& params) {
    if (!params.contains("path") || !params.contains("file_text")) {
        return {{"success", false}, {"error", "Missing 'path' or 'file_text' parameter"}};
    }

    std::string path_str = params["path"];
    std::string file_text = params["file_text"];

    auto validated_path = validate_path(path_str);
    if (!validated_path) {
        return {{"success", false}, {"error", "Invalid or unsafe path"}};
    }

    try {
        // Create parent directories if needed
        std::filesystem::create_directories(validated_path->parent_path());

        // Write file
        std::ofstream file(*validated_path, std::ios::out | std::ios::trunc);
        if (!file) {
            return {{"success", false}, {"error", "Failed to create file"}};
        }

        file << file_text;
        file.close();

        return {
            {"success", true},
            {"path", path_str}
        };

    } catch (const std::exception& e) {
        return {{"success", false}, {"error", std::string("Failed to create file: ") + e.what()}};
    }
}

json MemoryToolHandler::cmd_str_replace(const json& params) {
    if (!params.contains("path") || !params.contains("old_str") || !params.contains("new_str")) {
        return {{"success", false}, {"error", "Missing required parameters"}};
    }

    std::string path_str = params["path"];
    std::string old_str = params["old_str"];
    std::string new_str = params["new_str"];

    auto validated_path = validate_path(path_str);
    if (!validated_path) {
        return {{"success", false}, {"error", "Invalid or unsafe path"}};
    }

    if (!std::filesystem::exists(*validated_path)) {
        return {{"success", false}, {"error", "File does not exist"}};
    }

    try {
        // Read file content
        std::string content = read_file_content(*validated_path);

        // Find old_str (must be unique)
        size_t pos = content.find(old_str);
        if (pos == std::string::npos) {
            return {{"success", false}, {"error", "String not found in file"}};
        }

        // Check if old_str appears multiple times
        size_t second_pos = content.find(old_str, pos + old_str.length());
        if (second_pos != std::string::npos) {
            return {{"success", false}, {"error", "String appears multiple times, must be unique"}};
        }

        // Replace
        content.replace(pos, old_str.length(), new_str);

        // Write back
        std::ofstream file(*validated_path, std::ios::out | std::ios::trunc);
        file << content;
        file.close();

        return {
            {"success", true},
            {"replacements", 1}
        };

    } catch (const std::exception& e) {
        return {{"success", false}, {"error", std::string("Replace failed: ") + e.what()}};
    }
}

json MemoryToolHandler::cmd_insert(const json& params) {
    if (!params.contains("path") || !params.contains("insert_line") || !params.contains("insert_text")) {
        return {{"success", false}, {"error", "Missing required parameters"}};
    }

    std::string path_str = params["path"];
    int insert_line = params["insert_line"];
    std::string insert_text = params["insert_text"];

    auto validated_path = validate_path(path_str);
    if (!validated_path) {
        return {{"success", false}, {"error", "Invalid or unsafe path"}};
    }

    if (!std::filesystem::exists(*validated_path)) {
        return {{"success", false}, {"error", "File does not exist"}};
    }

    try {
        // Read file lines
        auto lines = read_file_lines(*validated_path);

        // Insert text at specified line (0-indexed in params, but insert function expects position)
        if (insert_line < 0 || insert_line > static_cast<int>(lines.size())) {
            return {{"success", false}, {"error", "Line number out of range"}};
        }

        lines.insert(lines.begin() + insert_line, insert_text);

        // Write back
        write_file_lines(*validated_path, lines);

        return {{"success", true}};

    } catch (const std::exception& e) {
        return {{"success", false}, {"error", std::string("Insert failed: ") + e.what()}};
    }
}

json MemoryToolHandler::cmd_delete(const json& params) {
    if (!params.contains("path")) {
        return {{"success", false}, {"error", "Missing 'path' parameter"}};
    }

    std::string path_str = params["path"];
    auto validated_path = validate_path(path_str);

    if (!validated_path) {
        return {{"success", false}, {"error", "Invalid or unsafe path"}};
    }

    if (!std::filesystem::exists(*validated_path)) {
        return {{"success", false}, {"error", "Path does not exist"}};
    }

    try {
        // Remove file or directory recursively
        std::filesystem::remove_all(*validated_path);

        return {{"success", true}};

    } catch (const std::exception& e) {
        return {{"success", false}, {"error", std::string("Delete failed: ") + e.what()}};
    }
}

json MemoryToolHandler::cmd_rename(const json& params) {
    if (!params.contains("old_path") || !params.contains("new_path")) {
        return {{"success", false}, {"error", "Missing 'old_path' or 'new_path' parameter"}};
    }

    std::string old_path_str = params["old_path"];
    std::string new_path_str = params["new_path"];

    auto validated_old = validate_path(old_path_str);
    auto validated_new = validate_path(new_path_str);

    if (!validated_old || !validated_new) {
        return {{"success", false}, {"error", "Invalid or unsafe path"}};
    }

    if (!std::filesystem::exists(*validated_old)) {
        return {{"success", false}, {"error", "Source path does not exist"}};
    }

    try {
        // Create parent directory for new path if needed
        std::filesystem::create_directories(validated_new->parent_path());

        // Rename/move
        std::filesystem::rename(*validated_old, *validated_new);

        return {
            {"success", true},
            {"new_path", new_path_str}
        };

    } catch (const std::exception& e) {
        return {{"success", false}, {"error", std::string("Rename failed: ") + e.what()}};
    }
}

std::vector<std::string> MemoryToolHandler::read_file_lines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line)) {
        lines.push_back(line);
    }

    return lines;
}

void MemoryToolHandler::write_file_lines(const std::filesystem::path& path, const std::vector<std::string>& lines) {
    std::ofstream file(path, std::ios::out | std::ios::trunc);

    for (size_t i = 0; i < lines.size(); ++i) {
        file << lines[i];
        if (i < lines.size() - 1) {
            file << "\n";
        }
    }

    file.close();
}

std::string MemoryToolHandler::read_file_content(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace llm_re
