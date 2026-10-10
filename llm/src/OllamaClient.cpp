#include "OllamaClient.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_host.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif

#include <unistd.h>

namespace karnix::llm {
namespace {

constexpr auto kHealthTimeout = std::chrono::seconds(5);
constexpr std::uintmax_t kMaxImageBytes = 10 * 1024 * 1024;
constexpr std::size_t kMaxImagesInHistory = 4;

std::string encode_base64(std::string_view bytes) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const auto first = static_cast<unsigned char>(bytes[index]);
        const auto second = index + 1 < bytes.size()
                                ? static_cast<unsigned char>(bytes[index + 1])
                                : 0;
        const auto third = index + 2 < bytes.size()
                               ? static_cast<unsigned char>(bytes[index + 2])
                               : 0;
        encoded.push_back(alphabet[first >> 2]);
        encoded.push_back(alphabet[((first & 0x03) << 4) | (second >> 4)]);
        encoded.push_back(index + 1 < bytes.size()
                              ? alphabet[((second & 0x0f) << 2) | (third >> 6)]
                              : '=');
        encoded.push_back(index + 2 < bytes.size() ? alphabet[third & 0x3f]
                                                    : '=');
    }
    return encoded;
}

bool is_supported_image(std::string_view bytes) {
    constexpr unsigned char png_signature[] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
    };
    const bool is_png =
        bytes.size() >= sizeof(png_signature) &&
        std::equal(std::begin(png_signature), std::end(png_signature),
                   reinterpret_cast<const unsigned char*>(bytes.data()));
    const bool is_jpeg = bytes.size() >= 3 &&
                         static_cast<unsigned char>(bytes[0]) == 0xff &&
                         static_cast<unsigned char>(bytes[1]) == 0xd8 &&
                         static_cast<unsigned char>(bytes[2]) == 0xff;
    return is_png || is_jpeg;
}

class CurlGlobal {
public:
    CurlGlobal() {
        const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (result != CURLE_OK) {
            throw std::runtime_error("Could not initialize libcurl");
        }
    }
    ~CurlGlobal() { curl_global_cleanup(); }
};

CurlGlobal& curl_global() {
    static CurlGlobal instance;
    return instance;
}

struct CurlDeleter {
    void operator()(CURL* handle) const { curl_easy_cleanup(handle); }
};

using CurlHandle = std::unique_ptr<CURL, CurlDeleter>;

struct Buffer {
    std::string body;
    std::string error;
};

std::size_t append_body(char* data, std::size_t size, std::size_t count,
                        void* user_data) {
    const std::size_t bytes = size * count;
    auto* response = static_cast<Buffer*>(user_data);
    try {
        response->body.append(data, bytes);
        return bytes;
    } catch (const std::exception& exception) {
        response->error = "Could not buffer Ollama response: " +
                          std::string(exception.what());
        return 0;
    }
}

bool local_http_url(std::string_view url) {
    constexpr std::string_view prefix = "http://";
    if (!url.starts_with(prefix)) return false;

    const std::size_t authority_end = url.find('/', prefix.size());
    const std::string_view authority = url.substr(
        prefix.size(), authority_end == std::string_view::npos
            ? url.size() - prefix.size()
            : authority_end - prefix.size());
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return false;
    }

    const std::size_t colon = authority.find(':');
    const std::string_view host = authority.substr(0, colon);
    if (host != "127.0.0.1" && host != "localhost") return false;
    if (colon == std::string_view::npos) return true;

    const std::string_view port_text = authority.substr(colon + 1);
    if (port_text.empty() ||
        !std::all_of(port_text.begin(), port_text.end(),
                     [](unsigned char character) {
                         return std::isdigit(character) != 0;
                     })) {
        return false;
    }
    try {
        const unsigned long port = std::stoul(std::string(port_text));
        return port > 0 && port <= 65535;
    } catch (const std::exception&) {
        return false;
    }
}

std::chrono::milliseconds timeout_for(std::chrono::milliseconds timeout) {
    return std::max(timeout, std::chrono::milliseconds(1));
}

std::string curl_failure(CURLcode result, std::string_view base_url) {
    if (result == CURLE_COULDNT_CONNECT) {
        return "Cannot connect to local Ollama at " + std::string(base_url) +
               ". Install Ollama and start its local service (ollama serve).";
    }
    if (result == CURLE_OPERATION_TIMEDOUT) {
        return "The local Ollama request timed out.";
    }
    return "Local Ollama request failed: " +
           std::string(curl_easy_strerror(result));
}

nlohmann::json perform_get(std::string_view url,
                           std::chrono::milliseconds timeout) {
    (void)curl_global();
    CurlHandle handle(curl_easy_init());
    if (!handle) throw std::runtime_error("Could not create HTTP request");

    Buffer response;
    const std::string url_string(url);
    curl_easy_setopt(handle.get(), CURLOPT_URL, url_string.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_NOPROXY, "127.0.0.1,localhost");
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS,
                     static_cast<long>(timeout_for(timeout).count()));
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response);

    const CURLcode result = curl_easy_perform(handle.get());
    if (!response.error.empty()) {
        throw std::runtime_error(response.error);
    }
    if (result != CURLE_OK) {
        throw std::runtime_error(curl_failure(result, url));
    }

    long status_code = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status_code);
    if (status_code < 200 || status_code >= 300) {
        throw std::runtime_error("Local Ollama returned HTTP " +
                                 std::to_string(status_code) + ": " +
                                 response.body);
    }
    try {
        return nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("Ollama returned invalid JSON: " +
                                 std::string(error.what()));
    }
}

struct StreamState {
    static constexpr std::string_view kThoughtStart = "<unused94>";
    static constexpr std::string_view kThoughtEnd = "<unused95>";
    static constexpr std::string_view kImageStart = "<start_of_image>";
    static constexpr std::string_view kImageEnd = "<end_of_image>";

    std::string line_buffer;
    std::string content_pending;
    std::string response;
    std::string error;
    std::function<void(std::string_view)> on_token;
    const volatile std::sig_atomic_t* cancellation_requested = nullptr;
    bool done = false;
    bool in_thought = false;

    void emit_visible(std::string_view text) {
        if (text.empty()) return;
        response.append(text);
        if (on_token) on_token(text);
    }

    void filter_content(std::string_view content, bool final_chunk = false) {
        content_pending.append(content);
        while (!content_pending.empty()) {
            if (in_thought) {
                const std::size_t end = content_pending.find(kThoughtEnd);
                if (end != std::string::npos) {
                    content_pending.erase(0, end + kThoughtEnd.size());
                    in_thought = false;
                    continue;
                }
                if (final_chunk) {
                    content_pending.clear();
                    return;
                }
                const std::size_t keep = std::min(
                    content_pending.size(), kThoughtEnd.size() - 1);
                content_pending.erase(0, content_pending.size() - keep);
                return;
            }

            std::size_t marker_position = std::string::npos;
            std::string_view marker;
            const auto consider_marker = [&](std::string_view candidate) {
                const std::size_t position = content_pending.find(candidate);
                if (position != std::string::npos &&
                    (marker_position == std::string::npos ||
                     position < marker_position)) {
                    marker_position = position;
                    marker = candidate;
                }
            };
            consider_marker(kThoughtStart);
            consider_marker(kImageStart);
            consider_marker(kImageEnd);
            if (marker_position != std::string::npos) {
                emit_visible(std::string_view(content_pending)
                                 .substr(0, marker_position));
                content_pending.erase(0, marker_position + marker.size());
                in_thought = marker == kThoughtStart;
                continue;
            }

            if (final_chunk) {
                emit_visible(content_pending);
                content_pending.clear();
                return;
            }

            const std::size_t max_marker_length =
                std::max({kThoughtStart.size(), kImageStart.size(),
                          kImageEnd.size()});
            const std::size_t keep = std::min(content_pending.size(),
                                              max_marker_length - 1);
            const std::size_t visible = content_pending.size() - keep;
            emit_visible(std::string_view(content_pending).substr(0, visible));
            content_pending.erase(0, visible);
            return;
        }
    }

    bool parse_line(std::string_view line) {
        if (line.empty()) return true;
        try {
            const auto event = nlohmann::json::parse(line);
            if (event.contains("error")) {
                error = event.at("error").get<std::string>();
                return false;
            }
            if (event.contains("message") &&
                event.at("message").contains("content")) {
                const std::string content =
                    event.at("message").at("content").get<std::string>();
                try {
                    filter_content(content);
                } catch (const std::exception& exception) {
                    error = "Streaming output callback failed: " +
                            std::string(exception.what());
                    return false;
                }
            }
            if (event.value("done", false)) {
                try {
                    filter_content({}, true);
                } catch (const std::exception& exception) {
                    error = "Streaming output callback failed: " +
                            std::string(exception.what());
                    return false;
                }
                done = true;
            }
            return true;
        } catch (const nlohmann::json::exception& exception) {
            error = "Ollama returned invalid streaming JSON: " +
                    std::string(exception.what());
            return false;
        }
    }

    bool consume(const char* data, std::size_t bytes) {
        line_buffer.append(data, bytes);
        std::size_t newline = 0;
        while ((newline = line_buffer.find('\n')) != std::string::npos) {
            const std::string line = line_buffer.substr(0, newline);
            line_buffer.erase(0, newline + 1);
            if (!parse_line(line)) return false;
        }
        return true;
    }
};

std::size_t stream_body(char* data, std::size_t size, std::size_t count,
                        void* user_data) {
    const std::size_t bytes = size * count;
    auto* state = static_cast<StreamState*>(user_data);
    try {
        return state->consume(data, bytes) ? bytes : 0;
    } catch (const std::exception& exception) {
        state->error = "Could not process Ollama stream: " +
                       std::string(exception.what());
        return 0;
    }
}

int check_cancellation(void* user_data, curl_off_t, curl_off_t,
                       curl_off_t, curl_off_t) {
    const auto* state = static_cast<const StreamState*>(user_data);
    return state->cancellation_requested &&
                   *state->cancellation_requested != 0
               ? 1
               : 0;
}

std::string path_executable(std::string_view name) {
    const char* path_value = std::getenv("PATH");
    if (!path_value) return {};
    std::stringstream paths(path_value);
    std::string directory;
    while (std::getline(paths, directory, ':')) {
        if (directory.empty()) directory = ".";
        const std::filesystem::path candidate =
            std::filesystem::path(directory) / name;
        if (::access(candidate.c_str(), X_OK) == 0) return candidate.string();
    }
    return {};
}

}  // namespace

OllamaClient::OllamaClient(std::string base_url,
                           std::chrono::milliseconds timeout)
    : base_url_(std::move(base_url)), timeout_(timeout_for(timeout)) {
    while (!base_url_.empty() && base_url_.back() == '/') {
        base_url_.pop_back();
    }
    if (!local_http_url(base_url_)) {
        throw std::invalid_argument(
            "Karnix only permits an HTTP Ollama endpoint on localhost "
            "(127.0.0.1 or localhost).");
    }
}

nlohmann::json OllamaClient::get_json(std::string_view path) const {
    return perform_get(base_url_ + std::string(path), kHealthTimeout);
}

RuntimeStatus OllamaClient::status() const {
    RuntimeStatus result;
    result.installed = ollama_cli_installed();
    try {
        const auto response = get_json("/api/tags");
        if (!response.contains("models") || !response.at("models").is_array()) {
            throw std::runtime_error(
                "Ollama model-list response has no models array.");
        }
        for (const auto& model : response.at("models")) {
            if (model.contains("name") && model.at("name").is_string()) {
                result.models.push_back(model.at("name").get<std::string>());
            }
        }
        result.running = true;
        result.detail = "Connected to local Ollama.";
    } catch (const std::exception& error) {
        result.detail = error.what();
    }
    return result;
}

void Conversation::add_user_message(std::string content,
                                    std::vector<std::string> images) {
    messages_.push_back(
        {"user", std::move(content), std::move(images)});
    OllamaClient::trim_history(messages_);
}

void Conversation::add_assistant_message(std::string content) {
    messages_.push_back({"assistant", std::move(content)});
    OllamaClient::trim_history(messages_);
}

void Conversation::discard_pending_user_message() {
    if (!messages_.empty() && messages_.back().role == "user") {
        messages_.pop_back();
    }
}

void Conversation::clear() noexcept {
    messages_.clear();
}

const std::vector<Message>& Conversation::messages() const noexcept {
    return messages_;
}

bool OllamaClient::has_model(std::string_view model) const {
    const auto response = get_json("/api/tags");
    if (!response.contains("models") || !response.at("models").is_array()) {
        throw std::runtime_error(
            "Ollama model-list response has no models array.");
    }
    return std::any_of(
        response.at("models").begin(), response.at("models").end(),
        [model](const nlohmann::json& entry) {
            return entry.contains("name") && entry.at("name").is_string() &&
                   entry.at("name").get<std::string>() == model;
        });
}

void OllamaClient::require_model(std::string_view model) const {
    if (model.empty()) {
        throw std::invalid_argument("Model name cannot be empty.");
    }
    if (!has_model(model)) {
        throw std::runtime_error(
            "Model '" + std::string(model) +
            "' is not installed locally. Download it explicitly with: "
            "ollama pull " + std::string(model));
    }
}

nlohmann::json OllamaClient::make_chat_request(
    std::string_view model, const std::vector<Message>& messages,
    std::size_t context_tokens) {
    if (model.empty()) throw std::invalid_argument("Model name cannot be empty.");
    if (messages.empty()) throw std::invalid_argument("A prompt is required.");

    nlohmann::json serialized_messages = nlohmann::json::array();
    for (const auto& message : messages) {
        if (message.role != "user" && message.role != "assistant" &&
            message.role != "system") {
            throw std::invalid_argument("Unsupported conversation role.");
        }
        if (message.images.size() > kMaxImagesInHistory) {
            throw std::invalid_argument(
                "A message can contain no more than four images.");
        }
        nlohmann::json serialized_message = {
            {"role", message.role},
            {"content", message.content},
        };
        if (!message.images.empty()) {
            serialized_message["images"] = message.images;
        }
        serialized_messages.push_back(std::move(serialized_message));
    }
    return {
        {"model", model},
        {"messages", std::move(serialized_messages)},
        {"stream", true},
        {"options", {{"num_ctx", context_tokens}}},
    };
}

std::string OllamaClient::load_image_base64(std::string_view path) {
    if (path.empty()) throw std::invalid_argument("Image path cannot be empty.");
    std::ifstream image_file(std::string(path), std::ios::binary | std::ios::ate);
    if (!image_file) {
        throw std::runtime_error("Cannot open image file: " + std::string(path));
    }

    const std::streampos end = image_file.tellg();
    if (end <= 0) {
        throw std::runtime_error("Image file is empty: " + std::string(path));
    }
    const auto image_size = static_cast<std::uintmax_t>(end);
    if (image_size > kMaxImageBytes) {
        throw std::runtime_error(
            "Image exceeds the 10 MiB limit: " + std::string(path));
    }

    std::string bytes(static_cast<std::size_t>(image_size), '\0');
    image_file.seekg(0, std::ios::beg);
    if (!image_file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Could not read image file: " +
                                 std::string(path));
    }
    if (!is_supported_image(bytes)) {
        throw std::runtime_error(
            "Unsupported image format for '" + std::string(path) +
            "'. Use a PNG or JPEG image.");
    }
    return encode_base64(bytes);
}

void OllamaClient::trim_history(std::vector<Message>& messages,
                                std::size_t max_characters) {
    std::size_t image_count = 0;
    for (const auto& message : messages) image_count += message.images.size();
    for (auto& message : messages) {
        while (image_count > kMaxImagesInHistory && !message.images.empty()) {
            message.images.erase(message.images.begin());
            --image_count;
        }
    }

    const auto total_characters = [&messages]() {
        std::size_t total = 0;
        for (const auto& message : messages) total += message.content.size();
        return total;
    };
    while (messages.size() > 1 && total_characters() > max_characters) {
        auto first_conversation_message = messages.begin();
        if (first_conversation_message != messages.end() &&
            first_conversation_message->role == "system") {
            ++first_conversation_message;
        }
        if (first_conversation_message == messages.end() ||
            std::next(first_conversation_message) == messages.end()) {
            break;
        }
        messages.erase(first_conversation_message,
                       std::next(first_conversation_message, 2));
    }
}

std::string OllamaClient::chat(
    std::string_view model, const std::vector<Message>& messages,
    const std::function<void(std::string_view)>& on_token,
    const volatile std::sig_atomic_t* cancellation_requested) const {
    (void)curl_global();
    const auto payload = make_chat_request(model, messages);
    const std::string request_body = payload.dump();
    CurlHandle handle(curl_easy_init());
    if (!handle) throw std::runtime_error("Could not create HTTP request");

    struct curl_slist* raw_headers = nullptr;
    raw_headers = curl_slist_append(raw_headers, "Content-Type: application/json");
    if (!raw_headers) {
        throw std::runtime_error("Could not allocate HTTP request headers");
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        raw_headers, curl_slist_free_all);

    StreamState state;
    state.on_token = on_token;
    state.cancellation_requested = cancellation_requested;
    const std::string url = base_url_ + "/api/chat";
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(request_body.size()));
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(handle.get(), CURLOPT_NOPROXY, "127.0.0.1,localhost");
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS,
                     static_cast<long>(timeout_.count()));
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, stream_body);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION, check_cancellation);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, &state);

    const CURLcode result = curl_easy_perform(handle.get());
    if (!state.error.empty()) throw std::runtime_error(state.error);
    if (result == CURLE_ABORTED_BY_CALLBACK && cancellation_requested &&
        *cancellation_requested != 0) {
        throw std::runtime_error("Generation cancelled by Ctrl+C.");
    }
    if (result != CURLE_OK) {
        throw std::runtime_error(curl_failure(result, base_url_));
    }

    long status_code = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status_code);
    if (status_code < 200 || status_code >= 300) {
        throw std::runtime_error("Local Ollama returned HTTP " +
                                 std::to_string(status_code));
    }
    if (!state.line_buffer.empty() && !state.parse_line(state.line_buffer)) {
        throw std::runtime_error(state.error);
    }
    if (!state.done) {
        throw std::runtime_error(
            "Ollama stream ended before a complete response was received.");
    }
    return state.response;
}

bool ollama_cli_installed() {
    return !path_executable("ollama").empty();
}

std::string default_model() {
    if (const char* configured = std::getenv("KARNIX_MODEL");
        configured && *configured) {
        return configured;
    }
    return "medgemma1.5:4b";
}

SystemResources available_resources() {
    SystemResources result;
    std::error_code error;
    result.available_disk_bytes =
        std::filesystem::space(std::filesystem::current_path(), error).available;

#ifdef __APPLE__
    const mach_port_t host = mach_host_self();
    vm_statistics64_data_t statistics{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_size_t page_size = 0;
    if (host_page_size(host, &page_size) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64,
                         reinterpret_cast<host_info64_t>(&statistics),
                         &count) == KERN_SUCCESS) {
        result.available_memory_bytes =
            static_cast<std::uint64_t>(statistics.free_count +
                                       statistics.inactive_count +
                                       statistics.speculative_count) *
            page_size;
    }
    mach_port_deallocate(mach_task_self(), host);
#elif defined(__linux__)
    struct sysinfo information {};
    if (sysinfo(&information) == 0) {
        result.available_memory_bytes =
            static_cast<std::uint64_t>(information.freeram +
                                       information.bufferram) *
            information.mem_unit;
    }
#endif
    return result;
}

}  // namespace karnix::llm
