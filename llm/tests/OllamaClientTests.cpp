#include "OllamaClient.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

class TestHttpServer {
public:
    explicit TestHttpServer(std::string response_body, int delay_ms = 0)
        : response_body_(std::move(response_body)), delay_ms_(delay_ms) {
        socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) throw std::runtime_error("socket() failed");

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(socket_, reinterpret_cast<sockaddr*>(&address),
                   sizeof(address)) != 0 ||
            ::listen(socket_, 1) != 0) {
            ::close(socket_);
            throw std::runtime_error("Could not start local test server");
        }

        socklen_t length = sizeof(address);
        if (::getsockname(socket_, reinterpret_cast<sockaddr*>(&address),
                          &length) != 0) {
            ::close(socket_);
            throw std::runtime_error("getsockname() failed");
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serve_one_request(); });
    }

    ~TestHttpServer() {
        if (socket_ >= 0) {
            ::shutdown(socket_, SHUT_RDWR);
            ::close(socket_);
            socket_ = -1;
        }
        if (worker_.joinable()) worker_.join();
    }

    std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    std::string request_body() const { return request_body_; }

    void join() {
        if (worker_.joinable()) worker_.join();
    }

private:
    void serve_one_request() {
        const int client = ::accept(socket_, nullptr, nullptr);
        if (client < 0) return;

        std::string request;
        char buffer[4096];
        std::size_t header_end = std::string::npos;
        std::size_t content_length = 0;
        while ((header_end = request.find("\r\n\r\n")) == std::string::npos) {
            const ssize_t received = ::recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) break;
            request.append(buffer, static_cast<std::size_t>(received));
        }
        if (header_end != std::string::npos) {
            const std::string headers = request.substr(0, header_end);
            const std::string marker = "Content-Length:";
            const std::size_t length_pos = headers.find(marker);
            if (length_pos != std::string::npos) {
                content_length = static_cast<std::size_t>(
                    std::stoul(headers.substr(length_pos + marker.size())));
            }
            const std::size_t body_start = header_end + 4;
            while (request.size() - body_start < content_length) {
                const ssize_t received = ::recv(client, buffer, sizeof(buffer), 0);
                if (received <= 0) break;
                request.append(buffer, static_cast<std::size_t>(received));
            }
            request_body_ = request.substr(body_start, content_length);
        }

        if (delay_ms_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        }
        const std::string response =
            "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\n"
            "Content-Length: " +
            std::to_string(response_body_.size()) +
            "\r\nConnection: close\r\n\r\n" + response_body_;
        (void)::send(client, response.data(), response.size(), 0);
        ::shutdown(client, SHUT_RDWR);
        ::close(client);
    }

    int socket_ = -1;
    unsigned short port_ = 0;
    std::string response_body_;
    int delay_ms_ = 0;
    std::string request_body_;
    std::thread worker_;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_request_payload_and_context() {
    using karnix::llm::Message;
    using karnix::llm::OllamaClient;
    const std::vector<Message> messages = {
        {"user", "Explain pointers."},
        {"assistant", "A pointer stores an address."},
        {"user", "What about references?"},
    };
    const auto request = OllamaClient::make_chat_request(
        "medgemma1.5:4b", messages, 2048);
    require(request.at("model") == "medgemma1.5:4b", "Model was not serialized.");
    require(request.at("stream") == true, "Streaming must be enabled.");
    require(request.at("options").at("num_ctx") == 2048,
            "Context limit was not serialized.");
    require(request.at("messages").size() == 3,
            "Conversation history was omitted.");

    auto history = messages;
    history.push_back({"assistant", "A reference is an alias."});
    history.push_back({"user", "Newest turn"});
    OllamaClient::trim_history(history, 40);
    require(history.back().content == "Newest turn",
            "History trimming removed the current prompt.");
    require(history.size() < 5, "Old conversation history was not trimmed.");
}

void test_image_file_is_encoded_and_serialized() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("karnix-test-image-" + std::to_string(::getpid()) +
                       ".png");
    const unsigned char png_signature[] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
    };
    {
        std::ofstream image(path, std::ios::binary);
        image.write(reinterpret_cast<const char*>(png_signature),
                    sizeof(png_signature));
        require(static_cast<bool>(image), "Could not create PNG test file.");
    }

    const std::string encoded =
        karnix::llm::OllamaClient::load_image_base64(path.string());
    std::filesystem::remove(path);
    require(encoded == "iVBORw0KGgo=",
            "PNG image was not encoded as base64.");

    const auto request = karnix::llm::OllamaClient::make_chat_request(
        "medgemma1.5:4b",
        {{"user", "Describe this image.", {encoded}}});
    require(request.at("messages").at(0).at("images").size() == 1,
            "Image data was omitted from the Ollama request.");
    require(request.at("messages").at(0).at("images").at(0) == encoded,
            "Image data changed while serializing the Ollama request.");

    const auto jpeg_path =
        std::filesystem::temp_directory_path() /
        ("karnix-test-image-" + std::to_string(::getpid()) + ".jpg");
    const unsigned char jpeg_signature[] = {0xff, 0xd8, 0xff};
    {
        std::ofstream image(jpeg_path, std::ios::binary);
        image.write(reinterpret_cast<const char*>(jpeg_signature),
                    sizeof(jpeg_signature));
        require(static_cast<bool>(image), "Could not create JPEG test file.");
    }
    const std::string jpeg_encoded =
        karnix::llm::OllamaClient::load_image_base64(jpeg_path.string());
    std::filesystem::remove(jpeg_path);
    require(jpeg_encoded == "/9j/",
            "JPEG image was not encoded as base64.");
}

void test_image_loader_rejects_non_images() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("karnix-test-not-image-" + std::to_string(::getpid()));
    {
        std::ofstream file(path, std::ios::binary);
        file << "not an image";
        require(static_cast<bool>(file), "Could not create invalid test file.");
    }

    bool rejected = false;
    try {
        (void)karnix::llm::OllamaClient::load_image_base64(path.string());
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("PNG or JPEG") !=
                   std::string::npos;
    }
    std::filesystem::remove(path);
    require(rejected, "Non-image input was not rejected with a useful error.");
}

void test_conversation_history_can_be_cleared() {
    karnix::llm::Conversation conversation;
    conversation.add_user_message("First question");
    conversation.add_assistant_message("First answer");
    conversation.add_user_message("Follow-up question");
    require(conversation.messages().size() == 3,
            "Conversation turns were not retained.");
    conversation.clear();
    require(conversation.messages().empty(),
            "Clearing a conversation retained prior messages.");
}

void test_image_context_is_retained_and_bounded() {
    karnix::llm::Conversation conversation;
    conversation.add_user_message("Describe this.", {"cG5n"});
    conversation.add_assistant_message("A test image.");
    conversation.add_user_message("What detail did you notice?");
    require(conversation.messages().front().images.size() == 1,
            "Image context was discarded before a follow-up turn.");

    std::vector<karnix::llm::Message> history = {
        {"user", "old", {"one", "two", "three"}},
        {"assistant", "answer"},
        {"user", "new", {"four", "five", "six"}},
    };
    karnix::llm::OllamaClient::trim_history(history);
    std::size_t image_count = 0;
    for (const auto& message : history) image_count += message.images.size();
    require(image_count == 4,
            "Image history exceeded its four-image context limit.");
    require(history.back().images.size() == 3,
            "Recent image context was not retained during trimming.");
}

void test_streamed_chat_preserves_history() {
    const std::string body =
        "{\"message\":{\"content\":\"Hello \"},\"done\":false}\n"
        "{\"message\":{\"content\":\"from local Ollama.\"},\"done\":true}\n";
    TestHttpServer server(body);
    karnix::llm::OllamaClient client(server.url(),
                                     std::chrono::seconds(2));
    std::string streamed;
    const std::vector<karnix::llm::Message> history = {
        {"user", "Explain pointers."},
        {"assistant", "A pointer stores an address."},
        {"user", "What about references?"},
    };
    const std::string response = client.chat(
        "medgemma1.5:4b", history,
        [&streamed](std::string_view token) { streamed.append(token); });
    server.join();
    require(response == "Hello from local Ollama.",
            "Streamed response content was not assembled.");
    require(streamed == response, "Streaming callback output differed.");
    const auto request =
        nlohmann::json::parse(server.request_body());
    require(request.at("messages").size() == 3,
            "HTTP request did not include conversation history.");
    require(request.at("messages").at(1).at("role") == "assistant",
            "Assistant turn was not included in the request.");
}

void test_internal_thought_tokens_are_not_exposed() {
    const std::string body =
        "{\"message\":{\"content\":\"<start_of_ima\"},\"done\":false}\n"
        "{\"message\":{\"content\":\"ge>Final answer starts. <unused94>tho\"},"
        "\"done\":false}\n"
        "{\"message\":{\"content\":\"ught private reasoning<unused95>Public "
        "answer.<end_of_ima\"},\"done\":false}\n"
        "{\"message\":{\"content\":\"ge>\"},\"done\":true}\n";
    TestHttpServer server(body);
    karnix::llm::OllamaClient client(server.url(),
                                     std::chrono::seconds(2));
    std::string streamed;
    const std::string response = client.chat(
        "medgemma1.5:4b", {{"user", "Test thought filtering"}},
        [&streamed](std::string_view token) { streamed.append(token); });
    server.join();
    require(response == "Final answer starts. Public answer.",
            "Internal thought content was included in the response.");
    require(streamed == response,
            "Internal thought content was sent to the streaming callback.");
}

void test_runtime_status_and_missing_model() {
    TestHttpServer server(
        "{\"models\":[{\"name\":\"medgemma1.5:4b\"}]}\n");
    karnix::llm::OllamaClient client(server.url(),
                                     std::chrono::seconds(2));
    const auto status = client.status();
    require(status.running, "A reachable local server was not detected.");
    require(status.models.size() == 1 && status.models[0] == "medgemma1.5:4b",
            "Installed local model list was not parsed.");
    server.join();

    TestHttpServer empty_server("{\"models\":[]}\n");
    karnix::llm::OllamaClient empty_client(empty_server.url(),
                                           std::chrono::seconds(2));
    bool actionable = false;
    try {
        empty_client.require_model("medgemma1.5:4b");
    } catch (const std::runtime_error& error) {
        actionable = std::string(error.what()).find(
                         "ollama pull medgemma1.5:4b") != std::string::npos;
    }
    empty_server.join();
    require(actionable, "Missing model did not include installation guidance.");
}

void test_invalid_stream_is_reported() {
    TestHttpServer server("this is not json\n");
    karnix::llm::OllamaClient client(server.url(),
                                     std::chrono::seconds(2));
    bool failed = false;
    try {
        (void)client.chat("medgemma1.5:4b", {{"user", "Test"}});
    } catch (const std::runtime_error& error) {
        failed = std::string(error.what()).find("invalid streaming JSON") !=
                 std::string::npos;
    }
    server.join();
    require(failed, "Malformed streaming JSON did not produce an error.");
}

void test_stopped_runtime_is_reported() {
    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    require(socket_fd >= 0, "Could not create a closed-port test socket.");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    require(::bind(socket_fd, reinterpret_cast<sockaddr*>(&address),
                   sizeof(address)) == 0,
            "Could not reserve a local test port.");
    socklen_t length = sizeof(address);
    require(::getsockname(socket_fd, reinterpret_cast<sockaddr*>(&address),
                          &length) == 0,
            "Could not inspect the local test port.");
    const unsigned short port = ntohs(address.sin_port);
    ::close(socket_fd);

    karnix::llm::OllamaClient client(
        "http://127.0.0.1:" + std::to_string(port),
        std::chrono::seconds(1));
    const auto status = client.status();
    require(!status.running && !status.detail.empty(),
            "An unavailable local runtime was not reported.");
}

void test_request_timeout_is_reported() {
    TestHttpServer server(
        "{\"message\":{\"content\":\"late\"},\"done\":true}\n", 250);
    karnix::llm::OllamaClient client(server.url(),
                                     std::chrono::milliseconds(50));
    bool timed_out = false;
    try {
        (void)client.chat("medgemma1.5:4b", {{"user", "Wait"}});
    } catch (const std::runtime_error& error) {
        timed_out = std::string(error.what()).find("timed out") !=
                    std::string::npos;
    }
    server.join();
    require(timed_out, "Timed-out request did not return a useful error.");
}

void test_generation_can_be_cancelled() {
    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    require(socket_fd >= 0, "Could not create a cancellation test socket.");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    require(::bind(socket_fd, reinterpret_cast<sockaddr*>(&address),
                   sizeof(address)) == 0,
            "Could not reserve a cancellation test port.");
    socklen_t length = sizeof(address);
    require(::getsockname(socket_fd, reinterpret_cast<sockaddr*>(&address),
                          &length) == 0,
            "Could not inspect the cancellation test port.");
    const unsigned short port = ntohs(address.sin_port);
    ::close(socket_fd);

    karnix::llm::OllamaClient client(
        "http://127.0.0.1:" + std::to_string(port),
        std::chrono::seconds(2));
    volatile std::sig_atomic_t cancellation_requested = 1;
    bool cancelled = false;
    try {
        (void)client.chat("medgemma1.5:4b", {{"user", "Cancel"}},
                          {}, &cancellation_requested);
    } catch (const std::runtime_error& error) {
        cancelled = std::string(error.what()).find("cancelled by Ctrl+C") !=
                    std::string::npos;
    }
    require(cancelled, "A cancelled generation did not report cancellation.");
}

void test_external_endpoint_is_rejected() {
    bool rejected = false;
    try {
        karnix::llm::OllamaClient client("https://example.com");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "A non-local inference endpoint was accepted.");
}

}  // namespace

int main() {
    try {
        test_request_payload_and_context();
        test_image_file_is_encoded_and_serialized();
        test_image_loader_rejects_non_images();
        test_conversation_history_can_be_cleared();
        test_image_context_is_retained_and_bounded();
        test_streamed_chat_preserves_history();
        test_internal_thought_tokens_are_not_exposed();
        test_runtime_status_and_missing_model();
        test_invalid_stream_is_reported();
        test_stopped_runtime_is_reported();
        test_request_timeout_is_reported();
        test_generation_can_be_cancelled();
        test_external_endpoint_is_rejected();
        std::cout << "All local LLM client tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
