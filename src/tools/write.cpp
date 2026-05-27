#include "write.hpp"
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;
namespace pi {
ToolDefinition WriteTool::definition() const {
    return {
        .name = "write",
        .description = "Write content to a file, creating parent directories if needed.",
        .parameters = {
            {"path", {
                .type = "string",
                .description = "File path to write",
                .required = true,
            }},
            {"content", {
                .type = "string",
                .description = "Content to write to file",
                .required = true,
            }},
        },
    };
}

Result<ToolResult> WriteTool::execute(std::string_view args_json) {
    try {
        auto j = nlohmann::json::parse(args_json);

        if (!j.contains("path") || !j["path"].is_string()) {
            return ToolResult{.success = false, .error_message = "Missing required 'path' argument (string)"};
        }
        if (!j.contains("content") || !j["content"].is_string()) {
            return ToolResult{.success = false, .error_message = "Missing required 'content' argument (string)"};
        }

        std::string path = j["path"].get<std::string>();
        std::string content = j["content"].get<std::string>();

        // Create parent directories
        fs::path fspath(path);
        fs::create_directories(fspath.parent_path());

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            return ToolResult{
                .success = false,
                .error_message = std::format("Cannot open file for writing: {}", path),
            };
        }

        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.close();

        return ToolResult{
            .success = true,
            .output = std::format("Successfully wrote {} bytes to {}", content.size(), path),
        };

    } catch (const nlohmann::json::exception& e) {
        return ToolResult{
            .success = false,
            .error_message = std::format("JSON parse error: {}", e.what()),
        };
    } catch (const fs::filesystem_error& e) {
        return ToolResult{
            .success = false,
            .error_message = std::format("Filesystem error: {}", e.what()),
        };
    }
}

} // namespace pi
