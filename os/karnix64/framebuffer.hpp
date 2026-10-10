#ifndef KARNIX64_FRAMEBUFFER_HPP
#define KARNIX64_FRAMEBUFFER_HPP

#include <stdint.h>

namespace framebuffer {

bool initialize(uint32_t multiboot_magic, uint32_t info_address);
bool ready();
uint32_t width();
uint32_t height();
uint32_t pitch();
void clear(uint8_t color);
void fill_rect_rgb(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t color);
void fill_rounded_rect_rgb(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t radius, uint32_t color);
void draw_text_rgb(uint32_t x, uint32_t y, const char* text, uint32_t scale,
                   uint32_t foreground, uint32_t background);
void draw_console_cell(uint32_t column, uint32_t row, uint8_t character,
                       uint8_t foreground, uint8_t background);
void draw_console_cursor(uint32_t column, uint32_t row, uint8_t color);

}  // namespace framebuffer

#endif
