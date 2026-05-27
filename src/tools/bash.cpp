#include "bash.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <thread>

namespace pi {

ToolDefinition BashTool::definition() const {
    return {
        .name = "bash",
        .description = "Execute a bash command with optional timeout in seconds.",
        .parameters = {
            {"command", {
                .type = "string",
                .description = "The bash command to execute",
                .required = true,
            }},
            {"timeout", {
                .type = "number",
                .description = "Timeout in seconds (default: 30)",
                .required = false,
            }},
            {"description", {
                .type = "string",
                .description = "Description of what this command does (for logging)",
                .required = false,
            }},
        },
    };
}

Result<ToolResult> BashTool::execute(std::string_view args_json) {
    try {
        auto j = nlohmann::json::parse(args_json);

        if (!j.contains("command") || !j["command"].is_string()) {
            return ToolResult{.success = false, .error_message = "Missing required 'command' argument (string)"};
        }

        std::string command = j["command"].get<std::string>();
        int timeout = j.value("timeout", 30);

        // Execute command via popen
        std::string full_cmd = command + " 2>&1";

        std::array<char, 4096> buffer;
        std::string result;
        bool timed_out = false;

        auto start_time = std::chrono::steady_clock::now();

        FILE* pipe = popen(full_cmd.c_str(), "r");
        if (!pipe) {
            return ToolResult{
                .success = false,
                .error_message = std::format("Failed to execute command: {}", command),
            };
        }

        // Read output with timeout check
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();

            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (timeout > 0 && elapsed > std::chrono::seconds(timeout)) {
                timed_out = true;
                break;
            }
        }

        int exit_code = pclose(pipe);

        if (timed_out) {
            result += "\n[Command timed out after {} seconds]\n" + std::to_string(timeout);
        }

        if (exit_code != 0 && !timed_out) {
            return ToolResult{
                .success = true, // Still return success since command ran; LLM can see error
                .output = std::format("Exit code: {}\n{}", exit_code, result),
                .error_message = std::format("Command exited with code {}", exit_code),
            };
        }

        return ToolResult{
            .success = true,
            .output = result.empty()
                      ? std::format("Command completed (exit code 0, no output)")
                      : std::format("Exit code: {}\n{}", exit_code, result),
        };

    } catch (const nlohmann::json::exception& e) {
        return ToolResult{
            .success = false,
            .error_message = std::format("JSON parse error: {}", e.what()),
        };
    }
}

} // namespace pi
