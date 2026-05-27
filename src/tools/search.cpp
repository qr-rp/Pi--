#include "search.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>
namespace fs = std::filesystem;
namespace pi {

ToolDefinition SearchTool::definition() const {
    return {
        .name = "search",
        .description = "Search files using regex patterns. Returns matching lines with context.",
        .parameters = {
            {"pattern", {
                .type = "string",
                .description = "Regex pattern to search for",
                .required = true,
            }},
            {"path", {
                .type = "string",
                .description = "File path or directory to search (default: current directory)",
                .required = false,
            }},
            {"file_pattern", {
                .type = "string",
                .description = "Glob pattern for files to search (e.g., '*.cpp', '*.ts')",
                .required = false,
            }},
        },
    };
}

Result<ToolResult> SearchTool::execute(std::string_view args_json) {
    try {
        auto j = nlohmann::json::parse(args_json);

        if (!j.contains("pattern") || !j["pattern"].is_string()) {
            return ToolResult{.success = false, .error_message = "Missing required 'pattern' argument (string)"};
        }

        std::string pattern_str = j["pattern"].get<std::string>();
        std::string search_path = j.value("path", ".");

        std::regex pattern(pattern_str, std::regex::ECMAScript | std::regex::optimize);

        fs::path root(search_path);
        if (!fs::exists(root)) {
            return ToolResult{
                .success = false,
                .error_message = std::format("Path does not exist: {}", search_path),
            };
        }

        std::string file_pattern = j.value("file_pattern", "");
        std::regex file_regex;
        bool use_file_pattern = !file_pattern.empty();
        if (use_file_pattern) {
            // Convert simple glob to regex
            std::string glob_regex;
            for (char c : file_pattern) {
                switch (c) {
                    case '*': glob_regex += ".*"; break;
                    case '?': glob_regex += "."; break;
                    case '.': glob_regex += "\\."; break;
                    default:  glob_regex += c; break;
                }
            }
            file_regex = std::regex(glob_regex, std::regex::ECMAScript | std::regex::icase);
        }

        std::ostringstream oss;
        int total_matches = 0;
        int file_count = 0;
        constexpr int MAX_MATCHES = 200;
        constexpr size_t MAX_FILE_SIZE = 10 * 1024 * 1024; // 10MB

        std::function<void(const fs::path&)> search_dir;
        search_dir = [&](const fs::path& dir) {
            try {
                for (auto& entry : fs::directory_iterator(dir)) {
                    if (total_matches >= MAX_MATCHES) return;

                    if (entry.is_directory()) {
                        std::string filename = entry.path().filename().string();
                        if (filename[0] != '.') {
                            search_dir(entry.path());
                        }
                        continue;
                    }

                    if (!entry.is_regular_file()) continue;

                    auto file_path = entry.path();
                    std::string filename = file_path.filename().string();

                    // Skip binary extensions
                    std::string ext = file_path.extension().string();
                    static const std::vector<std::string> binary_exts = {
                        ".o", ".so", ".a", ".dylib", ".exe", ".dll", ".obj",
                        ".png", ".jpg", ".jpeg", ".gif", ".ico", ".bmp",
                        ".mp3", ".mp4", ".avi", ".mov", ".mkv",
                        ".zip", ".tar", ".gz", ".bz2", ".7z",
                        ".pdf", ".doc", ".docx", ".xls", ".xlsx",
                        ".ttf", ".otf", ".woff", ".woff2",
                    };
                    if (std::find(binary_exts.begin(), binary_exts.end(), ext) != binary_exts.end()) {
                        return;
                    }

                    // Check file pattern
                    if (use_file_pattern && !std::regex_match(filename, file_regex)) {
                        return;
                    }

                    // Check file size
                    std::error_code ec;
                    auto fsize = fs::file_size(file_path, ec);
                    if (ec || fsize > MAX_FILE_SIZE) return;

                    // Read and search
                    std::ifstream ifs(file_path);
                    if (!ifs.is_open()) return;

                    std::string line;
                    int line_num = 0;
                    bool file_has_matches = false;

                    while (std::getline(ifs, line)) {
                        line_num++;
                        if (std::regex_search(line, pattern)) {
                            if (!file_has_matches) {
                                oss << "\n" << file_path.string() << ":\n";
                                file_has_matches = true;
                                file_count++;
                            }
                            oss << "  " << line_num << ": " << line << "\n";
                            total_matches++;

                            if (total_matches >= MAX_MATCHES) {
                                oss << "[Reached max {} matches; truncating]\n" + std::to_string(MAX_MATCHES);
                                return;
                            }
                        }
                    }
                }
            } catch (const fs::filesystem_error&) {
                // Skip inaccessible directories
            }
        };

        if (fs::is_regular_file(root)) {
            // Single file search
            std::ifstream ifs(root);
            if (ifs.is_open()) {
                std::string line;
                int line_num = 0;
                while (std::getline(ifs, line)) {
                    line_num++;
                    if (std::regex_search(line, pattern)) {
                        oss << root.string() << ":" << line_num << ": " << line << "\n";
                        total_matches++;
                        if (total_matches >= MAX_MATCHES) break;
                    }
                }
            }
        } else {
            search_dir(root);
        }

        if (total_matches == 0) {
            return ToolResult{
                .success = true,
                .output = std::format("No matches found for pattern '{}' in {}", pattern_str, search_path),
            };
        }

        std::string summary = std::format("Found {} matches in {} files\n", total_matches, file_count);
        return ToolResult{
            .success = true,
            .output = summary + oss.str(),
        };

    } catch (const nlohmann::json::exception& e) {
        return ToolResult{
            .success = false,
            .error_message = std::format("JSON parse error: {}", e.what()),
        };
    } catch (const std::regex_error& e) {
        return ToolResult{
            .success = false,
            .error_message = std::format("Invalid regex pattern: {}", e.what()),
        };
    }
}

} // namespace pi
