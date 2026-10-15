#include "../desktop.hpp"
#include "../framebuffer.hpp"

#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>

namespace {
uint32_t screen_width;
uint32_t screen_height;
uint32_t selected_rows;
uint32_t programs;
bool available = true;

void check_bounds(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    assert(x <= screen_width && width <= screen_width - x);
    assert(y <= screen_height && height <= screen_height - y);
}
}  // namespace

namespace framebuffer {
bool ready() { return available; }
uint32_t width() { return screen_width; }
uint32_t height() { return screen_height; }
void fill_rect_rgb(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t color) {
    check_bounds(x, y, width, height);
    if (color == 0xB3B3B3) ++selected_rows;
}
void draw_text_rgb(uint32_t x, uint32_t y, const char* value, uint32_t scale,
                   uint32_t, uint32_t) {
    check_bounds(x, y, static_cast<uint32_t>(std::strlen(value)) * 6u * scale,
                 8u * scale);
    if (std::strcmp(value, ">") == 0) ++programs;
}
}  // namespace framebuffer

int main() {
    const uint32_t sizes[][2] = {
        {480, 200}, {640, 480}, {800, 600}, {1024, 768},
        {1280, 720}, {1600, 1200}, {1920, 1080}, {4096, 2160},
    };
    for (const auto& size : sizes) {
        screen_width = size[0];
        screen_height = size[1];
        for (uint8_t selected = 0; selected < 5; ++selected) {
            for (bool menu : {false, true}) {
                selected_rows = 0;
                programs = 0;
                desktop::render(menu, selected,
                    "Settings: 1024x768 RGB framebuffer; polled PS/2 keyboard.");
                assert(programs == 5);
                assert(selected_rows == (menu ? 1u : 0u));
            }
        }
    }
    available = false;
    programs = 0;
    desktop::render(true, 0, nullptr);
    assert(programs == 0);
    std::puts("Desktop layout bounds and menu selection checks passed.");
}
