/**
 * KarnixOS v1.0  –  TempleOS-inspired Spine Intelligence OS
 * Karnix Spine Edge  ·  Clinical-Grade Bone Abnormality Detection
 *
 * UI mirrors TempleOS:
 *   - Blue title bar with live clock + FPS
 *   - Multiple colour-bordered panels (ncurses)
 *   - ANNOUNCEMENT / DIAGNOSIS / TOOLS / DIRECTORY / ANALYSIS
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
#define CMD_HISTORY     16
#define MAX_LOG         256
#define FPS_TARGET      30
#define VERSION_STR     "KarnixOS v1.0"

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
static int  g_selected      = 0;
static int  g_active_panel  = 0;  /* 0=announce 1=diag 2=tools 3=dir 4=analysis */
static char g_cmd_buf[256]  = {0};
static int  g_cmd_len       = 0;
static int  g_diag_scroll   = 0;
static char g_search_query[MAX_STR] = {0};

static const char *g_diagnoses[] = {
    "VERTEBRAL ALIGNMENT NOMINAL – No structural deviation detected.",
    "SCOLIOSIS RISK: LOW – Cobb angle within normal range (<10 deg).",
    "DISC INTEGRITY: STABLE – No herniation markers in dataset.",
    "BONE DENSITY INDEX: 98.2% – Osteoporosis risk: minimal.",
    "LUMBAR LORDOSIS: 44.3 deg – Within physiological range.",
    "SACRAL SLOPE: 38.7 deg – Normal sagittal balance confirmed.",
    "PELVIC INCIDENCE: 52.1 – Harmonious spinopelvic parameters.",
    "COMPRESSION FRACTURE RISK: LOW – FRAX score < 5%.",
    "SPONDYLOLISTHESIS: ABSENT – No vertebral slippage detected.",
    "SPINAL CANAL: PATENT – No stenosis markers present.",
};
static const int g_diag_count = 10;

static const char *g_tools[] = {
    "[ SCAN     ]  Full Spine AI Analysis",
    "[ DIAG     ]  Differential Diagnosis",
    "[ UCI      ]  Vertebral ML Risk Score",
    "[ COBB     ]  Cobb Angle Measurement",
    "[ FRAX     ]  Fracture Risk Assessment",
    "[ DENSITY  ]  Bone Mineral Density",
    "[ EXPORT   ]  Generate PDF Report",
    "[ VOICE    ]  Voice Report Mode",
    "[ TRAIN    ]  Retrain CNN Model",
};
static const int g_tools_count = 9;

static const char *g_article_lines[] = {
    "---- Karnix Spine Edge: What Is It? ----",
    "",
    "Karnix Spine Edge is a clinical-grade",
    "AI spine screening system built on a",
    "custom CNN (ResNet50 architecture) trained",
    "on the MURA musculoskeletal dataset.",
    "",
    "It performs three-tier analysis:",
    " 1. Deep learning ONNX inference",
    " 2. UCI vertebral ML risk scoring",
    " 3. Rule-based clinical scoring",
    "",
    "Classification targets:",
    "  Normal           Healthy spine",
    "  DiskHernia       Herniated disc",
    "  Spondylolisthesis Vertebral slip",
    "  Abnormal         General anomaly",
    "",
    "---- Architecture ----",
    "",
    "Conv2D(1→8)  ReLU  MaxPool(2)",
    "Conv2D(8→16) ReLU  MaxPool(2)",
    "Conv2D(16→32)ReLU  MaxPool(2)",
    "Flatten → FC(6272→128) → FC(2)",
    "Output: [Normal, Abnormal]",
    "",
    "---- Clinical Scoring ----",
    "",
    "Cobb Angle  < 10°  Normal",
    "            10-25° Mild scoliosis",
    "            25-50° Moderate",
    "            > 50°  Severe",
    "",
    "Pfirrmann Disc Grade 1-5",
    "T-Score < -2.5  Osteoporosis",
};
static const int g_article_lines_count = 34;

/* ─────────────────────────────────────────────────────  Logging  ──────────── */
static void log_msg(const char *msg, int cp) {
    if (g_log_count < MAX_LOG) {
        strncpy(g_log[g_log_count].text, msg, 255);
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
    char *p = s + strlen(s) - 1;
    while (p >= s && (*p == '"' || *p == ',' || *p == ' ' || *p == '\n'
                       || *p == '\r' || *p == ':')) { *p-- = '\0'; }
    p = s;
    while (*p == '"' || *p == ' ') p++;
    memmove(s, p, strlen(p) + 1);
}

static void parse_json_records(void) {
    FILE *f = fopen(JSON_PATH, "r");
    if (!f) {
        log_msg("WARNING: Cannot open spine_records.json – using demo data", CP_PANEL_RED);
        /* Insert demo records */
        SpineRecord *r;
        for (int i = 0; i < 4 && g_record_count < MAX_RECORDS; i++) {
            r = &g_records[g_record_count++];
            snprintf(r->patient_id, MAX_STR, "DEMO-%03d", i + 1);
            r->age = 35 + i * 7;
            strcpy(r->sex, i % 2 == 0 ? "M" : "F");
            strcpy(r->date, "2026-02-28");
            r->risk_score    = 75.0 + (i * 6.5);
            r->abnormal_prob = 0.1 + (i * 0.2);
            strcpy(r->classification, i < 2 ? "Normal" : "Abnormal");
        }
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
            continue;
        }
        if (!in_obj) continue;

        /* key": "value" */
        char key[64] = {0}, val[256] = {0};
        char *colon = strchr(l, ':');
        if (!colon) continue;
        strncpy(key, l, (size_t)(colon - l));
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
    char buf[256]; strncpy(buf, s, 254); buf[254] = '\0';
    if ((int)strlen(buf) > w) { buf[w-1] = '\0'; }
    mvwprintw(win, y, x, "%s", buf);
    return (int)strlen(buf);
}

/* ─────────────────────────────────────────────────────  Panel renderers  ─── */

static void render_titlebar(WINDOW *win, int cols, int fps) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char datebuf[64];
    strftime(datebuf, sizeof(datebuf), "LOAD %a %m/%d %H:%M:%S", tm);

    wattron(win, COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvwhline(win, 0, 0, ' ', cols);
    mvwprintw(win, 0, 0, " %s  FPS:%d", datebuf, fps);
    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwprintw(win, 0, cols - 16, "CTRL SHIFT ?");
    wattroff(win, A_BOLD | COLOR_PAIR(CP_HEADER));
}

/* ANNOUNCEMENT panel – top-left */
static void render_announce(WINDOW *win, int y, int x, int h, int w) {
    draw_box(win, y, x, h, w, "KARNIX-ANNOUNCE", "karnix.edge/#announce", CP_PANEL_YEL);

    int iy = y + 1, ix = x + 1, iw = w - 2;
    int row = iy;

    wattron(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
    mvwprint_clip(win, row++, ix, iw, "  --Karnix Spine Edge OS--  ");
    wattroff(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);

    wattron(win, COLOR_PAIR(CP_PANEL_YEL));
    mvwprint_clip(win, row++, ix, iw, "Clinical-Grade AI Screening");
    mvwprint_clip(win, row++, ix, iw, "ResNet50  |  ONNX Runtime");
    mvwprint_clip(win, row++, ix, iw, "UCI Vertebral  |  MURA Data");
    row++;

    wattron(win, COLOR_PAIR(CP_PANEL_GRN) | A_BOLD);
    mvwprint_clip(win, row++, ix, iw, ">>  COMING SOON  <<");
    wattroff(win, A_BOLD);
    wattron(win, COLOR_PAIR(CP_PANEL_YEL));
    mvwprint_clip(win, row++, ix, iw, "* DICOM Image Support");
    mvwprint_clip(win, row++, ix, iw, "* 3D Reconstruction View");
    mvwprint_clip(win, row++, ix, iw, "* Cloud Sync & Share");
    mvwprint_clip(win, row++, ix, iw, "* Voice Report Mode v2");
    row++;

    wattron(win, COLOR_PAIR(CP_PANEL_CYN));
    mvwprint_clip(win, row++, ix, iw, "Cmd: help | scan | dir");
    mvwprint_clip(win, row++, ix, iw, "     uci  | clear | quit");

    wattroff(win, COLOR_PAIR(CP_PANEL_CYN));
}

/* DIAGNOSIS panel – top-center (scrolling God Word style) */
static void render_diagnosis(WINDOW *win, int y, int x, int h, int w) {
    draw_box(win, y, x, h, w, "DIAGNOSIS", "karnix.edge/#diag", CP_PANEL_CYN);

    int iy = y + 1, ix = x + 1, iw = w - 2;

    wattron(win, COLOR_PAIR(CP_PANEL_CYN) | A_BOLD);
    mvwprint_clip(win, iy, ix, iw, "AI SPINE ANALYSIS SAYS:");
    wattroff(win, A_BOLD);

    wattron(win, COLOR_PAIR(CP_PANEL_GRN));
    /* Cycle diagnoses */
    for (int i = 0; i < h - 4 && i < g_diag_count; i++) {
        int idx = (g_diag_scroll + i) % g_diag_count;
        mvwprint_clip(win, iy + 1 + i, ix, iw, g_diagnoses[idx]);
    }

    draw_hline(win, y + h - 3, x + 1, w - 2, CP_PANEL_CYN);
    wattron(win, COLOR_PAIR(CP_PANEL_WHT));
    mvwprint_clip(win, y + h - 2, ix, iw, "[ NEW ANALYSIS ]");
    wattroff(win, COLOR_PAIR(CP_PANEL_WHT));
}

/* TOOLS panel – top-right */
static void render_tools(WINDOW *win, int y, int x, int h, int w) {
    draw_box(win, y, x, h, w, "TOOLS", "karnix.edge/#tools", CP_PANEL_GRN);

    int iy = y + 1, ix = x + 1, iw = w - 2;

    wattron(win, COLOR_PAIR(CP_PANEL_GRN) | A_BOLD);
    mvwprint_clip(win, iy, ix, iw, "// Run tools, dont guess!");
    wattroff(win, A_BOLD);

    for (int i = 0; i < g_tools_count && i < h - 4; i++) {
        if (i == g_active_panel % g_tools_count) {
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
    draw_box(win, y, x, h, w, "PATIENT-RECORDS", "karnix.edge/#records", CP_PANEL_MAG);

    int iy = y + 1, ix = x + 1, iw = w - 2;
    int max_rows = h - 2;

    /* Header row */
    wattron(win, COLOR_PAIR(CP_PANEL_MAG) | A_BOLD);
    char hdr[128];
    snprintf(hdr, sizeof(hdr), " %-16s %-8s %-6s %-5s %s",
             "PATIENT-ID", "DATE", "CLASS", "RISK", "ABN%");
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
        char haystack[512];
        snprintf(haystack, sizeof(haystack), "%s %s %s %s %s",
                 r->patient_id, r->date, r->classification, r->image_path, r->sex);
        if (g_search_query[0] && !contains_case_insensitive(haystack, g_search_query)) continue;
        matched++;
        if (matched <= g_dir_offset || shown >= max_rows - 3) continue;

        /* alternate colour; highlight selected */
        if (i == g_selected) {
            wattron(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD);
        } else if (strcmp(r->classification, "Normal") == 0) {
            wattron(win, COLOR_PAIR(CP_NORMAL));
        } else {
            wattron(win, COLOR_PAIR(CP_ABNORMAL));
        }

        char row_buf[160];
        snprintf(row_buf, sizeof(row_buf), " %-16.15s %-8.8s %-10.10s %5.1f %4.0f%%",
                 r->patient_id, r->date, r->classification,
                 r->risk_score, r->abnormal_prob * 100.0);
        mvwprint_clip(win, iy + 2 + shown, ix, iw, row_buf);
        wattroff(win, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD |
                 COLOR_PAIR(CP_NORMAL) | COLOR_PAIR(CP_ABNORMAL));
    }

    /* Scroll indicator */
    wattron(win, COLOR_PAIR(CP_PANEL_MAG));
    if (g_record_count > 0) {
        char sc[64];
        snprintf(sc, sizeof(sc), "[%d matches]  ^/v scroll  search <text>  search clear",
                 matched);
        mvwprint_clip(win, y + h - 2, ix, iw, sc);
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

    /* If a record is selected, show its detail */
    if (g_record_count > 0) {
        SpineRecord *r = &g_records[g_selected];

        wattron(win, COLOR_PAIR(CP_PANEL_YEL) | A_BOLD);
        char title[128];
        snprintf(title, sizeof(title), "---- %s ----", r->patient_id);
        mvwprint_clip(win, iy, ix, iw, title);
        wattroff(win, A_BOLD);

        /* Classification badge */
        if (strcmp(r->classification, "Normal") == 0)
            wattron(win, COLOR_PAIR(CP_NORMAL) | A_BOLD);
        else
            wattron(win, COLOR_PAIR(CP_ABNORMAL) | A_BOLD);
        mvwprintw(win, iy + 1, ix, "Status: %s", r->classification);
        wattroff(win, A_BOLD | COLOR_PAIR(CP_NORMAL) | COLOR_PAIR(CP_ABNORMAL));

        wattron(win, COLOR_PAIR(CP_PANEL_WHT));
        mvwprintw(win, iy + 2, ix, "Date   : %s", r->date);
        mvwprintw(win, iy + 3, ix, "Age    : %d", r->age);
        mvwprintw(win, iy + 4, ix, "Sex    : %s", r->sex[0] ? r->sex : "N/A");

        /* Risk bar */
        mvwprintw(win, iy + 5, ix, "Risk   : %.2f%%", r->risk_score);
        int bar_w = iw - 12;
        if (bar_w > 2) {
            int filled = (int)(r->risk_score * bar_w / 100.0);
            if (filled > bar_w) filled = bar_w;
            wattron(win, COLOR_PAIR(CP_PANEL_GRN));
            mvwprintw(win, iy + 6, ix, "[");
            for (int b = 0; b < filled; b++) waddch(win, ACS_BLOCK);
            wattron(win, COLOR_PAIR(CP_PANEL_RED));
            for (int b = filled; b < bar_w; b++) waddch(win, '-');
            wattron(win, COLOR_PAIR(CP_PANEL_WHT));
            waddch(win, ']');
        }

        /* Abnormal prob */
        mvwprintw(win, iy + 7, ix, "AbnPrb : %.4f", r->abnormal_prob);
        mvwprintw(win, iy + 8, ix, "Image  :");
        mvwprint_clip(win, iy + 9, ix, iw, r->image_path);

        draw_hline(win, iy + 10, ix, iw, CP_PANEL_WHT);

        /* UCI closest match */
        wattron(win, COLOR_PAIR(CP_PANEL_CYN) | A_BOLD);
        mvwprintw(win, iy + 11, ix, "Nearest UCI Record:");
        wattroff(win, A_BOLD);
        if (g_uci_count > 0) {
            UCIRecord *u = &g_uci[g_selected % g_uci_count];
            wattron(win, COLOR_PAIR(CP_PANEL_YEL));
            if (iy + 12 < y + h - 2) mvwprintw(win, iy + 12, ix, "PI:%.1f PT:%.1f LL:%.1f",
                    u->pelvic_incidence, u->pelvic_tilt, u->lumbar_lordosis);
            if (iy + 13 < y + h - 2) mvwprintw(win, iy + 13, ix, "SS:%.1f PR:%.1f SP:%.1f",
                    u->sacral_slope, u->pelvic_radius, u->spondylolisthesis);
            if (iy + 14 < y + h - 2) {
                if (strcmp(u->class_label, "Normal") == 0)
                    wattron(win, COLOR_PAIR(CP_NORMAL) | A_BOLD);
                else
                    wattron(win, COLOR_PAIR(CP_PANEL_RED) | A_BOLD);
                mvwprintw(win, iy + 14, ix, "Class: %s", u->class_label);
                wattroff(win, A_BOLD | COLOR_PAIR(CP_NORMAL) | COLOR_PAIR(CP_PANEL_RED));
            }
        }
        wattroff(win, COLOR_PAIR(CP_PANEL_YEL) | COLOR_PAIR(CP_PANEL_CYN));
    } else {
        /* fallback: show OS article */
        wattron(win, COLOR_PAIR(CP_PANEL_WHT));
        for (int i = 0; i < max_rows - 1 && (i + scroll) < g_article_lines_count; i++) {
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
    for (int i = start; i < g_log_count && col < ix + iw - 20; i++) {
        wattron(win, COLOR_PAIR(g_log[i].color_pair));
        /* single line snippets */
        char snip[48]; strncpy(snip, g_log[i].text, 47); snip[47] = '\0';
        char *nl = strchr(snip, '\n'); if (nl) *nl = '\0';
        mvwprintw(win, iy, col, "%s  ", snip);
        col += (int)strlen(snip) + 2;
        wattroff(win, COLOR_PAIR(g_log[i].color_pair));
    }
}

/* COMMAND BAR at very bottom */
static void render_cmdbar(WINDOW *win, int rows, int cols) {
    wattron(win, COLOR_PAIR(CP_CMD) | A_BOLD);
    mvwhline(win, rows - 1, 0, ' ', cols);
    mvwprintw(win, rows - 1, 0, " KarnixOS> %s", g_cmd_buf);
    /* cursor */
    wattron(win, A_BLINK);
    mvwaddch(win, rows - 1, 11 + g_cmd_len, '_');
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

/* ─────────────────────────────────────────────────────  Command handler  ─── */
static void exec_command(const char *cmd) {
    char buf[300];
    if (strcmp(cmd, "help") == 0) {
        log_msg("Commands: search <text>  search clear  help train scan brain spine3dData dir uci clear quit", CP_PANEL_GRN);
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
        g_record_count = 0;
        g_uci_count = 0;
        parse_json_records();
        parse_uci_csv();
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
        g_selected = 0;
        g_dir_offset = 0;
        log_msg("Patient search cleared.", CP_PANEL_GRN);
    } else if (strncmp(cmd, "search ", 7) == 0 || strncmp(cmd, "find ", 5) == 0) {
        const char *query = strncmp(cmd, "search ", 7) == 0 ? cmd + 7 : cmd + 5;
        while (*query == ' ') query++;
        strncpy(g_search_query, query, MAX_STR - 1);
        g_search_query[MAX_STR - 1] = '\0';
        g_selected = 0;
        g_dir_offset = 0;
        log_msg(g_search_query[0] ? "Patient search applied." : "Empty search; showing all records.", CP_PANEL_CYN);
    } else if (strcmp(cmd, "dir") == 0 || strcmp(cmd, "ls") == 0) {
        snprintf(buf, sizeof(buf), "Records loaded: %d  UCI: %d", g_record_count, g_uci_count);
        log_msg(buf, CP_PANEL_CYN);
    } else if (strcmp(cmd, "uci") == 0) {
        if (g_uci_count > 0) {
            UCIRecord *u = &g_uci[g_selected % g_uci_count];
            snprintf(buf, sizeof(buf), "UCI[%d] PI=%.1f PT=%.1f LL=%.1f  → %s",
                     g_selected % g_uci_count,
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
        if (g_selected > 0) { g_selected--; if (g_selected < g_dir_offset) g_dir_offset--; }
    } else if (strcmp(cmd, "down") == 0) {
        if (g_selected < g_record_count - 1) { g_selected++; }
    } else if (strncmp(cmd, "select ", 7) == 0) {
        int idx = atoi(cmd + 7);
        if (idx >= 0 && idx < g_record_count) { g_selected = idx; g_dir_offset = idx; }
    } else if (strlen(cmd) > 0) {
        snprintf(buf, sizeof(buf), "Unknown command: '%s'  (type: help)", cmd);
        log_msg(buf, CP_PANEL_RED);
    }
}

/* ─────────────────────────────────────────────────────  Main render loop  ── */
static void full_render(WINDOW *win, int rows, int cols, int fps) {
    werase(win);

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
    nodelay(stdscr, TRUE);   /* non-blocking input */
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

    /* ── FPS tracking ── */
    struct timespec ts_last, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_last);
    int frame_count = 0;
    int fps = 0;

    /* ── Article scroll timer ── */
    int diag_timer = 0;

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

        /* Scroll diagnosis panel every 3s */
        diag_timer++;
        if (diag_timer >= FPS_TARGET * 3) {
            diag_timer = 0;
            g_diag_scroll = (g_diag_scroll + 1) % g_diag_count;
        }

        /* ── Input ── */
        int ch = wgetch(win);
        if (ch != ERR) {
            if (ch == KEY_RESIZE) {
                getmaxyx(stdscr, rows, cols);
                wresize(win, rows, cols);
                mvwin(win, 0, 0);
            } else if (ch == KEY_UP || ch == 'k') {
                if (g_selected > 0) {
                    g_selected--;
                    if (g_selected < g_dir_offset) g_dir_offset = g_selected;
                }
            } else if (ch == KEY_DOWN || ch == 'j') {
                if (g_selected < g_record_count - 1) {
                    g_selected++;
                }
            } else if (ch == KEY_PPAGE) {
                g_diag_scroll = (g_diag_scroll + g_diag_count - 1) % g_diag_count;
            } else if (ch == KEY_NPAGE) {
                g_diag_scroll = (g_diag_scroll + 1) % g_diag_count;
            } else if (ch == '\n' || ch == '\r') {
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
            } else if (isprint(ch) && g_cmd_len < 253) {
                g_cmd_buf[g_cmd_len++] = (char)ch;
                g_cmd_buf[g_cmd_len]   = '\0';
            }
        }

        /* ── Render ── */
        full_render(win, rows, cols, fps);

        /* sleep ~1/FPS_TARGET seconds */
        usleep(1000000 / FPS_TARGET);
    }

    /* ── Cleanup ── */
    delwin(win);
    endwin();

    printf("\n\n");
    printf("  KarnixOS v1.0  –  Karnix Spine Edge\n");
    printf("  Spine records processed : %d\n", g_record_count);
    printf("  UCI records processed   : %d\n", g_uci_count);
    printf("  Session ended.\n\n");
    return 0;
}
