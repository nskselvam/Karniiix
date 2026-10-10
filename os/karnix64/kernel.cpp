#include <stdint.h>
#include "desktop.hpp"
#include "framebuffer.hpp"

namespace {

constexpr uint16_t kSerialPort = 0x3F8;
constexpr uint16_t kDebugExitPort = 0xF4;
constexpr uint16_t kKeyboardDataPort = 0x60;
constexpr uint16_t kKeyboardStatusPort = 0x64;
constexpr uint16_t kVgaCursorPort = 0x3D4;
constexpr uint16_t kVgaCursorDataPort = 0x3D5;
constexpr uint32_t kColumns = 80;
constexpr uint32_t kRows = 25;
constexpr uint32_t kOutputTop = 4;
constexpr uint32_t kOutputBottom = 21;
constexpr uint32_t kPromptRow = 23;
constexpr uint32_t kCommandCapacity = 240;

constexpr uint8_t kBlue = 1;
constexpr uint8_t kConsoleBackground = kBlue;
constexpr uint8_t kLightGray = 7;
constexpr uint8_t kWhite = 15;
constexpr uint8_t kYellow = 14;
constexpr uint8_t kLightGreen = 10;

volatile uint16_t* const kVga =
    reinterpret_cast<volatile uint16_t*>(0xB8000);

uint32_t g_cursor_x = 0;
uint32_t g_cursor_y = kOutputTop;
char g_command[kCommandCapacity + 1] = {};
uint32_t g_command_length = 0;
bool g_open_desktop = false;
bool g_shell_return_to_desktop = false;

[[noreturn]] void desktop_run();

void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_initialize() {
    out8(kSerialPort + 1, 0x00);
    out8(kSerialPort + 3, 0x80);
    out8(kSerialPort + 0, 0x01);
    out8(kSerialPort + 1, 0x00);
    out8(kSerialPort + 3, 0x03);
    out8(kSerialPort + 2, 0xC7);
    out8(kSerialPort + 4, 0x0B);
}

void serial_putc(char character) {
    for (uint32_t attempts = 0; attempts < 1000000; ++attempts) {
        if ((in8(kSerialPort + 5) & 0x20) != 0) {
            out8(kSerialPort, static_cast<uint8_t>(character));
            return;
        }
    }
}

void serial_write(const char* text) {
    while (*text != '\0') {
        if (*text == '\n') serial_putc('\r');
        serial_putc(*text++);
    }
}

char serial_read_character() {
    while ((in8(kSerialPort + 5) & 0x01) == 0) {
        __asm__ volatile("pause");
    }
    return static_cast<char>(in8(kSerialPort));
}

bool serial_read_line(char* output, uint32_t capacity) {
    uint32_t length = 0;
    bool overflow = false;
    for (;;) {
        const char character = serial_read_character();
        if (character == '\r' || character == '\n') {
            if (capacity > 0) {
                output[length] = '\0';
            }
            return !overflow;
        }
        if (length + 1 < capacity) {
            output[length++] = character;
        } else {
            overflow = true;
        }
    }
}

uint16_t vga_cell(char character, uint8_t foreground, uint8_t background) {
    const uint8_t attribute =
        static_cast<uint8_t>((background << 4) | foreground);
    return static_cast<uint16_t>(static_cast<uint8_t>(character)) |
           (static_cast<uint16_t>(attribute) << 8);
}

void vga_set(uint32_t x, uint32_t y, char character, uint8_t foreground,
             uint8_t background = kConsoleBackground) {
    if (x < kColumns && y < kRows) {
        kVga[y * kColumns + x] = vga_cell(character, foreground, background);
        framebuffer::draw_console_cell(
            x, y, static_cast<uint8_t>(character), foreground, background);
    }
}

void vga_set_cursor(uint32_t x, uint32_t y) {
    const uint16_t position = static_cast<uint16_t>(y * kColumns + x);
    out8(kVgaCursorPort, 0x0F);
    out8(kVgaCursorDataPort, static_cast<uint8_t>(position));
    out8(kVgaCursorPort, 0x0E);
    out8(kVgaCursorDataPort, static_cast<uint8_t>(position >> 8));
    framebuffer::draw_console_cursor(x, y, kWhite);
}

void vga_write_at(uint32_t x, uint32_t y, const char* text,
                  uint8_t foreground,
                  uint8_t background = kConsoleBackground) {
    while (*text != '\0' && x < kColumns) {
        vga_set(x++, y, *text++, foreground, background);
    }
}

void output_newline() {
    g_cursor_x = 0;
    ++g_cursor_y;
    if (g_cursor_y > kOutputBottom) {
        for (uint32_t row = kOutputTop; row < kOutputBottom; ++row) {
            for (uint32_t column = 0; column < kColumns; ++column) {
                const uint16_t cell =
                    kVga[(row + 1) * kColumns + column];
                kVga[row * kColumns + column] = cell;
                framebuffer::draw_console_cell(
                    column, row, static_cast<uint8_t>(cell),
                    static_cast<uint8_t>((cell >> 8) & 0x0F),
                    static_cast<uint8_t>((cell >> 12) & 0x0F));
            }
        }
        for (uint32_t column = 0; column < kColumns; ++column) {
            vga_set(column, kOutputBottom, ' ', kLightGray);
        }
        g_cursor_y = kOutputBottom;
    }
}

void output_character(char character, uint8_t foreground = kWhite) {
    if (character == '\n') {
        output_newline();
        return;
    }
    if (g_cursor_x >= kColumns) output_newline();
    vga_set(g_cursor_x++, g_cursor_y, character, foreground);
}

void output_text(const char* text, uint8_t foreground = kWhite) {
    while (*text != '\0') output_character(*text++, foreground);
}

void output_wrapped_text(const char* text, uint8_t foreground = kWhite) {
    while (*text != '\0') {
        while (*text == ' ' || *text == '\t') ++text;
        if (*text == '\0') break;
        if (*text == '\n' || *text == '\r') {
            while (*text == '\n' || *text == '\r') ++text;
            output_newline();
            continue;
        }

        const char* word_end = text;
        while (*word_end != '\0' && *word_end != ' ' && *word_end != '\t' &&
               *word_end != '\n' && *word_end != '\r') {
            ++word_end;
        }
        const uint32_t word_length = static_cast<uint32_t>(word_end - text);
        const uint32_t space_length = g_cursor_x == 0 ? 0 : 1;
        if (g_cursor_x + space_length + word_length > kColumns) {
            output_newline();
            output_text("  ", foreground);
        } else if (space_length != 0) {
            output_character(' ', foreground);
        }

        while (text < word_end) output_character(*text++, foreground);
    }
}

void output_line(const char* text, uint8_t foreground = kWhite) {
    output_wrapped_text(text, foreground);
    output_newline();
}

void screen_draw_prompt() {
    for (uint32_t column = 0; column < kColumns; ++column) {
        vga_set(column, kPromptRow, ' ', kWhite, kConsoleBackground);
    }
    vga_write_at(0, kPromptRow, "Karnix>", kYellow);
    vga_write_at(7, kPromptRow, g_command, kWhite);
    const uint32_t cursor_x =
        g_command_length + 7 < kColumns ? g_command_length + 7 : kColumns - 1;
    vga_set_cursor(cursor_x, kPromptRow);
}

void screen_initialize() {
    framebuffer::clear(kConsoleBackground);
    for (uint32_t cell = 0; cell < kColumns * kRows; ++cell) {
        kVga[cell] = vga_cell(' ', kLightGray, kConsoleBackground);
    }

    vga_write_at(0, 0, " KARNIXOS 64-BIT ", kWhite, kBlue);
    vga_write_at(18, 0, " INDEPENDENT KERNEL / TEXT SHELL ", kYellow);
    vga_write_at(
        0, 1,
        framebuffer::ready()
            ? "80x25 DOS-style console | keyboard-driven | QEMU x86_64"
            : "80x25 DOS-style VGA console | keyboard-driven | QEMU x86_64",
        kLightGray);
    for (uint32_t column = 0; column < kColumns; ++column) {
        vga_set(column, 2, '-', kLightGreen);
        vga_set(column, 22, '-', kLightGreen);
    }
    vga_write_at(0, 24, "HELP for commands | CALC accepts integer arithmetic", kLightGray);

    g_cursor_x = 0;
    g_cursor_y = kOutputTop;
    output_line("KarnixOS has started in 64-bit long mode.", kLightGreen);
    output_line("This is an independent DOS and TempleOS-inspired shell.",
                kYellow);
    output_line("QEMU can attach local Ollama chat through the host bridge.");
    output_line("Enter ASK <prompt> to send a text question.");
    output_line("Type HELP to see the available built-in commands.");
    screen_draw_prompt();
}

bool ascii_equal_case_insensitive(const char* left, const char* right) {
    while (*left != '\0' && *right != '\0') {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *left == '\0' && *right == '\0';
}

bool starts_with_case_insensitive(const char* text, const char* prefix) {
    while (*prefix != '\0') {
        char a = *text++;
        char b = *prefix++;
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

const char* skip_spaces(const char* text) {
    while (*text == ' ') ++text;
    return text;
}

struct ExpressionParser {
    const char* cursor;
    bool failed;

    void skip_spaces() {
        while (*cursor == ' ') ++cursor;
    }

    int64_t parse_expression() {
        int64_t value = parse_term();
        while (!failed) {
            skip_spaces();
            const char operation = *cursor;
            if (operation != '+' && operation != '-') break;
            ++cursor;
            const int64_t right = parse_term();
            int64_t result = 0;
            const bool overflow =
                operation == '+'
                    ? __builtin_add_overflow(value, right, &result)
                    : __builtin_sub_overflow(value, right, &result);
            if (overflow) failed = true;
            value = result;
        }
        return value;
    }

    int64_t parse_term() {
        int64_t value = parse_unary();
        while (!failed) {
            skip_spaces();
            const char operation = *cursor;
            if (operation != '*' && operation != '/') break;
            ++cursor;
            const int64_t right = parse_unary();
            if (operation == '/') {
                if (right == 0 || (value == INT64_MIN && right == -1)) {
                    failed = true;
                } else {
                    value /= right;
                }
            } else {
                int64_t result = 0;
                if (__builtin_mul_overflow(value, right, &result)) {
                    failed = true;
                }
                value = result;
            }
        }
        return value;
    }

    int64_t parse_unary() {
        skip_spaces();
        if (*cursor == '-') {
            ++cursor;
            const int64_t value = parse_unary();
            if (value == INT64_MIN) {
                failed = true;
                return 0;
            }
            return -value;
        }
        if (*cursor == '+') {
            ++cursor;
            return parse_unary();
        }
        return parse_primary();
    }

    int64_t parse_primary() {
        skip_spaces();
        if (*cursor == '(') {
            ++cursor;
            const int64_t value = parse_expression();
            skip_spaces();
            if (*cursor != ')') {
                failed = true;
            } else {
                ++cursor;
            }
            return value;
        }

        if (*cursor < '0' || *cursor > '9') {
            failed = true;
            return 0;
        }

        int64_t value = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            const int64_t digit = *cursor++ - '0';
            int64_t next = 0;
            if (__builtin_mul_overflow(value, static_cast<int64_t>(10), &next) ||
                __builtin_add_overflow(next, digit, &value)) {
                failed = true;
                return 0;
            }
        }
        return value;
    }
};

void format_integer(int64_t value, char* output) {
    char digits[21];
    uint32_t length = 0;
    bool negative = value < 0;
    uint64_t magnitude = negative
                             ? static_cast<uint64_t>(-(value + 1)) + 1
                             : static_cast<uint64_t>(value);
    do {
        digits[length++] = static_cast<char>('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0 && length < sizeof(digits));

    if (negative) *output++ = '-';
    while (length > 0) *output++ = digits[--length];
    *output = '\0';
}

void output_integer(int64_t value) {
    char buffer[22];
    format_integer(value, buffer);
    output_text(buffer);
}

void execute_command() {
    const char* command = skip_spaces(g_command);
    if (*command == '\0') return;

    serial_write("shell-command=");
    serial_write(command);
    serial_write("\n");
    output_text(">", kYellow);
    output_line(command, kLightGray);

    if (ascii_equal_case_insensitive(command, "help")) {
        output_line("HELP          Show this command list.", kLightGreen);
        output_line("ABOUT         Describe this Karnix kernel.", kLightGreen);
        output_line("STATUS        Show current boot capabilities.", kLightGreen);
        output_line("CLEAR         Clear the text screen.", kLightGreen);
        output_line("ECHO <text>   Print text to the console.", kLightGreen);
        output_line("CALC <expr>   Evaluate integer + - * / and parentheses.", kLightGreen);
        output_line("ASK <prompt>  Ask MedGemma through host Ollama in QEMU.", kLightGreen);
        output_line("DESKTOP       Return to the graphical desktop.", kLightGreen);
        output_line("The shell is not a full HolyC implementation.", kYellow);
    } else if (ascii_equal_case_insensitive(command, "about")) {
        output_line("KarnixOS x86_64: freestanding kernel and early shell.");
        output_line("Its colorful, keyboard-first console is independently written.");
        output_line("This is not TempleOS and does not contain TempleOS code or assets.",
                    kYellow);
    } else if (ascii_equal_case_insensitive(command, "status")) {
        output_line("CPU mode: x86_64 long mode.", kLightGreen);
        output_line("Memory map: first 4 GiB mapped with 2 MiB pages.");
        output_line(framebuffer::ready()
                        ? "Input: polled PS/2 keyboard; display: RGB framebuffer."
                        : "Input: polled PS/2 keyboard; display: VGA text mode.");
        output_line("MedGemma runs in host Ollama; guest connects over QEMU serial.",
                    kYellow);
    } else if (ascii_equal_case_insensitive(command, "desktop")) {
        g_open_desktop = true;
        output_line("Returning to the KarnixOS desktop.", kLightGreen);
    } else if (ascii_equal_case_insensitive(command, "clear")) {
        for (uint32_t row = kOutputTop; row <= kOutputBottom; ++row) {
            for (uint32_t column = 0; column < kColumns; ++column) {
                vga_set(column, row, ' ', kLightGray);
            }
        }
        g_cursor_x = 0;
        g_cursor_y = kOutputTop;
        output_line("Console cleared.", kLightGreen);
    } else if (starts_with_case_insensitive(command, "echo ") &&
               command[5] != '\0') {
        output_line(command + 5);
    } else if (starts_with_case_insensitive(command, "calc ") &&
               command[5] != '\0') {
        ExpressionParser parser{command + 5, false};
        const int64_t result = parser.parse_expression();
        parser.skip_spaces();
        if (parser.failed || *parser.cursor != '\0') {
            output_line("calc: invalid expression or integer overflow.", kYellow);
        } else {
            output_text("= ", kLightGreen);
            output_integer(result);
            output_newline();
            char result_text[22];
            format_integer(result, result_text);
            serial_write("calc-result=");
            serial_write(result_text);
            serial_write("\n");
        }
    } else if (starts_with_case_insensitive(command, "ask ") &&
               command[4] != '\0') {
        output_line("MedGemma is thinking via the local host Ollama bridge...",
                    kYellow);
        serial_write("KARNIX_CHAT:");
        serial_write(command + 4);
        serial_write("\n");

        char response[6144];
        if (!serial_read_line(response, sizeof(response))) {
            output_line("MedGemma bridge response exceeded the guest buffer.",
                        kYellow);
        } else if (starts_with_case_insensitive(response, "KARNIX_REPLY:")) {
            output_line("MedGemma:", kLightGreen);
            output_line(response + 13);
        } else if (starts_with_case_insensitive(response, "KARNIX_ERROR:")) {
            serial_write("KARNIX_CHAT_ERROR_RECEIVED\n");
            output_line(response + 13, kYellow);
        } else {
            output_line("Invalid response from the local MedGemma bridge.",
                        kYellow);
        }
        if (starts_with_case_insensitive(response, "KARNIX_REPLY:")) {
            serial_write("KARNIX_CHAT_REPLY_RECEIVED\n");
        }
    } else {
        output_line("Unknown command. Type HELP for the command list.", kYellow);
    }
}

char keyboard_read_character() {
    static bool left_shift = false;
    static bool right_shift = false;
    static bool caps_lock = false;
    static bool extended = false;

    static const char normal[128] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
        '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
        '\n', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0,
        ' ', 0
    };
    static const char shifted[128] = {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
        '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
        '\n', 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
        0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0,
        ' ', 0
    };

    for (;;) {
        while ((in8(kKeyboardStatusPort) & 0x01) == 0) {
            __asm__ volatile("pause");
        }

        const uint8_t scan_code = in8(kKeyboardDataPort);
        if (scan_code == 0xE0) {
            extended = true;
            continue;
        }
        if (extended) {
            extended = false;
            continue;
        }
        if (scan_code == 0x2A) {
            left_shift = true;
            continue;
        }
        if (scan_code == 0x36) {
            right_shift = true;
            continue;
        }
        if (scan_code == 0xAA) {
            left_shift = false;
            continue;
        }
        if (scan_code == 0xB6) {
            right_shift = false;
            continue;
        }
        if (scan_code == 0x3A) {
            caps_lock = !caps_lock;
            continue;
        }
        if ((scan_code & 0x80) != 0 || scan_code >= 128) continue;

        const bool shift_down = left_shift || right_shift;
        const char character =
            (shift_down ? shifted : normal)[scan_code];
        if (character == 0) continue;
        if (character >= 'a' && character <= 'z') {
            return (shift_down != caps_lock)
                       ? static_cast<char>(character - 'a' + 'A')
                       : character;
        }
        return character;
    }
}

[[noreturn]] void shell_run() {
    for (;;) {
        const char character = keyboard_read_character();
        if (character == '\n') {
            output_newline();
            g_command[g_command_length] = '\0';
            execute_command();
            g_command_length = 0;
            g_command[0] = '\0';
            if (g_open_desktop) {
                g_open_desktop = false;
                g_shell_return_to_desktop = false;
                desktop_run();
            }
            screen_draw_prompt();
        } else if (character == '\b') {
            if (g_command_length > 0) {
                --g_command_length;
                g_command[g_command_length] = '\0';
                screen_draw_prompt();
            }
        } else if (character == 27) {
            if (g_shell_return_to_desktop) {
                g_shell_return_to_desktop = false;
                g_command_length = 0;
                g_command[0] = '\0';
                serial_write("desktop-home=returned\n");
                desktop_run();
            }
        } else if (character >= ' ' && character <= '~' &&
                   g_command_length < kCommandCapacity) {
            g_command[g_command_length++] = character;
            g_command[g_command_length] = '\0';
            screen_draw_prompt();
        }
    }
}

[[noreturn]] void open_terminal(const char* message) {
    g_shell_return_to_desktop = true;
    screen_initialize();
    if (message != nullptr) output_line(message, kLightGreen);
    output_line("Press ESC or type DESKTOP to return to the home screen.",
                kYellow);
    screen_draw_prompt();
    shell_run();
}

[[noreturn]] void desktop_run() {
    bool overview = false;
    uint8_t selected = 0;
    const char* notice = "Press a number to start a program.";
    desktop::render(overview, selected, notice);

    for (;;) {
        char key = keyboard_read_character();
        if (key >= 'A' && key <= 'Z') {
            key = static_cast<char>(key - 'A' + 'a');
        }

        if (key == 27) {
            serial_write("desktop-home=shown\n");
            if (overview) {
                overview = false;
                notice = "Home screen.";
                desktop::render(overview, selected, notice);
            }
            continue;
        }
        if (key == 'a') {
            overview = !overview;
            notice = overview ? "Program menu selected."
                              : "Home screen.";
            serial_write(overview ? "desktop-overview=opened\n"
                                  : "desktop-overview=closed\n");
            desktop::render(overview, selected, notice);
            continue;
        }
        if ((key == 'n' || key == '\t') && overview) {
            selected = static_cast<uint8_t>((selected + 1u) % 6u);
            desktop::render(overview, selected,
                            "Press the app number to launch.");
            continue;
        }
        if (key == 't' || key == '1') {
            serial_write("desktop-app=terminal\n");
            open_terminal(nullptr);
        }
        if (key == '2' || key == 'l') {
            serial_write("desktop-app=local-ai\n");
            open_terminal(
                "Local AI uses the existing host Ollama bridge. Try ASK <prompt>.");
        }
        if (key == '3') {
            serial_write("desktop-app=math-lab\n");
            open_terminal("Math Lab uses the checked integer CALC command.");
        }
        if (key == '4' || key == 'm') {
            serial_write("desktop-app=medical-imaging-unavailable\n");
            overview = false;
            notice = "Medical Imaging unavailable: no guest file access.";
            desktop::render(overview, selected, notice);
            continue;
        }
        if (key == '5' || key == 'c') {
            serial_write("desktop-app=cnn-lab-unavailable\n");
            overview = false;
            notice = "CNN Lab unavailable: no guest model viewer.";
            desktop::render(overview, selected, notice);
            continue;
        }
        if (key == '6' || key == 's') {
            serial_write("desktop-app=settings-basic\n");
            overview = false;
            notice = "Settings: 1024x768 RGB framebuffer; polled PS/2 keyboard.";
            desktop::render(overview, selected, notice);
            continue;
        }
    }
}

}  // namespace

extern "C" [[noreturn]] void kernel_main(uint32_t boot_magic,
                                         uint32_t boot_info_address) {
    serial_initialize();
    const bool has_framebuffer =
        framebuffer::initialize(boot_magic, boot_info_address);
    serial_write(has_framebuffer ? "framebuffer=ready\n"
                                 : "framebuffer=unavailable\n");
    if (has_framebuffer) {
        desktop::render(false, 0, "Keyboard-first command desktop.");
    } else {
        screen_initialize();
    }
    serial_write(has_framebuffer ? "desktop-ui=ready\n"
                                 : "desktop-ui=unavailable\n");
    serial_write(has_framebuffer ? "framebuffer-console=ready\n"
                                 : "framebuffer-console=unavailable\n");

    serial_write("KARNIX64_BOOT_OK\n");
    serial_write("desktop=ready\n");
    serial_write("long_mode=active\n");
    serial_write("identity_map=4GiB\n");
    serial_write("guest_llm_inference=not_implemented\n");
    serial_write("shell=ready\n");

    // QEMU's optional debug-exit device makes the same image usable in CI.
    out8(kDebugExitPort, 0x10);

    if (has_framebuffer) desktop_run();
    shell_run();
}
