#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_ttf.h>

#include "BrainTumourCNN.hpp"

#include <algorithm>
#include <chrono>
#include <dirent.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int WINDOW_WIDTH = 1440;
constexpr int WINDOW_HEIGHT = 900;
const std::vector<std::string> LABELS = {"GLIOMA", "MENINGIOMA", "NO TUMOR", "PITUITARY"};
const std::vector<std::string> COLORS = {"red", "amber", "green", "blue"};

struct Color { Uint8 r; Uint8 g; Uint8 b; Uint8 a = 255; };
struct Sample { std::string path; std::string label; };

void set_color(SDL_Renderer* renderer, Color color) { SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a); }
void fill(SDL_Renderer* renderer, SDL_Rect rectangle, Color color) { set_color(renderer, color); SDL_RenderFillRect(renderer, &rectangle); }
void stroke(SDL_Renderer* renderer, SDL_Rect rectangle, Color color) { set_color(renderer, color); SDL_RenderDrawRect(renderer, &rectangle); }

void text(SDL_Renderer* renderer, TTF_Font* font, const std::string& value, int x, int y, Color color) {
    SDL_Color c{color.r, color.g, color.b, color.a};
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, value.c_str(), c);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect target{x, y, surface->w, surface->h};
    SDL_FreeSurface(surface);
    if (texture) { SDL_RenderCopy(renderer, texture, nullptr, &target); SDL_DestroyTexture(texture); }
}

std::string basename(const std::string& path) {
    std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool has_suffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<Sample> scan_samples(const std::string& root) {
    std::vector<Sample> samples;
    for (const auto& label : {"glioma_tumor", "meningioma_tumor", "no_tumor", "pituitary_tumor"}) {
        std::string directory = root + "/" + label;
        DIR* handle = opendir(directory.c_str());
        if (!handle) continue;
        dirent* entry = nullptr;
        while ((entry = readdir(handle)) != nullptr) {
            std::string name = entry->d_name;
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (has_suffix(lower, ".jpg") || has_suffix(lower, ".jpeg") || has_suffix(lower, ".png")) {
                samples.push_back({directory + "/" + name, label});
            }
        }
        closedir(handle);
    }
    std::sort(samples.begin(), samples.end(), [](const Sample& left, const Sample& right) { return left.path < right.path; });
    return samples;
}

SDL_Texture* image_texture(SDL_Renderer* renderer, const RawImage& image) {
    std::vector<Uint32> pixels(image.width * image.height);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        Uint8 gray = static_cast<Uint8>(std::max(0.0, std::min(255.0, image.pixels[index] * 255.0)));
        pixels[index] = 0xff000000u | (static_cast<Uint32>(gray) << 16) | (static_cast<Uint32>(gray) << 8) | gray;
    }
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
    if (texture) { SDL_UpdateTexture(texture, nullptr, pixels.data(), image.width * sizeof(Uint32)); SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear); }
    return texture;
}

Color class_color(int index) {
    static const Color colors[] = {{245, 92, 92}, {247, 184, 72}, {61, 201, 151}, {99, 160, 255}};
    return colors[index % 4];
}

void draw_stage(SDL_Renderer* renderer, TTF_Font* small, int x, int y, int width, const std::string& number, const std::string& title, const std::string& detail, bool active) {
    Color accent = active ? Color{99, 160, 255} : Color{85, 99, 115};
    fill(renderer, {x, y, width, 58}, active ? Color{23, 39, 59} : Color{17, 24, 32});
    stroke(renderer, {x, y, width, 58}, accent);
    text(renderer, small, number, x + 14, y + 8, accent);
    text(renderer, small, title, x + 54, y + 8, Color{235, 242, 249});
    text(renderer, small, detail, x + 54, y + 31, Color{151, 167, 184});
}

} // namespace

int main(int argc, char** argv) {
    std::string directory = argc > 1 ? argv[1] : "data/brain_tumor/Testing";
    std::string model_path = argc > 2 ? argv[2] : "models/brain_tumour_cnn.weights";
    std::vector<Sample> samples = scan_samples(directory);
    if (samples.empty()) throw std::runtime_error("No brain MRI images found in: " + directory);

    BrainTumourCNN model;
    model.load_weights(model_path);
    if (!model.are_weights_loaded()) throw std::runtime_error("Cannot load model: " + model_path);

    if (SDL_Init(SDL_INIT_VIDEO) != 0 || TTF_Init() != 0) throw std::runtime_error("SDL2 initialization failed");
    SDL_Window* window = SDL_CreateWindow("KARNIX // BRAIN MRI CNN", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window || !renderer) throw std::runtime_error("Cannot create visualizer window");

    const std::vector<std::string> font_paths = {
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/opt/homebrew/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    TTF_Font* heading = nullptr;
    TTF_Font* body = nullptr;
    for (const auto& path : font_paths) {
        if (!heading) heading = TTF_OpenFont(path.c_str(), 28);
        if (!body) body = TTF_OpenFont(path.c_str(), 16);
    }
    if (!heading || !body) throw std::runtime_error("No usable system font found");

    bool running = true;
    std::size_t sample_index = 0;
    Uint32 last_change = SDL_GetTicks();
    const Uint32 frame_duration = 3500;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RIGHT) { sample_index = (sample_index + 1) % samples.size(); last_change = SDL_GetTicks(); }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_LEFT) { sample_index = (sample_index + samples.size() - 1) % samples.size(); last_change = SDL_GetTicks(); }
        }
        if (SDL_GetTicks() - last_change > frame_duration) { sample_index = (sample_index + 1) % samples.size(); last_change = SDL_GetTicks(); }

        const Sample& sample = samples[sample_index];
        RawImage image = BrainImageIO::load_image(sample.path);
        auto inference_started = std::chrono::steady_clock::now();
        Tensor probabilities = model.forward(model.preprocess(sample.path));
        double inference_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inference_started).count();
        const auto& values = probabilities.get_data();
        int best = static_cast<int>(std::distance(values.begin(), std::max_element(values.begin(), values.end())));

        fill(renderer, {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT}, Color{8, 13, 20});
        fill(renderer, {0, 0, WINDOW_WIDTH, 74}, Color{13, 23, 34});
        text(renderer, heading, "KARNIX / BRAIN MRI CNN", 36, 20, Color{232, 242, 252});
        text(renderer, body, "FOUR-CLASS INFERENCE CONSOLE", 1048, 29, Color{113, 174, 255});
        text(renderer, body, "← → change scan   ESC close", 36, 850, Color{126, 143, 160});

        SDL_Rect image_panel{36, 108, 470, 620};
        fill(renderer, image_panel, Color{15, 23, 32});
        stroke(renderer, image_panel, Color{54, 74, 96});
        text(renderer, body, "CURRENT MRI SLICE", image_panel.x + 22, image_panel.y + 20, Color{137, 183, 235});
        SDL_Texture* texture = image_texture(renderer, image);
        SDL_Rect image_target{image_panel.x + 30, image_panel.y + 66, 410, 410};
        if (texture) { SDL_RenderCopy(renderer, texture, nullptr, &image_target); SDL_DestroyTexture(texture); }
        stroke(renderer, image_target, Color{92, 119, 147});
        text(renderer, body, "FILE  " + basename(sample.path), image_panel.x + 22, image_panel.y + 500, Color{221, 229, 237});
        text(renderer, body, "SOURCE LABEL  " + sample.label, image_panel.x + 22, image_panel.y + 532, Color{151, 167, 184});
        text(renderer, body, "INFERENCE  " + std::to_string(inference_ms).substr(0, 6) + " ms", image_panel.x + 22, image_panel.y + 564, Color{151, 167, 184});

        SDL_Rect result_panel{544, 108, 410, 620};
        fill(renderer, result_panel, Color{15, 23, 32});
        stroke(renderer, result_panel, class_color(best));
        text(renderer, body, "MODEL DECISION", result_panel.x + 24, result_panel.y + 22, Color{137, 183, 235});
        text(renderer, heading, LABELS[best], result_panel.x + 24, result_panel.y + 62, class_color(best));
        text(renderer, body, "TOP-1 CONFIDENCE  " + std::to_string(values[best] * 100.0).substr(0, 5) + "%", result_panel.x + 24, result_panel.y + 108, Color{235, 242, 249});
        for (int index = 0; index < 4; ++index) {
            int y = result_panel.y + 176 + index * 82;
            text(renderer, body, LABELS[index], result_panel.x + 24, y, Color{207, 219, 231});
            SDL_Rect track{result_panel.x + 24, y + 29, 350, 18};
            fill(renderer, track, Color{36, 50, 65});
            fill(renderer, {track.x, track.y, static_cast<int>(track.w * values[index]), track.h}, class_color(index));
            text(renderer, body, std::to_string(values[index] * 100.0).substr(0, 5) + "%", result_panel.x + 300, y, Color{214, 225, 235});
        }
        text(renderer, body, "Four probabilities, one selected class", result_panel.x + 24, result_panel.y + 548, Color{126, 143, 160});

        SDL_Rect pipeline_panel{992, 108, 412, 620};
        fill(renderer, pipeline_panel, Color{15, 23, 32});
        stroke(renderer, pipeline_panel, Color{54, 74, 96});
        text(renderer, body, "WHAT THE CNN DID", pipeline_panel.x + 22, pipeline_panel.y + 22, Color{137, 183, 235});
        draw_stage(renderer, body, pipeline_panel.x + 22, pipeline_panel.y + 64, 368, "01", "INPUT", "MRI grayscale 64 x 64", true);
        draw_stage(renderer, body, pipeline_panel.x + 22, pipeline_panel.y + 130, 368, "02", "CONV BLOCKS", "learn edges -> textures -> patterns", true);
        draw_stage(renderer, body, pipeline_panel.x + 22, pipeline_panel.y + 196, 368, "03", "POOLING", "compress spatial evidence", true);
        draw_stage(renderer, body, pipeline_panel.x + 22, pipeline_panel.y + 262, 368, "04", "DENSE HEAD", "576 features -> 32 activations", true);
        draw_stage(renderer, body, pipeline_panel.x + 22, pipeline_panel.y + 328, 368, "05", "SOFTMAX", "four class probabilities", true);
        text(renderer, body, "This is a screening model, not a diagnosis.", pipeline_panel.x + 22, pipeline_panel.y + 430, Color{245, 184, 72});
        text(renderer, body, "Review the original MRI with a qualified clinician.", pipeline_panel.x + 22, pipeline_panel.y + 462, Color{151, 167, 184});

        fill(renderer, {36, 760, 1368, 54}, Color{13, 23, 34});
        text(renderer, body, "SCAN " + std::to_string(sample_index + 1) + "/" + std::to_string(samples.size()), 58, 778, Color{137, 183, 235});
        text(renderer, body, "MODEL  BrainTumourCNN  |  4 classes  |  C++", 260, 778, Color{151, 167, 184});
        text(renderer, body, "STATUS  READY", 1160, 778, Color{61, 201, 151});

        SDL_RenderPresent(renderer);
    }

    TTF_CloseFont(heading);
    TTF_CloseFont(body);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
