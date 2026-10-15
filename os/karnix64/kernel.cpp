#include <stdint.h>
#include "desktop.hpp"
#include "framebuffer.hpp"

namespace {

bool starts_with_case_insensitive(const char* text, const char* prefix);
void decode_chat_reply(char* text);

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
uint16_t g_console_cells[kColumns * kRows] = {};

uint32_t g_cursor_x = 0;
uint32_t g_cursor_y = kOutputTop;
char g_command[kCommandCapacity + 1] = {};
uint32_t g_command_length = 0;
bool g_open_desktop = false;
bool g_shell_return_to_desktop = false;
bool g_local_ai_mode = false;

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

bool serial_try_read_line(char* output, uint32_t capacity) {
    uint32_t length = 0;
    bool overflow = false;
    for (;;) {
        bool received = false;
        for (uint32_t attempts = 0; attempts < 100000000u; ++attempts) {
            if ((in8(kSerialPort + 5) & 0x01) != 0) {
                received = true;
                break;
            }
            __asm__ volatile("pause");
        }
        if (!received) {
            if (capacity > 0) output[0] = '\0';
            return false;
        }

        const char character = static_cast<char>(in8(kSerialPort));
        if (character == '\r') continue;
        if (character == '\n') {
            if (capacity > 0) output[length] = '\0';
            return !overflow;
        }
        if (length + 1 < capacity) {
            output[length++] = character;
        } else {
            overflow = true;
        }
    }
}

void launch_host_app(const char* app, const char* notice) {
    serial_write("KARNIX_LAUNCH:");
    serial_write(app);
    serial_write("\n");

    char response[256] = {};
    if (!serial_try_read_line(response, sizeof(response))) {
        desktop::render(false, 0, "Host bridge offline. Run ./setup.sh.");
        return;
    }
    if (starts_with_case_insensitive(response, "KARNIX_APP_LAUNCHED:")) {
        serial_write("desktop-app=host-app-launched\n");
        desktop::render(false, 0, notice);
    } else if (starts_with_case_insensitive(response, "KARNIX_ERROR:")) {
        serial_write("desktop-app=host-app-error\n");
        desktop::render(false, 0, response + 13);
    } else {
        serial_write("desktop-app=host-app-invalid-response\n");
        desktop::render(false, 0, "Host app returned an invalid response.");
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
        const uint32_t index = y * kColumns + x;
        const uint16_t cell = vga_cell(character, foreground, background);
        g_console_cells[index] = cell;
        kVga[index] = cell;
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
                const uint32_t source = (row + 1) * kColumns + column;
                const uint32_t destination = row * kColumns + column;
                const uint16_t cell = g_console_cells[source];
                g_console_cells[destination] = cell;
                kVga[destination] = cell;
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
    const char* prompt = g_local_ai_mode ? "Ask> " : "Karnix>";
    const uint32_t prompt_length = g_local_ai_mode ? 5u : 7u;
    vga_write_at(0, kPromptRow, prompt, kYellow);
    vga_write_at(prompt_length, kPromptRow, g_command, kWhite);
    const uint32_t cursor_x =
        g_command_length + prompt_length < kColumns
            ? g_command_length + prompt_length
            : kColumns - 1;
    vga_set_cursor(cursor_x, kPromptRow);
}

void screen_initialize() {
    framebuffer::clear(kConsoleBackground);
    for (uint32_t cell = 0; cell < kColumns * kRows; ++cell) {
        const uint16_t blank = vga_cell(' ', kLightGray, kConsoleBackground);
        g_console_cells[cell] = blank;
        kVga[cell] = blank;
    }

    vga_write_at(0, 0,
                 g_local_ai_mode ? " KARNIXOS LOCAL AI " : " KARNIXOS 64-BIT ",
                 kWhite, kBlue);
    vga_write_at(18, 0,
                 g_local_ai_mode ? " CHAT / LOCAL OLLAMA " :
                                   " INDEPENDENT KERNEL / TEXT SHELL ",
                 kYellow);
    vga_write_at(
        0, 1,
        framebuffer::ready()
            ? "80x25 text console | keyboard-driven | QEMU x86_64"
            : "80x25 VGA text console | keyboard-driven | QEMU x86_64",
        kLightGray);
    for (uint32_t column = 0; column < kColumns; ++column) {
        vga_set(column, 2, '-', kLightGreen);
        vga_set(column, 22, '-', kLightGreen);
    }
    vga_write_at(0, 24,
                 g_local_ai_mode ? "TYPE A QUESTION | ESC TO RETURN HOME"
                                 : "HELP for commands | CALC accepts integer arithmetic",
                 kLightGray);

    g_cursor_x = 0;
    g_cursor_y = kOutputTop;
    if (g_local_ai_mode) {
        output_line("LOCAL AI CHAT", kLightGreen);
        output_line("Local Ollama answers with Karnix project context.");
        output_line("Demo records are synthetic, never real patients.");
        output_line("Ask a question, then press Enter.", kLightGreen);
        output_line("Escape returns to the desktop.");
    } else {
        output_line("KarnixOS has started in 64-bit long mode.", kLightGreen);
        output_line("This is the KarnixOS built-in command shell.", kYellow);
        output_line("QEMU can attach local Ollama chat through the host bridge.");
        output_line("Enter ASK <prompt> to send a text question.");
        output_line("Type HELP to see the available built-in commands.");
    }
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

void clear_console_output() {
    for (uint32_t row = kOutputTop; row <= kOutputBottom; ++row) {
        for (uint32_t column = 0; column < kColumns; ++column) {
            vga_set(column, row, ' ', kLightGray);
        }
    }
    g_cursor_x = 0;
    g_cursor_y = kOutputTop;
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
        if (g_local_ai_mode) {
            output_line("Type a message at Ask> and press Enter.", kLightGreen);
            output_line("Press ESC to return to the desktop.", kLightGreen);
        } else {
            output_line("HELP          Show this command list.", kLightGreen);
            output_line("ABOUT         Describe this Karnix kernel.", kLightGreen);
            output_line("STATUS        Show current boot capabilities.", kLightGreen);
            output_line("CLEAR         Clear the text screen.", kLightGreen);
            output_line("ECHO <text>   Print text to the console.", kLightGreen);
            output_line("CALC <expr>   Evaluate integer arithmetic.", kLightGreen);
            output_line("ASK <prompt>  Ask MedGemma through host Ollama.", kLightGreen);
            output_line("DESKTOP       Return to the graphical desktop.", kLightGreen);
        }
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
        clear_console_output();
        output_line("Console cleared.", kLightGreen);
    } else if (starts_with_case_insensitive(command, "echo ") &&
               command[5] != '\0') {
        output_line(command + 5);
    } else if (!g_local_ai_mode &&
               starts_with_case_insensitive(command, "calc ") &&
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
    } else if (g_local_ai_mode ||
               (starts_with_case_insensitive(command, "ask ") &&
                command[4] != '\0')) {
        const char* prompt = g_local_ai_mode ? command : command + 4;
        output_line("Local AI is preparing your answer...",
                    kYellow);
        serial_write("KARNIX_CHAT:");
        serial_write(prompt);
        serial_write("\n");

        char response[6144];
        if (!serial_read_line(response, sizeof(response))) {
            output_line("Local AI response exceeded the guest buffer.",
                        kYellow);
        } else if (starts_with_case_insensitive(response, "KARNIX_REPLY:")) {
            const char* answer = response + 13;
            if (*skip_spaces(answer) == '\0') {
                output_line("Local AI returned an empty answer. Try again or "
                            "check the selected model.",
                            kYellow);
            } else {
                decode_chat_reply(response + 13);
                output_line("LOCAL AI:", kLightGreen);
                output_line(answer);
            }
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
        output_line(g_local_ai_mode
                        ? "Chat accepts a prompt, or press ESC to return home."
                        : "Unknown command. Type HELP for the command list.",
                    kYellow);
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

struct PatientRecordField {
    const char* key;
    const char* label;
    const char* hint;
    bool numeric;
    bool integer;
    bool nullable;
    bool array;
};

constexpr PatientRecordField kPatientRecordFields[] = {
    {"age", "Age", "18 to 120", true, true, false, false},
    {"gender", "Gender", "female, male, non_binary, prefer_not_to_say", false, false, false, false},
    {"ethnicity", "Ethnicity", "asian, black, hispanic_latino, white, middle_eastern, indigenous, other, prefer_not_to_say", false, false, false, false},
    {"smoking_status", "Smoking status", "never, former, current", false, false, false, false},
    {"cigarettes_per_day", "Cigarettes per day", "0 to 200", true, false, false, false},
    {"years_smoked", "Years smoked", "0 to 105", true, false, false, false},
    {"age_started_smoking", "Age started smoking", "5 to 120, or - for not applicable", true, true, true, false},
    {"secondhand_smoke_exposure", "Secondhand smoke", "never, occasional, frequent", false, false, false, false},
    {"tobacco_type", "Tobacco type", "cigarettes, cigars, pipe, smokeless, other, none", false, false, false, false},
    {"other_tobacco_type", "Other tobacco type", "text, or - for not applicable", false, false, true, false},
    {"alcohol_consumption", "Alcohol consumption", "never, occasional, moderate, frequent", false, false, false, false},
    {"physical_activity", "Physical activity", "very_low, low, moderate, high", false, false, false, false},
    {"weekly_physical_activity", "Weekly activity", "none, under_1_hour, 1_3_hours, 3_5_hours, over_5_hours", false, false, false, false},
    {"sleep_duration", "Sleep duration", "under_5_hours, 5_6_hours, 6_8_hours, over_8_hours", false, false, false, false},
    {"diet_quality", "Diet quality", "poor, average, good", false, false, false, false},
    {"height_cm", "Height in cm", "80 to 250", true, false, false, false},
    {"weight_kg", "Weight in kg", "20 to 400", true, false, false, false},
    {"air_pollution_exposure", "Air pollution", "low, moderate, high", false, false, false, false},
    {"living_environment", "Living environment", "urban, suburban, rural", false, false, false, false},
    {"traffic_pollution_exposure", "Traffic pollution", "low, moderate, high", false, false, false, false},
    {"residential_pollution_exposure", "Residential pollution", "low, moderate, high", false, false, false, false},
    {"occupational_exposure", "Occupational exposure", "yes, no, unknown", false, false, false, false},
    {"occupational_exposure_types", "Occupational exposure types", "comma-separated: radon, asbestos, diesel_exhaust, chemical_dust, industrial_fumes, smoke, other; - for none", false, false, false, true},
    {"occupational_exposure_duration", "Exposure duration", "under_1_year, 1_5_years, 5_10_years, over_10_years, or -", false, false, true, false},
    {"family_history_lung_cancer", "Family lung cancer history", "yes, no, unknown", false, false, false, false},
    {"family_history_other_cancer", "Family other cancer history", "yes, no, unknown", false, false, false, false},
    {"family_cancer_relationship", "Family cancer relationship", "parent, sibling, child, grandparent, other_close_relative, unknown, or -", false, false, true, false},
};

void wipe_guest_buffer(char* buffer, uint32_t capacity) {
    volatile char* data = buffer;
    for (uint32_t index = 0; index < capacity; ++index) data[index] = '\0';
}

bool read_patient_input(char* buffer, uint32_t capacity, bool hidden = false) {
    if (capacity == 0) return false;
    uint32_t length = 0;
    buffer[0] = '\0';
    for (;;) {
        const char character = keyboard_read_character();
        if (character == 27) {
            wipe_guest_buffer(buffer, capacity);
            output_newline();
            return false;
        }
        if (character == '\n') {
            buffer[length] = '\0';
            output_newline();
            return true;
        }
        if (character == '\b') {
            if (length > 0) {
                --length;
                buffer[length] = '\0';
                if (g_cursor_x > 0) {
                    --g_cursor_x;
                    vga_set(g_cursor_x, g_cursor_y, ' ', kWhite);
                }
            }
            continue;
        }
        if (character >= ' ' && character <= '~' && length + 1 < capacity) {
            buffer[length++] = character;
            buffer[length] = '\0';
            output_character(hidden ? '*' : character, kWhite);
        }
    }
}

bool append_json_text(char* output, uint32_t capacity, uint32_t* length,
                     const char* text) {
    while (*text != '\0') {
        if (*length + 1 >= capacity) return false;
        output[(*length)++] = *text++;
    }
    output[*length] = '\0';
    return true;
}

bool append_json_string(char* output, uint32_t capacity, uint32_t* length,
                        const char* text) {
    if (!append_json_text(output, capacity, length, "\"")) return false;
    for (const char* character = text; *character != '\0'; ++character) {
        if (*character == '"' || *character == '\\') {
            if (*length + 2 >= capacity) return false;
            output[(*length)++] = '\\';
            output[(*length)++] = *character;
            output[*length] = '\0';
        } else if (static_cast<unsigned char>(*character) < 0x20) {
            return false;
        } else {
            if (*length + 1 >= capacity) return false;
            output[(*length)++] = *character;
            output[*length] = '\0';
        }
    }
    return append_json_text(output, capacity, length, "\"");
}

bool valid_patient_number(const char* value, bool integer) {
    if (*value == '\0') return false;
    bool dot_seen = false;
    bool digit_seen = false;
    for (const char* character = value; *character != '\0'; ++character) {
        if (*character >= '0' && *character <= '9') {
            digit_seen = true;
        } else if (*character == '.' && !integer && !dot_seen) {
            dot_seen = true;
        } else {
            return false;
        }
    }
    return digit_seen;
}

bool append_patient_field(char* json, uint32_t capacity, uint32_t* length,
                          const PatientRecordField& field, char* value) {
    if (!append_json_string(json, capacity, length, field.key) ||
        !append_json_text(json, capacity, length, ":")) return false;
    if (field.nullable && (value[0] == '\0' ||
                           (value[0] == '-' && value[1] == '\0'))) {
        return append_json_text(json, capacity, length, "null");
    }
    if (field.numeric) {
        if (!valid_patient_number(value, field.integer)) return false;
        return append_json_text(json, capacity, length, value);
    }
    if (!field.array) return append_json_string(json, capacity, length, value);

    if (!append_json_text(json, capacity, length, "[")) return false;
    if (value[0] == '-' && value[1] == '\0') value[0] = '\0';
    bool first = true;
    char* item = value;
    while (*item != '\0') {
        char* end = item;
        while (*end != '\0' && *end != ',') ++end;
        const char delimiter = *end;
        *end = '\0';
        if (*item == '\0' || (!first &&
            !append_json_text(json, capacity, length, ",")) ||
            !append_json_string(json, capacity, length, item)) return false;
        first = false;
        if (delimiter == '\0') break;
        item = end + 1;
        if (*item == '\0') return false;
    }
    return append_json_text(json, capacity, length, "]");
}

bool read_host_response(char* response, uint32_t capacity) {
    if (!serial_try_read_line(response, capacity)) {
        output_line("Records host service did not respond.", kYellow);
        return false;
    }
    if (starts_with_case_insensitive(response, "KARNIX_ERROR:")) {
        output_line(response + 13, kYellow);
        return false;
    }
    return true;
}

void decode_chat_reply(char* text) {
    char* read = text;
    char* write = text;
    while (*read != '\0') {
        if (*read == '\\' && read[1] != '\0') {
            ++read;
            if (*read == 'n' || *read == 'r') *write++ = '\n';
            else if (*read == '\\') *write++ = '\\';
            else {
                *write++ = '\\';
                *write++ = *read;
            }
            ++read;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

void list_patient_records() {
    serial_write("KARNIX_RECORDS_LIST\n");
    char response[512];
    if (!read_host_response(response, sizeof(response))) return;
    if (ascii_equal_case_insensitive(response, "KARNIX_RECORDS_LIST_EMPTY")) {
        output_line("No lung-risk assessments are saved for this account.");
        return;
    }

    uint32_t count = 0;
    while (starts_with_case_insensitive(response, "KARNIX_RECORDS_ITEM:")) {
        output_line("Date | Age | Smoking | Pack-years | BMI", kYellow);
        output_line(response + 20, kLightGreen);
        ++count;
        if (!serial_try_read_line(response, sizeof(response))) {
            output_line("Records response ended unexpectedly.", kYellow);
            return;
        }
    }
    if (!ascii_equal_case_insensitive(response, "KARNIX_RECORDS_LIST_DONE")) {
        output_line("Invalid response from records host service.", kYellow);
        return;
    }
    if (count == 0) output_line("No records were returned.");
    else output_line("Showing the latest records for your account.");
}

void show_sample_patient_records() {
    constexpr uint32_t kSampleCount = 100;
    constexpr uint32_t kPageSize = 10;
    constexpr uint32_t kPageCount = kSampleCount / kPageSize;
    uint32_t page = 0;
    char command[16];

    for (;;) {
        clear_console_output();
        output_line("SYNTHETIC DEMO DATA - NOT REAL PATIENT RECORDS", kYellow);
        output_text("Sample page ", kLightGreen);
        output_integer(static_cast<int64_t>(page + 1));
        output_text(" of ", kLightGreen);
        output_integer(kPageCount);
        output_line(" | 100 samples total", kLightGreen);
        output_line("# | AGE | SMOKING | PACK-YEARS | BMI | FAMILY HX | AIR",
                    kYellow);

        const uint32_t first = page * kPageSize;
        for (uint32_t offset = 0; offset < kPageSize; ++offset) {
            const uint32_t sample = first + offset + 1;
            const uint32_t age = 20 + (sample * 7) % 61;
            const uint32_t pack_years_tenths =
                sample % 3 == 0 ? 0 : (sample * 13) % 300 + 1;
            const uint32_t bmi_tenths = 185 + (sample * 17) % 165;
            const char* smoking =
                sample % 3 == 0 ? "NEVER" : sample % 3 == 1 ? "FORMER" : "CURRENT";
            const char* family_history = sample % 4 == 0 ? "YES" : "NO";
            const char* air_exposure =
                sample % 3 == 0 ? "LOW" : sample % 3 == 1 ? "MODERATE" : "HIGH";

            output_text("DEMO-", kLightGray);
            output_integer(sample);
            output_text(" | ", kLightGray);
            output_integer(age);
            output_text(" | ", kLightGray);
            output_text(smoking, kLightGray);
            output_text(" | ", kLightGray);
            output_integer(pack_years_tenths / 10);
            output_text(".", kLightGray);
            output_integer(pack_years_tenths % 10);
            output_text(" | ", kLightGray);
            output_integer(bmi_tenths / 10);
            output_text(".", kLightGray);
            output_integer(bmi_tenths % 10);
            output_text(" | ", kLightGray);
            output_text(family_history, kLightGray);
            output_text(" | ", kLightGray);
            output_line(air_exposure, kLightGray);
        }

        output_line("Commands: next, prev, exit.", kYellow);
        output_text("demo> ", kYellow);
        if (!read_patient_input(command, sizeof(command))) return;
        if (ascii_equal_case_insensitive(command, "exit")) return;
        if (ascii_equal_case_insensitive(command, "next")) {
            if (page + 1 < kPageCount) ++page;
            else output_line("Already at the last sample page.", kYellow);
        } else if (ascii_equal_case_insensitive(command, "prev")) {
            if (page > 0) --page;
            else output_line("Already at the first sample page.", kYellow);
        } else {
            output_line("Use next, prev, or exit.", kYellow);
        }
    }
}

void create_patient_record() {
    constexpr uint32_t kJsonCapacity = 4096;
    char json[kJsonCapacity];
    wipe_guest_buffer(json, sizeof(json));
    uint32_t length = 0;
    if (!append_json_text(json, sizeof(json), &length, "{")) {
        output_line("Could not prepare the assessment form.", kYellow);
        return;
    }

    char value[512];
    bool cancelled = false;
    for (uint32_t index = 0;
         index < sizeof(kPatientRecordFields) / sizeof(kPatientRecordFields[0]);
         ++index) {
        const PatientRecordField& field = kPatientRecordFields[index];
        output_text("[", kYellow);
        output_integer(static_cast<int64_t>(index + 1));
        output_text("/", kYellow);
        output_integer(static_cast<int64_t>(
            sizeof(kPatientRecordFields) / sizeof(kPatientRecordFields[0])));
        output_text("] ", kYellow);
        output_line(field.label, kLightGreen);
        output_line(field.hint, kLightGray);
        output_text("> ", kYellow);
        if (!read_patient_input(value, sizeof(value))) {
            cancelled = true;
            break;
        }
        if (length > 1 && !append_json_text(json, sizeof(json), &length, ",")) {
            output_line("Assessment is too large.", kYellow);
            cancelled = true;
            break;
        }
        if (!append_patient_field(json, sizeof(json), &length, field, value)) {
            output_line("Invalid value or unsupported characters; cancel and retry this form.", kYellow);
            cancelled = true;
            break;
        }
    }
    wipe_guest_buffer(value, sizeof(value));
    if (cancelled || !append_json_text(json, sizeof(json), &length, "}")) {
        wipe_guest_buffer(json, sizeof(json));
        output_line("Assessment entry cancelled; nothing was sent.", kYellow);
        return;
    }

    serial_write("KARNIX_RECORDS_CREATE:");
    serial_write(json);
    serial_write("\n");
    wipe_guest_buffer(json, sizeof(json));
    char response[512];
    if (!read_host_response(response, sizeof(response))) return;
    if (starts_with_case_insensitive(response, "KARNIX_RECORDS_CREATED:")) {
        output_line("Lung-risk assessment saved to your account.", kLightGreen);
    } else {
        output_line("Invalid response from records host service.", kYellow);
    }
}

void patient_records_run() {
    screen_initialize();
    output_line("PATIENT RECORDS - KARNIX OS NATIVE APP", kLightGreen);
    output_line("Stores lung-risk assessments in your authenticated KARNIX account.");
    output_line("The host service forwards records; the guest has no database access.");
    output_line("Commands: login, list, new, demo, logout, exit.", kYellow);

    char command[32];
    char username[256];
    char password[256];
    bool session_active = false;
    for (;;) {
        output_text("records> ", kYellow);
        if (!read_patient_input(command, sizeof(command))) break;
        if (ascii_equal_case_insensitive(command, "exit")) break;
        if (ascii_equal_case_insensitive(command, "demo")) {
            show_sample_patient_records();
            continue;
        }
        if (ascii_equal_case_insensitive(command, "login")) {
            output_text("Username: ", kYellow);
            if (!read_patient_input(username, sizeof(username))) continue;
            output_text("Password: ", kYellow);
            if (!read_patient_input(password, sizeof(password), true)) {
                wipe_guest_buffer(username, sizeof(username));
                continue;
            }
            serial_write("KARNIX_RECORDS_LOGIN:");
            serial_write(username);
            serial_write(":");
            serial_write(password);
            serial_write("\n");
            wipe_guest_buffer(username, sizeof(username));
            wipe_guest_buffer(password, sizeof(password));
            char response[512];
            if (read_host_response(response, sizeof(response)) &&
                ascii_equal_case_insensitive(response,
                                             "KARNIX_RECORDS_LOGIN_OK")) {
                session_active = true;
                output_line("Signed in. Session stays in host memory.", kLightGreen);
            }
            continue;
        }
        if (ascii_equal_case_insensitive(command, "list")) {
            list_patient_records();
        } else if (ascii_equal_case_insensitive(command, "new")) {
            create_patient_record();
        } else if (ascii_equal_case_insensitive(command, "logout")) {
            serial_write("KARNIX_RECORDS_LOGOUT\n");
            char response[512];
            if (read_host_response(response, sizeof(response)) &&
                ascii_equal_case_insensitive(response,
                                             "KARNIX_RECORDS_LOGOUT_OK")) {
                session_active = false;
                output_line("Signed out.", kLightGreen);
            }
        } else {
            output_line("Use login, list, new, demo, logout, or exit.", kYellow);
        }
    }
    wipe_guest_buffer(command, sizeof(command));
    wipe_guest_buffer(username, sizeof(username));
    wipe_guest_buffer(password, sizeof(password));
    if (session_active) {
        serial_write("KARNIX_RECORDS_LOGOUT\n");
        char response[512];
        if (!read_host_response(response, sizeof(response)) ||
            !ascii_equal_case_insensitive(response,
                                          "KARNIX_RECORDS_LOGOUT_OK")) {
            output_line("Sign-out could not be confirmed; host service will retry on shutdown.",
                        kYellow);
        }
    }
    serial_write("desktop-app=patient-records-closed\n");
    desktop::render(false, 0, "Patient Records closed.");
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
                g_local_ai_mode = false;
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
                g_local_ai_mode = false;
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

[[noreturn]] void open_local_ai_chat(const char* message) {
    g_local_ai_mode = true;
    g_shell_return_to_desktop = true;
    screen_initialize();
    serial_write("local-ai-chat=ready\n");
    if (message != nullptr) output_line(message, kLightGreen);
    output_line("Press ESC to return to the desktop.", kYellow);
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
            selected = static_cast<uint8_t>((selected + 1u) % 5u);
            desktop::render(overview, selected,
                            "Press Enter or the app number to launch.");
            continue;
        }
        if (key == '\n' && overview) {
            key = static_cast<char>('1' + selected);
        }
        if (key == '1' || key == 'l') {
            serial_write("desktop-app=local-ai\n");
            open_local_ai_chat(
                "The local MedGemma model is ready for your question.");
        }
        if (key == '2' || key == 'm') {
            launch_host_app(
                "MEDICAL_IMAGING",
                "Brain MRI visualizer opened in a host window.");
            continue;
        }
        if (key == '3' || key == 'c') {
            launch_host_app(
                "CNN_LAB",
                "CNN activation visualizer opened in a host window.");
            continue;
        }
        if (key == '4' || key == 'p') {
            serial_write("desktop-app=patient-records\n");
            patient_records_run();
            continue;
        }
        if (key == '5' || key == 's') {
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
