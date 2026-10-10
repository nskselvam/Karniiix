#include "OllamaClient.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

constexpr std::size_t kProtocolLineCapacity = 8192;
constexpr std::size_t kMaximumReplyBytes = 6000;
constexpr std::string_view kChatPrefix = "KARNIX_CHAT:";

std::string socket_error(std::string_view operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}

bool read_line(int fd, std::string& line) {
    line.clear();
    char character = '\0';
    while (line.size() < kProtocolLineCapacity) {
        const ssize_t count = ::read(fd, &character, 1);
        if (count == 0) return !line.empty();
        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(socket_error("Reading QEMU serial socket"));
        }
        if (character == '\n' || character == '\r') {
            if (!line.empty()) return true;
            continue;
        }
        line.push_back(character);
    }
    throw std::runtime_error("QEMU serial protocol line exceeded 8192 bytes.");
}

void write_all(int fd, std::string_view text) {
    while (!text.empty()) {
        const ssize_t count = ::write(fd, text.data(), text.size());
        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(socket_error("Writing QEMU serial socket"));
        }
        if (count == 0) {
            throw std::runtime_error("QEMU serial socket closed during write.");
        }
        text.remove_prefix(static_cast<std::size_t>(count));
    }
}

void send_error(int fd, std::string_view message) {
    std::string reply = "KARNIX_ERROR:";
    for (const char character : message) {
        if (character == '\n' || character == '\r' || character == '\t') {
            reply.push_back(' ');
        } else if (character >= ' ' && character <= '~') {
            reply.push_back(character);
        }
    }
    reply.push_back('\n');
    write_all(fd, reply);
}

std::string create_response(karnix::llm::OllamaClient& client,
                            karnix::llm::Conversation& history,
                            std::string_view model, std::string_view prompt) {
    if (prompt.empty()) {
        throw std::invalid_argument("Prompt cannot be empty.");
    }
    client.require_model(model);
    history.add_user_message(std::string(prompt));
    auto context = history.messages();
    karnix::llm::OllamaClient::trim_history(context, 8000);

    std::string response;
    response.reserve(kMaximumReplyBytes);
    const std::string complete_response = client.chat(
        model, context, [&response](std::string_view token) {
            if (response.size() < kMaximumReplyBytes) {
                const std::size_t remaining =
                    kMaximumReplyBytes - response.size();
                response.append(token.substr(0, remaining));
            }
        });
    history.add_assistant_message(complete_response);
    if (complete_response.size() > kMaximumReplyBytes) {
        response.append(" [reply shortened for the OS console]");
    }
    return response;
}

std::string sanitize_reply(std::string_view response) {
    std::string sanitized;
    sanitized.reserve(response.size());
    bool previous_space = false;
    for (const unsigned char character : response) {
        char output = ' ';
        if (character >= ' ' && character <= '~') {
            output = static_cast<char>(character);
        } else if (character == '\n' || character == '\r' ||
                   character == '\t') {
            output = ' ';
        } else {
            continue;
        }
        if (output == ' ' && previous_space) continue;
        sanitized.push_back(output);
        previous_space = output == ' ';
    }
    return sanitized;
}

void handle_prompt(int fd, karnix::llm::OllamaClient& client,
                   karnix::llm::Conversation& history,
                   std::string_view model, std::string_view prompt) {
    try {
        const std::string response =
            create_response(client, history, model, prompt);
        write_all(fd, "KARNIX_REPLY:");
        write_all(fd, sanitize_reply(response));
        write_all(fd, "\n");
        std::cerr << "MedGemma reply delivered to the KarnixOS console.\n";
    } catch (const std::exception& error) {
        const auto& messages = history.messages();
        if (!messages.empty() && messages.back().role == "user" &&
            messages.back().content == prompt) {
            history.discard_pending_user_message();
        }
        std::cerr << "MedGemma request failed: " << error.what() << '\n';
        send_error(fd, error.what());
    }
}

int run(const std::string& socket_path, const std::string& model,
        const std::string& ollama_url) {
    sockaddr_un address{};
    if (socket_path.empty() || socket_path.size() >= sizeof(address.sun_path)) {
        throw std::invalid_argument("Unix socket path is empty or too long.");
    }

    int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) throw std::runtime_error(socket_error("Creating socket"));

    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) < 0) {
        const std::string error = socket_error("Binding bridge socket");
        ::close(server_fd);
        throw std::runtime_error(error);
    }
    if (::chmod(socket_path.c_str(), S_IRUSR | S_IWUSR) < 0) {
        const std::string error = socket_error("Restricting bridge socket");
        ::close(server_fd);
        ::unlink(socket_path.c_str());
        throw std::runtime_error(error);
    }
    if (::listen(server_fd, 1) < 0) {
        const std::string error = socket_error("Listening on bridge socket");
        ::close(server_fd);
        ::unlink(socket_path.c_str());
        throw std::runtime_error(error);
    }

    std::cerr << "Waiting for the KarnixOS QEMU serial connection.\n"
              << "Ollama endpoint: " << ollama_url << '\n'
              << "Model: " << model << '\n';

    int guest_fd = -1;
    try {
        guest_fd = ::accept(server_fd, nullptr, nullptr);
        if (guest_fd < 0) {
            throw std::runtime_error(socket_error("Accepting QEMU connection"));
        }
        ::close(server_fd);
        server_fd = -1;
        karnix::llm::OllamaClient client(ollama_url);
        karnix::llm::Conversation history;
        std::string line;
        while (read_line(guest_fd, line)) {
            if (line == "KARNIX_CHAT_REPLY_RECEIVED") {
                std::cerr << "KarnixOS guest received the MedGemma reply.\n";
                continue;
            }
            if (line == "KARNIX_CHAT_ERROR_RECEIVED") {
                std::cerr << "KarnixOS guest received a bridge error.\n";
                continue;
            }
            if (!line.starts_with(kChatPrefix)) continue;
            const std::string_view prompt(line.data() + kChatPrefix.size(),
                                          line.size() - kChatPrefix.size());
            handle_prompt(guest_fd, client, history, model, prompt);
        }
        ::close(guest_fd);
        ::unlink(socket_path.c_str());
        return 0;
    } catch (...) {
        if (guest_fd >= 0) ::close(guest_fd);
        if (server_fd >= 0) ::close(server_fd);
        ::unlink(socket_path.c_str());
        throw;
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::signal(SIGPIPE, SIG_IGN);
        std::string socket_path;
        std::string model = karnix::llm::default_model();
        std::string ollama_url = "http://127.0.0.1:11434";
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--socket" || argument == "--model" ||
                argument == "--ollama-url") {
                if (++index >= argc) {
                    throw std::invalid_argument(
                        std::string(argument) + " requires a value.");
                }
                if (argument == "--socket") socket_path = argv[index];
                else if (argument == "--model") model = argv[index];
                else ollama_url = argv[index];
            } else if (argument == "--help" || argument == "-h") {
                std::cout
                    << "Usage: karnix_qemu_medgemma_bridge --socket PATH "
                       "[--model NAME] [--ollama-url URL]\n";
                return 0;
            } else {
                throw std::invalid_argument(
                    "Unknown argument: " + std::string(argument));
            }
        }
        if (socket_path.empty()) {
            throw std::invalid_argument("--socket PATH is required.");
        }
        return run(socket_path, model, ollama_url);
    } catch (const std::exception& error) {
        std::cerr << "KarnixOS MedGemma bridge error: " << error.what() << '\n';
        return 1;
    }
}
