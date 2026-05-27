#include "read.hpp"

#include <fstream>
#include <sstream>

namespace pi {

ToolDefinition ReadTool::definition() const {
    return {
        .name = "read",
        .description = "Read file contents from the filesystem. Optionally specify line range.",
        .parameters = {
            {"path", {
                .type = "string",
                .description = "File path to read",
                .required = true,
            }},
            {"offset", {
                .type = "number",
                .description = "Starting line (1-indexed, optional)",
                .required = false,
            }},
            {"limit", {
                .type = "number",
                .description = "Number of lines to read (optional)",
                .required = false,
            }},
        },
    };
}

Result<ToolResult> ReadTool::execute(std::string_view args_json) {
    try {
        auto j = nlohmann::json::parse(args_json);

        if (!j.contains("path") || !j["path"].is_string()) {
            return ToolResult{.success = false, .error_message = "Missing required 'path' argument (string)"};
        }

        std::string path = j["path"].get<std::string>();
        int offset = j.value("offset", 1);
        int limit = j.value("limit", 0);

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            return ToolResult{
                .success = false,
                .error_message = std::format("Cannot open file: {}", path),
            };
        }

        if (limit <= 0 && offset <= 1) {
            // Read entire file
            std::ostringstream oss;
            oss << ifs.rdbuf();
            return ToolResult{.success = true, .output = oss.str()};
        }

        // Read specific range
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(ifs, line)) {
            lines.push_back(line);
        }

        size_t start = std::max(0, offset - 1);
        size_t count = limit > 0 ? static_cast<size_t>(limit) : lines.size() - start;

        std::ostringstream oss;
        size_t end = std::min(start + count, lines.size());
        for (size_t i = start; i < end; ++i) {
            oss << (i + 1) << ":" << lines[i] << "\n";
        }

        if (end < lines.size()) {
            oss << std::format("[{} lines elided; use offset={} to continue]\n",
                               lines.size() - end, end + 1);
        }

        return ToolResult{.success = true, .output = oss.str()};

    } catch (const nlohmann::json::exception& e) {
        return ToolResult{
            .success = false,
            .error_message = std::format("JSON parse error: {}", e.what()),
        };
    }
}

} // namespace pi
