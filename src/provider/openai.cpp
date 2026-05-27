#include "openai.hpp"

#include "core/error.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace pi {

OpenAIProvider::OpenAIProvider(std::string_view api_key, std::string_view base_url,
                                std::string_view provider_name,
                                std::unordered_map<std::string, std::string> extra_headers)
    : api_key_(api_key)
    , base_url_(base_url)
    , provider_name_(provider_name)
    , extra_headers_(std::move(extra_headers))
{
    while (!base_url_.empty() && base_url_.back() == '/') {
        base_url_.pop_back();
    }
}

OpenAIProvider::~OpenAIProvider() = default;

nlohmann::json OpenAIProvider::message_to_json(const Message& msg) const {
    nlohmann::json j;
    j["role"] = role_name(msg.role);

    if (msg.role == Role::kTool) {
        j["tool_call_id"] = msg.tool_call_id;
        if (!msg.name.empty()) j["name"] = msg.name;
        j["content"] = msg.content;
        return j;
    }

    if (msg.role == Role::kAssistant && !msg.tool_calls.empty()) {
        if (!msg.content.empty()) {
            j["content"] = msg.content;
        } else {
            j["content"] = nullptr;
        }
        nlohmann::json tcs = nlohmann::json::array();
        for (auto& tc : msg.tool_calls) {
            nlohmann::json tcj;
            tcj["id"] = tc.id;
            tcj["type"] = "function";
            tcj["function"]["name"] = tc.name;
            try {
                tcj["function"]["arguments"] = nlohmann::json::parse(tc.arguments).dump();
            } catch (...) {
                tcj["function"]["arguments"] = tc.arguments;
            }
            tcs.push_back(tcj);
        }
        j["tool_calls"] = tcs;
        return j;
    }

    j["content"] = msg.content;
    if (!msg.name.empty()) {
        j["name"] = msg.name;
    }
    return j;
}

nlohmann::json OpenAIProvider::build_request_body(
    const std::vector<Message>& messages,
    const ChatOptions& options,
    const std::vector<ToolDefinition>& tools
) const {
    nlohmann::json body;
    body["model"] = options.model;
    body["stream"] = options.stream;
    body["temperature"] = options.temperature;
    if (options.max_tokens) body["max_tokens"] = *options.max_tokens;
    if (options.top_p) body["top_p"] = *options.top_p;
    if (options.stop) body["stop"] = *options.stop;

    nlohmann::json msgs = nlohmann::json::array();
    for (auto& msg : messages) {
        msgs.push_back(message_to_json(msg));
    }
    body["messages"] = msgs;

    if (!tools.empty()) {
        nlohmann::json tl = nlohmann::json::array();
        for (auto& tool : tools) {
            nlohmann::json t;
            t["type"] = "function";
            t["function"]["name"] = tool.name;
            t["function"]["description"] = tool.description;

            nlohmann::json params;
            params["type"] = "object";
            nlohmann::json props = nlohmann::json::object();
            nlohmann::json required = nlohmann::json::array();

            for (auto& [pname, prop] : tool.parameters) {
                nlohmann::json p;
                p["type"] = prop.type;
                p["description"] = prop.description;
                if (prop.enum_values) {
                    std::stringstream ss(prop.enum_values.value());
                    std::string token;
                    nlohmann::json enums = nlohmann::json::array();
                    while (std::getline(ss, token, ',')) {
                        enums.push_back(token);
                    }
                    p["enum"] = enums;
                }
                props[pname] = p;
                if (prop.required) required.push_back(pname);
            }
            params["properties"] = props;
            if (!required.empty()) params["required"] = required;

            t["function"]["parameters"] = params;
            tl.push_back(t);
        }
        body["tools"] = tl;
    }

    for (auto& [key, val] : options.extra_body.items()) {
        body[key] = val;
    }

    return body;
}

bool OpenAIProvider::parse_sse_line(std::string_view line, StreamCallback& callback,
                                     std::string& buffer) const {
    if (line.empty()) {
        if (buffer.empty()) return true;

        if (buffer == "[DONE]") {
            callback(StreamEvent{.type = StreamEventType::kDone});
            buffer.clear();
            return true;
        }

        try {
            auto j = nlohmann::json::parse(buffer);

            if (j.contains("error") && !j["error"].is_null()) {
                std::string err_msg = j["error"].value("message", "Unknown API error");
                callback(StreamEvent{
                    .type = StreamEventType::kError,
                    .error_message = err_msg,
                });
                buffer.clear();
                return false;
            }

            if (!j.contains("choices") || j["choices"].empty()) {
                buffer.clear();
                return true;
            }

            auto& choice = j["choices"][0];

            if (choice.contains("delta")) {
                auto& delta = choice["delta"];

                if (delta.contains("content") && !delta["content"].is_null()) {
                    callback(StreamEvent{
                        .type = StreamEventType::kChunk,
                        .content = delta["content"].get<std::string>(),
                    });
                }

                if (delta.contains("tool_calls") && !delta["tool_calls"].is_null()) {
                    for (auto& tc : delta["tool_calls"]) {
                        ToolCall tool_call;
                        tool_call.id = tc.value("id", "");
                        if (tc.contains("function")) {
                            tool_call.name = tc["function"].value("name", "");
                            tool_call.arguments = tc["function"].value("arguments", "");
                        }
                        if (!tool_call.id.empty() || !tool_call.name.empty()) {
                            callback(StreamEvent{
                                .type = StreamEventType::kToolCall,
                                .tool_call = std::move(tool_call),
                            });
                        }
                    }
                }
            }

            if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                std::string reason = choice["finish_reason"].get<std::string>();
                if (reason == "stop" || reason == "length") {
                    callback(StreamEvent{.type = StreamEventType::kDone});
                }
            }

        } catch (const nlohmann::json::parse_error& e) {
            callback(StreamEvent{
                .type = StreamEventType::kError,
                .error_message = std::format("JSON parse: {}", e.what()),
            });
            buffer.clear();
            return false;
        }

        buffer.clear();
        return true;
    }

    if (line.starts_with("data: ")) {
        std::string_view data = line.substr(6);
        buffer += data;
        if (buffer.size() > 1 << 20) {
            buffer.clear();
        }
    }

    return true;
}

size_t OpenAIProvider::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    auto* ctx = static_cast<StreamContext*>(userdata);
    std::string_view chunk(ptr, total);

    size_t start = 0;
    while (start < chunk.size()) {
        auto end = chunk.find('\n', start);
        if (end == std::string_view::npos) {
            *ctx->line_buffer += chunk.substr(start);
            break;
        }
        std::string line = *ctx->line_buffer + std::string(chunk.substr(start, end - start));
        ctx->line_buffer->clear();
        if (!ctx->self->parse_sse_line(line, *ctx->callback, *ctx->line_buffer)) {
            ctx->ok = false;
        }
        start = end + 1;
    }
    return total;
}

Result<void> OpenAIProvider::chat_completion(
    const std::vector<Message>& messages,
    const ChatOptions& options,
    const std::vector<ToolDefinition>& tools,
    StreamCallback callback
) {
    std::string url = base_url_ + "/chat/completions";
    auto body = build_request_body(messages, options, tools);

    auto* curl = curl_easy_init();
    if (!curl) {
        return make_result_error(ErrCode::kInternalError, "Failed to initialize CURL");
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (!api_key_.empty()) {
        std::string auth = "Authorization: Bearer " + api_key_;
        headers = curl_slist_append(headers, auth.c_str());
    }

    for (auto& [hk, hv] : extra_headers_) {
        headers = curl_slist_append(headers, (hk + ": " + hv).c_str());
    }
    for (auto& [hk, hv] : options.extra_headers) {
        headers = curl_slist_append(headers, (hk + ": " + hv).c_str());
    }
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    std::string body_str = body.dump();
    std::string line_buffer;
    StreamContext ctx{this, &callback, &line_buffer, true};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_str.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pi-coding-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTP_TRANSFER_DECODING, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::string err = std::format("CURL error: {}", curl_easy_strerror(res));
        callback(StreamEvent{.type = StreamEventType::kError, .error_message = err});
        return make_result_error(ErrCode::kNetworkError, err);
    }

    if (http_code >= 400) {
        std::string err = std::format("HTTP {} from {} API", http_code, provider_name_);
        callback(StreamEvent{.type = StreamEventType::kError, .error_message = err});
        return make_result_error(ErrCode::kHttpError, err);
    }

    return {};
}

Result<std::vector<Model>> OpenAIProvider::discover_models() {
    std::string url = base_url_ + "/models";

    auto* curl = curl_easy_init();
    if (!curl) {
        return std::unexpected(Error(ErrCode::kInternalError, "Failed to init CURL"));
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!api_key_.empty()) {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());
    }
    for (auto& [hk, hv] : extra_headers_) {
        headers = curl_slist_append(headers, (hk + ": " + hv).c_str());
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        auto* buf = static_cast<std::string*>(userdata);
        buf->append(ptr, size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pi-coding-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return make_result_error(ErrCode::kNetworkError, "CURL error: {}", curl_easy_strerror(res));
    }
    if (http_code != 200) {
        return make_result_error(ErrCode::kHttpError, "HTTP {} listing models for {}", http_code, provider_name_);
    }

    std::vector<Model> models;
    try {
        auto j = nlohmann::json::parse(response);
        if (j.contains("data") && j["data"].is_array()) {
            for (auto& m : j["data"]) {
                Model model;
                model.id       = m.value("id", "");
                model.provider = provider_name_;
                model.api      = "openai-completions";
                model.base_url = base_url_;
                model.name     = m.value("id", model.id);
                model.reasoning = model.id.find("o1") != std::string::npos ||
                                  model.id.find("o3") != std::string::npos ||
                                  model.id.find("reasoner") != std::string::npos;
                // Mark as discovered (no static override)
                models.push_back(std::move(model));
            }
        }
    } catch (const nlohmann::json::exception& e) {
        return make_result_error(ErrCode::kStreamError, "JSON parse error listing models: {}", e.what());
    }

    return models;
}

} // namespace pi