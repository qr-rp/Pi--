#include "edit.hpp"

#include <fstream>
#include <sstream>

namespace pi {

ToolDefinition EditTool::definition() const {
    return {
        .name = "edit",
        .description = "Edit a file by replacing text. 'old_text' must match exactly once.",
        .parameters = {
            {"path", {
                .type = "string",
                .description = "File path to edit",
                .required = true,
            }},
            {"old_text", {
                .type = "string",
                .description = "Exact text to find and replace",
                .required = true,
            }},
            {"new_text", {
                .type = "string",
                .description = "Replacement text",
                .required = true,
            }},
        },
    };
}

Result<ToolResult> EditTool::execute(std::string_view args_json) {
    try {
        auto j = nlohmann::json::parse(args_json);

        if (!j.contains("path") || !j["path"].is_string()) {
            return ToolResult{.success = false, .error_message = "Missing required 'path' argument (string)"};
        }
        if (!j.contains("old_text") || !j["old_text"].is_string()) {
            return ToolResult{.success = false, .error_message = "Missing required 'old_text' argument (string)"};
        }
        if (!j.contains("new_text") || !j["new_text"].is_string()) {
            return ToolResult{.success = false, .error_message = "Missing required 'new_text' argument (string)"};
        }

        std::string path = j["path"].get<std::string>();
        std::string old_text = j["old_text"].get<std::string>();
        std::string new_text = j["new_text"].get<std::string>();

        // Read file
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            return ToolResult{
                .success = false,
                .error_message = std::format("Cannot open file: {}", path),
            };
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        // Count occurrences
        auto pos = content.find(old_text);
        if (pos == std::string::npos) {
            return ToolResult{
                .success = false,
                .error_message = std::format("old_text not found in file: {}", path),
            };
        }

        // Check for multiple occurrences
        auto next_pos = content.find(old_text, pos + old_text.size());
        if (next_pos != std::string::npos) {
            return ToolResult{
                .success = false,
                .error_message = std::format("old_text found multiple times in file: {}. Use exact, unique text.",
                                              path),
            };
        }

        // Replace
        content.replace(pos, old_text.size(), new_text);

        // Write back
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            return ToolResult{
                .success = false,
                .error_message = std::format("Cannot write to file: {}", path),
            };
        }
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.close();

        return ToolResult{
            .success = true,
            .output = std::format("Successfully edited {} ({} chars replaced)", path, old_text.size()),
        };

    } catch (const nlohmann::json::exception& e) {
        return ToolResult{
            .success = false,
            .error_message = std::format("JSON parse error: {}", e.what()),
        };
    }
}

} // namespace pi
