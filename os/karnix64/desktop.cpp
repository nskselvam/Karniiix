#include "desktop.hpp"

#include "framebuffer.hpp"

namespace {

constexpr uint32_t kBackground = 0x00006B;
constexpr uint32_t kPanel = 0x0000AA;
constexpr uint32_t kPanelDark = 0x00004F;
constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kMuted = 0xC7D8FF;
constexpr uint32_t kAccent = 0x00FFFF;
constexpr uint32_t kHighlight = 0xFFFF55;
constexpr uint32_t kUnavailable = 0xFFAA55;
constexpr uint32_t kReady = 0x55FF55;

struct Application {
    char key;
    const char* name;
    const char* description;
    const char* state;
    uint32_t state_color;
};

constexpr Application kApplications[] = {
    {'1', "TERMINAL", "Command line and calculator", "READY", kReady},
    {'2', "LOCAL AI", "Chat through host Ollama", "READY", kReady},
    {'3', "MATH LAB", "Integer expression calculator", "READY", kReady},
    {'4', "MEDICAL IMAGING", "Guest image viewer unavailable", "UNAVAILABLE",
     kUnavailable},
    {'5', "CNN LAB", "Guest model viewer unavailable", "UNAVAILABLE",
     kUnavailable},
    {'6', "SETTINGS", "Display and keyboard status", "BASIC", kAccent},
};

uint32_t min_value(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

uint32_t max_value(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

void text(uint32_t x, uint32_t y, const char* value, uint32_t scale,
          uint32_t foreground, uint32_t background) {
    framebuffer::draw_text_rgb(x, y, value, scale, foreground, background);
}

void horizontal_rule(uint32_t x, uint32_t y, uint32_t width,
                     uint32_t thickness, uint32_t color) {
    framebuffer::fill_rect_rgb(x, y, width, thickness, color);
}

void draw_application(uint32_t x, uint32_t y, uint32_t width,
                      uint32_t height, uint32_t scale,
                      const Application& application, bool selected) {
    const uint32_t background = selected ? kPanel : kPanelDark;
    const uint32_t border = selected ? kHighlight : kAccent;
    framebuffer::fill_rect_rgb(x, y, width, height, background);
    horizontal_rule(x, y, width, scale, border);
    horizontal_rule(x, y + height - scale, width, scale, border);
    framebuffer::fill_rect_rgb(x, y, scale, height, border);
    framebuffer::fill_rect_rgb(x + width - scale, y, scale, height, border);

    const uint32_t padding = 12u * scale;
    char label[4] = {'[', application.key, ']', '\0'};
    text(x + padding, y + 12u * scale, label, 2u * scale, kHighlight,
         background);
    text(x + 72u * scale, y + 12u * scale, application.name, 2u * scale,
         kText, background);
    text(x + padding, y + 48u * scale, application.description, 2u * scale,
         kMuted, background);
    text(x + width - 132u * scale, y + 12u * scale, application.state,
         2u * scale, application.state_color, background);
}

void draw_home(uint32_t width, uint32_t height, uint32_t scale,
               bool program_menu, uint8_t selected, const char* notice) {
    framebuffer::fill_rect_rgb(0, 0, width, height, kBackground);
    framebuffer::fill_rect_rgb(0, 0, width, 42u * scale, kPanel);
    text(24u * scale, 10u * scale, "KARNIXOS 64-BIT", 2u * scale, kText,
         kPanel);
    text(width - 245u * scale, 15u * scale, "TEXT MODE", 2u * scale,
         kHighlight, kPanel);

    const uint32_t margin = 40u * scale;
    const uint32_t frame_x = margin;
    const uint32_t frame_y = 70u * scale;
    const uint32_t frame_width = width - 2u * margin;
    const uint32_t frame_height =
        height > 240u * scale ? height - 140u * scale : height - frame_y;

    horizontal_rule(frame_x, frame_y, frame_width, 2u * scale, kAccent);
    horizontal_rule(frame_x, frame_y + frame_height - 2u * scale,
                    frame_width, 2u * scale, kAccent);
    framebuffer::fill_rect_rgb(frame_x, frame_y, 2u * scale, frame_height,
                               kAccent);
    framebuffer::fill_rect_rgb(frame_x + frame_width - 2u * scale, frame_y,
                               2u * scale, frame_height, kAccent);

    const char* heading =
        program_menu ? "PROGRAM SELECT" : "KARNIXOS SYSTEM MENU";
    text(frame_x + 20u * scale, frame_y + 14u * scale, heading,
         2u * scale, kHighlight, kBackground);
    text(frame_x + 20u * scale, frame_y + 48u * scale,
         "SELECT A PROGRAM WITH ITS NUMBER KEY", 2u * scale, kText,
         kBackground);
    horizontal_rule(frame_x + 20u * scale, frame_y + 78u * scale,
                    frame_width - 40u * scale, scale, kAccent);
    if (notice != nullptr) {
        text(frame_x + 20u * scale, frame_y + 84u * scale, notice,
             2u * scale, kText, kBackground);
    }

    const uint32_t grid_x = frame_x + 20u * scale;
    const uint32_t grid_y = frame_y + 120u * scale;
    const uint32_t gap = 16u * scale;
    const uint32_t columns = 2;
    const uint32_t tile_width =
        (frame_width - 40u * scale - gap) / columns;
    const uint32_t tile_height = 104u * scale;
    for (uint32_t index = 0;
         index < sizeof(kApplications) / sizeof(kApplications[0]); ++index) {
        const uint32_t column = index % columns;
        const uint32_t row = index / columns;
        const uint32_t x = grid_x + column * (tile_width + gap);
        const uint32_t y = grid_y + row * (tile_height + gap);
        draw_application(x, y, tile_width, tile_height, scale,
                         kApplications[index],
                         program_menu && index == selected);
    }

    const uint32_t footer_y = frame_y + frame_height - 42u * scale;
    text(frame_x + 20u * scale, footer_y,
         "ESC HOME   A MENU   1-6 RUN APP", 2u * scale, kHighlight,
         kBackground);
}

}  // namespace

namespace desktop {

void render(bool overview, uint8_t selected, const char* notice) {
    if (!framebuffer::ready()) return;
    const uint32_t width = framebuffer::width();
    const uint32_t height = framebuffer::height();
    const uint32_t scale =
        max_value(1u, min_value(width / 1024u, height / 768u));
    draw_home(width, height, scale, overview, selected, notice);
}

}  // namespace desktop
