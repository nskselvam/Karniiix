#include "framebuffer.hpp"

namespace {

constexpr uint32_t kMultiboot2BootMagic = 0x36D76289;
constexpr uint32_t kFramebufferTag = 8;
constexpr uint64_t kMappedPhysicalLimit = 0x100000000ULL;
constexpr uint32_t kMaximumWidth = 4096;
constexpr uint32_t kMaximumHeight = 2160;
constexpr uint32_t kMaximumPitch = 32768;
constexpr uint32_t kMaximumBootInfoSize = 1024 * 1024;
constexpr uint32_t kMaximumCornerRadius = 32;
constexpr uint32_t kConsoleColumns = 80;
constexpr uint32_t kConsoleRows = 25;
constexpr uint32_t kGlyphWidth = 5;
constexpr uint32_t kGlyphHeight = 7;
constexpr uint32_t kCellWidth = 6;
constexpr uint32_t kCellHeight = 8;

struct Color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct State {
    volatile uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t red_position;
    uint8_t red_size;
    uint8_t green_position;
    uint8_t green_size;
    uint8_t blue_position;
    uint8_t blue_size;
    uint32_t scale;
    uint32_t console_x;
    uint32_t console_y;
    bool ready;
};

State g_state = {};

constexpr Color kVgaPalette[16] = {
    {0, 0, 0},       {0, 0, 170},       {0, 170, 0},       {0, 170, 170},
    {170, 0, 0},     {170, 0, 170},     {170, 85, 0},      {170, 170, 170},
    {85, 85, 85},    {85, 85, 255},     {85, 255, 85},     {85, 255, 255},
    {255, 85, 85},   {255, 85, 255},    {255, 255, 85},    {255, 255, 255},
};

// Original KarnixOS 5x7 console glyphs; see FONT-LICENSE.txt.
uint8_t glyph_row(char character, uint32_t row) {
    static constexpr uint8_t kLetters[26][7] = {
        {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
        {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
        {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
        {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
        {31, 4, 4, 4, 4, 4, 31}, {7, 2, 2, 2, 18, 18, 12},
        {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
        {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
        {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
        {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
        {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4},
        {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
        {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
        {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31},
    };
    static constexpr uint8_t kDigits[10][7] = {
        {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
        {14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30},
        {2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30},
        {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
        {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
    };
    static constexpr uint8_t kLowercase[26][7] = {
        {0, 0, 14, 1, 15, 17, 15},   {16, 16, 30, 17, 17, 17, 30},
        {0, 0, 14, 16, 16, 17, 14},  {1, 1, 15, 17, 17, 17, 15},
        {0, 0, 14, 17, 31, 16, 14},  {6, 8, 8, 30, 8, 8, 8},
        {0, 0, 15, 17, 15, 1, 14},   {16, 16, 30, 17, 17, 17, 17},
        {4, 0, 12, 4, 4, 4, 14},      {2, 0, 6, 2, 2, 18, 12},
        {16, 16, 18, 20, 24, 20, 18}, {12, 4, 4, 4, 4, 4, 14},
        {0, 0, 26, 21, 21, 21, 21},   {0, 0, 30, 17, 17, 17, 17},
        {0, 0, 14, 17, 17, 17, 14},   {0, 0, 30, 17, 30, 16, 16},
        {0, 0, 15, 17, 15, 1, 1},     {0, 0, 22, 25, 16, 16, 16},
        {0, 0, 15, 16, 14, 1, 30},    {8, 8, 30, 8, 8, 9, 6},
        {0, 0, 17, 17, 17, 19, 13},   {0, 0, 17, 17, 17, 10, 4},
        {0, 0, 17, 17, 21, 21, 10},   {0, 0, 17, 10, 4, 10, 17},
        {0, 0, 17, 17, 15, 1, 14},    {0, 0, 31, 2, 4, 8, 31},
    };
    static constexpr uint8_t kQuestion[7] = {14, 17, 1, 2, 4, 0, 4};
    static constexpr uint8_t kPeriod[7] = {0, 0, 0, 0, 0, 12, 12};
    static constexpr uint8_t kComma[7] = {0, 0, 0, 0, 12, 8, 16};
    static constexpr uint8_t kColon[7] = {0, 12, 12, 0, 12, 12, 0};
    static constexpr uint8_t kSemicolon[7] = {0, 12, 12, 0, 12, 8, 16};
    static constexpr uint8_t kExclamation[7] = {4, 4, 4, 4, 4, 0, 4};
    static constexpr uint8_t kAsterisk[7] = {0, 17, 10, 31, 10, 17, 0};
    static constexpr uint8_t kPlus[7] = {0, 4, 4, 31, 4, 4, 0};
    static constexpr uint8_t kHyphen[7] = {0, 0, 0, 31, 0, 0, 0};
    static constexpr uint8_t kUnderscore[7] = {0, 0, 0, 0, 0, 0, 31};
    static constexpr uint8_t kEquals[7] = {0, 31, 0, 31, 0, 0, 0};
    static constexpr uint8_t kSlash[7] = {1, 2, 2, 4, 8, 8, 16};
    static constexpr uint8_t kBackslash[7] = {16, 8, 8, 4, 2, 2, 1};
    static constexpr uint8_t kLeftParen[7] = {2, 4, 8, 8, 8, 4, 2};
    static constexpr uint8_t kRightParen[7] = {8, 4, 2, 2, 2, 4, 8};
    static constexpr uint8_t kLeftBracket[7] = {14, 8, 8, 8, 8, 8, 14};
    static constexpr uint8_t kRightBracket[7] = {14, 2, 2, 2, 2, 2, 14};
    static constexpr uint8_t kLessThan[7] = {2, 4, 8, 16, 8, 4, 2};
    static constexpr uint8_t kGreaterThan[7] = {8, 4, 2, 1, 2, 4, 8};
    static constexpr uint8_t kSingleQuote[7] = {4, 4, 8, 0, 0, 0, 0};
    static constexpr uint8_t kQuote[7] = {10, 10, 20, 0, 0, 0, 0};
    static constexpr uint8_t kPipe[7] = {4, 4, 4, 4, 4, 4, 4};
    static constexpr uint8_t kSpace[7] = {};
    if (row >= 7) return 0;
    if (character >= 'a' && character <= 'z') {
        return kLowercase[character - 'a'][row];
    }
    if (character >= 'A' && character <= 'Z') {
        return kLetters[character - 'A'][row];
    }
    if (character >= '0' && character <= '9') {
        return kDigits[character - '0'][row];
    }

    const uint8_t* pattern = kQuestion;
    switch (character) {
        case ' ': pattern = kSpace; break;
        case '!': pattern = kExclamation; break;
        case '"': pattern = kQuote; break;
        case '\'': pattern = kSingleQuote; break;
        case '*': pattern = kAsterisk; break;
        case '+': pattern = kPlus; break;
        case ',': pattern = kComma; break;
        case '-': pattern = kHyphen; break;
        case '.': pattern = kPeriod; break;
        case '/': pattern = kSlash; break;
        case ':': pattern = kColon; break;
        case ';': pattern = kSemicolon; break;
        case '<': pattern = kLessThan; break;
        case '=': pattern = kEquals; break;
        case '>': pattern = kGreaterThan; break;
        case '[': pattern = kLeftBracket; break;
        case '\\': pattern = kBackslash; break;
        case ']': pattern = kRightBracket; break;
        case '_': pattern = kUnderscore; break;
        case '|': pattern = kPipe; break;
        case '(': pattern = kLeftParen; break;
        case ')': pattern = kRightParen; break;
        default: break;
    }
    return pattern[row];
}

uint32_t channel_mask(uint8_t position, uint8_t size) {
    return ((1u << size) - 1u) << position;
}

bool valid_channel(uint8_t position, uint8_t size) {
    return size > 0 && size <= 8 && position < 32 &&
           static_cast<uint32_t>(position) + size <= 32;
}

uint32_t pack_color(Color color) {
    const uint32_t red_max = (1u << g_state.red_size) - 1u;
    const uint32_t green_max = (1u << g_state.green_size) - 1u;
    const uint32_t blue_max = (1u << g_state.blue_size) - 1u;
    const uint32_t red = static_cast<uint32_t>(color.red) * red_max / 255u;
    const uint32_t green =
        static_cast<uint32_t>(color.green) * green_max / 255u;
    const uint32_t blue =
        static_cast<uint32_t>(color.blue) * blue_max / 255u;
    return (red << g_state.red_position) |
           (green << g_state.green_position) |
           (blue << g_state.blue_position);
}

Color palette_color(uint8_t index) {
    return kVgaPalette[index & 0x0F];
}

void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
               uint32_t pixel) {
    if (!g_state.ready || x >= g_state.width || y >= g_state.height) return;
    const uint32_t clipped_width =
        width > g_state.width - x ? g_state.width - x : width;
    const uint32_t clipped_height =
        height > g_state.height - y ? g_state.height - y : height;
    const uint32_t pixels_per_row = g_state.pitch / sizeof(uint32_t);
    for (uint32_t row = 0; row < clipped_height; ++row) {
        volatile uint32_t* destination =
            g_state.pixels + (y + row) * pixels_per_row + x;
        for (uint32_t column = 0; column < clipped_width; ++column) {
            destination[column] = pixel;
        }
    }
}

bool parse_framebuffer(uint32_t info_address, uint64_t* address,
                       uint32_t* pitch, uint32_t* width, uint32_t* height,
                       uint8_t* red_position, uint8_t* red_size,
                       uint8_t* green_position, uint8_t* green_size,
                       uint8_t* blue_position, uint8_t* blue_size) {
    if (info_address == 0 ||
        static_cast<uint64_t>(info_address) + sizeof(uint32_t) * 2 >
            kMappedPhysicalLimit) {
        return false;
    }

    const uint32_t* info = reinterpret_cast<const uint32_t*>(info_address);
    const uint32_t total_size = info[0];
    if (total_size < 16 || total_size > kMaximumBootInfoSize ||
        static_cast<uint64_t>(info_address) + total_size >
            kMappedPhysicalLimit) {
        return false;
    }

    uint32_t offset = 8;
    while (offset <= total_size - 8) {
        const uint32_t* tag =
            reinterpret_cast<const uint32_t*>(info_address + offset);
        const uint32_t type = tag[0];
        const uint32_t size = tag[1];
        if (size < 8 || size > total_size - offset) return false;
        if (type == 0) break;
        if (type == kFramebufferTag) {
            if (size < 38) return false;
            const uint8_t* bytes =
                reinterpret_cast<const uint8_t*>(info_address + offset);
            *address = *reinterpret_cast<const uint64_t*>(bytes + 8);
            *pitch = *reinterpret_cast<const uint32_t*>(bytes + 16);
            *width = *reinterpret_cast<const uint32_t*>(bytes + 20);
            *height = *reinterpret_cast<const uint32_t*>(bytes + 24);
            if (bytes[28] != 32 || bytes[29] != 1) return false;
            *red_position = bytes[32];
            *red_size = bytes[33];
            *green_position = bytes[34];
            *green_size = bytes[35];
            *blue_position = bytes[36];
            *blue_size = bytes[37];
            return true;
        }

        const uint64_t next_offset =
            (static_cast<uint64_t>(offset) + size + 7u) & ~7ULL;
        if (next_offset <= offset || next_offset > total_size) return false;
        offset = static_cast<uint32_t>(next_offset);
    }
    return false;
}

}  // namespace

namespace framebuffer {

bool initialize(uint32_t multiboot_magic, uint32_t info_address) {
    g_state.ready = false;
    if (multiboot_magic != kMultiboot2BootMagic) return false;

    uint64_t address = 0;
    uint32_t pitch_value = 0;
    uint32_t width_value = 0;
    uint32_t height_value = 0;
    uint8_t red_position = 0;
    uint8_t red_size = 0;
    uint8_t green_position = 0;
    uint8_t green_size = 0;
    uint8_t blue_position = 0;
    uint8_t blue_size = 0;
    if (!parse_framebuffer(info_address, &address, &pitch_value, &width_value,
                           &height_value, &red_position, &red_size,
                           &green_position, &green_size, &blue_position,
                           &blue_size)) {
        return false;
    }

    if (width_value < kConsoleColumns * kCellWidth ||
        width_value > kMaximumWidth ||
        height_value < kConsoleRows * kCellHeight ||
        height_value > kMaximumHeight || pitch_value > kMaximumPitch ||
        (pitch_value & 3u) != 0 || address > UINT32_MAX ||
        (address & 3u) != 0 || pitch_value < width_value * 4u ||
        (static_cast<uint64_t>(pitch_value) * height_value >
         kMappedPhysicalLimit - address) ||
        !valid_channel(red_position, red_size) ||
        !valid_channel(green_position, green_size) ||
        !valid_channel(blue_position, blue_size)) {
        return false;
    }

    const uint32_t red_mask = channel_mask(red_position, red_size);
    const uint32_t green_mask = channel_mask(green_position, green_size);
    const uint32_t blue_mask = channel_mask(blue_position, blue_size);
    if ((red_mask & green_mask) != 0 || (red_mask & blue_mask) != 0 ||
        (green_mask & blue_mask) != 0) {
        return false;
    }

    g_state.pixels = reinterpret_cast<volatile uint32_t*>(address);
    g_state.width = width_value;
    g_state.height = height_value;
    g_state.pitch = pitch_value;
    g_state.red_position = red_position;
    g_state.red_size = red_size;
    g_state.green_position = green_position;
    g_state.green_size = green_size;
    g_state.blue_position = blue_position;
    g_state.blue_size = blue_size;

    const uint32_t horizontal_scale =
        width_value / (kConsoleColumns * kCellWidth);
    const uint32_t vertical_scale = height_value / (kConsoleRows * kCellHeight);
    g_state.scale = horizontal_scale < vertical_scale ? horizontal_scale
                                                      : vertical_scale;
    if (g_state.scale == 0) g_state.scale = 1;
    const uint32_t console_width =
        kConsoleColumns * kCellWidth * g_state.scale;
    const uint32_t console_height =
        kConsoleRows * kCellHeight * g_state.scale;
    g_state.console_x = (width_value - console_width) / 2u;
    g_state.console_y = (height_value - console_height) / 2u;
    g_state.ready = true;
    return true;
}

bool ready() {
    return g_state.ready;
}

uint32_t width() {
    return g_state.width;
}

uint32_t height() {
    return g_state.height;
}

uint32_t pitch() {
    return g_state.pitch;
}

void clear(uint8_t color) {
    if (!g_state.ready) return;
    fill_rect(0, 0, g_state.width, g_state.height,
              pack_color(palette_color(color)));
}

void fill_rect_rgb(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t color) {
    const Color rgb = {
        static_cast<uint8_t>(color >> 16),
        static_cast<uint8_t>(color >> 8),
        static_cast<uint8_t>(color),
    };
    fill_rect(x, y, width, height, pack_color(rgb));
}

void fill_rounded_rect_rgb(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t radius, uint32_t color) {
    if (!g_state.ready || x >= g_state.width || y >= g_state.height) return;
    width = width > g_state.width - x ? g_state.width - x : width;
    height = height > g_state.height - y ? g_state.height - y : height;
    radius = radius > kMaximumCornerRadius ? kMaximumCornerRadius : radius;
    radius = radius > width / 2u ? width / 2u : radius;
    radius = radius > height / 2u ? height / 2u : radius;
    if (radius == 0) {
        fill_rect_rgb(x, y, width, height, color);
        return;
    }

    const Color rgb = {
        static_cast<uint8_t>(color >> 16),
        static_cast<uint8_t>(color >> 8),
        static_cast<uint8_t>(color),
    };
    const uint32_t pixel = pack_color(rgb);
    for (uint32_t row = 0; row < height; ++row) {
        uint32_t inset = 0;
        if (row < radius) {
            const uint32_t distance = radius - row;
            while (inset < radius &&
                   (radius - inset) * (radius - inset) +
                           distance * distance >
                       radius * radius) {
                ++inset;
            }
        } else if (row >= height - radius) {
            const uint32_t distance = row - (height - radius - 1u);
            while (inset < radius &&
                   (radius - inset) * (radius - inset) +
                           distance * distance >
                       radius * radius) {
                ++inset;
            }
        }
        fill_rect(x + inset, y + row, width - 2u * inset, 1, pixel);
    }
}

void draw_text_rgb(uint32_t x, uint32_t y, const char* text, uint32_t scale,
                   uint32_t foreground, uint32_t background) {
    if (!g_state.ready || text == nullptr || scale == 0) return;
    scale = scale > 8u ? 8u : scale;
    const uint32_t cell_width = kCellWidth * scale;
    const uint32_t cell_height = kCellHeight * scale;
    const Color fg = {
        static_cast<uint8_t>(foreground >> 16),
        static_cast<uint8_t>(foreground >> 8),
        static_cast<uint8_t>(foreground),
    };
    const Color bg = {
        static_cast<uint8_t>(background >> 16),
        static_cast<uint8_t>(background >> 8),
        static_cast<uint8_t>(background),
    };
    const uint32_t foreground_pixel = pack_color(fg);
    const uint32_t background_pixel = pack_color(bg);
    while (*text != '\0' && x < g_state.width) {
        fill_rect(x, y, cell_width, cell_height, background_pixel);
        for (uint32_t glyph_y = 0; glyph_y < kGlyphHeight; ++glyph_y) {
            const uint8_t bits = glyph_row(*text, glyph_y);
            for (uint32_t glyph_x = 0; glyph_x < kGlyphWidth; ++glyph_x) {
                if ((bits & (1u << (kGlyphWidth - 1u - glyph_x))) != 0) {
                    fill_rect(x + glyph_x * scale, y + glyph_y * scale,
                              scale, scale, foreground_pixel);
                }
            }
        }
        ++text;
        if (g_state.width - x <= cell_width) break;
        x += cell_width;
    }
}

void draw_console_cell(uint32_t column, uint32_t row, uint8_t character,
                       uint8_t foreground, uint8_t background) {
    if (!g_state.ready || column >= kConsoleColumns || row >= kConsoleRows) {
        return;
    }

    const uint32_t scale = g_state.scale;
    const uint32_t cell_x = g_state.console_x + column * kCellWidth * scale;
    const uint32_t cell_y = g_state.console_y + row * kCellHeight * scale;
    fill_rect(cell_x, cell_y, kCellWidth * scale, kCellHeight * scale,
              pack_color(palette_color(background)));

    const Color foreground_color = palette_color(foreground);
    for (uint32_t glyph_y = 0; glyph_y < kGlyphHeight; ++glyph_y) {
        const uint8_t bits =
            glyph_row(static_cast<char>(character), glyph_y);
        for (uint32_t glyph_x = 0; glyph_x < kGlyphWidth; ++glyph_x) {
            if ((bits & (1u << (kGlyphWidth - 1u - glyph_x))) != 0) {
                fill_rect(cell_x + glyph_x * scale,
                          cell_y + glyph_y * scale, scale, scale,
                          pack_color(foreground_color));
            }
        }
    }
}

void draw_console_cursor(uint32_t column, uint32_t row, uint8_t color) {
    if (!g_state.ready || column >= kConsoleColumns || row >= kConsoleRows) {
        return;
    }
    const uint32_t scale = g_state.scale;
    const uint32_t x = g_state.console_x + column * 4u * scale;
    const uint32_t y =
        g_state.console_y + (row * kCellHeight + kGlyphHeight) * scale;
    fill_rect(x, y, kGlyphWidth * scale, scale,
              pack_color(palette_color(color)));
}

}  // namespace framebuffer
