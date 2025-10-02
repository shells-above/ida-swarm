//
// Memory Tool Handler - Client-side implementation of Anthropic's Memory tool
// Handles file operations in a sandboxed /memories directory
//

#ifndef MEMORY_TOOL_H
#define MEMORY_TOOL_H

#include "core/common.h"
#include <filesystem>
#include <optional>

namespace llm_re {

/**
 * Handles Anthropic Memory tool commands (view, create, str_replace, insert, delete, rename)
 * Provides secure, sandboxed file system access within a designated memory directory.
 */
class MemoryToolHandler {
public:
    /**
     * Initialize handler with a memory root directory
     * @param memory_dir_path Absolute path to the memory directory (e.g., /tmp/ida_swarm_workspace/binary/memories)
     */
    explicit MemoryToolHandler(const std::string& memory_dir_path);

    /**
     * Execute a memory tool command
     * @param input JSON object with "command" field and command-specific parameters
     * @return JSON result with success/failure and command-specific data
     */
    json execute_command(const json& input);

private:
    std::filesystem::path memory_root_;

    // Command implementations
    json cmd_view(const json& params);
    json cmd_create(const json& params);
    json cmd_str_replace(const json& params);
    json cmd_insert(const json& params);
    json cmd_delete(const json& params);
    json cmd_rename(const json& params);

    /**
     * Validate and resolve a path to ensure it's within memory_root_
     * @param path User-provided path (relative or absolute)
     * @return Validated absolute path, or nullopt if path is invalid/unsafe
     */
    std::optional<std::filesystem::path> validate_path(const std::string& path);

    /**
     * Read file contents as lines
     * @param path Validated file path
     * @return Vector of lines (without newline characters)
     */
    std::vector<std::string> read_file_lines(const std::filesystem::path& path);

    /**
     * Write lines to file
     * @param path Validated file path
     * @param lines Vector of lines to write (newlines added automatically)
     */
    void write_file_lines(const std::filesystem::path& path, const std::vector<std::string>& lines);

    /**
     * Read entire file as string
     * @param path Validated file path
     * @return File contents
     */
    std::string read_file_content(const std::filesystem::path& path);
};

} // namespace llm_re

#endif //MEMORY_TOOL_H
