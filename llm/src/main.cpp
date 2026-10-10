#include "OllamaClient.hpp"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using karnix::llm::OllamaClient;
using karnix::llm::Conversation;
constexpr std::size_t kMaxImagesPerPrompt = 4;
volatile std::sig_atomic_t cancellation_requested = 0;

void handle_interrupt(int) {
    cancellation_requested = 1;
}

void print_help() {
    std::cout
        << "Karnix local LLM commands:\n"
        << "  karnix_llm --status\n"
        << "  karnix_llm \"Explain a matrix\"\n"
        << "  karnix_llm --image scan.png \"Describe this image\"\n"
        << "  karnix_llm --interactive\n"
        << "  karnix_llm --model medgemma1.5:4b --interactive\n\n"
        << "Interactive commands: /help, /clear, /image <path> [prompt], "
           "/model [name], /status, /exit\n"
        << "Attach up to four PNG/JPEG images per prompt; each must be at most "
           "10 MiB.\n"
        << "Model defaults to KARNIX_MODEL or medgemma1.5:4b. Only localhost Ollama "
           "is supported.\n";
}

bool print_status(const OllamaClient& client, const std::string& model) {
    const auto status = client.status();
    const auto resources = karnix::llm::available_resources();
    const bool model_installed =
        std::find(status.models.begin(), status.models.end(), model) !=
        status.models.end();
    std::cout << "KARNIX LOCAL LLM\n"
              << "Runtime: Ollama ("
              << (status.installed ? "CLI installed" : "CLI not found")
              << ")\n"
              << "Model: " << model << "\n"
              << "Configured model: "
              << (model_installed ? "installed" : "not installed") << "\n"
              << "Inference: Local only\n"
              << "Service: " << (status.running ? "running" : "unavailable")
              << " — " << status.detail << '\n';
    std::cout << "Available memory: "
              << resources.available_memory_bytes / (1024ULL * 1024ULL * 1024ULL)
              << " GiB\n"
              << "Available disk: "
              << resources.available_disk_bytes / (1024ULL * 1024ULL * 1024ULL)
              << " GiB\n";
    if (model_installed) {
        std::cout << "The configured model is installed; current resource "
                     "figures are informational, not a performance guarantee.\n";
    } else {
        std::cout << "Check MedGemma's model requirements before downloading; "
                     "this client reports available resources but does not "
                     "predict performance.\n";
    }
    if (!status.models.empty()) {
        std::cout << "Installed models:\n";
        for (const auto& available : status.models) {
            std::cout << "  - " << available << '\n';
        }
    } else if (status.running) {
        std::cout << "No local models are installed.\n";
    }
    if (!status.running && !status.installed) {
        std::cout << "Install Ollama from https://ollama.com/download, "
                     "then open a new terminal.\n";
    } else if (!status.running) {
        std::cout << "Start the local service with: ollama serve\n";
    } else if (!model_installed) {
        std::cout << "To install the configured model explicitly: ollama pull "
                  << model << '\n';
    }
    return status.running && model_installed;
}

std::string generate(OllamaClient& client, const std::string& model,
                     Conversation& history,
                     const std::string& prompt,
                     const std::vector<std::string>& image_paths = {}) {
    if (prompt.empty()) {
        throw std::invalid_argument("Prompt cannot be empty.");
    }
    if (image_paths.size() > kMaxImagesPerPrompt) {
        throw std::invalid_argument("Attach no more than four images per prompt.");
    }
    client.require_model(model);
    std::vector<std::string> images;
    images.reserve(image_paths.size());
    for (const auto& path : image_paths) {
        images.push_back(OllamaClient::load_image_base64(path));
    }
    history.add_user_message(prompt, std::move(images));
    try {
        cancellation_requested = 0;
        if (!image_paths.empty()) std::cout << "[image attached] ";
        const std::string response = client.chat(
            model, history.messages(), [](std::string_view token) {
                std::cout << token << std::flush;
            }, &cancellation_requested);
        history.add_assistant_message(response);
        std::cout << '\n';
        return response;
    } catch (...) {
        history.discard_pending_user_message();
        throw;
    }
}

void parse_image_command(std::string_view line, std::string& path,
                         std::string& prompt) {
    std::istringstream input(std::string(line.substr(7)));
    if (!(input >> std::quoted(path)) || path.empty()) {
        throw std::invalid_argument("Usage: /image <path> [prompt]");
    }
    std::getline(input, prompt);
    const std::size_t first = prompt.find_first_not_of(" \t");
    if (first == std::string::npos) {
        prompt = "Describe the visible contents of this image.";
        return;
    }
    prompt.erase(0, first);
    if (prompt.size() >= 2 && prompt.front() == '"' && prompt.back() == '"') {
        prompt = prompt.substr(1, prompt.size() - 2);
    }
}

int interactive(OllamaClient& client, std::string model) {
    print_status(client, model);
    Conversation history;
    std::string line;
    while (std::cout << "\nYou: " && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line == "/help") {
            print_help();
        } else if (line == "/clear") {
            history.clear();
            std::cout << "Conversation history cleared.\n";
        } else if (line == "/status") {
            print_status(client, model);
        } else if (line == "/exit") {
            break;
        } else if (line == "/image" || line.starts_with("/image ")) {
            try {
                std::string path;
                std::string prompt;
                parse_image_command(line, path, prompt);
                std::cout << "Karnix: ";
                generate(client, model, history, prompt, {path});
            } catch (const std::exception& error) {
                std::cerr << "\nLocal image inference failed: " << error.what()
                          << '\n';
            }
        } else if (line == "/model") {
            print_status(client, model);
        } else if (line.starts_with("/model ")) {
            const std::string requested = line.substr(7);
            try {
                client.require_model(requested);
                model = requested;
                std::cout << "Model set to " << model << ".\n";
            } catch (const std::exception& error) {
                std::cerr << "Error: " << error.what() << '\n';
            }
        } else if (line.front() == '/') {
            std::cerr << "Unknown command. Type /help.\n";
        } else {
            try {
                std::cout << "Karnix: ";
                generate(client, model, history, line);
            } catch (const std::exception& error) {
                std::cerr << "\nLocal inference failed: " << error.what()
                          << '\n';
            }
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::signal(SIGINT, handle_interrupt);
        std::string model = karnix::llm::default_model();
        bool show_status = false;
        bool use_interactive = false;
        std::string prompt;
        std::vector<std::string> image_paths;

        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--help" || argument == "-h") {
                print_help();
                return 0;
            }
            if (argument == "--status") {
                show_status = true;
            } else if (argument == "--interactive") {
                use_interactive = true;
            } else if (argument == "--model") {
                if (++index >= argc) {
                    throw std::invalid_argument("--model requires a name.");
                }
                model = argv[index];
            } else if (argument == "--prompt") {
                if (++index >= argc) {
                    throw std::invalid_argument("--prompt requires text.");
                }
                prompt = argv[index];
            } else if (argument == "--image") {
                if (++index >= argc) {
                    throw std::invalid_argument("--image requires a path.");
                }
                image_paths.emplace_back(argv[index]);
            } else {
                if (argument.starts_with("--")) {
                    throw std::invalid_argument("Unknown argument: " + argument);
                }
                if (!prompt.empty()) prompt += ' ';
                prompt += argument;
            }
        }

        const char* endpoint = std::getenv("KARNIX_OLLAMA_URL");
        OllamaClient client(endpoint ? endpoint : "http://127.0.0.1:11434");
        if (show_status) {
            return print_status(client, model) ? 0 : 1;
        }
        if (use_interactive) {
            if (!prompt.empty() || !image_paths.empty()) {
                throw std::invalid_argument(
                    "--interactive cannot be combined with a prompt or image.");
            }
            return interactive(client, model);
        }
        if (prompt.empty() && !image_paths.empty()) {
            prompt = "Describe the visible contents of this image.";
        }
        if (prompt.empty()) {
            print_help();
            return 2;
        }

        client.require_model(model);
        std::cout << "Karnix: ";
        Conversation history;
        generate(client, model, history, prompt, image_paths);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Karnix local LLM: " << error.what() << '\n';
        return 1;
    }
}
