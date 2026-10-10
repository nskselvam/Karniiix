#pragma once

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace karnix::llm {

struct Message {
    Message(std::string message_role, std::string message_content,
            std::vector<std::string> message_images = {})
        : role(std::move(message_role)),
          content(std::move(message_content)),
          images(std::move(message_images)) {}

    std::string role;
    std::string content;
    std::vector<std::string> images;
};

struct RuntimeStatus {
    bool installed = false;
    bool running = false;
    std::vector<std::string> models;
    std::string detail;
};

struct SystemResources {
    std::uint64_t available_memory_bytes = 0;
    std::uintmax_t available_disk_bytes = 0;
};

class Conversation {
public:
    void add_user_message(
        std::string content,
        std::vector<std::string> images = {});
    void add_assistant_message(std::string content);
    void discard_pending_user_message();
    void clear() noexcept;
    const std::vector<Message>& messages() const noexcept;

private:
    std::vector<Message> messages_;
};

class OllamaClient {
public:
    explicit OllamaClient(
        std::string base_url = "http://127.0.0.1:11434",
        std::chrono::milliseconds timeout = std::chrono::minutes(10));

    RuntimeStatus status() const;
    bool has_model(std::string_view model) const;
    void require_model(std::string_view model) const;

    std::string chat(
        std::string_view model,
        const std::vector<Message>& messages,
        const std::function<void(std::string_view)>& on_token = {},
        const volatile std::sig_atomic_t* cancellation_requested = nullptr) const;

    static nlohmann::json make_chat_request(
        std::string_view model,
        const std::vector<Message>& messages,
        std::size_t context_tokens = 4096);

    static std::string load_image_base64(std::string_view path);

    static void trim_history(
        std::vector<Message>& messages,
        std::size_t max_characters = 16000);

private:
    nlohmann::json get_json(std::string_view path) const;
    std::string base_url_;
    std::chrono::milliseconds timeout_;
};

bool ollama_cli_installed();
std::string default_model();
SystemResources available_resources();

}  // namespace karnix::llm
