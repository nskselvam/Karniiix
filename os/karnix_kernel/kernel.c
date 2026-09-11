/*
 * KarnixOS kernel.c  –  Bare-metal x86 Protected-Mode Kernel
 * TempleOS-style multi-panel UI with PS/2 mouse + keyboard
 * Karnix Spine Edge data embedded  –  runs in QEMU
 *
 * Compile:  i686-elf-gcc -std=gnu99 -ffreestanding -O2 -Wall -Wextra
 *           -fno-stack-protector -fno-builtin -nostdlib kernel.c boot.o
 *           -T linker.ld -lgcc -o karnix.elf
 */

/* ============================================================
   Basic integer types (no stdint.h in freestanding mode)
   ============================================================ */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef unsigned int       size_t;

/* ============================================================
   I/O port helpers
   ============================================================ */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
/* Write a string to QEMU debug console (port 0xe9 = Bochs/QEMU debugcon) */
static void debug_puts(const char *s) {
    while (*s) { outb(0xe9, (uint8_t)(*s)); s++; }
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) { outb(0x80, 0x00); }

/* ============================================================
   VGA Text Mode  (80 × 25)
   ============================================================ */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEM     ((volatile uint16_t*)0xB8000)

/* Shadow buffer – all rendering targets RAM; vga_flush() pushes only
 * changed cells to video memory in one pass, eliminating tearing/flicker. */
static uint16_t g_shadow[VGA_HEIGHT * VGA_WIDTH];
static uint16_t g_committed[VGA_HEIGHT * VGA_WIDTH];  /* what is on screen now */
#define SHADOW(row,col) g_shadow[(row)*VGA_WIDTH+(col)]

/* TempleOS / VGA colour indices */
enum vga_color {
    VGA_BLACK=0, VGA_BLUE=1, VGA_GREEN=2, VGA_CYAN=3,
    VGA_RED=4,   VGA_MAGENTA=5, VGA_BROWN=6, VGA_LGRAY=7,
    VGA_DGRAY=8, VGA_LBLUE=9,  VGA_LGREEN=10,VGA_LCYAN=11,
    VGA_LRED=12, VGA_LMAG=13,  VGA_YELLOW=14,VGA_WHITE=15
};

#define VGA_ENTRY(c,fg,bg) ((uint16_t)(c) | ((uint16_t)((bg)<<4|(fg))<<8))
#define VGA_ATTR(fg,bg)    ((uint8_t)((bg)<<4|(fg)))

/* Write one cell into the shadow buffer only (never directly to VGA) */
static void vga_putc(int col, int row, char c, uint8_t fg, uint8_t bg) {
    if (col<0||col>=VGA_WIDTH||row<0||row>=VGA_HEIGHT) return;
    SHADOW(row,col) = VGA_ENTRY((unsigned char)c, fg, bg);
}
static void vga_puts(int col, int row, const char *s,
                     uint8_t fg, uint8_t bg, int maxw) {
    for (int i=0; s[i] && (maxw<0||i<maxw); i++)
        vga_putc(col+i, row, s[i], fg, bg);
}
static void vga_fill(int col, int row, int w, int h,
                     char c, uint8_t fg, uint8_t bg) {
    for (int r=row; r<row+h; r++)
        for (int x=col; x<col+w; x++)
            vga_putc(x, r, c, fg, bg);
}
static void vga_box(int col, int row, int w, int h,
                    uint8_t fg, uint8_t bg) {
    /* top + bottom horizontal double lines */
    vga_putc(col,     row,     '\xC9', fg, bg);
    vga_putc(col+w-1, row,     '\xBB', fg, bg);
    vga_putc(col,     row+h-1, '\xC8', fg, bg);
    vga_putc(col+w-1, row+h-1, '\xBC', fg, bg);
    for (int i=1;i<w-1;i++) {
        vga_putc(col+i, row,     '\xCD', fg, bg);
        vga_putc(col+i, row+h-1, '\xCD', fg, bg);
    }
    for (int i=1;i<h-1;i++) {
        vga_putc(col,     row+i, '\xBA', fg, bg);
        vga_putc(col+w-1, row+i, '\xBA', fg, bg);
    }
    /* clear interior */
    for (int r=row+1;r<row+h-1;r++)
        for (int x=col+1;x<col+w-1;x++)
            vga_putc(x, r, ' ', fg, bg);
}
/* print integer */
static void __attribute__((unused)) vga_puti(int col, int row, int32_t val,
                     uint8_t fg, uint8_t bg) {
    char buf[16]; int i=15, neg=0;
    buf[i]='\0';
    if (val<0){neg=1;val=-val;}
    if (val==0){buf[--i]='0';}
    while(val>0){buf[--i]='0'+(val%10);val/=10;}
    if (neg) buf[--i]='-';
    vga_puts(col, row, buf+i, fg, bg, -1);
}
/* hardware text cursor off */
static void vga_cursor_off(void) {
    outb(0x3D4, 0x0A); outb(0x3D5, 0x20);
}

/* Push changed shadow cells to real VGA memory.
 * Walking 2000 cells and comparing is far faster than writing all 2000
 * every frame, and removes all tearing. */
static void vga_flush(void) {
    int i;
    for (i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        if (g_shadow[i] != g_committed[i]) {
            VGA_MEM[i]     = g_shadow[i];
            g_committed[i] = g_shadow[i];
        }
    }
}

/* ============================================================
   Mini string helpers  (no libc)
   ============================================================ */
static int kstrlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static int kstrcmp(const char *a, const char *b) {
    while (*a && *a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b;
}
static int kstrncmp(const char *a, const char *b, int n) {
    while (n && *a && *a==*b){a++;b++;n--;} if(!n) return 0;
    return (unsigned char)*a-(unsigned char)*b;
}
static char *kstrcpy(char *d, const char *s) {
    char *r=d; while((*d++=*s++)); return r; }
static void kitoa(int32_t val, char *buf) {
    if (val==0){buf[0]='0';buf[1]='\0';return;}
    char tmp[16]; int i=0; int neg=0;
    if(val<0){neg=1;val=-val;}
    while(val>0){tmp[i++]='0'+(val%10);val/=10;}
    if(neg) tmp[i++]='-';
    for(int j=0;j<i;j++) buf[j]=tmp[i-1-j];
    buf[i]='\0';
}
/* ============================================================
   GDT – flat 32-bit   (code + data at ring 0, 4 GB each)
   ============================================================ */
struct gdt_entry { uint16_t lim_lo,base_lo; uint8_t base_mid,access,gran,base_hi; }
    __attribute__((packed));
struct gdt_ptr  { uint16_t limit; uint32_t base; } __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr   gdtptr;

static void gdt_set(int i, uint32_t base, uint32_t lim,
                    uint8_t access, uint8_t gran) {
    gdt[i].base_lo  = base & 0xFFFF;
    gdt[i].base_mid = (base>>16)&0xFF;
    gdt[i].base_hi  = (base>>24)&0xFF;
    gdt[i].lim_lo   = lim & 0xFFFF;
    gdt[i].gran     = ((lim>>16)&0x0F)|gran;
    gdt[i].access   = access;
}
static void gdt_flush(struct gdt_ptr *p) {
    __asm__ volatile (
        "lgdtl (%0)\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "r"(p) : "eax", "memory"
    );
}
static void gdt_init(void) {
    gdt_set(0, 0,0,0,0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF);  /* code */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF);  /* data */
    gdtptr.limit = sizeof(gdt)-1;
    gdtptr.base  = (uint32_t)&gdt;
    gdt_flush(&gdtptr);
}

/* ============================================================
   IDT  (256 entries, interrupt gates)
   ============================================================ */
struct idt_entry { uint16_t off_lo,sel; uint8_t zero,flags; uint16_t off_hi; }
    __attribute__((packed));
struct idt_ptr  { uint16_t limit; uint32_t base; } __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtptr;

static void idt_set(int n, uint32_t base, uint8_t flags) {
    idt[n].off_lo = base & 0xFFFF;
    idt[n].off_hi = (base>>16)&0xFFFF;
    idt[n].sel    = 0x08;
    idt[n].zero   = 0;
    idt[n].flags  = flags;
}

/* Generic ISR stub – asm wrapper calls c_isr_dispatch(vector) */
/* We generate 8 IRQ stubs manually – IRQ0-7 (master PIC) and IRQ8-15 (slave) */
#define IRQ_BASE 0x20

static volatile uint32_t g_tick = 0;

/* ── PIC ──────────────────────────────────────────────────── */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_init(void) {
    /* Remap IRQs: master 0x20-0x27, slave 0x28-0x2F */
    outb(PIC1_CMD,  0x11); io_wait();   /* init */
    outb(PIC2_CMD,  0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();   /* master vector offset 0x20 */
    outb(PIC2_DATA, 0x28); io_wait();   /* slave  vector offset 0x28 */
    outb(PIC1_DATA, 0x04); io_wait();   /* master: slave at IRQ2 (bit2) */
    outb(PIC2_DATA, 0x02); io_wait();   /* slave:  cascade identity = 2 */
    outb(PIC1_DATA, 0x01); io_wait();   /* 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();   /* 8086 mode */

    /*
     * IRQ mask: 0 = enabled, 1 = masked.
     *
     * Master PIC (IRQ0-7):
     *   Bit 0 = IRQ0  (PIT timer)       → ENABLE
     *   Bit 1 = IRQ1  (PS/2 keyboard)   → ENABLE
     *   Bit 2 = IRQ2  (slave cascade)   → ENABLE  ← MUST be 0 or slave
     *                                               PIC (mouse) is deaf!
     *   Bits 3-7    (all others)        → mask
     *   0xF8 = 1111 1000
     *
     * Slave PIC (IRQ8-15):
     *   Bit 4 = IRQ12 (PS/2 mouse)      → ENABLE
     *   0xEF = 1110 1111
     */
    outb(PIC1_DATA, 0xF8);   /* enable IRQ0, IRQ1, IRQ2(cascade) */
    outb(PIC2_DATA, 0xEF);   /* enable IRQ12 */
}

static void pic_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

/*
 * ISR entry stubs are in boot.asm (pure hand-written asm).
 * Declare them extern so we can take their addresses for the IDT.
 * The C handlers below (irq0_handler / irq1_handler / irq12_handler)
 * are called BY those asm stubs, so they must have external linkage.
 */
extern void _isr_irq0(void);
extern void _isr_irq1(void);
extern void _isr_irq12(void);
extern void _isr_dummy(void);

static void idt_init(void) {
    uint32_t dummy = (uint32_t)_isr_dummy;
    for (int i=0;i<256;i++) idt_set(i, dummy, 0x8E);

    idt_set(IRQ_BASE+0,  (uint32_t)_isr_irq0,  0x8E);
    idt_set(IRQ_BASE+1,  (uint32_t)_isr_irq1,  0x8E);
    idt_set(IRQ_BASE+12, (uint32_t)_isr_irq12, 0x8E);

    idtptr.limit = sizeof(idt)-1;
    idtptr.base  = (uint32_t)&idt;
    __asm__ volatile ("lidtl (%0)" : : "r"(&idtptr));
}

/* ── PIT Timer – 100 Hz ────────────────────────────────────── */
#define PIT_HZ   100
#define PIT_DIV  (1193180 / PIT_HZ)

static void pit_init(void) {
    outb(0x43, 0x36);
    outb(0x40, PIT_DIV & 0xFF);
    outb(0x40, (PIT_DIV>>8) & 0xFF);
}

/* Called from _isr_irq0 in boot.asm */
void irq0_handler(void) {
    g_tick++;
    pic_eoi(0);
}

/* ============================================================
   PC Speaker – TempleOS-style boot chime + key click
   ============================================================ */
#define SPEAKER_PORT 0x61

static void speaker_on(uint32_t freq) {
    if (freq == 0) return;
    uint32_t div = 1193180u / freq;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));
    uint8_t tmp = inb(SPEAKER_PORT);
    if (tmp != (uint8_t)(tmp | 3)) outb(SPEAKER_PORT, (uint8_t)(tmp | 3));
}
static void speaker_off(void) {
    uint8_t tmp = inb(SPEAKER_PORT) & 0xFC;
    outb(SPEAKER_PORT, tmp);
}
/* Blocking beep, length measured in PIT ticks (100/sec) */
static void beep_ticks(uint32_t freq, uint32_t ticks) {
    speaker_on(freq);
    uint32_t start = g_tick;
    while (g_tick - start < ticks) { __asm__ volatile ("sti\n\thlt\n" ::: "memory"); }
    speaker_off();
}
/* Short non-blocking click heard on every keypress */
static void key_click(void) {
    speaker_on(1800);
    for (volatile int i = 0; i < 4000; i++) { __asm__ volatile ("nop"); }
    speaker_off();
}
/* Divine three-note ascending fanfare played once at boot */
static void boot_chime(void) {
    beep_ticks(523, 8);   /* C5 */
    beep_ticks(659, 8);   /* E5 */
    beep_ticks(784, 18);  /* G5 */
}

/* ============================================================
   PS/2 Keyboard Driver
   ============================================================ */
#define KBD_DATA 0x60
#define KBD_STAT 0x64

static const char sc_normal[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0
};
static const char sc_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0
};

#define KEY_UP    0x80
#define KEY_DOWN  0x48+0x100
#define KEY_LEFT  0x4B+0x100
#define KEY_RIGHT 0x4D+0x100
#define KEY_F1    0x3B
#define KEY_DEL   0x53
#define KEY_ESC   0x01

static volatile uint8_t  kbd_shift   = 0;
static volatile uint8_t  kbd_ext     = 0;   /* 0xE0 prefix */
static volatile char     kbd_char    = 0;
static volatile int      kbd_pending = 0;
static volatile uint8_t  kbd_special = 0;  /* UP/DOWN/LEFT/RIGHT etc. */

/* Called from _isr_irq1 in boot.asm */
void irq1_handler(void) {
    uint8_t sc = inb(KBD_DATA);
    if (sc == 0xE0) { kbd_ext = 1; pic_eoi(1); return; }
    if (sc == 0xE1) { kbd_ext = 0; pic_eoi(1); return; } /* Pause/Break ignore */

    if (kbd_ext) {
        kbd_ext = 0;
        if (!(sc & 0x80)) { /* key press, not release */
            kbd_special = sc;
            kbd_pending = 2; /* signal extended key */
        }
        pic_eoi(1);
        return;
    }

    if (sc == 0x2A || sc == 0x36) { kbd_shift=1; pic_eoi(1); return; }
    if (sc == 0xAA || sc == 0xB6) { kbd_shift=0; pic_eoi(1); return; }

    if (sc & 0x80) { /* key release */ pic_eoi(1); return; }

    if (sc == KEY_ESC) { kbd_special=KEY_ESC; kbd_pending=2; pic_eoi(1); return; }

    char c = kbd_shift ? sc_shift[sc] : sc_normal[sc];
    if (c) {
        kbd_char = c;
        kbd_pending = 1;  /* normal ASCII */
    }
    pic_eoi(1);
}

/* ============================================================
   PS/2 Mouse Driver
   ============================================================ */
#define MOUSE_STATUS   0x64
#define MOUSE_ACOMMAND 0x64
#define MOUSE_DATA     0x60
#define MOUSE_INPUT_FULL  0x02
#define MOUSE_OUTPUT_FULL 0x01

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (!type) {
        while (timeout-- && !(inb(MOUSE_STATUS) & MOUSE_OUTPUT_FULL));
    } else {
        while (timeout-- && (inb(MOUSE_STATUS) & MOUSE_INPUT_FULL));
    }
}
static void mouse_write(uint8_t val) {
    mouse_wait(1);
    outb(MOUSE_ACOMMAND, 0xD4);
    mouse_wait(1);
    outb(MOUSE_DATA, val);
}
static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(MOUSE_DATA);
}

static volatile int  mouse_x = 40, mouse_y = 12;
static volatile uint8_t mouse_buttons = 0;
static volatile int  mouse_click = 0;
static uint8_t       mouse_cycle = 0;
static uint8_t       mouse_packet[3];

/* Called from _isr_irq12 in boot.asm */
void irq12_handler(void) {
    /* drain the PS/2 output buffer */
    if (!(inb(MOUSE_STATUS) & MOUSE_OUTPUT_FULL)) { pic_eoi(12); return; }

    uint8_t val = inb(MOUSE_DATA);

    /* Simple 3-byte packet accumulator.
     * Byte 0: sync – bit3 must be set (always 1 for standard PS/2 mouse).
     * We re-sync if byte 0 is invalid so a single dropped byte can't
     * corrupt the stream permanently. */
    if (mouse_cycle == 0) {
        /* Discard bytes that can't be a valid first byte */
        if (!(val & 0x08)) { pic_eoi(12); return; }
    }

    mouse_packet[mouse_cycle] = val;
    mouse_cycle++;

    if (mouse_cycle == 3) {
        mouse_cycle = 0;

        int8_t  dx   = (int8_t)mouse_packet[1];
        int8_t  dy   = -(int8_t)mouse_packet[2]; /* VGA y inverted */

        /* Scale movement: shift right 2 so large sweeps still move fast
           but small nudges still register */
        int nx = mouse_x + (int)dx / 4;
        int ny = mouse_y + (int)dy / 4;
        if (nx < 0)            nx = 0;
        if (nx >= VGA_WIDTH)   nx = VGA_WIDTH - 1;
        if (ny < 1)            ny = 1;
        if (ny >= VGA_HEIGHT-1) ny = VGA_HEIGHT - 2;
        mouse_x = nx;
        mouse_y = ny;

        uint8_t prev  = mouse_buttons;
        mouse_buttons = mouse_packet[0] & 0x07;
        if ((mouse_buttons & 1) && !(prev & 1)) mouse_click = 1;
    }
    pic_eoi(12);
}

/* Drain any stale PS/2 bytes */
static void ps2_flush(void) {
    int i;
    for (i = 0; i < 64; i++) {
        if (!(inb(0x64) & 0x01)) break;
        inb(0x60);
        io_wait();
    }
}

static void mouse_init(void) {
    ps2_flush();

    /* 1. Enable PS/2 aux port */
    mouse_wait(1); outb(MOUSE_ACOMMAND, 0xA8); io_wait();

    /* 2. Read Compaq status byte, set bit1 (enable aux IRQ12), clear bit5 */
    mouse_wait(1); outb(MOUSE_ACOMMAND, 0x20); io_wait();
    mouse_wait(0);
    uint8_t status = (inb(MOUSE_DATA) | 0x02) & ~0x20;
    mouse_wait(1); outb(MOUSE_ACOMMAND, 0x60); io_wait();
    mouse_wait(1); outb(MOUSE_DATA, status);   io_wait();

    /* 3. Reset – replies: ACK(0xFA) + BAT_OK(0xAA) + DevID(0x00) */
    mouse_write(0xFF);
    mouse_read();   /* 0xFA ACK  */
    mouse_read();   /* 0xAA BAT  */
    mouse_read();   /* 0x00 ID   */

    /* 4. Set defaults */
    mouse_write(0xF6); mouse_read(); /* ACK */

    /* 5. Set sample rate to 40 (reduces noise) */
    mouse_write(0xF3); mouse_read();
    mouse_write(40);   mouse_read();

    /* 6. Enable data reporting */
    mouse_write(0xF4); mouse_read(); /* ACK */

    mouse_cycle = 0;
    mouse_x = VGA_WIDTH  / 2;
    mouse_y = VGA_HEIGHT / 2;
}

/* ============================================================
   Patient data – generated by db_exporter.js from PostgreSQL
   Privacy: patient_id codes only, no names, no doctor PII
   Run:  make data   to refresh from live database
   ============================================================ */
#include "db_data.h"

/* ============================================================
   UI State
   ============================================================ */
#define CMD_MAX 72

static char     g_cmd[CMD_MAX+1] = {0};
static int      g_cmd_len = 0;
static int      g_sel = 0;          /* selected record (directory) */
static int      g_dir_offset = 0;   /* scroll offset */
/* risk filter: 0=all  1=HIGH  2=MODERATE  3=LOW */
static int      g_risk_filter = 0;
/* view mode: 0=normal dashboard  1=heatmap fullscreen */
static int      g_view = 0;
/* heatmap scroll offset (first record shown at top of grid) */
static int      g_hmap_offset = 0;
static uint32_t g_last_tick = 0;
static int      g_fps = 0;
static int      g_frame_count = 0;
static uint32_t g_fps_tick = 0;
static int      g_diag_line = 0;
static uint32_t g_diag_tick = 0;


/* log ring buffer */
#define LOG_MAX 8
static char  g_log[LOG_MAX][72];
static int   g_log_head = 0, g_log_count = 0;
#define LOG_CP_INFO  VGA_LGRAY
#define LOG_CP_OK    VGA_LGREEN
#define LOG_CP_WARN  VGA_YELLOW
#define LOG_CP_ERR   VGA_LRED

static void log_push(const char *s) {
    int i = g_log_head;
    int n = 0;
    while (s[n] && n < 71) { g_log[i][n] = s[n]; n++; }
    g_log[i][n] = '\0';
    g_log_head = (g_log_head + 1) % LOG_MAX;
    if (g_log_count < LOG_MAX) g_log_count++;
}

/* diagnoses – cycle every 3 seconds */
static const char *g_diags[12] = {
    "VERTEBRAL ALIGNMENT: NOMINAL",
    "SCOLIOSIS RISK: LOW  (<10 deg Cobb)",
    "DISC INTEGRITY: STABLE",
    "BONE DENSITY INDEX: 98.2%",
    "LUMBAR LORDOSIS: 44.3 deg  [OK]",
    "SACRAL SLOPE: 38.7 deg  [OK]",
    "PELVIC INCIDENCE: 52.1  [OK]",
    "FRACTURE RISK: <5%  FRAX",
    "SPONDYLOLISTHESIS: ABSENT",
    "SPINAL CANAL: PATENT",
    "COMPRESSION FX RISK: LOW",
    "CNN CONFIDENCE: 99.94%",
};
#define NDIAGS 12

/* ============================================================
   Minimal HolyC-inspired expression evaluator
   Recursive-descent parser: + - * / ( ) unary minus, integers only.
   Typed straight on the command line, e.g.  calc (2+3)*4
   ============================================================ */
static const char *g_eval_p;
static int         g_eval_err;

static int holy_eval_expr(void);

static void holy_eval_skip(void) { while (*g_eval_p == ' ') g_eval_p++; }

static int holy_eval_num(void) {
    holy_eval_skip();
    if (*g_eval_p == '(') {
        g_eval_p++;
        int v = holy_eval_expr();
        holy_eval_skip();
        if (*g_eval_p == ')') g_eval_p++; else g_eval_err = 1;
        return v;
    }
    if (*g_eval_p == '-') { g_eval_p++; return -holy_eval_num(); }
    int v = 0, any = 0;
    while (*g_eval_p >= '0' && *g_eval_p <= '9') { v = v*10 + (*g_eval_p - '0'); g_eval_p++; any = 1; }
    if (!any) g_eval_err = 1;
    return v;
}
static int holy_eval_term(void) {
    int v = holy_eval_num();
    for (;;) {
        holy_eval_skip();
        if (*g_eval_p == '*') { g_eval_p++; v *= holy_eval_num(); }
        else if (*g_eval_p == '/') {
            g_eval_p++;
            int d = holy_eval_num();
            if (d == 0) { g_eval_err = 1; v = 0; } else v /= d;
        } else break;
    }
    return v;
}
static int holy_eval_expr(void) {
    int v = holy_eval_term();
    for (;;) {
        holy_eval_skip();
        if (*g_eval_p == '+') { g_eval_p++; v += holy_eval_term(); }
        else if (*g_eval_p == '-') { g_eval_p++; v -= holy_eval_term(); }
        else break;
    }
    return v;
}
/* Returns 1 on success (writes *out), 0 on syntax error */
static int holy_eval(const char *expr, int *out) {
    g_eval_p = expr; g_eval_err = 0;
    int v = holy_eval_expr();
    holy_eval_skip();
    if (*g_eval_p != '\0') g_eval_err = 1;
    *out = v;
    return !g_eval_err;
}

/* ============================================================
   Command handler
   ============================================================ */
static void exec_cmd(void) {
    const char *cmd = g_cmd;
    if (!cmd[0]) return;

    if (kstrcmp(cmd,"help")==0) {
        log_push("scan dir uci stats db risk sel info heatmap back");
        log_push("spine3d brain  clear  quit  calc <expr>  paint");

    } else if (kstrncmp(cmd,"calc ",5)==0) {
        int result;
        if (holy_eval(cmd+5, &result)) {
            char buf[72]; kstrcpy(buf,"= "); kitoa(result, buf+2);
            log_push(buf);
            beep_ticks(900, 4);
        } else {
            log_push("calc: syntax error");
        }

    } else if (kstrcmp(cmd,"paint")==0) {
        g_view = 2;
        log_push("Holy Paint opened  0-9/A-F=colour Space=paint Del=clear Esc=back");

    } else if (kstrcmp(cmd,"scan")==0) {
        char buf[72]; kstrcpy(buf,"DB:"); kitoa(NRECORDS,buf+3);
        kstrcpy(buf+kstrlen(buf)," recs  UCI:"); kitoa(NUCI,buf+kstrlen(buf));
        kstrcpy(buf+kstrlen(buf),"  Total:"); kitoa(DB_TOTAL_PATIENTS,buf+kstrlen(buf));
        log_push(buf);

    } else if (kstrcmp(cmd,"dir")==0 || kstrcmp(cmd,"ls")==0) {
        char buf[72]; kstrcpy(buf,"Records:"); kitoa(NRECORDS,buf+8);
        kstrcpy(buf+kstrlen(buf)," UCI:"); kitoa(NUCI,buf+kstrlen(buf));
        kstrcpy(buf+kstrlen(buf)," DB-total:"); kitoa(DB_TOTAL_PATIENTS,buf+kstrlen(buf));
        log_push(buf);

    } else if (kstrcmp(cmd,"uci")==0) {
        const UCIRecord *u = &g_uci[g_sel % NUCI];
        char buf[72]; kstrcpy(buf,"PI:");
        kitoa(u->pi10/10,buf+3);
        kstrcpy(buf+kstrlen(buf)," PT:"); kitoa(u->pt10/10,buf+kstrlen(buf));
        kstrcpy(buf+kstrlen(buf)," LL:"); kitoa(u->ll10/10,buf+kstrlen(buf));
        kstrcpy(buf+kstrlen(buf)," => "); kstrcpy(buf+kstrlen(buf),u->label);
        log_push(buf);

    } else if (kstrcmp(cmd,"stats")==0) {
        char buf[72];
        kstrcpy(buf,"Total:"); kitoa(DB_TOTAL_PATIENTS,buf+6);
        kstrcpy(buf+kstrlen(buf)," H:"); kitoa(DB_HIGH_RISK,buf+kstrlen(buf));
        kstrcpy(buf+kstrlen(buf)," M:"); kitoa(DB_MOD_RISK,buf+kstrlen(buf));
        kstrcpy(buf+kstrlen(buf)," L:"); kitoa(DB_LOW_RISK,buf+kstrlen(buf));
        kstrcpy(buf+kstrlen(buf)," Avg%:"); kitoa(DB_AVG_RISK_X10/10,buf+kstrlen(buf));
        log_push(buf);

    } else if (kstrcmp(cmd,"db")==0) {
        log_push("SRC:" DB_SOURCE);

    } else if (kstrcmp(cmd,"risk")==0||kstrcmp(cmd,"risk all")==0) {
        g_risk_filter=0; g_sel=0; g_dir_offset=0; log_push("Filter:ALL");
    } else if (kstrncmp(cmd,"risk ",5)==0) {
        char ch=cmd[5];
        if (ch=='H'||ch=='h'){g_risk_filter=1;log_push("Filter:HIGH");}
        else if(ch=='M'||ch=='m'){g_risk_filter=2;log_push("Filter:MODERATE");}
        else if(ch=='L'||ch=='l'){g_risk_filter=3;log_push("Filter:LOW");}
        else log_push("usage: risk H|M|L|all");
        g_sel=0; g_dir_offset=0;
    } else if (kstrcmp(cmd,"high")==0){g_risk_filter=1;g_sel=0;g_dir_offset=0;log_push("Filter:HIGH");
    } else if (kstrcmp(cmd,"mid")==0||kstrcmp(cmd,"mod")==0){g_risk_filter=2;g_sel=0;g_dir_offset=0;log_push("Filter:MODERATE");
    } else if (kstrcmp(cmd,"low")==0){g_risk_filter=3;g_sel=0;g_dir_offset=0;log_push("Filter:LOW");

    } else if (kstrcmp(cmd,"sel")==0) {
        log_push("usage: sel N  (0-based index)");
    } else if (kstrncmp(cmd,"sel ",4)==0) {
        int idx=0; const char *p=cmd+4;
        while(*p>='0'&&*p<='9'){idx=idx*10+(*p-'0');p++;}
        if(idx>=0&&idx<NRECORDS){g_sel=idx;log_push("Record selected.");}
        else log_push("sel: index out of range");

    } else if (kstrcmp(cmd,"info")==0) {
        const SpineRec *r=&g_records[g_sel];
        char buf[72]; kstrcpy(buf,r->patient_id);
        kstrcpy(buf+kstrlen(buf)," "); kstrcpy(buf+kstrlen(buf),r->classification);
        kstrcpy(buf+kstrlen(buf)," Risk:"); kitoa(r->risk_score_int/10,buf+kstrlen(buf));
        kstrcpy(buf+kstrlen(buf),"% "); kstrcpy(buf+kstrlen(buf),r->risk_category);
        log_push(buf);
        char buf2[72];
        kstrcpy(buf2,"Cobb:"); kitoa(r->cobb_x10/10,buf2+5);
        kstrcpy(buf2+kstrlen(buf2),"deg Bone:");
        { int bd=r->bone_x10; if(bd<0){kstrcpy(buf2+kstrlen(buf2),"-");kitoa(-bd/10,buf2+kstrlen(buf2));}
          else kitoa(bd/10,buf2+kstrlen(buf2)); }
        kstrcpy(buf2+kstrlen(buf2)," Frac:"); kitoa(r->frac_grade,buf2+kstrlen(buf2));
        kstrcpy(buf2+kstrlen(buf2)," Disc:"); kitoa(r->disc_grade,buf2+kstrlen(buf2));
        log_push(buf2);
        char buf3[72];
        kstrcpy(buf3,"TBS:"); kitoa(r->tbs_x100/100,buf3+kstrlen(buf3));
        kstrcpy(buf3+kstrlen(buf3),"."); kitoa((r->tbs_x100/10)%10,buf3+kstrlen(buf3));
        kstrcpy(buf3+kstrlen(buf3)," CTthin:"); kitoa(r->cortical_idx,buf3+kstrlen(buf3));
        kstrcpy(buf3+kstrlen(buf3)," MRI-dark:"); kitoa(r->marrow_dark_pct,buf3+kstrlen(buf3));
        kstrcpy(buf3+kstrlen(buf3),"%");
        log_push(buf3);

    } else if (kstrcmp(cmd,"heatmap")==0||kstrcmp(cmd,"hmap")==0) {
        g_view=1; g_hmap_offset=0;
        log_push("Heatmap/ opened  Esc or 'back' to return");

    } else if (kstrcmp(cmd,"back")==0||kstrcmp(cmd,"home")==0) {
        g_view=0; log_push("Dashboard restored.");

    } else if (kstrcmp(cmd,"spine3d")==0||kstrcmp(cmd,"3d")==0||kstrcmp(cmd,"viewer")==0) {
        log_push("Launching 3D Spine Viewer on host...");
        log_push("Check the host desktop for the viewer.");
        debug_puts("KARNIX_LAUNCH_SPINE3D\n");

    } else if (kstrcmp(cmd,"brain")==0||kstrcmp(cmd,"brain_tumour")==0||kstrcmp(cmd,"braintumour")==0) {
        log_push("Launching Brain Tumour CNN visualizer on host...");
        log_push("Check the host desktop for the MRI CNN console.");
        debug_puts("KARNIX_LAUNCH_BRAIN_TUMOUR\n");

    } else if (kstrcmp(cmd,"spine3dData")==0 || kstrcmp(cmd,"moonday")==0 || kstrcmp(cmd,"jester")==0) {
        log_push("Launching Moonday Spine Data interface on host...");
        log_push("Electron app will provide macOS controls and the spine dashboard.");
        debug_puts("KARNIX_LAUNCH_MOONDAY\n");

    } else if (kstrcmp(cmd,"clear")==0) {
        g_log_count=0; g_log_head=0; log_push("Log cleared.");

    } else if (kstrcmp(cmd,"quit")==0||kstrcmp(cmd,"exit")==0) {
        log_push("System halted. Reset to restart.");
        vga_fill(0,0,VGA_WIDTH,VGA_HEIGHT,' ',VGA_WHITE,VGA_BLACK);
        vga_puts(25,12,"  KarnixOS  Session ended.  ",VGA_WHITE,VGA_BLUE,40);
        __asm__ volatile ("cli;hlt");
    } else {
        char buf[72]; kstrcpy(buf,"Unknown: "); kstrcpy(buf+9,cmd);
        log_push(buf);
    }

    g_cmd[0]='\0'; g_cmd_len=0;
}

/* ============================================================
   Render helpers
   ============================================================ */
/* box title in the top border */
static void box_title(int col, int row, const char *title, const char *url,
                      uint8_t fg, uint8_t bg) {
    int tl = kstrlen(title)+2;
    vga_putc(col+1, row, '-', fg, bg);
    vga_puts(col+2, row, title, fg, bg, tl-2);
    vga_putc(col+tl, row, '-', fg, bg);
    (void)url;
}
static void box_close(int col, int row, int w) {
    /* [X] button – red on black right corner */
    vga_putc(col+w-4, row, '[', VGA_LRED, VGA_BLACK);
    vga_putc(col+w-3, row, 'X', VGA_LRED, VGA_BLACK);
    vga_putc(col+w-2, row, ']', VGA_LRED, VGA_BLACK);
}

/* ── Panel: ANNOUNCE (top-left) ─────────────────────────────────────────────*/
static void render_announce(int y, int x, int h, int w) {
    vga_box(x,y,w,h, VGA_LBLUE, VGA_BLACK);
    box_title(x,y,"KARNIX-EDGE","",VGA_YELLOW,VGA_BLACK);
    box_close(x,y,w);

    int r=y+1, ix=x+1, iw=w-2;
    vga_puts(ix,r++,"  --KarnixOS  v1.0--  ",VGA_BLACK,VGA_YELLOW,iw);
    vga_puts(ix,r++,"Clinical AI  Spine OS",VGA_YELLOW,VGA_BLACK,iw); r++;
    vga_puts(ix,r++,"ResNet50 | ONNX | UCI",VGA_LGRAY,VGA_BLACK,iw);
    vga_puts(ix,r++,"MURA trained | 173 pts",VGA_LGRAY,VGA_BLACK,iw); r++;
    vga_puts(ix,r++,">> COMING SOON <<",VGA_LGREEN,VGA_BLACK,iw);
    vga_puts(ix,r++,"* DICOM Image Support",VGA_YELLOW,VGA_BLACK,iw);
    vga_puts(ix,r++,"* 3D Reconstruction",VGA_YELLOW,VGA_BLACK,iw);
    vga_puts(ix,r++,"* Voice Report v2",VGA_YELLOW,VGA_BLACK,iw); r++;
    vga_puts(ix,r++,"Keys:",VGA_CYAN,VGA_BLACK,iw);
    vga_puts(ix,r++," arrows=navigate",VGA_LGRAY,VGA_BLACK,iw);
    vga_puts(ix,r++," Enter=run cmd",VGA_LGRAY,VGA_BLACK,iw);
    vga_puts(ix,r++," Esc=quit",VGA_LGRAY,VGA_BLACK,iw);
}

/* ── Panel: DIAGNOSIS (top-center, scrolling) ────────────────────────────── */
static void render_diag(int y, int x, int h, int w) {
    vga_box(x,y,w,h, VGA_LBLUE, VGA_BLACK);
    box_title(x,y,"DIAGNOSIS","",VGA_YELLOW,VGA_BLACK);
    box_close(x,y,w);

    int ix=x+1, iw=w-2;
    vga_puts(ix,y+1,"AI SPINE ANALYSIS SAYS:",VGA_LBLUE,VGA_BLACK,iw);

    for (int i=0; i<h-4; i++) {
        int idx=(g_diag_line+i)%NDIAGS;
        uint8_t fg=(i==0)?VGA_LGREEN:VGA_LGRAY;
        /* clear row first */
        vga_fill(ix,y+2+i,iw,1,' ',VGA_LGRAY,VGA_BLACK);
        vga_puts(ix,y+2+i,g_diags[idx],fg,VGA_BLACK,iw);
    }

    /* separator + button */
    for (int c=x+1;c<x+w-1;c++) vga_putc(c,y+h-3,'\xCD',VGA_LBLUE,VGA_BLACK);
    vga_puts(ix,y+h-2,"[ NEW ANALYSIS ]",VGA_YELLOW,VGA_BLACK,iw);
}

/* ── Panel: TOOLS (top-right) ────────────────────────────────────────────── */
static const char *g_tool_names[9] = {
    "[SCAN ]  Full Spine AI Scan",
    "[DIAG ]  Differential Diag",
    "[UCI  ]  Vertebral ML Score",
    "[COBB ]  Cobb Angle Measure",
    "[FRAX ]  Fracture Risk",
    "[DENS ]  Bone Mineral Den.",
    "[EXPORT] PDF Report",
    "[VOICE]  Voice Report",
    "[HMAP ]  Generate Heat Map",  /* index 8 – opens heatmap view */
};

static void render_tools(int y, int x, int h, int w) {
    vga_box(x,y,w,h, VGA_LBLUE, VGA_BLACK);
    box_title(x,y,"TOOLS","",VGA_YELLOW,VGA_BLACK);
    box_close(x,y,w);

    int ix=x+1, iw=w-2;
    vga_puts(ix,y+1,"// Run tools,dont guess",VGA_LBLUE,VGA_BLACK,iw);
    for (int i=0; i<9 && i<h-3; i++) {
        uint8_t fg=VGA_WHITE, bg=VGA_BLACK;
        /* highlight if mouse is hovering */
        if (mouse_y == y+2+i) { fg=VGA_BLACK; bg=VGA_YELLOW; }
        vga_fill(ix,y+2+i,iw,1,' ',fg,bg);
        vga_puts(ix,y+2+i,g_tool_names[i],fg,bg,iw);
    }
}

/* ── Panel: PATIENT RECORDS directory (bottom-left) ─────────────────────── */
static void render_dir(int y, int x, int h, int w) {
    vga_box(x,y,w,h, VGA_LBLUE, VGA_BLACK);
    /* title shows active filter */
    if (g_risk_filter == 0) box_title(x,y,"PATIENT-RECORDS","",VGA_YELLOW,VGA_BLACK);
    else if (g_risk_filter == 1) box_title(x,y,"RECORDS-HIGH","",VGA_LRED,VGA_BLACK);
    else if (g_risk_filter == 2) box_title(x,y,"RECORDS-MOD","",VGA_YELLOW,VGA_BLACK);
    else box_title(x,y,"RECORDS-LOW","",VGA_LGREEN,VGA_BLACK);
    box_close(x,y,w);

    int ix=x+1, iw=w-2;
    /* column headers */
    vga_puts(ix,y+1," ID     DATE       CLASS        RISK CAT ",VGA_LBLUE,VGA_BLACK,iw);
    for (int c=ix;c<ix+iw;c++) vga_putc(c,y+2,'\xCD',VGA_LBLUE,VGA_BLACK);

    int rows = h - 5;  /* usable rows */

    /* Build filtered index list on the fly (no malloc in bare metal) */
    /* First pass: find g_sel in filtered list to keep offset correct */
    int visible_count = 0;
    for (int i=0; i<NRECORDS; i++) {
        const SpineRec *r = &g_records[i];
        int pass = (g_risk_filter == 0) ||
                   (g_risk_filter == 1 && kstrcmp(r->risk_category,"HIGH")==0) ||
                   (g_risk_filter == 2 && kstrcmp(r->risk_category,"MODERATE")==0) ||
                   (g_risk_filter == 3 && kstrcmp(r->risk_category,"LOW")==0);
        if (pass) visible_count++;
    }

    /* Clamp selection */
    if (g_sel >= NRECORDS) g_sel = NRECORDS-1;
    if (g_sel < 0)         g_sel = 0;

    /* Scroll: ensure selected record is visible */
    if (g_sel >= g_dir_offset + rows) g_dir_offset = g_sel - rows + 1;
    if (g_sel < g_dir_offset)         g_dir_offset = g_sel;

    /* Second pass: render rows */
    int row_drawn = 0;
    int filtered_idx = 0;   /* index within filtered results */
    for (int i=0; i<NRECORDS && row_drawn<rows; i++) {
        const SpineRec *r = &g_records[i];
        int pass = (g_risk_filter == 0) ||
                   (g_risk_filter == 1 && kstrcmp(r->risk_category,"HIGH")==0) ||
                   (g_risk_filter == 2 && kstrcmp(r->risk_category,"MODERATE")==0) ||
                   (g_risk_filter == 3 && kstrcmp(r->risk_category,"LOW")==0);
        if (!pass) continue;

        int display_pos = filtered_idx - g_dir_offset;
        filtered_idx++;
        if (display_pos < 0) continue;
        if (display_pos >= rows) break;

        int sel = (i == g_sel);
        uint8_t fg, bg;
        /* color by risk category */
        if (sel) {
            fg = VGA_BLACK; bg = VGA_YELLOW;
        } else if (kstrcmp(r->risk_category,"HIGH")==0) {
            fg = VGA_LRED;   bg = VGA_BLACK;
        } else if (kstrcmp(r->risk_category,"MODERATE")==0) {
            fg = VGA_YELLOW; bg = VGA_BLACK;
        } else {
            fg = VGA_LGREEN; bg = VGA_BLACK;
        }

        vga_fill(ix, y+3+display_pos, iw, 1, ' ', fg, bg);

        /* ID (anonymised patient code only) */
        vga_puts(ix,   y+3+display_pos, r->patient_id,      fg,bg, 7);
        /* Date (scan date) */
        vga_puts(ix+8, y+3+display_pos, r->date,            fg,bg,10);
        /* Classification */
        vga_puts(ix+19,y+3+display_pos, r->classification,  fg,bg,11);
        /* Risk score */
        char rb[8]; kitoa(r->risk_score_int/10, rb);
        vga_puts(ix+31,y+3+display_pos, rb,                 fg,bg, 4);
        /* Severity badge H/M/L */
        char badge = (kstrcmp(r->risk_category,"HIGH")==0)     ? 'H' :
                     (kstrcmp(r->risk_category,"MODERATE")==0) ? 'M' : 'L';
        uint8_t bfg = (badge=='H') ? VGA_LRED :
                      (badge=='M') ? VGA_YELLOW : VGA_LGREEN;
        if (sel) bfg = VGA_BLACK;
        vga_putc(ix+36, y+3+display_pos, badge, bfg, bg);

        row_drawn++;
    }
    /* empty rows */
    for (int i=row_drawn; i<rows; i++)
        vga_fill(ix, y+3+i, iw, 1, ' ', VGA_BLACK, VGA_BLACK);

    /* scroll indicator */
    char sc[48];
    sc[0]='['; kitoa(g_sel+1,sc+1);
    int l=kstrlen(sc); sc[l]='/'; kitoa(NRECORDS,sc+l+1); l=kstrlen(sc); sc[l]=']'; sc[l+1]=' '; sc[l+2]='\0';
    if (g_risk_filter==1) kstrcpy(sc+kstrlen(sc),"HIGH ");
    else if (g_risk_filter==2) kstrcpy(sc+kstrlen(sc),"MOD ");
    else if (g_risk_filter==3) kstrcpy(sc+kstrlen(sc),"LOW ");
    kstrcpy(sc+kstrlen(sc),"arrows/click");
    vga_puts(ix, y+h-2, sc, VGA_LBLUE, VGA_BLACK, iw);
    (void)visible_count;
}

/* ── Panel: ANALYSIS detail (bottom-right) ───────────────────────────────── */
static void render_analysis(int y, int x, int h, int w) {
    vga_box(x,y,w,h, VGA_WHITE, VGA_BLACK);
    box_title(x,y,"ANALYSIS","",VGA_YELLOW,VGA_BLACK);
    box_close(x,y,w);

    const SpineRec *r = &g_records[g_sel];
    int ix=x+1, iw=w-2, row=y+1;

    /* Patient ID (anonymised code – no real name shown) */
    vga_puts(ix,row,r->patient_id,VGA_YELLOW,VGA_BLACK,7);
    vga_puts(ix+8,row,"  Spine Record",VGA_LGRAY,VGA_BLACK,iw-8);
    row++;

    /* Classification + risk category badge */
    uint8_t sfg = (kstrcmp(r->classification,"Normal")==0) ? VGA_LGREEN : VGA_LRED;
    uint8_t cfg = (kstrcmp(r->risk_category,"HIGH")==0)     ? VGA_LRED   :
                  (kstrcmp(r->risk_category,"MODERATE")==0) ? VGA_YELLOW  : VGA_LGREEN;
    vga_puts(ix,row,"Status:",VGA_WHITE,VGA_BLACK,8);
    vga_puts(ix+8,row,r->classification,sfg,VGA_BLACK,12);
    vga_puts(ix+21,row,"[",VGA_WHITE,VGA_BLACK,1);
    vga_puts(ix+22,row,r->risk_category,cfg,VGA_BLACK,8);
    vga_puts(ix+30,row,"]",VGA_WHITE,VGA_BLACK,1);
    row++;

    vga_puts(ix,row,"Date  :",VGA_WHITE,VGA_BLACK,8); vga_puts(ix+8,row,r->date,VGA_LGRAY,VGA_BLACK,iw-8); row++;
    vga_puts(ix,row,"Age   :",VGA_WHITE,VGA_BLACK,8); { char ab[6]; kitoa(r->age,ab); vga_puts(ix+8,row,ab,VGA_LGRAY,VGA_BLACK,6); } row++;
    vga_puts(ix,row,"Sex   :",VGA_WHITE,VGA_BLACK,8); vga_puts(ix+8,row,r->sex,VGA_LGRAY,VGA_BLACK,4); row++;

    /* Risk bar */
    vga_puts(ix,row,"Risk  :",VGA_WHITE,VGA_BLACK,8);
    { char rb[8]; kitoa(r->risk_score_int/10,rb); vga_puts(ix+8,row,rb,cfg,VGA_BLACK,5); }
    vga_putc(ix+13,row,'%',cfg,VGA_BLACK);
    row++;

    /* Visual risk bar */
    int bar_max = iw - 1;
    int filled  = (r->risk_score_int * bar_max) / 1000;
    if (filled>bar_max) filled=bar_max;
    vga_putc(ix, row, '[', VGA_WHITE, VGA_BLACK);
    /* color: green=low, yellow=moderate, red=high */
    uint8_t bclr = (filled > (bar_max*2/3)) ? VGA_LRED :
                   (filled > (bar_max/3))   ? VGA_YELLOW : VGA_LGREEN;
    for (int b=0;b<filled;b++) vga_putc(ix+1+b, row, '\xDB', bclr, VGA_BLACK);
    for (int b=filled;b<bar_max-1;b++) vga_putc(ix+1+b, row, '-', VGA_DGRAY, VGA_BLACK);
    vga_putc(ix+bar_max, row, ']', VGA_WHITE, VGA_BLACK);
    row++;

    /* Abnormal prob */
    vga_puts(ix,row,"AbnPrb:",VGA_WHITE,VGA_BLACK,8);
    { char ab[8]; kitoa(r->abnorm_pct,ab); vga_puts(ix+8,row,ab,sfg,VGA_BLACK,5); }
    vga_putc(ix+13,row,'%',sfg,VGA_BLACK);
    row++;

    /* Clinical scores separator */
    for (int c=ix;c<ix+iw;c++) vga_putc(c,row,'\xCD',VGA_LBLUE,VGA_BLACK);
    row++;

    /* Cobb angle + bone density */
    if (row < y+h-2) {
        vga_puts(ix,row,"Cobb:",VGA_WHITE,VGA_BLACK,5);
        { char b[8]; kitoa(r->cobb_x10/10,b); vga_puts(ix+5,row,b,VGA_YELLOW,VGA_BLACK,4); }
        vga_puts(ix+9,row,"deg",VGA_LGRAY,VGA_BLACK,3);
        vga_puts(ix+14,row,"Bone:",VGA_WHITE,VGA_BLACK,5);
        {
            char b[8];
            int bd = r->bone_x10;
            /* format signed value */
            if (bd < 0) { b[0]='-'; kitoa((-bd)/10,b+1); }
            else kitoa(bd/10,b);
            vga_puts(ix+19,row,b,
                (bd < -25) ? VGA_LRED : (bd < -10) ? VGA_YELLOW : VGA_LGREEN,
                VGA_BLACK,5);
        }
        row++;
    }

    /* Fracture grade + disc grade */
    if (row < y+h-2) {
        vga_puts(ix,row,"FracGr:",VGA_WHITE,VGA_BLACK,7);
        {
            char b[4]; kitoa(r->frac_grade,b);
            uint8_t fg2 = (r->frac_grade>=2) ? VGA_LRED :
                          (r->frac_grade==1) ? VGA_YELLOW : VGA_LGREEN;
            vga_puts(ix+7,row,b,fg2,VGA_BLACK,2);
        }
        vga_puts(ix+11,row,"DiscGr:",VGA_WHITE,VGA_BLACK,7);
        {
            char b[4]; kitoa(r->disc_grade,b);
            uint8_t fg2 = (r->disc_grade>=3) ? VGA_LRED :
                          (r->disc_grade>=2) ? VGA_YELLOW : VGA_LGREEN;
            vga_puts(ix+18,row,b,fg2,VGA_BLACK,2);
        }
        row++;
    }

    /* DEXA / CT / MRI advanced imaging support */
    if (row < y+h-2) {
        vga_puts(ix,row,"TBS:",VGA_WHITE,VGA_BLACK,4);
        { char b[8]; kitoa(r->tbs_x100/100,b); vga_puts(ix+4,row,b,VGA_LGRAY,VGA_BLACK,2); }
        vga_putc(ix+5,row,'.',VGA_LGRAY,VGA_BLACK);
        { char b[4]; kitoa((r->tbs_x100/10)%10,b); vga_puts(ix+6,row,b,VGA_LGRAY,VGA_BLACK,1); }
        vga_puts(ix+9,row,"CT:",VGA_WHITE,VGA_BLACK,3);
        { char b[4]; kitoa(r->cortical_idx,b); vga_puts(ix+12,row,b,(r->cortical_idx>=65)?VGA_LRED:(r->cortical_idx>=40)?VGA_YELLOW:VGA_LGREEN,VGA_BLACK,3); }
        vga_puts(ix+16,row,"MRI:",VGA_WHITE,VGA_BLACK,4);
        { char b[4]; kitoa(r->marrow_dark_pct,b); vga_puts(ix+20,row,b,(r->marrow_dark_pct>=45)?VGA_LRED:VGA_LGREEN,VGA_BLACK,3); }
        vga_putc(ix+23,row,'%',VGA_LGRAY,VGA_BLACK);
        row++;
    }

    /* UCI nearest match */
    if (row < y+h-1) {
        for (int c=ix;c<ix+iw;c++) vga_putc(c,row,'\xC4',VGA_DGRAY,VGA_BLACK);
        row++;
    }
    if (row < y+h-1) {
        vga_puts(ix,row,"Nearest UCI:",VGA_LBLUE,VGA_BLACK,12);
        const UCIRecord *u = &g_uci[g_sel % NUCI];
        uint8_t clf = (kstrcmp(u->label,"Normal")==0) ? VGA_LGREEN : VGA_LRED;
        vga_puts(ix+13,row,u->label,clf,VGA_BLACK,iw-13);
        row++;
    }
    if (row < y+h-1) {
        const UCIRecord *u = &g_uci[g_sel % NUCI];
        vga_puts(ix,row,"PI:",VGA_LGRAY,VGA_BLACK,3);
        { char b[6]; kitoa(u->pi10/10,b); vga_puts(ix+3,row,b,VGA_YELLOW,VGA_BLACK,4); }
        vga_puts(ix+8,row,"PT:",VGA_LGRAY,VGA_BLACK,3);
        { char b[6]; kitoa(u->pt10/10,b); vga_puts(ix+11,row,b,VGA_YELLOW,VGA_BLACK,4); }
        vga_puts(ix+16,row,"SS:",VGA_LGRAY,VGA_BLACK,3);
        { char b[6]; kitoa(u->ss10/10,b); vga_puts(ix+19,row,b,VGA_YELLOW,VGA_BLACK,4); }
        row++;
    }
    (void)row;
}

/* ============================================================
   HEATMAP view  – fullscreen grid of all patient risk tiles
   Called when g_view == 1.

   FORWARD declarations (render_cmdbar and render_mouse are defined
   later in the file but called here first).
   ============================================================ */
static void render_cmdbar(void);
static void render_mouse(void);

/* Risk colour for a record */
static uint8_t risk_color(const SpineRec *r) {
    if (kstrcmp(r->risk_category,"HIGH")==0)     return VGA_LRED;
    if (kstrcmp(r->risk_category,"MODERATE")==0) return VGA_YELLOW;
    return VGA_LGREEN;
}

static void render_heatmap(void) {
    /* ── Title bar ─────────────────────────────────────────── */
    vga_fill(0, 0, VGA_WIDTH, 1, ' ', VGA_WHITE, VGA_BLUE);
    vga_puts(1, 0, "HEATMAP/", VGA_YELLOW, VGA_BLUE, 8);
    vga_puts(10,0, "Risk Grid:", VGA_WHITE, VGA_BLUE, 10);
    { char b[8]; kitoa(NRECORDS, b);
      vga_puts(20,0, b, VGA_YELLOW, VGA_BLUE, 4); }
    vga_puts(25,0, "records from PostgreSQL", VGA_WHITE, VGA_BLUE, 24);
    vga_puts(50,0, "[H]=HIGH [M]=MOD [L]=LOW", VGA_WHITE, VGA_BLUE, 24);
    vga_puts(75,0, "[Esc]=back", VGA_WHITE, VGA_BLUE, 10);

    /* ── Grid ──────────────────────────────────────────────── */
    /* Tile = 10 cols wide × 3 rows tall:
     *   row0: ID    (6) + badge(1) + spc(3)
     *   row1: bar   (10) heat bar 0-9 blocks
     *   row2: class (10) classification label
     * 8 tiles across (8*10=80), up to 7 tiles high (7*3=21 rows, rows 1-22)
     * = 56 tiles per page */
    #define HM_COLS  8
    #define HM_ROWS  7
    #define HM_TW    10   /* tile width  */
    #define HM_TH    3    /* tile height */
    #define HM_PER_PAGE (HM_COLS * HM_ROWS)  /* 56 */

    /* Clamp scroll offset to page boundary */
    if (g_hmap_offset < 0) g_hmap_offset = 0;
    if (g_hmap_offset > NRECORDS - 1) g_hmap_offset = NRECORDS - 1;
    /* Align to page start */
    int page_start = (g_hmap_offset / HM_PER_PAGE) * HM_PER_PAGE;

    /* Clear grid area */
    for (int r = 1; r <= HM_ROWS * HM_TH; r++)
        vga_fill(0, r, VGA_WIDTH, 1, ' ', VGA_LGRAY, VGA_BLACK);

    for (int ti = 0; ti < HM_PER_PAGE; ti++) {
        int idx = page_start + ti;
        if (idx >= NRECORDS) break;
        const SpineRec *r = &g_records[idx];

        int tile_col = ti % HM_COLS;
        int tile_row = ti / HM_COLS;
        int cx = tile_col * HM_TW;   /* pixel column */
        int cy = 1 + tile_row * HM_TH; /* pixel row   */

        uint8_t rc = risk_color(r);
        uint8_t bg = VGA_BLACK;
        /* highlight selected record */
        if (idx == g_sel) bg = VGA_DGRAY;

        /* ── row 0: ID + badge ────────────────────────────── */
        /* background strip */
        vga_fill(cx, cy, HM_TW, 1, ' ', rc, bg);
        /* patient ID (up to 6 chars) */
        vga_puts(cx, cy, r->patient_id, VGA_BLACK, rc, 6);
        /* H/M/L badge */
        char badge = (kstrcmp(r->risk_category,"HIGH")==0)     ? 'H' :
                     (kstrcmp(r->risk_category,"MODERATE")==0) ? 'M' : 'L';
        vga_putc(cx+7, cy, badge, VGA_BLACK, rc);
        /* age */
        { char ab[4]; kitoa(r->age, ab);
          vga_puts(cx+8, cy, ab, VGA_BLACK, rc, 2); }

        /* ── row 1: heat bar (0-9 filled blocks) ─────────── */
        {
            /* risk_score_int is risk*10 so max=1000; map to 0-9 */
            int blocks = (r->risk_score_int * 9) / 1000;
            /* invert: low risk_score_int = high abnormality visually */
            int heat   = 9 - blocks;   /* 0=cool(normal) 9=hot(abnormal) */
            /* heat color: gradient green→yellow→red */
            uint8_t hclr = (heat >= 7) ? VGA_LRED :
                           (heat >= 4) ? VGA_YELLOW : VGA_LGREEN;
            vga_fill(cx, cy+1, HM_TW, 1, ' ', VGA_DGRAY, VGA_BLACK);
            for (int b = 0; b < heat && b < HM_TW; b++)
                vga_putc(cx+b, cy+1, '\xDB', hclr, VGA_BLACK);
            /* fill rest with dim dashes */
            for (int b = heat; b < HM_TW; b++)
                vga_putc(cx+b, cy+1, '\xB0', VGA_DGRAY, VGA_BLACK);
        }

        /* ── row 2: classification label ─────────────────── */
        vga_fill(cx, cy+2, HM_TW, 1, ' ', VGA_LGRAY, VGA_BLACK);
        /* abbreviate: Normal→Norm  DiskHernia→Disk  Spondylolisthesis→Spnd */
        const char *lbl;
        if      (kstrcmp(r->classification,"Normal")==0)           lbl = "Norm";
        else if (kstrncmp(r->classification,"Disk",4)==0 ||
                 kstrncmp(r->classification,"Disc",4)==0)           lbl = "Disk";
        else if (kstrncmp(r->classification,"Spondyl",7)==0)        lbl = "Spnd";
        else if (kstrcmp(r->classification,"Abnormal")==0)          lbl = "Abn!";
        else                                                          lbl = "Unk?";
        vga_puts(cx, cy+2, lbl, rc, VGA_BLACK, 5);
        /* abnormal% small */
        { char ab[4]; kitoa(r->abnorm_pct, ab);
          vga_puts(cx+5, cy+2, ab, VGA_LGRAY, VGA_BLACK, 3);
          vga_putc(cx+8, cy+2, '%', VGA_DGRAY, VGA_BLACK); }
    }

    /* ── Page indicator + legend ──────────────────────────── */
    int total_pages = (NRECORDS + HM_PER_PAGE - 1) / HM_PER_PAGE;
    int cur_page    = page_start / HM_PER_PAGE + 1;
    vga_fill(0, 23, VGA_WIDTH, 1, ' ', VGA_LGRAY, VGA_BLUE);
    vga_puts(1, 23, "Page:", VGA_WHITE, VGA_BLUE, 5);
    { char b[4]; kitoa(cur_page, b);  vga_puts(6, 23, b, VGA_WHITE, VGA_BLUE, 3); }
    vga_putc(9, 23, '/', VGA_WHITE, VGA_BLUE);
    { char b[4]; kitoa(total_pages,b); vga_puts(10,23, b, VGA_WHITE, VGA_BLUE, 3); }
    vga_puts(14,23, "  DB:", VGA_LGRAY, VGA_BLUE, 5);
    { char b[6]; kitoa(DB_TOTAL_PATIENTS,b); vga_puts(19,23,b,VGA_YELLOW,VGA_BLUE,5); }
    vga_puts(24,23, "total  ", VGA_LGRAY, VGA_BLUE, 7);
    vga_puts(31,23, "H=HIGH(red) M=MOD(yel) L=LOW(grn)", VGA_LGRAY, VGA_BLUE, 34);
    vga_puts(65,23, "Up/Dn=scroll", VGA_WHITE, VGA_BLUE, 12);

    /* ── Command bar ──────────────────────────────────────── */
    render_cmdbar();

    render_mouse();
    vga_flush();
    #undef HM_COLS
    #undef HM_ROWS
    #undef HM_TW
    #undef HM_TH
    #undef HM_PER_PAGE
}
static void render_titlebar(void) {
    /* Full blue bar */
    vga_fill(0,0,VGA_WIDTH,1,' ',VGA_WHITE,VGA_BLUE);

    /* "LOAD day mm/dd hh:mm:ss" */
    uint32_t secs = g_tick / PIT_HZ;
    uint32_t mins = secs / 60;
    uint32_t hrs  = mins / 60;
    secs %= 60; mins %= 60; hrs %= 24;

    char buf[40];
    /* LOAD Sat 02/28 HH:MM:SS */
    buf[0]='L';buf[1]='O';buf[2]='A';buf[3]='D';buf[4]=' ';
    kitoa((int)hrs,  buf+5);  int l=kstrlen(buf); if(l<7){buf[5]='0';buf[6]='0'+hrs;buf[7]='\0';}
    l=kstrlen(buf);
    buf[l]=':'; kitoa((int)mins, buf+l+1); l=kstrlen(buf);
    if(buf[l-1]==':'||kstrlen(buf+l-2)<2){buf[l]='0';kitoa((int)mins,buf+l+1);l=kstrlen(buf);}
    buf[l]=':';
    if(secs<10){buf[l+1]='0';buf[l+2]='0'+(char)secs;buf[l+3]='\0';}
    else kitoa((int)secs, buf+l+1);

    vga_puts(1, 0, "LOAD 02/28 ", VGA_WHITE, VGA_BLUE, 11);
    { char hb[4];
      if(hrs<10){hb[0]='0';hb[1]='0'+(char)(hrs);hb[2]='\0';} else kitoa((int)hrs,hb);
      vga_puts(12,0,hb,VGA_WHITE,VGA_BLUE,2);
    }
    vga_putc(14,0,':',VGA_WHITE,VGA_BLUE);
    { char mb[4];
      if(mins<10){mb[0]='0';mb[1]='0'+(char)(mins);mb[2]='\0';} else kitoa((int)mins,mb);
      vga_puts(15,0,mb,VGA_WHITE,VGA_BLUE,2);
    }
    vga_putc(17,0,':',VGA_WHITE,VGA_BLUE);
    { char sb[4];
      if(secs<10){sb[0]='0';sb[1]='0'+(char)(secs);sb[2]='\0';} else kitoa((int)secs,sb);
      vga_puts(18,0,sb,VGA_WHITE,VGA_BLUE,2);
    }

    vga_puts(22,0,"  FPS:",VGA_WHITE,VGA_BLUE,6);
    { char fb[6]; kitoa(g_fps,fb); vga_puts(28,0,fb,VGA_YELLOW,VGA_BLUE,4); }

    /* center: OS name */
    vga_puts(32,0,"  KARNIX SPINE OS  ",VGA_YELLOW,VGA_BLUE,20);

    /* right: help */
    vga_puts(72,0,"CTRL?",VGA_WHITE,VGA_BLUE,5);
}

/* ── Command bar (bottom row) ───────────────────────────────────────────── */
static void render_cmdbar(void) {
    vga_fill(0,VGA_HEIGHT-1,VGA_WIDTH,1,' ',VGA_BLACK,VGA_LGRAY);
    vga_puts(0, VGA_HEIGHT-1, "KarnixOS> ", VGA_BLACK, VGA_LGRAY, 10);
    vga_puts(10, VGA_HEIGHT-1, g_cmd, VGA_BLACK, VGA_LGRAY, g_cmd_len);
    /* blinking cursor block */
    int blink = (g_tick / (PIT_HZ/2)) & 1;
    if (blink) vga_putc(10+g_cmd_len, VGA_HEIGHT-1, '\xDB', VGA_BLUE, VGA_LGRAY);
}

/* ── Log/status bar (row 23) ─────────────────────────────────────────────── */
static void render_log(void) {
    vga_fill(0,VGA_HEIGHT-2,VGA_WIDTH,1,' ',VGA_DGRAY,VGA_BLACK);
    if (g_log_count == 0) {
        vga_puts(1,VGA_HEIGHT-2,"[LOG] Type 'help' for commands",VGA_DGRAY,VGA_BLACK,40);
        return;
    }
    int idx = (g_log_head - 1 + LOG_MAX) % LOG_MAX;
    vga_puts(1, VGA_HEIGHT-2, "[LOG] ", VGA_LGRAY, VGA_BLACK, 6);
    vga_puts(7, VGA_HEIGHT-2, g_log[idx], VGA_LGREEN, VGA_BLACK, VGA_WIDTH-8);
}

/* ── Mouse cursor: drawn into shadow buffer last, before flush ──────────── */
static int g_prev_mx = -1, g_prev_my = -1;
static uint16_t g_cursor_saved = 0;  /* shadow cell under cursor */

static void render_mouse(void) {
    /* Restore the cell the cursor was covering last frame */
    if (g_prev_mx >= 0 && g_prev_my >= 0)
        SHADOW(g_prev_my, g_prev_mx) = g_cursor_saved;

    int mx = mouse_x, my = mouse_y;
    /* Save what the fully-rendered shadow has at this position */
    g_cursor_saved = SHADOW(my, mx);
    g_prev_mx = mx;
    g_prev_my = my;

    /* Draw cursor: swap fg/bg of the cell under pointer + solid block */
    uint8_t attr = (uint8_t)(g_cursor_saved >> 8);
    uint8_t fg   = attr & 0x0F;
    uint8_t bg   = (attr >> 4) & 0x0F;
    SHADOW(my, mx) = VGA_ENTRY('\xDB', bg, fg);
}

/* ============================================================
   Holy Paint – text-mode pixel canvas editor (TempleOS-style)
   True VESA graphics needs real-mode BIOS calls unavailable from this
   32-bit protected-mode kernel, so each "pixel" is a VGA text-mode
   block ('\xDB') coloured with one of the 16 EGA colours – the same
   low-res trick TempleOS's own paint tools rely on.
   ============================================================ */
#define CANVAS_W VGA_WIDTH
#define CANVAS_H (VGA_HEIGHT - 3)   /* rows 1..21, leaves palette+cmd bar */

static uint8_t g_canvas[CANVAS_H][CANVAS_W];
static int     g_paint_cx = CANVAS_W/2, g_paint_cy = CANVAS_H/2;
static uint8_t g_paint_color = VGA_YELLOW;   /* gold by default */

static void paint_clear(void) {
    for (int y=0; y<CANVAS_H; y++)
        for (int x=0; x<CANVAS_W; x++)
            g_canvas[y][x] = VGA_BLACK;
}

static void render_paint(void) {
    vga_fill(0,0,VGA_WIDTH,1,' ',VGA_WHITE,VGA_BLUE);
    vga_puts(1,0,"HOLY PAINT/", VGA_YELLOW, VGA_BLUE, 11);
    vga_puts(13,0,"0-9/A-F=colour  Space/Click=paint  Del=clear  Esc=back",
              VGA_WHITE, VGA_BLUE, 60);

    for (int y=0; y<CANVAS_H; y++)
        for (int x=0; x<CANVAS_W; x++)
            vga_putc(x, 1+y, '\xDB', g_canvas[y][x], VGA_BLACK);

    /* cursor: invert whatever colour sits underneath */
    vga_putc(g_paint_cx, 1+g_paint_cy, '\xDB',
             (g_paint_color==VGA_WHITE) ? VGA_BLACK : VGA_WHITE, g_paint_color);

    /* palette bar */
    vga_fill(0, VGA_HEIGHT-2, VGA_WIDTH, 1, ' ', VGA_LGRAY, VGA_BLACK);
    for (int c=0; c<16; c++) {
        vga_putc(1+c*2, VGA_HEIGHT-2, '\xDB', (uint8_t)c, VGA_BLACK);
        if ((uint8_t)c == g_paint_color) vga_putc(1+c*2, VGA_HEIGHT-2, '^', VGA_WHITE, VGA_BLACK);
    }
    vga_puts(36, VGA_HEIGHT-2, "gold(E)=Yellow blue(1)=Blue white(F)=White",
              VGA_LGRAY, VGA_BLACK, 43);

    render_cmdbar();
    render_mouse();
    vga_flush();
}

/* ============================================================
   Main render
   ============================================================ */
static void render_all(void) {
    if (g_view == 2) {
        render_paint();
        return;
    }
    if (g_view == 1) {
        render_heatmap();
        return;
    }
    /* ── layout ──
       Row 0    : title bar
       Rows 1-11: top panels  (height=11)
       Rows 12-22: bottom panels (height=11)
       Row 23   : log bar
       Row 24   : command bar
    */
    int top_y=1, bot_y=12;
    int ph=11;   /* panel height */

    /* top panels – three columns: 22 | 33 | 25 */
    render_announce (top_y, 0,   ph, 22);
    render_diag     (top_y, 22,  ph, 33);
    render_tools    (top_y, 55,  ph, 25);

    /* bottom panels – two columns: 47 | 33 */
    render_dir      (bot_y, 0,   ph, 47);
    render_analysis (bot_y, 47,  ph, 33);

    render_titlebar();
    render_log();
    render_cmdbar();

    /* Mouse cursor is drawn LAST into shadow so it sits on top,
     * then the single flush pushes everything to VGA at once. */
    render_mouse();
    vga_flush();
}

/* ============================================================
   kernel_main
   ============================================================ */
void kernel_main(uint32_t mb_magic, void *mb_info) {
    (void)mb_magic; (void)mb_info;

    /* ── Boot sequence ─────────────────────────────────────── */
    /* Clear both shadow and committed buffers so the first flush
     * writes every cell cleanly and committed starts in sync. */
    {
        int i;
        uint16_t blank = VGA_ENTRY(' ', VGA_WHITE, VGA_BLACK);
        for (i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            g_shadow[i]    = blank;
            g_committed[i] = blank;
            VGA_MEM[i]     = blank;
        }
    }
    vga_cursor_off();

    gdt_init();
    idt_init();
    pic_init();
    pit_init();

    mouse_init();

    __asm__ volatile ("sti");  /* enable interrupts */

    boot_chime();

    log_push("KarnixOS v1.0  –  Karnix Spine Edge OS");
    log_push("Loaded 20 spine records + 10 UCI samples");
    log_push("PS/2 mouse ready  |  Keyboard ready");
    log_push("Type 'help' for commands");

    g_fps_tick  = g_tick;
    g_diag_tick = g_tick;

    /* ── Main loop ─────────────────────────────────────────── */
    while (1) {
        /* Sleep until the next IRQ fires (timer/keyboard/mouse).
         * sti+hlt is atomic: an IRQ that arrives between sti and hlt
         * is guaranteed to wake the CPU before it actually halts. */
        __asm__ volatile ("sti\n\thlt\n" ::: "memory");

        /* ── FPS / tick bookkeeping ─── */
        {
            uint32_t t = g_tick;  /* snapshot (volatile, read once) */
            if (t != g_last_tick) {
                g_last_tick = t;
                g_frame_count++;
                if (t - g_fps_tick >= (uint32_t)PIT_HZ) {
                    g_fps       = g_frame_count;
                    g_frame_count = 0;
                    g_fps_tick  = t;
                }
                /* scroll diagnosis every 4 s */
                if (t - g_diag_tick >= (uint32_t)(PIT_HZ*4)) {
                    g_diag_line = (g_diag_line+1) % NDIAGS;
                    g_diag_tick = t;
                }
            }
        }

        /* ── Keyboard ─── */
        if (kbd_pending == 1) {
            kbd_pending = 0;
            char c = kbd_char;
            key_click();
            if (g_view == 2) {
                /* Holy Paint: hex digit selects colour, space paints */
                if      (c>='0'&&c<='9') g_paint_color=(uint8_t)(c-'0');
                else if (c>='a'&&c<='f') g_paint_color=(uint8_t)(10+(c-'a'));
                else if (c>='A'&&c<='F') g_paint_color=(uint8_t)(10+(c-'A'));
                else if (c==' ') g_canvas[g_paint_cy][g_paint_cx]=g_paint_color;
            } else if (c == '\n' || c == '\r') {
                exec_cmd();
            } else if (c == '\b') {
                if (g_cmd_len > 0) { g_cmd_len--; g_cmd[g_cmd_len]='\0'; }
            } else if (c && g_cmd_len < CMD_MAX) {
                g_cmd[g_cmd_len++] = c;
                g_cmd[g_cmd_len]   = '\0';
            }
        } else if (kbd_pending == 2) {
            kbd_pending = 0;
            uint8_t sp = kbd_special;
            key_click();
            if (g_view == 2) {
                /* Holy Paint: arrows move cursor, Del clears, Esc exits */
                if      (sp == 0x4B) { if (g_paint_cx > 0)            g_paint_cx--; }
                else if (sp == 0x4D) { if (g_paint_cx < CANVAS_W-1)   g_paint_cx++; }
                else if (sp == 0x48) { if (g_paint_cy > 0)            g_paint_cy--; }
                else if (sp == 0x50) { if (g_paint_cy < CANVAS_H-1)   g_paint_cy++; }
                else if (sp == KEY_DEL) { paint_clear(); log_push("Canvas cleared."); }
                else if (sp == KEY_ESC) { g_view=0; log_push("Holy Paint closed."); }
            } else if (g_view == 1) {
                /* In heatmap view: Up/Dn scroll pages, Esc=back */
                #define HM_PER_PAGE 56
                if      (sp == 0x48) { g_hmap_offset -= HM_PER_PAGE; if (g_hmap_offset<0) g_hmap_offset=0; }
                else if (sp == 0x50) { g_hmap_offset += HM_PER_PAGE; if (g_hmap_offset>=NRECORDS) g_hmap_offset=NRECORDS-1; }
                else if (sp == KEY_ESC) { g_view=0; log_push("Heatmap closed."); }
                #undef HM_PER_PAGE
            } else {
                if      (sp == 0x48) { if (g_sel > 0)            g_sel--; }
                else if (sp == 0x50) { if (g_sel < NRECORDS-1)   g_sel++; }
                else if (sp == 0x4B) { g_diag_line = (g_diag_line+NDIAGS-1)%NDIAGS; }
                else if (sp == 0x4D) { g_diag_line = (g_diag_line+1)%NDIAGS; }
                else if (sp == KEY_ESC) { log_push("Esc – type 'quit' to halt."); }
            }
        }

        /* ── Mouse click → select record OR open heatmap OR paint ─── */
        if (mouse_click) {
            mouse_click = 0;
            if (g_view == 2) {
                if (mouse_y >= 1 && mouse_y <= CANVAS_H && mouse_x < CANVAS_W) {
                    g_paint_cx = mouse_x;
                    g_paint_cy = mouse_y - 1;
                    g_canvas[g_paint_cy][g_paint_cx] = g_paint_color;
                }
            } else if (g_view == 0) {
                /* Tools panel: col 55-79, rows 2-10 (y+2+i where top_y=1) */
                if (mouse_x >= 56 && mouse_x < 79 &&
                    mouse_y >= 3  && mouse_y <= 10) {
                    int tool_idx = mouse_y - 3;
                    if (tool_idx == 8) {   /* [HMAP] row */
                        g_view = 1; g_hmap_offset = 0;
                        log_push("Heatmap/ opened – Esc=back");
                    }
                }
                /* Patient list panel */
                if (mouse_x >= 0 && mouse_x < 47 &&
                    mouse_y >= 15 && mouse_y < 22) {
                    int clicked = g_dir_offset + (mouse_y - 15);
                    if (clicked >= 0 && clicked < NRECORDS)
                        g_sel = clicked;
                }
            } else {
                /* In heatmap: click a tile to select record + go back */
                #define HM_COLS 8
                #define HM_TW   10
                #define HM_TH   3
                #define HM_PER_PAGE 56
                int page_start = (g_hmap_offset / HM_PER_PAGE) * HM_PER_PAGE;
                if (mouse_y >= 1 && mouse_y < 1 + 7*HM_TH) {
                    int tc = mouse_x / HM_TW;
                    int tr = (mouse_y - 1) / HM_TH;
                    int ti = tr * HM_COLS + tc;
                    int idx = page_start + ti;
                    if (idx >= 0 && idx < NRECORDS) {
                        g_sel  = idx;
                        g_view = 0;
                        log_push("Record selected from heatmap.");
                    }
                }
                #undef HM_COLS
                #undef HM_TW
                #undef HM_TH
                #undef HM_PER_PAGE
            }
        }

        /* ── Render at most 30 FPS (every 3 PIT ticks @ 100 Hz) ─────────
         * The static g_render_tick tracks when we last rendered.
         * This prevents writing 2000 shadow cells 100× per second;
         * 30× per second is imperceptibly smooth and eliminates flicker. */
        {
            static uint32_t g_render_tick = 0;
            if (g_tick - g_render_tick >= 3) {
                g_render_tick = g_tick;
                render_all();
            }
        }
    }
}
