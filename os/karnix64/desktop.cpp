#include "desktop.hpp"

#include "framebuffer.hpp"

namespace {

// cavOS's .xinitrc root color and .twmrc window/menu palette.
constexpr uint32_t kBackground = 0x006666;
constexpr uint32_t kSurface = 0x22AA99;
constexpr uint32_t kBorder = 0x708090;
constexpr uint32_t kText = 0xD9D9D9;
constexpr uint32_t kSelected = 0xB3B3B3;
constexpr uint32_t kInk = 0x123B3B;
constexpr uint32_t kShadow = 0x004C4C;

struct Application {
    char key;
    const char* name;
    const char* description;
    const char* location;
};

constexpr Application kApplications[] = {
    {'1', "LOCAL AI", "LOCAL MEDGEMMA CHAT", "HOST OLLAMA"},
    {'2', "MEDICAL IMAGING", "BRAIN MRI RESEARCH", "HOST WINDOW"},
    {'3', "CNN LAB", "CNN ACTIVATION VIEWER", "HOST WINDOW"},
    {'4', "PATIENT RECORDS", "ACCOUNT SIGN-IN AND RECORDS", "GUEST APP"},
    {'5', "SETTINGS", "DISPLAY AND KEYBOARD", "GUEST APP"},
};

constexpr uint32_t kApplicationCount =
    sizeof(kApplications) / sizeof(kApplications[0]);

void text(uint32_t x, uint32_t y, const char* value, uint32_t scale,
          uint32_t foreground, uint32_t background) {
    framebuffer::draw_text_rgb(x, y, value, scale, foreground, background);
}

void rectangle(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
               uint32_t color) {
    framebuffer::fill_rect_rgb(x, y, width, height, color);
}

void frame(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
           uint32_t scale, uint32_t background) {
    rectangle(x + 5u * scale, y + 5u * scale, width, height, kShadow);
    rectangle(x, y, width, height, kBorder);
    rectangle(x + 2u * scale, y + 2u * scale,
              width - 4u * scale, height - 4u * scale, background);
}

void title_bar(uint32_t x, uint32_t y, uint32_t width, uint32_t scale,
               const char* title) {
    rectangle(x + 2u * scale, y + 2u * scale,
              width - 4u * scale, 28u * scale, kSurface);
    rectangle(x + 2u * scale, y + 30u * scale,
              width - 4u * scale, 2u * scale, kBorder);
    rectangle(x + 9u * scale, y + 9u * scale, 14u * scale,
              14u * scale, kBorder);
    rectangle(x + 12u * scale, y + 12u * scale, 8u * scale,
              8u * scale, kText);
    text(x + 33u * scale, y + 10u * scale, title, scale, kText, kSurface);
    for (uint32_t row = 0; row < 4; ++row) {
        rectangle(x + width - 28u * scale, y + (9u + row * 4u) * scale,
                  16u * scale, scale, kBorder);
    }
}

void draw_programs(uint32_t x, uint32_t y, uint32_t scale,
                   uint8_t selected, bool menu, bool compact = false) {
    const uint32_t width = 254u * scale;
    const uint32_t row_height = compact ? 22u : 36u;
    const uint32_t height = (66u + kApplicationCount * row_height) * scale;
    frame(x, y, width, height, scale, kSurface);
    title_bar(x, y, width, scale, menu ? "KARNIX / PROGRAMS"
                                      : "KARNIX / ICON MANAGER");
    for (uint32_t index = 0; index < kApplicationCount; ++index) {
        const uint32_t row_y = y + (38u + index * row_height) * scale;
        const bool active = menu && index == selected;
        const uint32_t background = active ? kSelected : kSurface;
        const uint32_t foreground = active ? kInk : kText;
        rectangle(x + 6u * scale, row_y, width - 12u * scale,
                  (row_height - 4u) * scale, background);
        const char shortcut[] = {kApplications[index].key, '\0'};
        text(x + 16u * scale, row_y + 5u * scale, shortcut,
             (compact ? 1u : 2u) * scale, foreground, background);
        text(x + 42u * scale, row_y + (compact ? 5u : 12u) * scale,
             kApplications[index].name, scale, foreground, background);
        text(x + width - 22u * scale, row_y + (compact ? 5u : 12u) * scale,
             ">", scale, foreground, background);
    }
    text(x + 13u * scale, y + height - 20u * scale, "A MENU    1-5 LAUNCH",
         scale, kInk, kSurface);
}

void draw_workspace(uint32_t x, uint32_t y, uint32_t width,
                    uint32_t scale, uint8_t selected, bool menu) {
    frame(x, y, width, 330u * scale, scale, kText);
    title_bar(x, y, width, scale, "KARNIXOS / WORKSPACE");
    text(x + 22u * scale, y + 55u * scale, "KARNIXOS",
         3u * scale, kInk, kText);
    text(x + 22u * scale, y + 91u * scale,
         "AI AND HEALTH WORKSPACE", scale, kInk, kText);
    rectangle(x + 22u * scale, y + 115u * scale,
              width - 44u * scale, scale, kBorder);
    const Application& application =
        kApplications[selected < kApplicationCount ? selected : 0];
    text(x + 22u * scale, y + 138u * scale,
         menu ? "SELECTED PROGRAM" : "DESKTOP READY",
         scale, kInk, kText);
    text(x + 22u * scale, y + 160u * scale,
         menu ? application.name : "A  OPEN PROGRAM MENU",
         scale, kInk, kText);
    text(x + 22u * scale, y + 183u * scale,
         menu ? application.description : "1-5  LAUNCH A PROGRAM",
         scale, kInk, kText);
    text(x + 22u * scale, y + 206u * scale,
         menu ? application.location : "ESC  RETURN TO DESKTOP",
         scale, kInk, kText);
    rectangle(x + 22u * scale, y + 234u * scale,
              width - 44u * scale, scale, kBorder);
    text(x + 22u * scale, y + 256u * scale,
         "PS/2 KEYBOARD / FRAMEBUFFER", scale, kInk, kText);
    text(x + 22u * scale, y + 278u * scale,
         "CAVOS-INSPIRED / NO X11 OR MOUSE", scale, kInk, kText);
    text(x + 22u * scale, y + 300u * scale,
         "MEDICAL VIEWERS OPEN ON HOST", scale, kInk, kText);
}

}  // namespace

namespace desktop {

void render(bool overview, uint8_t selected, const char* notice) {
    if (!framebuffer::ready()) return;
    const uint32_t width = framebuffer::width();
    const uint32_t height = framebuffer::height();
    uint32_t scale = width / 800u;
    if (height / 600u < scale) scale = height / 600u;
    if (scale == 0) scale = 1;

    rectangle(0, 0, width, height, kBackground);
    // Small supported framebuffers get a compact menu instead of off-screen windows.
    if (width < 800u || height < 600u) {
        draw_programs(8u, 8u, 1u, selected, overview, true);
        text(8u, height - 12u, "A MENU / TAB NEXT / ENTER LAUNCH / ESC HOME",
             1u, kText, kBackground);
        return;
    }

    const uint32_t content_width = 752u * scale;
    const uint32_t left = (width - content_width) / 2u;
    const uint32_t top = (height - 440u * scale) / 2u;
    draw_programs(left, top, scale, selected, overview);
    draw_workspace(left + 280u * scale, top + 40u * scale,
                   472u * scale, scale, selected, overview);

    frame(left, top + 392u * scale, content_width, 48u * scale, scale, kSurface);
    if (notice != nullptr) {
        text(left + 12u * scale, top + 404u * scale,
             notice, scale, kInk, kSurface);
    }
    text(left + 12u * scale, top + 424u * scale,
         "A MENU    TAB / N NEXT    ENTER LAUNCH    ESC DESKTOP",
         scale, kInk, kSurface);
}

}  // namespace desktop
