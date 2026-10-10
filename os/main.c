/**
 * KarnixOS v1.1  –  Terminal research-data viewer
 * Karnix Spine Edge  ·  Experimental, non-diagnostic software
 *
 * Terminal UI provides:
 *   - Blue title bar with live clock + FPS
 *   - Multiple colour-bordered panels (ncurses)
 *   - ANNOUNCEMENT / SCREENING STATUS / TOOLS / RECORDS / ANALYSIS
 *   - Interactive shell: type commands at bottom bar
 *
 * Build:
 *   gcc -std=c11 -O2 os/main.c -lncurses -o KarnixOS
 * Run:
 *   ./KarnixOS
 */

#define _POSIX_C_SOURCE 200112L
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* ─────────────────────────────────────  Colour pairs (TempleOS palette) ─── */
#define CP_TITLE      1   /* white on blue   – title bar          */
#define CP_PANEL_YEL  2   /* yellow on black – general panels     */
#define CP_PANEL_CYN  3   /* cyan on black   – diagnosis panel    */
#define CP_PANEL_GRN  4   /* green on black  – tools panel        */
#define CP_PANEL_RED  5   /* red on black    – warnings           */
#define CP_PANEL_MAG  6   /* magenta on black– analysis panel     */
#define CP_PANEL_WHT  7   /* white on black  – text               */
#define CP_CMD        8   /* black on white  – command bar        */
#define CP_HIGHLIGHT  9   /* black on yellow – highlights         */
#define CP_NORMAL     10  /* white on normal – Normal status      */
#define CP_ABNORMAL   11  /* red on black    – Abnormal status    */
#define CP_URL        12  /* cyan on black   – URLs               */
#define CP_HEADER     13  /* yellow on blue  – section headers    */

/* ─────────────────────────────────────────────────────  Config constants  ── */
#define MAX_RECORDS     256
#define MAX_STR         128
#define JSON_PATH       "./karnix_spine_edge/data/spine_records.json"
#define CSV_PATH        "./karnix_spine_edge/data/uci_vertebral_samples.csv"
#define MAX_LOG         256
#define FPS_TARGET      30
#define VERSION_STR     "KarnixOS v1.1"

/* ─────────────────────────────────────────────────────  Data structures  ── */
typedef struct {
    char patient_id[MAX_STR];
    int  age;
    char sex[8];
    char date[16];
    double risk_score;
    double abnormal_prob;
    char classification[32];
    char image_path[MAX_STR];
} SpineRecord;

typedef struct {
    double pelvic_incidence;
    double pelvic_tilt;
    double lumbar_lordosis;
    double sacral_slope;
    double pelvic_radius;
    double spondylolisthesis;
    char   class_label[32];
} UCIRecord;

typedef struct {
    char text[256];
    int  color_pair;
} LogEntry;

/* ─────────────────────────────────────────────────────  Global state  ────── */
static SpineRecord g_records[MAX_RECORDS];
static int         g_record_count = 0;

static UCIRecord   g_uci[MAX_RECORDS];
static int         g_uci_count = 0;

static LogEntry    g_log[MAX_LOG];
static int         g_log_count = 0;

static int  g_running       = 1;
static int  g_dir_offset    = 0;
static int  g_selected      = -1;
static char g_cmd_buf[256]  = {0};
static int  g_cmd_len       = 0;
static char g_search_query[MAX_STR] = {0};
static int  g_directory_visible_rows = 1;

static const char *g_tools[] = {
    "[ llm     ]  Chat with a local model",
    "[ scan    ]  Run the configured OA image scan",
    "[ predict ]  Run a task-specific image model",
    "[ train   ]  Train and evaluate the OA model",
    "[ brain   ]  Open the brain MRI model UI",
    "[ uci     ]  Inspect one sample UCI row",
    "[ dir     ]  Show loaded data counts",
    "[ search  ]  Filter loaded records",
    "[ help    ]  List commands",
    "[ quit    ]  Exit KarnixOS",
};
static const int g_tools_count = (int)(sizeof(g_tools) / sizeof(g_tools[0]));

static const char *g_article_lines[] = {
    "---- KarnixOS research viewer ----",
    "",
    "A terminal UI for local screening records",
    "and a small UCI vertebral sample dataset.",
    "",
    "The LLM runs in a hosted C++ companion",
    "and talks only to local Ollama.",
    "",
    "The bare-metal kernel is separate and",
    "does not provide LLM runtime services.",
    "",
    "Model commands use separate experimental",
    "research software and checkpoints.",
    "",
    "Record scores are experimental model",
    "outputs, not calibrated clinical risks.",
    "",
    "Missing data is not replaced with examples.",
    "Test records are not patient cases.",
    "",
    "Not a medical device. Not for diagnosis.",
};
static const int g_article_lines_count =
    (int)(sizeof(g_article_lines) / sizeof(g_article_lines[0]));

/* ─────────────────────────────────────────────────────  Logging  ──────────── */
static void log_msg(const char *msg, int cp) {
    if (g_log_count < MAX_LOG) {
        strncpy(g_log[g_log_count].text, msg, 255);
        g_log[g_log_count].text[255] = '\0';
        g_log[g_log_count].color_pair = cp;
        g_log_count++;
    }
}

static int contains_case_insensitive(const char *text, const char *query) {
    if (!query[0]) return 1;
    for (const char *start = text; *start; start++) {
        const char *left = start;
        const char *right = query;
        while (*left && *right && tolower((unsigned char)*left) == tolower((unsigned char)*right)) {
            left++;
            right++;
        }
        if (!*right) return 1;
    }
    return 0;
}

/* ─────────────────────────────────────────────────────  JSON parser  ──────── */
static void trim(char *s) {
    size_t length = strlen(s);
    while (length > 0 &&
           (s[length - 1] == '"' || s[length - 1] == ',' ||
            s[length - 1] == ' ' || s[length - 1] == '\n' ||
            s[length - 1] == '\r' || s[length - 1] == ':')) {
        s[--length] = '\0';
    }
    size_t start = 0;
    while (s[start] == '"' || s[start] == ' ') start++;
    if (start > 0) memmove(s, s + start, strlen(s + start) + 1);
}

static int record_matches_query(const SpineRecord *record) {
    char haystack[512];
    snprintf(haystack, sizeof(haystack), "%s %s %s %s %s",
             record->patient_id, record->date, record->classification,
             record->image_path, record->sex);
    return contains_case_insensitive(haystack, g_search_query);
}

static int matching_record_count(void) {
    int count = 0;
    for (int i = 0; i < g_record_count; i++) {
        if (record_matches_query(&g_records[i])) count++;
    }
    return count;
}

static int filtered_position_for_record(int record_index) {
    int position = 0;
    for (int i = 0; i < record_index; i++) {
        if (record_matches_query(&g_records[i])) position++;
    }
    return position;
}

static void move_selected_record(int direction) {
    int index = g_selected;
    if (index < 0 || index >= g_record_count ||
        !record_matches_query(&g_records[index])) {
        index = direction > 0 ? -1 : g_record_count;
    }

    for (index += direction; index >= 0 && index < g_record_count; index += direction) {
        if (!record_matches_query(&g_records[index])) continue;
        g_selected = index;
        int position = filtered_position_for_record(index);
        if (position < g_dir_offset) {
            g_dir_offset = position;
        } else if (position >= g_dir_offset + g_directory_visible_rows) {
            g_dir_offset = position - g_directory_visible_rows + 1;
        }
        return;
    }
}

static void parse_json_records(void) {
    FILE *f = fopen(JSON_PATH, "r");
    if (!f) {
        log_msg("No screening records found; no example predictions loaded.", CP_PANEL_RED);
        return;
    }

    char line[512];
    SpineRecord cur;
    memset(&cur, 0, sizeof(cur));
    int in_obj = 0;

    while (fgets(line, sizeof(line), f)) {
        /* strip leading whitespace */
        char *l = line;
        while (*l == ' ' || *l == '\t') l++;

        if (strchr(l, '{')) { in_obj = 1; memset(&cur, 0, sizeof(cur)); continue; }
        if (strchr(l, '}') && in_obj) {
            in_obj = 0;
            if (cur.patient_id[0] && g_record_count < MAX_RECORDS)
                g_records[g_record_count++] = cur;
            else if (cur.patient_id[0])
                log_msg("Record limit reached; additional records were skipped.", CP_PANEL_RED);
            continue;
        }
        if (!in_obj) continue;

        /* key": "value" */
        char key[64] = {0}, val[256] = {0};
        char *colon = strchr(l, ':');
        if (!colon) continue;
        size_t key_length = (size_t)(colon - l);
        if (key_length >= sizeof(key)) continue;
        memcpy(key, l, key_length);
        strncpy(val, colon + 1, 255);
        trim(key); trim(val);

        if      (!strcmp(key, "patient_id"))     strncpy(cur.patient_id, val, MAX_STR-1);
        else if (!strcmp(key, "age"))            cur.age = atoi(val);
        else if (!strcmp(key, "sex"))            strncpy(cur.sex, val, 7);
        else if (!strcmp(key, "date"))           strncpy(cur.date, val, 15);
        else if (!strcmp(key, "risk_score"))     cur.risk_score = atof(val);
        else if (!strcmp(key, "abnormal_prob"))  cur.abnormal_prob = atof(val);
        else if (!strcmp(key, "classification")) strncpy(cur.classification, val, 31);
        else if (!strcmp(key, "image_path"))     strncpy(cur.image_path, val, MAX_STR-1);
    }
    fclose(f);

    char buf[128];
    snprintf(buf, sizeof(buf), "Loaded %d spine records from JSON database.", g_record_count);
    log_msg(buf, CP_PANEL_GRN);
    g_selected = g_record_count > 0 ? 0 : -1;
}

static void parse_uci_csv(void) {
    FILE *f = fopen(CSV_PATH, "r");
    if (!f) {
        log_msg("WARNING: Cannot open uci_vertebral_samples.csv", CP_PANEL_RED);
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), f) && g_uci_count < MAX_RECORDS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        UCIRecord u;
        int n = sscanf(line, "%lf,%lf,%lf,%lf,%lf,%lf,%31s",
                       &u.pelvic_incidence, &u.pelvic_tilt,
                       &u.lumbar_lordosis,  &u.sacral_slope,
                       &u.pelvic_radius,    &u.spondylolisthesis,
                       u.class_label);
        if (n == 7) {
            /* strip newline from class label */
            char *nl = strchr(u.class_label, '\n');
            if (nl) *nl = '\0';
            g_uci[g_uci_count++] = u;
        }
    }
    fclose(f);

    char buf[128];
    snprintf(buf, sizeof(buf), "Loaded %d UCI vertebral samples.", g_uci_count);
    log_msg(buf, CP_PANEL_GRN);
}

/* ─────────────────────────────────────────────────────  Draw helpers  ─────── */

/* Draw a TempleOS-style bordered box with a title. Returns inner (y,x,h,w). */
static void draw_box(WINDOW *win, int y, int x, int h, int w,
                     const char *title, const char *url, int cp) {
    wattron(win, COLOR_PAIR(cp) | A_BOLD);
    mvwaddch(win, y,     x,     ACS_ULCORNER);
    mvwaddch(win, y,     x+w-1, ACS_URCORNER);
    mvwaddch(win, y+h-1, x,     ACS_LLCORNER);
    mvwaddch(win, y+h-1, x+w-1, ACS_LRCORNER);
    for (int i = 1; i < w-1; i++) { mvwaddch(win, y, x+i, ACS_HLINE); mvwaddch(win, y+h-1, x+i, ACS_HLINE); }
    for (int i = 1; i < h-1; i++) { mvwaddch(win, y+i, x, ACS_VLINE); mvwaddch(win, y+i, x+w-1, ACS_VLINE); }

    /* Title left */
    if (title && title[0]) {
        char buf[64]; snprintf(buf, 63, "-%s-", title);
        int tlen = (int)strlen(buf);
        if (tlen < w-2) mvwprintw(win, y, x+1, "%s", buf);
    }
    /* URL middle */
    if (url && url[0]) {
        wattron(win, COLOR_PAIR(CP_URL) | A_BOLD);
        int ulen = (int)strlen(url);
        int ux = x + (w - ulen) / 2;
        if (ux > x+1) mvwprintw(win, y, ux, "%s", url);
    }
    /* [X] close button */
    wattron(win, COLOR_PAIR(CP_PANEL_RED) | A_BOLD);
    if (w > 5) mvwprintw(win, y, x+w-4, "[X]");
    wattroff(win, COLOR_PAIR(CP_PANEL_RED) | A_BOLD | COLOR_PAIR(cp));
}

static void draw_hline(WINDOW *win, int y, int x, int w, int cp) {
    wattron(win, COLOR_PAIR(cp));
    mvwhline(win, y, x, ACS_HLINE, w);
    wattroff(win, COLOR_PAIR(cp));
}

/* Print clipped to width, return chars written */
static int mvwprint_clip(WINDOW *win, int y, int x, int w, const char *s) {
    int rows, cols;
    getmaxyx(win, rows, cols);
    if (y < 0 || y >= rows || x < 0 || x >= cols || w <= 0) return 0;
    int available = cols - x;
    if (w > available) w = available;
    if (w <= 0) return 0;
    char buf[256];
    size_t length = strlen(s);
    if (length >= sizeof(buf)) length = sizeof(buf) - 1;
    memcpy(buf, s, length);
    buf[length] = '\0';
    if ((int)length > w) buf[w] = '\0';
    mvwaddnstr(win, y, x, buf, w);
    return (int)strlen(buf);
}

/* ─────────────────────────────────────────────────────  Panel renderers  ─── */

static void render_titlebar(WINDOW *win, int cols, int fps) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char datebuf[64];
    if (tm) strftime(datebuf, sizeof(datebuf), "%a %m/%d %H:%M:%S", tm);
    else snprintf(datebuf, sizeof(datebuf), "LOCAL time unavailable");

    wattron(win, COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvwhline(win, 0, 0, ' ', cols);
    mvwprint_clip(win, 0, 0, cols, " KarnixOS | research viewer");
    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    if (cols >= 60) mvwprint_clip(win, 0, cols - 28, 20, datebuf);
    if (cols >= 60) {
        char fps_text[16];
        snprintf(fps_text, sizeof(fps_text), "FPS:%d", fps);
        mvwprint_clip(win, 0, cols - 7, 7, fps_text);
    }
    wattroff(win, A_BOLD | COLOR_PAIR(CP_HEADER));
}

/* ANNOUNCEMENT panel – top-left */
static void render_announce(WINDOW *win, int y, int x, int h, int w) {
    draw_box(win, y, x, h, w, "KARNIX-ANNOUNCE", "karnix.edge/#announce", CP_PANEL_YEL);

    int iy = y + 1, ix = x + 1, iw = w - 2;
    int row = iy;

    wattron(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
    if (row < y + h - 1) mvwprint_clip(win, row++, ix, iw, "Karnix Spine Edge OS");
    wattroff(win, A_BOLD | COLOR_PAIR(CP_HIGHLIGHT));

    wattron(win, COLOR_PAIR(CP_PANEL_YEL));
    if (row < y + h - 1) mvwprint_clip(win, row++, ix, iw, "Local experimental data viewer");
    if (row < y + h - 1) mvwprint_clip(win, row++, ix, iw, "No diagnosis or medical advice");
    char status[80];
    snprintf(status, sizeof(status), "* Screening records: %d", g_record_count);
    if (row < y + h - 1) mvwprint_clip(win, row++, ix, iw, status);
    snprintf(status, sizeof(status), "* UCI sample rows: %d", g_uci_count);
    if (row < y + h - 1) mvwprint_clip(win, row++, ix, iw, status);
    if (row < y + h - 1) mvwprint_clip(win, row++, ix, iw, "Missing data is not simulated");
    if (row < y + h - 1) {
        wattron(win, COLOR_PAIR(CP_PANEL_CYN));
        mvwprint_clip(win, row, ix, iw, "Cmd: help | llm | scan | dir");
        wattroff(win, COLOR_PAIR(CP_PANEL_CYN));
    }
}

/* Screening-status panel – top-center. */
static void render_diagnosis(WINDOW *win, int y, int x, int h, int w) {
    draw_box(win, y, x, h, w, "SCREENING STATUS", NULL, CP_PANEL_CYN);

    int iy = y + 1, ix = x + 1, iw = w - 2;

    wattron(win, COLOR_PAIR(CP_PANEL_CYN) | A_BOLD);
    mvwprint_clip(win, iy, ix, iw, "RESEARCH-ONLY DATA VIEW");
    wattroff(win, A_BOLD);

    wattron(win, COLOR_PAIR(CP_PANEL_YEL));
    mvwprint_clip(win, iy + 1, ix, iw, "This program displays saved outputs.");
    mvwprint_clip(win, iy + 2, ix, iw, "It does not diagnose or rule out disease.");
    mvwprint_clip(win, iy + 3, ix, iw, "Review findings with a qualified clinician.");
    if (h >= 9) {
        draw_hline(win, y + h - 3, x + 1, w - 2, CP_PANEL_CYN);
        mvwprint_clip(win, y + h - 2, ix, iw, "Commands: scan | brain | help");
    }
    wattroff(win, COLOR_PAIR(CP_PANEL_YEL));
}

/* TOOLS panel – top-right */
static void render_tools(WINDOW *win, int y, int x, int h, int w) {
    draw_box(win, y, x, h, w, "TOOLS", "karnix.edge/#tools", CP_PANEL_GRN);

    int iy = y + 1, ix = x + 1, iw = w - 2;

    wattron(win, COLOR_PAIR(CP_PANEL_GRN) | A_BOLD);
    mvwprint_clip(win, iy, ix, iw, "// Run tools, dont guess!");
    wattroff(win, A_BOLD);

    for (int i = 0; i < g_tools_count && i < h - 4; i++) {
        if (i == 0) {
            wattron(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
        } else {
            wattron(win, COLOR_PAIR(CP_PANEL_GRN));
        }
        mvwprint_clip(win, iy + 1 + i, ix, iw, g_tools[i]);
        wattroff(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD | COLOR_PAIR(CP_PANEL_GRN));
    }
}

/* DIRECTORY panel – bottom-left (patient records) */
static void render_directory(WINDOW *win, int y, int x, int h, int w) {
    draw_box(win, y, x, h, w, "SCREENING-RECORDS", "karnix.edge/#records", CP_PANEL_MAG);

    int iy = y + 1, ix = x + 1, iw = w - 2;
    int max_rows = h - 2;
    g_directory_visible_rows = max_rows - 3;
    if (g_directory_visible_rows < 1) g_directory_visible_rows = 1;

    /* Header row */
    wattron(win, COLOR_PAIR(CP_PANEL_MAG) | A_BOLD);
    char hdr[128];
    snprintf(hdr, sizeof(hdr), " %-16s %-8s %-6s %-5s %s",
             "RECORD-ID", "DATE", "CLASS", "SCORE", "ABN%");
    if (g_search_query[0]) {
        char search_header[128];
        snprintf(search_header, sizeof(search_header), " SEARCH: %s", g_search_query);
        strncat(hdr, search_header, sizeof(hdr) - strlen(hdr) - 1);
    }
    mvwprint_clip(win, iy, ix, iw, hdr);
    draw_hline(win, iy + 1, ix, iw, CP_PANEL_MAG);
    wattroff(win, A_BOLD);

    int shown = 0;
    int matched = 0;
    for (int i = 0; i < g_record_count; i++) {
        SpineRecord *r = &g_records[i];
        if (!record_matches_query(r)) continue;
        matched++;
        if (matched <= g_dir_offset || shown >= max_rows - 3) continue;

        /* alternate colour; highlight selected */
        if (i == g_selected) {
            wattron(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
        } else {
            wattron(win, COLOR_PAIR(CP_PANEL_WHT));
        }

        char row_buf[160];
        snprintf(row_buf, sizeof(row_buf), " %-16.15s %-8.8s %-10.10s %5.1f %4.0f%%",
                 r->patient_id, r->date, r->classification,
                 r->risk_score, r->abnormal_prob * 100.0);
        mvwprint_clip(win, iy + 2 + shown, ix, iw, row_buf);
        wattroff(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD |
                 COLOR_PAIR(CP_PANEL_WHT));
        shown++;
    }

    /* Scroll indicator */
    wattron(win, COLOR_PAIR(CP_PANEL_MAG));
    if (g_record_count > 0) {
        char sc[64];
        snprintf(sc, sizeof(sc), "[%d matches]  ^/v scroll  search <text>  search clear",
                 matched);
        mvwprint_clip(win, y + h - 2, ix, iw, sc);
        if (matched == 0) mvwprint_clip(win, iy + 2, ix, iw, "No records match this search.");
    } else {
        mvwprint_clip(win, y + h - 2, ix, iw, "No records loaded. Type: scan");
    }
    wattroff(win, COLOR_PAIR(CP_PANEL_MAG));
}

/* ANALYSIS / ARTICLE panel – bottom-right */
static void render_analysis(WINDOW *win, int y, int x, int h, int w, int scroll) {
    draw_box(win, y, x, h, w, "ANALYSIS", "karnix.edge/#analysis", CP_PANEL_WHT);

    int iy = y + 1, ix = x + 1, iw = w - 2;
    int max_rows = h - 2;

    /* If a matching record is selected, show its saved fields. */
    if (g_selected >= 0 && g_selected < g_record_count &&
        record_matches_query(&g_records[g_selected])) {
        SpineRecord *r = &g_records[g_selected];
        int row = iy;
        const int last_row = y + h - 1;
        char line[256];
        snprintf(line, sizeof(line), "Record: %s", r->patient_id);
        if (row < last_row) mvwprint_clip(win, row++, ix, iw, line);
        snprintf(line, sizeof(line), "Class: %s", r->classification);
        if (row < last_row) mvwprint_clip(win, row++, ix, iw, line);
        snprintf(line, sizeof(line), "Date: %s  Age: %d  Sex: %s",
                 r->date, r->age, r->sex[0] ? r->sex : "N/A");
        if (row < last_row) mvwprint_clip(win, row++, ix, iw, line);
        snprintf(line, sizeof(line), "Model score (unvalidated): %.2f", r->risk_score);
        if (row < last_row) mvwprint_clip(win, row++, ix, iw, line);
        snprintf(line, sizeof(line), "Abnormal-class probability: %.2f%%",
                 r->abnormal_prob * 100.0);
        if (row < last_row) mvwprint_clip(win, row++, ix, iw, line);
        if (row < last_row) mvwprint_clip(win, row++, ix, iw, "Image:");
        if (row < last_row) mvwprint_clip(win, row++, ix, iw, r->image_path);
        if (g_uci_count > 0 && row + 3 < last_row) {
            UCIRecord *u = &g_uci[0];
            mvwprint_clip(win, row++, ix, iw, "UCI sample (not linked to this scan):");
            snprintf(line, sizeof(line), "PI %.1f PT %.1f LL %.1f",
                     u->pelvic_incidence, u->pelvic_tilt, u->lumbar_lordosis);
            mvwprint_clip(win, row++, ix, iw, line);
            snprintf(line, sizeof(line), "SS %.1f PR %.1f SP %.1f (%s)",
                     u->sacral_slope, u->pelvic_radius, u->spondylolisthesis,
                     u->class_label);
            mvwprint_clip(win, row++, ix, iw, line);
        }
    } else if (g_record_count > 0 && g_search_query[0]) {
        mvwprint_clip(win, iy, ix, iw, "No records match the current search.");
    } else {
        /* fallback: show OS article */
        wattron(win, COLOR_PAIR(CP_PANEL_WHT));
        for (int i = 0; i < max_rows - 1 &&
             (i + scroll) < g_article_lines_count; i++) {
            mvwprint_clip(win, iy + i, ix, iw, g_article_lines[i + scroll]);
        }
        wattroff(win, COLOR_PAIR(CP_PANEL_WHT));
    }
}

/* LOG panel – bottom strip */
static void render_log(WINDOW *win, int y, int x, int h, int w) {
    (void)h;
    draw_box(win, y, x, 3, w, "SYSTEM-LOG", NULL, CP_PANEL_WHT);
    int iy = y + 1, ix = x + 1, iw = w - 2;

    int start = g_log_count - iw / 40;
    if (start < 0) start = 0;
    int col = ix;
    for (int i = start; i < g_log_count && col < ix + iw - 2; i++) {
        wattron(win, COLOR_PAIR(g_log[i].color_pair));
        /* single line snippets */
        char snip[48]; strncpy(snip, g_log[i].text, 47); snip[47] = '\0';
        char *nl = strchr(snip, '\n'); if (nl) *nl = '\0';
        int remaining = ix + iw - col;
        int text_length = (int)strlen(snip);
        if (text_length > remaining - 2) text_length = remaining - 2;
        if (text_length > 0) mvwaddnstr(win, iy, col, snip, text_length);
        col += text_length + 2;
        wattroff(win, COLOR_PAIR(g_log[i].color_pair));
    }
}

/* COMMAND BAR at very bottom */
static void render_cmdbar(WINDOW *win, int rows, int cols) {
    wattron(win, COLOR_PAIR(CP_CMD) | A_BOLD);
    mvwhline(win, rows - 1, 0, ' ', cols);
    mvwprint_clip(win, rows - 1, 0, cols, " KarnixOS> ");
    if (cols > 11) mvwprint_clip(win, rows - 1, 11, cols - 11, g_cmd_buf);
    /* cursor */
    wattron(win, A_BLINK);
    if (11 + g_cmd_len < cols) mvwaddch(win, rows - 1, 11 + g_cmd_len, '_');
    wattroff(win, A_BLINK | A_BOLD | COLOR_PAIR(CP_CMD));
}

static int run_host_command(const char *command) {
    def_prog_mode();
    endwin();
    int result = system(command);
    reset_prog_mode();
    refresh();
    clear();
    return result;
}

static int split_llm_arguments(const char *input, char *storage,
                               size_t storage_size, char **arguments,
                               size_t argument_capacity) {
    size_t read_index = 0;
    size_t write_index = 0;
    size_t count = 1;
    arguments[0] = "karnix_llm";

    while (input[read_index]) {
        while (isspace((unsigned char)input[read_index])) read_index++;
        if (!input[read_index]) break;
        if (count + 1 >= argument_capacity) return -1;
        if (write_index >= storage_size) return -1;
        arguments[count++] = storage + write_index;

        char quote = '\0';
        while (input[read_index]) {
            const char character = input[read_index++];
            if (quote) {
                if (character == quote) {
                    quote = '\0';
                } else {
                    if (write_index + 1 >= storage_size) return -1;
                    storage[write_index++] = character;
                }
            } else if (character == '"' || character == '\'') {
                quote = character;
            } else if (isspace((unsigned char)character)) {
                break;
            } else {
                if (write_index + 1 >= storage_size) return -1;
                storage[write_index++] = character;
            }
        }
        if (quote) return -1;
        storage[write_index++] = '\0';
    }
    arguments[count] = NULL;
    return (int)count;
}

static int run_llm_command(const char *arguments) {
    const char *program = "./llm/build/karnix_llm";
    if (access(program, X_OK) != 0) {
        log_msg("Build the local C++ LLM first: cmake -S llm -B llm/build.",
                CP_PANEL_RED);
        return -1;
    }

    const char *request = arguments;
    while (*request == ' ') request++;

    char argument_storage[256] = {0};
    char *child_arguments[18] = {0};
    int argument_count = split_llm_arguments(
        request, argument_storage, sizeof(argument_storage), child_arguments,
        sizeof(child_arguments) / sizeof(child_arguments[0]));
    if (argument_count < 0) {
        log_msg("Invalid LLM arguments; check quoting or shorten the command.",
                CP_PANEL_RED);
        return -1;
    }
    if (argument_count == 1) {
        child_arguments[1] = "--interactive";
        child_arguments[2] = NULL;
    }

    def_prog_mode();
    endwin();
    pid_t child = fork();
    if (child == 0) {
        execv(program, child_arguments);
        perror("Cannot launch local Karnix LLM");
        _exit(127);
    }

    if (child < 0) {
        reset_prog_mode();
        clearok(stdscr, TRUE);
        log_msg("Could not start local LLM process.", CP_PANEL_RED);
        return -1;
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        status = -1;
        break;
    }
    reset_prog_mode();
    clearok(stdscr, TRUE);
    refresh();
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        log_msg("Local LLM exited with an error; see its output above.",
                CP_PANEL_RED);
        return -1;
    }
    log_msg("Local LLM session ended.", CP_PANEL_GRN);
    return 0;
}

static int run_medical_prediction(const char *disease, const char *image_path) {
    const char *model_path = NULL;
    if (strcmp(disease, "brain_tumor") == 0 ||
        strcmp(disease, "brain_tumour") == 0) {
        model_path = "Brain_tumour/models/brain_tumour_cnn.weights";
    } else if (strcmp(disease, "osteoarthritis") == 0) {
        model_path = "karnix_spine_edge/models/osteoarthritis_cnn.weights";
    } else if (strcmp(disease, "osteoporosis") == 0) {
        model_path = "karnix_spine_edge/models/dexa_cnn.weights";
    } else {
        log_msg("Choose brain_tumor, osteoarthritis, or osteoporosis.", CP_PANEL_RED);
        return -1;
    }

    const char *program = "./Other_Disease/build/medical_screening";
    if (access(program, X_OK) != 0) {
        log_msg("Build Other_Disease target medical_screening before predicting.", CP_PANEL_RED);
        return -1;
    }
    if (access(model_path, F_OK) != 0) {
        log_msg("Required disease-specific model checkpoint is missing.", CP_PANEL_RED);
        return -1;
    }
    if (access(image_path, R_OK) != 0) {
        log_msg("Cannot read the requested image file.", CP_PANEL_RED);
        return -1;
    }

    def_prog_mode();
    endwin();
    pid_t child = fork();
    if (child == 0) {
        execl(program, "medical_screening", "predict",
              "--disease", disease, "--model", model_path,
              "--image", image_path, (char *)NULL);
        perror("Cannot launch medical_screening");
        _exit(127);
    }
    if (child < 0) {
        reset_prog_mode();
        clearok(stdscr, TRUE);
        log_msg("Could not start prediction process.", CP_PANEL_RED);
        return -1;
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        status = -1;
        break;
    }
    reset_prog_mode();
    clearok(stdscr, TRUE);
    refresh();

    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        log_msg("Prediction failed; see the model error above.", CP_PANEL_RED);
        return -1;
    }
    log_msg("Research prediction completed; not a diagnosis.", CP_PANEL_GRN);
    return 0;
}

/* ─────────────────────────────────────────────────────  Command handler  ─── */
static void exec_command(const char *cmd) {
    char buf[300];
    if (strcmp(cmd, "help") == 0) {
        log_msg("Commands: llm [--status|--interactive|<prompt>]; predict <disease> <image-path>; search; scan; train; brain; uci; quit", CP_PANEL_GRN);
    } else if (strcmp(cmd, "llm") == 0 || strncmp(cmd, "llm ", 4) == 0) {
        const char *arguments = cmd[3] == '\0' ? "" : cmd + 4;
        run_llm_command(arguments);
    } else if (strncmp(cmd, "predict ", 8) == 0) {
        const char *arguments = cmd + 8;
        while (*arguments == ' ') arguments++;
        char disease[32] = {0};
        size_t disease_length = 0;
        while (arguments[disease_length] &&
               !isspace((unsigned char)arguments[disease_length]) &&
               disease_length < sizeof(disease) - 1) {
            disease[disease_length] = arguments[disease_length];
            disease_length++;
        }
        const char *image_path = arguments + disease_length;
        while (*image_path == ' ') image_path++;
        char image_buffer[256];
        strncpy(image_buffer, image_path, sizeof(image_buffer) - 1);
        image_buffer[sizeof(image_buffer) - 1] = '\0';
        trim(image_buffer);
        if (disease_length == 0 || image_buffer[0] == '\0') {
            log_msg("Usage: predict <brain_tumor|osteoarthritis|osteoporosis> <image-path>", CP_PANEL_RED);
        } else {
            run_medical_prediction(disease, image_buffer);
        }
    } else if (strcmp(cmd, "train") == 0) {
        int result = run_host_command(
            "cd karnix_spine_edge && "
            "./build/spine_cli --train data/osteoarthritis/train.csv 3 && "
            "./build/spine_cli --evaluate data/osteoarthritis/test.csv");
        log_msg(result == 0 ? "OA CNN training and evaluation completed."
                            : "OA CNN training failed. Run karnix_spine_edge/setup.sh.",
                result == 0 ? CP_PANEL_GRN : CP_PANEL_RED);
    } else if (strcmp(cmd, "scan") == 0) {
        int result = run_host_command(
            "cd karnix_spine_edge && "
            "./build/spine_cli --screen data/osteoarthritis/screen");
        if (result == 0) {
            g_record_count = 0;
            g_uci_count = 0;
            g_selected = -1;
            g_dir_offset = 0;
            g_search_query[0] = '\0';
            parse_json_records();
            parse_uci_csv();
        }
        log_msg(result == 0 ? "OA screening complete; records reloaded."
                            : "OA screening failed. Run karnix_spine_edge/setup.sh.",
                result == 0 ? CP_PANEL_GRN : CP_PANEL_RED);
    } else if (strcmp(cmd, "brain") == 0 || strcmp(cmd, "brain_tumour") == 0) {
        int result = run_host_command(
            "cd Brain_tumour && ./setup.sh --skip-train --skip-evaluate");
        log_msg(result == 0 ? "Brain Tumour MRI visualizer launched."
                            : "Brain Tumour launch failed. Run Brain_tumour/setup.sh.",
                result == 0 ? CP_PANEL_GRN : CP_PANEL_RED);
    } else if (strcmp(cmd, "spine3dData") == 0 || strcmp(cmd, "moonday") == 0 || strcmp(cmd, "jester") == 0) {
        int result = run_host_command("cd jester && npm run app");
        log_msg(result == 0 ? "Moonday Electron interface launched."
                            : "Moonday launch failed. Run: cd jester && npm run app",
                result == 0 ? CP_PANEL_GRN : CP_PANEL_RED);
    } else if (strcmp(cmd, "search clear") == 0 || strcmp(cmd, "find clear") == 0) {
        g_search_query[0] = '\0';
        g_selected = g_record_count > 0 ? 0 : -1;
        g_dir_offset = 0;
        log_msg("Patient search cleared.", CP_PANEL_GRN);
    } else if (strncmp(cmd, "search ", 7) == 0 || strncmp(cmd, "find ", 5) == 0) {
        const char *query = strncmp(cmd, "search ", 7) == 0 ? cmd + 7 : cmd + 5;
        while (*query == ' ') query++;
        strncpy(g_search_query, query, MAX_STR - 1);
        g_search_query[MAX_STR - 1] = '\0';
        g_selected = -1;
        g_dir_offset = 0;
        move_selected_record(1);
        log_msg(g_search_query[0] ? "Patient search applied." : "Empty search; showing all records.", CP_PANEL_CYN);
    } else if (strcmp(cmd, "dir") == 0 || strcmp(cmd, "ls") == 0) {
        snprintf(buf, sizeof(buf), "Records loaded: %d  UCI: %d", g_record_count, g_uci_count);
        log_msg(buf, CP_PANEL_CYN);
    } else if (strcmp(cmd, "uci") == 0) {
        if (g_uci_count > 0) {
            UCIRecord *u = &g_uci[0];
            snprintf(buf, sizeof(buf), "UCI[%d] PI=%.1f PT=%.1f LL=%.1f  → %s",
                     0,
                     u->pelvic_incidence, u->pelvic_tilt, u->lumbar_lordosis,
                     u->class_label);
            log_msg(buf, CP_PANEL_YEL);
        } else {
            log_msg("No UCI data. Type: scan", CP_PANEL_RED);
        }
    } else if (strcmp(cmd, "clear") == 0) {
        g_log_count = 0;
        log_msg("Log cleared.", CP_PANEL_GRN);
    } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0 || strcmp(cmd, "exit") == 0) {
        g_running = 0;
    } else if (strcmp(cmd, "up") == 0) {
        move_selected_record(-1);
    } else if (strcmp(cmd, "down") == 0) {
        move_selected_record(1);
    } else if (strncmp(cmd, "select ", 7) == 0) {
        int idx = atoi(cmd + 7);
        if (idx >= 0 && idx < g_record_count &&
            record_matches_query(&g_records[idx])) {
            g_selected = idx;
            g_dir_offset = filtered_position_for_record(idx);
        } else {
            log_msg("Record index is invalid or filtered out.", CP_PANEL_RED);
        }
    } else if (strlen(cmd) > 0) {
        snprintf(buf, sizeof(buf), "Unknown command: '%s'  (type: help)", cmd);
        log_msg(buf, CP_PANEL_RED);
    }
}

/* ─────────────────────────────────────────────────────  Main render loop  ── */
static void full_render(WINDOW *win, int rows, int cols, int fps) {
    werase(win);

    if (rows < 24 || cols < 80) {
        char message[128];
        snprintf(message, sizeof(message),
                 "Resize terminal to at least 80x24 (current %dx%d).",
                 cols, rows);
        mvwprint_clip(win, 0, 0, cols, message);
        if (rows > 1)         mvwprint_clip(win, 1, 0, cols, "Type quit or press Ctrl+Q to exit.");
        if (rows > 2) render_cmdbar(win, rows, cols);
        wrefresh(win);
        return;
    }

    /* ── title bar ── */
    render_titlebar(win, cols, fps);

    /* ── layout constants ── */
    /* three columns: left 27%, center 38%, right 35% */
    int top_h   = (rows - 1) * 45 / 100;   /* top row height */
    int bot_h   = (rows - 1) - top_h - 4;  /* bottom row height (minus log+cmd) */
    if (bot_h < 5) bot_h = 5;

    int col_a   = cols * 27 / 100;          /* left panel width */
    int col_c   = cols * 35 / 100;          /* right panel width */
    int col_b   = cols - col_a - col_c;     /* center panel width */

    int top_y   = 1;
    int bot_y   = top_y + top_h;

    /* ── top row panels ── */
    render_announce  (win, top_y, 0,     top_h, col_a);
    render_diagnosis (win, top_y, col_a, top_h, col_b);
    render_tools     (win, top_y, col_a + col_b, top_h, col_c);

    /* ── bottom row panels ── */
    int dir_w  = cols * 60 / 100;
    int ana_w  = cols - dir_w;
    render_directory (win, bot_y, 0,     bot_h, dir_w);
    render_analysis  (win, bot_y, dir_w, bot_h, ana_w, 0);

    /* ── log bar ── */
    render_log(win, bot_y + bot_h, 0, 3, cols);

    /* ── command bar ── */
    render_cmdbar(win, rows, cols);

    refresh();
    wrefresh(win);
}

/* ─────────────────────────────────────────────────────  Signal handler  ──── */
static void handle_sigint(int sig) { (void)sig; g_running = 0; }

/* ─────────────────────────────────────────────────────  Entry point  ──────── */
int main(void) {
    signal(SIGINT, handle_sigint);

    /* ── ncurses init ── */
    initscr();
    if (!has_colors()) {
        endwin();
        fprintf(stderr, "KarnixOS requires a colour terminal.\n");
        return 1;
    }
    start_color();
    use_default_colors();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    /* ── colour pairs ── */
    init_pair(CP_TITLE,     COLOR_WHITE,   COLOR_BLUE);
    init_pair(CP_PANEL_YEL, COLOR_YELLOW,  COLOR_BLACK);
    init_pair(CP_PANEL_CYN, COLOR_CYAN,    COLOR_BLACK);
    init_pair(CP_PANEL_GRN, COLOR_GREEN,   COLOR_BLACK);
    init_pair(CP_PANEL_RED, COLOR_RED,     COLOR_BLACK);
    init_pair(CP_PANEL_MAG, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(CP_PANEL_WHT, COLOR_WHITE,   COLOR_BLACK);
    init_pair(CP_CMD,       COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_HIGHLIGHT, COLOR_BLACK,   COLOR_YELLOW);
    init_pair(CP_NORMAL,    COLOR_GREEN,   COLOR_BLACK);
    init_pair(CP_ABNORMAL,  COLOR_RED,     COLOR_BLACK);
    init_pair(CP_URL,       COLOR_CYAN,    COLOR_BLUE);
    init_pair(CP_HEADER,    COLOR_YELLOW,  COLOR_BLUE);

    /* ── boot messages ── */
    log_msg("KarnixOS v1.0 booting...", CP_PANEL_GRN);
    log_msg("Karnix Spine Edge initialised.", CP_PANEL_CYN);

    /* ── auto-load data ── */
    parse_json_records();
    parse_uci_csv();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    WINDOW *win = newwin(rows, cols, 0, 0);
    keypad(win, TRUE);
    nodelay(win, TRUE);

    /* ── FPS tracking ── */
    struct timespec ts_last, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_last);
    int frame_count = 0;
    int fps = 0;

    log_msg("Type 'help' for commands. Use arrow keys to navigate.", CP_PANEL_YEL);

    /* ── Main loop ── */
    while (g_running) {
        /* FPS calc */
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec - ts_last.tv_sec)
                       + (ts_now.tv_nsec - ts_last.tv_nsec) / 1e9;
        frame_count++;
        if (elapsed >= 1.0) {
            fps = frame_count;
            frame_count = 0;
            ts_last = ts_now;
        }

        /* ── Input ── */
        int ch = wgetch(win);
        if (ch != ERR) {
            if (ch == KEY_RESIZE) {
                getmaxyx(stdscr, rows, cols);
                wresize(win, rows, cols);
                mvwin(win, 0, 0);
            } else if (ch == KEY_UP || ch == 'k') {
                move_selected_record(-1);
            } else if (ch == KEY_DOWN || ch == 'j') {
                move_selected_record(1);
            } else if (ch == KEY_PPAGE) {
                g_dir_offset -= g_directory_visible_rows;
                if (g_dir_offset < 0) g_dir_offset = 0;
            } else if (ch == KEY_NPAGE) {
                int match_count = matching_record_count();
                g_dir_offset += g_directory_visible_rows;
                if (g_dir_offset + g_directory_visible_rows > match_count) {
                    g_dir_offset = match_count - g_directory_visible_rows;
                }
                if (g_dir_offset < 0) g_dir_offset = 0;
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                exec_command(g_cmd_buf);
                g_cmd_buf[0] = '\0';
                g_cmd_len = 0;
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (g_cmd_len > 0) {
                    g_cmd_len--;
                    g_cmd_buf[g_cmd_len] = '\0';
                }
            } else if (ch == 17 || ch == ('q' & 0x1f)) {
                /* Ctrl+Q */
                g_running = 0;
            } else if (ch >= 0 && ch <= UCHAR_MAX &&
                       isprint((unsigned char)ch) && g_cmd_len < 253) {
                g_cmd_buf[g_cmd_len++] = (char)ch;
                g_cmd_buf[g_cmd_len]   = '\0';
            }
        }

        /* ── Render ── */
        full_render(win, rows, cols, fps);

        /* sleep ~1/FPS_TARGET seconds */
        struct timespec frame_sleep = {0, 1000000000L / FPS_TARGET};
        nanosleep(&frame_sleep, NULL);
    }

    /* ── Cleanup ── */
    delwin(win);
    endwin();

    printf("\n\n");
    printf("  %s  –  Karnix Spine Edge\n", VERSION_STR);
    printf("  Spine records processed : %d\n", g_record_count);
    printf("  UCI records processed   : %d\n", g_uci_count);
    printf("  Session ended.\n\n");
    return 0;
}
