#include "core/config.hpp"
#include "core/error.hpp"
#include "i18n/i18n.hpp"
#include "provider/registry.hpp"
#include "tools/registry.hpp"
#include "session/session.hpp"
#include "agent/agent.hpp"
#include "tui/app.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>

// ── Usage string ─────────────────────────────────────────────────────────
static constexpr auto kUsage = R"(pi-coding-agent — C++20 coding agent with LLM provider support

Usage:
  pi-coding-agent [options] <prompt>
  pi-coding-agent --interactive
  pi-coding-agent --list-models
  pi-coding-agent --list-providers

Options:
  -m, --model <model>        Model to use (default: config default)
  -p, --provider <provider>  Provider to use (default: config default)
  -l, --locale <locale>      Locale for i18n (default: auto-detect)
  -c, --config <dir>         Config directory
  -i, --interactive          Interactive REPL mode
  --list-models              List available models
  --list-providers           List available providers
  -h, --help                 Show this help
  -v, --version              Show version

Environment Variables:
  OPENAI_API_KEY             API key for OpenAI provider
  DEEPSEEK_API_KEY           API key for DeepSeek provider
  ANTHROPIC_API_KEY          API key for Anthropic provider

Examples:
  pi-coding-agent "Write a hello world program"
  pi-coding-agent -p deepseek -m deepseek-chat "Explain quantum computing"
  pi-coding-agent --interactive -l zh-CN
)";

// ── Main ─────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // ── Parse arguments ───────────────────────────────────────────────────
    std::string config_dir;
    std::string model_override;
    std::string provider_override;
    std::string locale_override;
    bool interactive = false;
    bool list_models = false;
    bool list_providers = false;
    bool show_help = false;
    bool show_version = false;
    std::string prompt;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "-h" || arg == "--help") { show_help = true; break; }
        if (arg == "-v" || arg == "--version") { show_version = true; break; }
        if (arg == "-i" || arg == "--interactive") { interactive = true; continue; }
        if (arg == "--list-models") { list_models = true; continue; }
        if (arg == "--list-providers") { list_providers = true; continue; }
        if ((arg == "-m" || arg == "--model") && i + 1 < argc) { model_override = argv[++i]; continue; }
        if ((arg == "-p" || arg == "--provider") && i + 1 < argc) { provider_override = argv[++i]; continue; }
        if ((arg == "-l" || arg == "--locale") && i + 1 < argc) { locale_override = argv[++i]; continue; }
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) { config_dir = argv[++i]; continue; }
        if (arg[0] != '-') {
            if (!prompt.empty()) prompt += ' ';
            prompt += arg;
        }
    }

    // ── Help / Version ────────────────────────────────────────────────────
    if (show_help) {
        std::cout << kUsage;
        return 0;
    }
    if (show_version) {
        std::cout << "pi-coding-agent v" << PI_AGENT_VERSION << "\n";
        return 0;
    }

    // ── Initialize subsystems ────────────────────────────────────────────
    pi::Config config;
    if (!config_dir.empty()) {
        auto r = config.load_from(config_dir);
        if (!r) {
            std::cerr << "Warning: Could not load config from " << config_dir
                       << ": " << r.error().message << "\n";
        }
    } else {
        (void)config.load();
    }

    // Locale override
    if (!locale_override.empty()) {
        (void)pi::t().set_locale(locale_override);
    }

    // Provider overrides
    if (!provider_override.empty()) {
        config.agent().default_provider = provider_override;
    }

    // Provider registry
    pi::ProviderRegistry provider_registry;
    provider_registry.set_config(&config);

    // Tool registry
    pi::ToolRegistry tool_registry;
    tool_registry.set_config(&config);

    // ── List commands ─────────────────────────────────────────────────────
    if (list_providers) {
        std::cout << "Available providers:\n";
        for (auto& name : provider_registry.available_providers()) {
            auto* pc = config.find_provider(name);
            std::string desc;
            if (name == "openai") desc = "OpenAI-compatible API";
            else if (name == "deepseek") desc = "DeepSeek API (OpenAI-compatible)";
            else if (name == "anthropic") desc = "Anthropic API";
            else if (name == "google") desc = "Google Generative AI";
            else desc = "Custom provider";
            if (pc) {
                std::cout << "  " << name << " (" << pc->base_url << ") - " << desc << "\n";
            } else {
                std::cout << "  " << name << " - " << desc << " (not configured)\n";
            }
        }
        return 0;
    }

    if (list_models) {
        std::cout << "Available models:\n";
        std::vector<std::string> processed;

        // Try API discovery first
        auto discovered = provider_registry.discover_all_models();
        for (auto& m : discovered) {
            if (std::find(processed.begin(), processed.end(), m.id) != processed.end()) continue;
            processed.push_back(m.id);
            std::cout << "  " << m.id << " (provider: " << m.provider
                       << ", context: " << (m.context_window ? std::to_string(*m.context_window) : "?")
                       << ", reasoning: " << (m.reasoning ? "yes" : "no") << ")"
                       << " [auto-discovered]\n";
        }

        // Then static models (supplement discovered ones)
        for (auto& m : config.agent().custom_models) {
            if (std::find(processed.begin(), processed.end(), m.id) != processed.end()) continue;
            processed.push_back(m.id);
            std::cout << "  " << m.id << " (provider: " << m.provider
                       << ", context: " << (m.context_window ? std::to_string(*m.context_window) : "?")
                       << ", reasoning: " << (m.reasoning ? "yes" : "no") << ")\n";
        }

        // Provider entries
        for (auto& [name, pc] : config.agent().providers) {
            if (std::find(processed.begin(), processed.end(), name) == processed.end()) {
                std::cout << "  Provider: " << name << " @ " << pc.base_url << "\n";
            }
        }
        return 0;
    }

    // ── Need prompt or interactive mode ───────────────────────────────────
    if (!interactive && prompt.empty()) {
        std::cerr << "Error: No prompt provided. Use -h for help.\n";
        return 1;
    }
    // ── Interactive mode ──────────────────────────────────────────────────
    if (interactive) {
        pi::TuiApp tui(&config, &provider_registry, &tool_registry);
        return tui.run();
    }
    // ── Single prompt mode ────────────────────────────────────────────────
    pi::Agent agent(&config, &provider_registry, &tool_registry);
    pi::Session session;
    session.set_system_prompt(
        "You are pi-coding-agent, a C++20 coding agent with access to tools:\n"
        "- read/write/edit/bash/search.\n"
        "Use these tools to help the user with their tasks."
    );
    pi::AgentCallbacks callbacks{
        .on_chunk = [](std::string_view chunk) { std::cout << chunk << std::flush; },
        .on_tool_start = [](std::string_view name) {
            std::cout << "\n\033[90m[Using tool: " << name << "]\033[0m\n" << std::flush;
        },
        .on_tool_result = [](const pi::ToolResult& result) {
            if (!result.success) {
                std::cerr << "\033[91m" << result.error_message << "\033[0m\n";
            }
        },
        .on_error = [](std::string_view err) {
            std::cerr << "\033[91mError: " << err << "\033[0m\n";
        },
        .on_turn_complete = []{ std::cout << "\n"; },
    };

    auto result = agent.run_conversation(session, prompt, callbacks, model_override);
    if (!result) {
        std::cerr << "\033[91m" << pi::t().translate_fmt("error.general", {result.error().message}) << "\033[0m\n";
        return 1;
    }

    return 0;
}
