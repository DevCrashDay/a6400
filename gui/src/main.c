#include "rt.h"
#include "fb.h"

#define PID_FILE "/tmp/gui.pid"
#define CMD_FILE "/tmp/gui.cmd"
#define LOG_FILE "/tmp/gui.log"
#define RITUAL_INI "/mnt/RITUAL.ini"
#define RITUAL_INI_TMP "/tmp/RITUAL.ini"
#define RITUAL_STATUS_FILE "/tmp/ritual_status.txt"

/* Sony a6400 physical keycodes from Backup.bin & libObj.so */
#define KEY_TACT_UP      0x0003u
#define KEY_TACT_LEFT    0x0015u
#define KEY_TACT_DOWN    0x0030u
#define KEY_TACT_CUSTOM  0x0066u /* C1 button */
#define KEY_TACT_CAP     0x00a7u /* S2 full shutter */
#define KEY_TACT_MENU    0x01abu
#define KEY_TACT_ENTER   0x01acu /* Center wheel button */
#define KEY_TACT_RIGHT   0x01afu
#define KEY_TACT_REC     0x01f0u
#define KEY_TACT_FOCUS   0x01f4u /* S1 half shutter */
#define KEY_TACT_PLAY    0x01f7u
#define KEY_TACT_DELETE  0x01f8u /* C2 / Trash button */
#define KEY_TACT_DISP    0x01f9u
#define KEY_TACT_FN      0x7095u
#define KEY_TACT_DOWN_BI 0x70a9u
#define KEY_TACT_LEFT_BI 0x70aau
#define KEY_TACT_RIGHT_BI 0x70abu
#define KEY_TACT_UP_BI   0x70adu
#define KEY_TACT_ENT_BI  0x70c4u
#define KEY_KURU_LEFT    0x10000000u
#define KEY_KURU_RIGHT   0x10000001u
#define KEY_WHEEL_LEFT   0x10000003u
#define KEY_WHEEL_RIGHT  0x10000004u

enum nav_event {
    NAV_NONE = 0,
    NAV_UP,
    NAV_DOWN,
    NAV_LEFT,
    NAV_RIGHT,
    NAV_ENTER,
    NAV_BACK,
    NAV_TOGGLE_OSD,
    NAV_QUIT
};

static volatile int g_stop = 0;

static void on_sig(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* ---- String parsing helpers ---- */

static const char *find_substr(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (!nl)
        return hay;
    for (; *hay; hay++) {
        if (strncmp(hay, needle, nl) == 0)
            return hay;
    }
    return NULL;
}

static u32 parse_u32(const char *s)
{
    u32 v = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    for (; *s >= '0' && *s <= '9'; s++)
        v = v * 10 + (u32)(*s - '0');
    return v;
}

static u32 parse_hex(const char *s)
{
    u32 v = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    for (; *s; s++) {
        char c = *s;
        if (c >= '0' && c <= '9')
            v = (v << 4) | (u32)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v = (v << 4) | (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v = (v << 4) | (u32)(c - 'A' + 10);
        else
            break;
    }
    return v;
}

/* ---- Input polling (/proc/udm/input + /proc/input + /tmp/gui.cmd) ---- */

struct input_poller {
    const char *proc_path;
    int proc_ready;
    u32 last_keycode;
    u32 last_val;
    u32 last_press_ms;
};

static void input_init(struct input_poller *ip)
{
    memset(ip, 0, sizeof(*ip));
    if (write_file("/proc/udm/input", "get 0x01ac\n", 11, 0) >= 0) {
        ip->proc_path = "/proc/udm/input";
        ip->proc_ready = 1;
    } else if (write_file("/proc/input", "get 0x01ac\n", 11, 0) >= 0) {
        ip->proc_path = "/proc/input";
        ip->proc_ready = 1;
    } else {
        ip->proc_path = "/proc/udm/input";
    }
}

static const struct {
    u32 code;
    const char *qstr;
    size_t qlen;
    enum nav_event ev;
} POLL_KEYS[] = {
    { KEY_TACT_UP,     "get 0x0003\n", 11, NAV_UP },
    { KEY_TACT_DOWN,   "get 0x0030\n", 11, NAV_DOWN },
    { KEY_TACT_LEFT,   "get 0x0015\n", 11, NAV_LEFT },
    { KEY_TACT_RIGHT,  "get 0x01af\n", 11, NAV_RIGHT },
    { KEY_TACT_ENTER,  "get 0x01ac\n", 11, NAV_ENTER },
    { KEY_TACT_MENU,   "get 0x01ab\n", 11, NAV_BACK },
    { KEY_TACT_CUSTOM, "get 0x0066\n", 11, NAV_TOGGLE_OSD },
    { KEY_TACT_DELETE, "get 0x01f8\n", 11, NAV_QUIT },
};
#define NUM_POLL_KEYS 8

static enum nav_event input_poll(struct input_poller *ip)
{
    char buf[512];
    int i;
    u32 pressed_code = 0;
    enum nav_event pressed_ev = NAV_NONE;

    /* 1. Check external control command file (/tmp/gui.cmd) */
    if (read_file(CMD_FILE, buf, sizeof(buf)) > 0) {
        sys_unlink(CMD_FILE);
        if (strncmp(buf, "up", 2) == 0)
            return NAV_UP;
        if (strncmp(buf, "down", 4) == 0)
            return NAV_DOWN;
        if (strncmp(buf, "left", 4) == 0)
            return NAV_LEFT;
        if (strncmp(buf, "right", 5) == 0)
            return NAV_RIGHT;
        if (strncmp(buf, "enter", 5) == 0 || strncmp(buf, "ok", 2) == 0)
            return NAV_ENTER;
        if (strncmp(buf, "back", 4) == 0 || strncmp(buf, "menu", 4) == 0)
            return NAV_BACK;
        if (strncmp(buf, "toggle", 6) == 0 || strncmp(buf, "show", 4) == 0 || strncmp(buf, "hide", 4) == 0)
            return NAV_TOGGLE_OSD;
        if (strncmp(buf, "quit", 4) == 0 || strncmp(buf, "exit", 4) == 0)
            return NAV_QUIT;
    }

    /* 2. Poll /proc/udm/input (or /proc/input) via "get 0xXXXX" per keycode.
     * In input.ko (Getkey at 0x0e74..0x11fc and kbd_timer_hndlr at 0x2734..0x2740):
     *   value ([r4, #0xc]) == 0x0        -> key is RELEASED (after initial sample)
     *   value ([r4, #0xc]) == 0x7fffffff -> key is RELEASED (never changed since boot)
     *   value ([r4, #0xc]) == 0x7ffffffe -> keycode not found in table
     *   value != 0 && value != 0x7fffffff && value != 0x7ffffffe (e.g. 0x1) -> key is PRESSED */
    if (!ip->proc_ready) {
        input_init(ip);
    }
    if (ip->proc_ready) {
        for (i = 0; i < NUM_POLL_KEYS; i++) {
            if (write_file(ip->proc_path, POLL_KEYS[i].qstr, POLL_KEYS[i].qlen, 0) < 0)
                break;
            if (read_file(ip->proc_path, buf, sizeof(buf)) <= 0)
                break;
            {
                const char *vl = find_substr(buf, "value:");
                if (vl) {
                    u32 val = parse_hex(vl + 6);
                    if (val != 0u && val != 0x7fffffffu && val != 0x7ffffffeu) {
                        pressed_code = POLL_KEYS[i].code;
                        pressed_ev = POLL_KEYS[i].ev;
                        break;
                    }
                }
            }
        }
        if (pressed_code != 0) {
            u32 now = now_ms();
            if (pressed_code != ip->last_keycode || ip->last_val == 0 || (now - ip->last_press_ms) > 250) {
                ip->last_keycode = pressed_code;
                ip->last_val = 1;
                ip->last_press_ms = now;
                return pressed_ev;
            }
        } else {
            ip->last_val = 0;
        }
    }

    return NAV_NONE;
}

/* ---- Process execution helpers ---- */

static int g_bg_pid = 0;
static const char *g_bg_label = NULL;

static void reap_bg(void)
{
    int st = 0;
    if (g_bg_pid > 0) {
        int r = sys_wait4(g_bg_pid, &st, WNOHANG);
        if (r == g_bg_pid || r < 0) {
            g_bg_pid = 0;
            g_bg_label = NULL;
        }
    }
}

static void stop_bg(void)
{
    int st = 0;
    if (g_bg_pid > 0) {
        sys_kill(-g_bg_pid, SIGTERM);
        sys_kill(g_bg_pid, SIGTERM);
        msleep(100);
        sys_wait4(g_bg_pid, &st, WNOHANG);
        g_bg_pid = 0;
        g_bg_label = NULL;
    }
}

static int spawn_sh(const char *cmd, int background, const char *label)
{
    int pid = sys_fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        char *argv[4];
        char *envp[2];
        int fd;
        sys_setsid();
        fd = sys_open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            sys_dup2(fd, 1);
            sys_dup2(fd, 2);
            if (fd > 2)
                sys_close(fd);
        }
        argv[0] = "/bin/sh";
        argv[1] = "-c";
        argv[2] = (char *)cmd;
        argv[3] = NULL;
        envp[0] = "PATH=/usr/bin:/bin:/sbin:/usr/sbin:/tmp:/mnt";
        envp[1] = NULL;
        sys_execve("/bin/sh", argv, envp);
        sys_exit(127);
    }
    if (background) {
        g_bg_pid = pid;
        g_bg_label = label;
        return pid;
    } else {
        int st = 0;
        sys_wait4(pid, &st, 0);
        return (st >> 8) & 0xff;
    }
}

/* ---- Film Lab (/mnt/RITUAL.ini) state ---- */

static const char * const STOCKS[] = {
    "PORTRA_400",
    "CINESTILL_800T",
    "FUJI_PROVIA",
    "KODAK_GOLD",
    "TRI_X_400"
};
#define NUM_STOCKS 5

static const char * const GRAINS[] = {
    "OFF",
    "LOW",
    "MEDIUM",
    "HIGH"
};
#define NUM_GRAINS 4

static const char * const SHUTTER_MODES[] = {
    "Single Shot",
    "2s Delay Shot",
    "Half-Press AF"
};
#define NUM_SHUTTER_MODES 3

static const u32 BULB_SECS[] = { 10, 30, 60, 120, 240, 300 };
#define NUM_BULB_SECS 6

struct interval_preset {
    const char *name;
    const char *args;
};
static const struct interval_preset INTERVAL_PRESETS[] = {
    { "10x @ 5s",       "-n 10 -i 5" },
    { "30x @ 10s",      "-n 30 -i 10" },
    { "60x @ 15s",      "-n 60 -i 15" },
    { "Cont @ 5s",      "-n 0 -i 5" },
    { "20x 60s Bulb",   "-b 60 -n 20 -i 65" }
};
#define NUM_INTERVAL_PRESETS 5

struct bracket_preset {
    const char *name;
    const char *args;
};
static const struct bracket_preset BRACKET_PRESETS[] = {
    { "10f Small Far",  "-n 10 -z small -d far" },
    { "20f Small Far",  "-n 20 -z small -d far" },
    { "10f Med Far",    "-n 10 -z medium -d far" },
    { "10f Large Far",  "-n 10 -z large -d far" },
    { "10f Small Near", "-n 10 -z small -d near" }
};
#define NUM_BRACKET_PRESETS 5

#define NUM_ROWS 8

struct menu_state {
    int row;
    int shutter_idx;
    int bulb_idx;
    int interval_idx;
    int bracket_idx;
    int stock_idx;
    int grain_idx;
    int halation;
    int rec_unlimit_mode; /* 0 = unlock 13h, 1 = restore 30m */
    int exit_mode;        /* 0 = hide OSD, 1 = quit daemon */
    char status[64];
};

static void load_ritual_ini(struct menu_state *m)
{
    char buf[512];
    int i;
    m->stock_idx = 0;
    m->grain_idx = 2;
    m->halation = 1;
    if (read_file(RITUAL_INI_TMP, buf, sizeof(buf)) <= 0 &&
        read_file(RITUAL_INI, buf, sizeof(buf)) <= 0)
        return;
    for (i = 0; i < NUM_STOCKS; i++) {
        if (find_substr(buf, STOCKS[i])) {
            m->stock_idx = i;
            break;
        }
    }
    for (i = 0; i < NUM_GRAINS; i++) {
        char pat[24];
        fmt(pat, sizeof(pat), "GRAIN=%s", GRAINS[i]);
        if (find_substr(buf, pat)) {
            m->grain_idx = i;
            break;
        }
    }
    if (find_substr(buf, "HALATION=OFF") || find_substr(buf, "HALATION=0"))
        m->halation = 0;
    else if (find_substr(buf, "HALATION=ON") || find_substr(buf, "HALATION=1"))
        m->halation = 1;
}

static void save_ritual_ini(struct menu_state *m)
{
    char buf[256];
    int n = fmt(buf, sizeof(buf),
                "# Sony a6400 The Ritual Configuration\n"
                "STOCK=%s\n"
                "GRAIN=%s\n"
                "HALATION=%s\n",
                STOCKS[m->stock_idx],
                GRAINS[m->grain_idx],
                m->halation ? "ON" : "OFF");
    write_file(RITUAL_INI_TMP, buf, (size_t)n, 0);
    if (sys_access("/mnt/DCIM", 0) == 0)
        write_file(RITUAL_INI, buf, (size_t)n, 0);
}

static void ensure_ritual_daemon(void)
{
    const char *cmd =
        "if ! pidof ritual_daemon >/dev/null 2>&1; then "
        "  if [ -x /tmp/ritual_daemon ]; then /tmp/ritual_daemon >> /tmp/ritual.log 2>&1 & "
        "  elif [ -x /mnt/ritual_daemon ]; then /mnt/ritual_daemon >> /tmp/ritual.log 2>&1 & "
        "  fi; "
        "fi";
    spawn_sh(cmd, 0, NULL);
}

static const char *resolve_script(const char *tmp_path, const char *mnt_path, const char *sub_path)
{
    if (sys_access(tmp_path, 0) == 0)
        return tmp_path;
    if (sys_access(mnt_path, 0) == 0)
        return mnt_path;
    if (sub_path && sys_access(sub_path, 0) == 0)
        return sub_path;
    return tmp_path;
}

static void execute_row(struct menu_state *m, int *osd_visible)
{
    char cmd[256];
    reap_bg();

    /* If a background task is running and user hits ENTER on rows 1..3, stop it */
    if (g_bg_pid > 0 && (m->row == 1 || m->row == 2 || m->row == 3)) {
        stop_bg();
        fmt(m->status, sizeof(m->status), "Stopped background task.");
        return;
    }

    switch (m->row) {
    case 0: { /* Shutter */
        const char *sh = resolve_script("/tmp/shutter.sh", "/mnt/shutter.sh", "/mnt/astro_shutter/shutter.sh");
        if (m->shutter_idx == 0) {
            fmt(cmd, sizeof(cmd), "/bin/sh %s shot", sh);
            fmt(m->status, sizeof(m->status), "Firing shutter...");
            spawn_sh(cmd, 1, "Shutter Shot");
        } else if (m->shutter_idx == 1) {
            fmt(cmd, sizeof(cmd), "sleep 2 && /bin/sh %s shot", sh);
            fmt(m->status, sizeof(m->status), "2s timer started...");
            spawn_sh(cmd, 1, "2s Timer Shot");
        } else {
            fmt(cmd, sizeof(cmd), "/bin/sh %s half", sh);
            fmt(m->status, sizeof(m->status), "Half-press AF...");
            spawn_sh(cmd, 1, "Half-Press AF");
        }
        break;
    }
    case 1: { /* Bulb */
        const char *sh = resolve_script("/tmp/shutter.sh", "/mnt/shutter.sh", "/mnt/astro_shutter/shutter.sh");
        u32 secs = BULB_SECS[m->bulb_idx];
        fmt(cmd, sizeof(cmd), "/bin/sh %s bulb %u", sh, secs);
        fmt(m->status, sizeof(m->status), "Bulb %us started (ENTER stops)", secs);
        spawn_sh(cmd, 1, "Bulb Exposure");
        break;
    }
    case 2: { /* Intervalometer */
        const char *sh = resolve_script("/tmp/intervalometer.sh", "/mnt/intervalometer.sh", "/mnt/astro_shutter/intervalometer.sh");
        fmt(cmd, sizeof(cmd), "/bin/sh %s %s", sh, INTERVAL_PRESETS[m->interval_idx].args);
        fmt(m->status, sizeof(m->status), "Interval: %s", INTERVAL_PRESETS[m->interval_idx].name);
        spawn_sh(cmd, 1, INTERVAL_PRESETS[m->interval_idx].name);
        break;
    }
    case 3: { /* Focus Bracket */
        const char *sh = resolve_script("/tmp/focus_bracket.sh", "/mnt/focus_bracket.sh", "/mnt/focus_bracket/focus_bracket.sh");
        fmt(cmd, sizeof(cmd), "/bin/sh %s %s", sh, BRACKET_PRESETS[m->bracket_idx].args);
        fmt(m->status, sizeof(m->status), "Bracket: %s", BRACKET_PRESETS[m->bracket_idx].name);
        spawn_sh(cmd, 1, BRACKET_PRESETS[m->bracket_idx].name);
        break;
    }
    case 4:
    case 5: {
        save_ritual_ini(m);
        ensure_ritual_daemon();
        write_file("/tmp/ritual_develop_latest", "1\n", 2, 0);
        fmt(m->status, sizeof(m->status), "Applying %s (%s)...", STOCKS[m->stock_idx], GRAINS[m->grain_idx]);
        break;
    }
    case 6: { /* 30-Min Rec Limit */
        const char *sh = resolve_script("/tmp/rec_unlimit.sh", "/mnt/rec_unlimit.sh", "/mnt/rec_unlimit/rec_unlimit.sh");
        if (sys_access(sh, 0) == 0) {
            fmt(cmd, sizeof(cmd), "/bin/sh %s %s", sh, m->rec_unlimit_mode ? "--restore" : "--apply");
        } else if (m->rec_unlimit_mode == 0) {
            fmt(cmd, sizeof(cmd), "/usr/bin/bk.elf w 003c0373 0D && /usr/bin/bk.elf w 003c0374 01 && /usr/bin/bk.elf w 003c0375 00 && /usr/bin/bk.elf s");
        } else {
            fmt(cmd, sizeof(cmd), "/usr/bin/bk.elf w 003c0373 00 && /usr/bin/bk.elf w 003c0374 1D && /usr/bin/bk.elf w 003c0375 00 && /usr/bin/bk.elf s");
        }
        spawn_sh(cmd, 0, NULL);
        fmt(m->status, sizeof(m->status), m->rec_unlimit_mode ? "Restored 30m limit" : "Unlocked 13h rec limit!");
        break;
    }
    case 7:
        if (m->exit_mode == 0) {
            *osd_visible = 0;
        } else {
            g_stop = 1;
        }
        break;
    }
}

static void adjust_row(struct menu_state *m, int dir)
{
    switch (m->row) {
    case 0:
        m->shutter_idx = (m->shutter_idx + dir + NUM_SHUTTER_MODES) % NUM_SHUTTER_MODES;
        break;
    case 1:
        m->bulb_idx = (m->bulb_idx + dir + NUM_BULB_SECS) % NUM_BULB_SECS;
        break;
    case 2:
        m->interval_idx = (m->interval_idx + dir + NUM_INTERVAL_PRESETS) % NUM_INTERVAL_PRESETS;
        break;
    case 3:
        m->bracket_idx = (m->bracket_idx + dir + NUM_BRACKET_PRESETS) % NUM_BRACKET_PRESETS;
        break;
    case 4:
        m->stock_idx = (m->stock_idx + dir + NUM_STOCKS) % NUM_STOCKS;
        save_ritual_ini(m);
        fmt(m->status, sizeof(m->status), "Film Stock: %s", STOCKS[m->stock_idx]);
        break;
    case 5:
        if (dir > 0)
            m->grain_idx = (m->grain_idx + 1) % NUM_GRAINS;
        else
            m->halation = !m->halation;
        save_ritual_ini(m);
        fmt(m->status, sizeof(m->status), "Grain:%s Halation:%s", GRAINS[m->grain_idx], m->halation ? "ON" : "OFF");
        break;
    case 6:
        m->rec_unlimit_mode = !m->rec_unlimit_mode;
        break;
    case 7:
        m->exit_mode = !m->exit_mode;
        break;
    }
}

/* ---- OSD Rendering ---- */

static void render_menu(struct fb *f, const struct menu_state *m)
{
    u32 c_bg      = fb_rgb(f, 18, 20, 26);
    u32 c_hdr     = fb_rgb(f, 30, 34, 46);
    u32 c_sel     = fb_rgb(f, 48, 56, 78);
    u32 c_border  = fb_rgb(f, 230, 168, 40);
    u32 c_gold    = fb_rgb(f, 245, 194, 66);
    u32 c_white   = fb_rgb(f, 240, 244, 250);
    u32 c_muted   = fb_rgb(f, 155, 165, 180);
    u32 c_green   = fb_rgb(f, 80, 220, 120);
    u32 c_cyan    = fb_rgb(f, 90, 200, 250);

    int w = (int)f->rw;
    int h = (int)f->rh;
    int pad = 12;
    int row_h = 30;
    int y = 46;
    int i;
    char line[80];
    char val[48];

    gfx_fill(f, 0, 0, w, h, c_bg);
    gfx_fill(f, 2, 2, w - 4, 36, c_hdr);
    gfx_frame(f, 0, 0, w, h, 2, c_border);

    gfx_text(f, pad, 9, "SONY a6400 HOMEBREW OSD", c_gold, 1, w - 2 * pad);
    if (g_bg_pid > 0 && g_bg_label) {
        fmt(line, sizeof(line), "[RUN: %s]", g_bg_label);
        gfx_text(f, w - pad - gfx_text_w(line, 1), 9, line, c_green, 1, w / 2);
    } else {
        gfx_text(f, w - pad - gfx_text_w("v2.00", 1), 9, "v2.00", c_muted, 1, 80);
    }

    for (i = 0; i < NUM_ROWS; i++) {
        const char *label = "";
        int sel = (m->row == i);
        u32 fg_lbl = sel ? c_white : c_muted;
        u32 fg_val = sel ? c_gold : c_cyan;

        val[0] = 0;
        switch (i) {
        case 0:
            label = "1. Shutter Trigger";
            fmt(val, sizeof(val), "< %s >", SHUTTER_MODES[m->shutter_idx]);
            break;
        case 1:
            label = "2. Bulb Exposure";
            fmt(val, sizeof(val), "< %u sec >", BULB_SECS[m->bulb_idx]);
            break;
        case 2:
            label = "3. Intervalometer";
            fmt(val, sizeof(val), "< %s >", INTERVAL_PRESETS[m->interval_idx].name);
            break;
        case 3:
            label = "4. Focus Bracket";
            fmt(val, sizeof(val), "< %s >", BRACKET_PRESETS[m->bracket_idx].name);
            break;
        case 4:
            label = "5. Film Stock";
            fmt(val, sizeof(val), "< %s >", STOCKS[m->stock_idx]);
            break;
        case 5:
            label = "6. Grain / Halation";
            fmt(val, sizeof(val), "< %s | Hal:%s >", GRAINS[m->grain_idx], m->halation ? "ON" : "OFF");
            break;
        case 6:
            label = "7. 30m Video Limit";
            fmt(val, sizeof(val), "< %s >", m->rec_unlimit_mode ? "Restore 30m" : "Unlock 13h");
            break;
        case 7:
            label = "8. Close Menu";
            fmt(val, sizeof(val), "< %s >", m->exit_mode ? "Quit GUI" : "Hide (C1)");
            break;
        }

        if (sel) {
            gfx_fill(f, 6, y - 3, w - 12, row_h - 2, c_sel);
            gfx_frame(f, 6, y - 3, w - 12, row_h - 2, 1, c_border);
        }
        gfx_text(f, pad + 4, y, label, fg_lbl, 1, w / 2);
        gfx_text(f, w - pad - gfx_text_w(val, 1) - 4, y, val, fg_val, 1, w / 2 + 40);
        y += row_h;
    }

    /* Footer bar */
    gfx_fill(f, 2, h - 54, w - 4, 52, c_hdr);
    gfx_text(f, pad, h - 48, m->status, c_green, 1, w - 2 * pad);
    gfx_text(f, pad, h - 24, "UP/DN:Move  L/R:Change  ENTER:Run  C1:Hide  C2:Quit", c_muted, 1, w - 2 * pad);

    fb_present(f);
}

/* ---- Modes: info / banner / interactive menu ---- */

static int cmd_info(const char *fb_dev)
{
    struct fb f;
    char buf[4096];
    int rc;
    const char *in_path = "/proc/udm/input";

    printf("=== Sony a6400 GUI / Display / Input Probe ===\n");
    if (read_file("/proc/fb", buf, sizeof(buf)) > 0)
        printf("/proc/fb:\n%s\n", buf);
    else
        printf("/proc/fb: (empty or unavailable)\n");

    if (read_file("/proc/udm/ma_memspace_info", buf, sizeof(buf)) > 0)
        printf("\n/proc/udm/ma_memspace_info:\n%s\n", buf);
    else
        printf("\n/proc/udm/ma_memspace_info: unavailable\n");

    rc = fb_open(&f, fb_dev, 0);
    if (rc == 0) {
        fb_print_info(&f, 1);
        fb_close(&f);
    } else {
        printf("fb_open(%s) failed (errno-like rc=%d)\n", fb_dev ? fb_dev : "/dev/fb0", rc);
    }

    if (write_file(in_path, "table\n", 6, 0) < 0)
        in_path = "/proc/input";
    if (write_file(in_path, "table\n", 6, 0) >= 0 &&
        read_file(in_path, buf, sizeof(buf)) > 0) {
        printf("\n%s (table preview):\n%s\n", in_path, buf);
    } else {
        printf("\n/proc/udm/input & /proc/input: unavailable\n");
    }
    if (write_file(in_path, "get 0x01ac\n", 11, 0) >= 0 &&
        read_file(in_path, buf, sizeof(buf)) > 0) {
        printf("\n%s (get 0x01ac [ENTER] preview):\n%s\n", in_path, buf);
    }
    return 0;
}

static int cmd_banner(const char *fb_dev, const char *msg, u32 seconds)
{
    struct fb f;
    u32 bw, bh, bx, by;
    int r;

    r = fb_open(&f, fb_dev, 1);
    if (r < 0) {
        printf("fb_open(%s) failed: %d\n", fb_dev ? fb_dev : "/dev/fb0", r);
        return 1;
    }
    fb_print_info(&f, 1);

    bw = f.var.xres > 480 ? 460 : f.var.xres - 20;
    bh = 96;
    bx = (f.var.xres - bw) / 2;
    by = (f.var.yres - bh) / 2;

    if (fb_claim(&f, bx, by, bw, bh) < 0) {
        printf("fb_claim failed\n");
        fb_close(&f);
        return 1;
    }

    gfx_fill(&f, 0, 0, (int)bw, (int)bh, fb_rgb(&f, 18, 20, 26));
    gfx_frame(&f, 0, 0, (int)bw, (int)bh, 3, fb_rgb(&f, 230, 168, 40));
    gfx_text(&f, 16, 16, "SONY a6400 HOMEBREW", fb_rgb(&f, 245, 194, 66), 1, (int)bw - 32);
    gfx_text(&f, 16, 48, msg, fb_rgb(&f, 240, 244, 250), 1, (int)bw - 32);
    fb_present(&f);

    set_signal(SIGINT, on_sig);
    set_signal(SIGTERM, on_sig);
    set_signal(SIGHUP, on_sig);

    for (u32 t = 0; t < seconds * 20 && !g_stop; t++) {
        fb_present(&f);
        msleep(50);
    }

    fb_restore(&f);
    fb_close(&f);
    return 0;
}

static int claim_menu_rect(struct fb *f)
{
    u32 mw = f->var.xres > 560 ? 540 : (f->var.xres > 40 ? f->var.xres - 20 : f->var.xres);
    u32 mh = f->var.yres > 360 ? 348 : (f->var.yres > 20 ? f->var.yres - 10 : f->var.yres);
    u32 mx = (f->var.xres - mw) / 2;
    u32 my = (f->var.yres - mh) / 2;
    return fb_claim(f, mx, my, mw, mh);
}

int main(int argc, char **argv, char **envp)
{
    const char *fb_dev = "/dev/fb0";
    struct fb f;
    struct input_poller ip;
    struct menu_state m;
    int osd_visible = 1;
    int dirty = 1;
    char pidbuf[16];
    int n, argi = 1;

    (void)envp;

    if (argi < argc && strcmp(argv[argi], "-d") == 0 && argi + 1 < argc) {
        fb_dev = argv[argi + 1];
        argi += 2;
    }

    if (argi < argc) {
        if (strcmp(argv[argi], "info") == 0 || strcmp(argv[argi], "--info") == 0 || strcmp(argv[argi], "probe") == 0)
            return cmd_info(fb_dev);
        if (strcmp(argv[argi], "banner") == 0 || strcmp(argv[argi], "--banner") == 0) {
            const char *msg = (argi + 1 < argc) ? argv[argi + 1] : "OSD Overlay Ready!";
            u32 secs = (argi + 2 < argc) ? parse_u32(argv[argi + 2]) : 3;
            if (secs == 0)
                secs = 3;
            return cmd_banner(fb_dev, msg, secs);
        }
        if (strcmp(argv[argi], "--hidden") == 0 || strcmp(argv[argi], "--daemon") == 0)
            osd_visible = 0;
        if (strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0) {
            printf("Usage: %s [-d /dev/fb0] [menu | --daemon | info | banner <text> [sec]]\n", argv[0]);
            return 0;
        }
    }

    if (fb_open(&f, fb_dev, 1) < 0) {
        printf("error: cannot open framebuffer %s (run '%s info' to diagnose)\n", fb_dev, argv[0]);
        return 1;
    }
    fb_print_info(&f, 1);

    set_signal(SIGINT, on_sig);
    set_signal(SIGTERM, on_sig);
    set_signal(SIGHUP, on_sig);

    n = fmt(pidbuf, sizeof(pidbuf), "%d\n", sys_getpid());
    write_file(PID_FILE, pidbuf, (size_t)n, 0);

    memset(&m, 0, sizeof(m));
    m.bulb_idx = 1; /* 30s default */
    m.exit_mode = 1; /* Quit GUI default on Row 8 */
    ensure_ritual_daemon();
    load_ritual_ini(&m);
    save_ritual_ini(&m);
    fmt(m.status, sizeof(m.status), "Ready. C1:Hide/Show  C2(Trash):Quit");

    input_init(&ip);

    if (osd_visible) {
        if (claim_menu_rect(&f) < 0) {
            printf("error: fb_claim failed\n");
            fb_close(&f);
            sys_unlink(PID_FILE);
            return 1;
        }
    }

    char last_ritual_status[64];
    int status_tick = 0;
    last_ritual_status[0] = '\0';
    {
        char sbuf[128];
        if (read_file(RITUAL_STATUS_FILE, sbuf, sizeof(sbuf)) > 0) {
            int k = 0;
            while (sbuf[k] && sbuf[k] != '\n' && sbuf[k] != '\r' && k < (int)sizeof(last_ritual_status) - 1) {
                last_ritual_status[k] = sbuf[k];
                k++;
            }
            last_ritual_status[k] = '\0';
        }
    }

    while (!g_stop) {
        enum nav_event ev = input_poll(&ip);
        int prev_bg = g_bg_pid;
        reap_bg();
        if (prev_bg > 0 && g_bg_pid == 0) {
            fmt(m.status, sizeof(m.status), "Task completed.");
            dirty = 1;
        }

        if (++status_tick >= 20) {
            char sbuf[128];
            status_tick = 0;
            if (read_file(RITUAL_STATUS_FILE, sbuf, sizeof(sbuf)) > 0) {
                char line[64];
                int k = 0;
                while (sbuf[k] && sbuf[k] != '\n' && sbuf[k] != '\r' && k < (int)sizeof(line) - 1) {
                    line[k] = sbuf[k];
                    k++;
                }
                line[k] = '\0';
                if (line[0] != '\0' && strcmp(line, last_ritual_status) != 0) {
                    fmt(last_ritual_status, sizeof(last_ritual_status), "%s", line);
                    fmt(m.status, sizeof(m.status), "%s", line);
                    dirty = 1;
                }
            }
        }

        if (ev == NAV_QUIT) {
            break;
        } else if (ev == NAV_TOGGLE_OSD) {
            osd_visible = !osd_visible;
            if (osd_visible) {
                claim_menu_rect(&f);
                dirty = 1;
            } else {
                fb_restore(&f);
            }
        } else if (osd_visible && ev != NAV_NONE) {
            switch (ev) {
            case NAV_UP:
                m.row = (m.row - 1 + NUM_ROWS) % NUM_ROWS;
                dirty = 1;
                break;
            case NAV_DOWN:
                m.row = (m.row + 1) % NUM_ROWS;
                dirty = 1;
                break;
            case NAV_LEFT:
                adjust_row(&m, -1);
                dirty = 1;
                break;
            case NAV_RIGHT:
                adjust_row(&m, +1);
                dirty = 1;
                break;
            case NAV_ENTER:
                execute_row(&m, &osd_visible);
                if (!osd_visible)
                    fb_restore(&f);
                else
                    dirty = 1;
                break;
            case NAV_BACK:
                osd_visible = 0;
                fb_restore(&f);
                break;
            default:
                break;
            }
        }

        if (osd_visible) {
            if (dirty) {
                render_menu(&f, &m);
                dirty = 0;
            } else {
                fb_present(&f);
            }
        }

        msleep(40);
    }

    stop_bg();
    if (osd_visible)
        fb_restore(&f);
    fb_close(&f);
    sys_unlink(PID_FILE);
    return 0;
}
