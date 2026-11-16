#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>

#define CONFIG_PATH "/mnt/RITUAL.ini"
#define RITUAL_DIR "/mnt/RITUAL"
#define LOG_PATH "/mnt/RITUAL/.processed.log"

// Color codes
#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_DIM     "\033[2m"
#define CLR_AMBER   "\033[38;5;214m"
#define CLR_GOLD    "\033[38;5;220m"
#define CLR_RED     "\033[38;5;196m"
#define CLR_GREEN   "\033[38;5;82m"
#define CLR_CYAN    "\033[38;5;51m"
#define CLR_BG_SEL  "\033[48;5;236m"
#define CLR_CLEAR   "\033[2J\033[H"
#define CURSOR_HIDE "\033[?25l"
#define CURSOR_SHOW "\033[?25h"

static const char *STOCKS[] = {
    "PORTRA_400",
    "CINESTILL_800T",
    "FUJI_PROVIA",
    "KODAK_GOLD",
    "TRI_X_400"
};
#define NUM_STOCKS 5

static const char *STOCK_NAMES[] = {
    "Kodak Professional Portra 400 (Warm, Soft Tones)",
    "CineStill 800T (Tungsten Blue & Red Halation)",
    "Fujifilm Provia 100F (Natural Slide Contrast)",
    "Kodak Gold 200 (Classic Golden Vintage)",
    "Kodak Professional Tri-X 400 (High-Contrast B&W)"
};

static const char *GRAINS[] = {
    "OFF",
    "LOW",
    "MEDIUM",
    "HIGH"
};
#define NUM_GRAINS 4

typedef struct {
    int stock_idx;
    int grain_idx;
    int halation;
    int selected_row;
} MenuState;

static struct termios g_orig_termios;

static void reset_terminal(void) {
    printf(CURSOR_SHOW CLR_RESET "\n");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
}

static void setup_terminal(void) {
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    atexit(reset_terminal);

    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    printf(CURSOR_HIDE);
    fflush(stdout);
}

static void load_state(MenuState *s) {
    s->stock_idx = 0;
    s->grain_idx = 2; // MEDIUM
    s->halation = 1;
    s->selected_row = 0;

    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char k[64], v[64];
        if (sscanf(line, "%63[^=]=%63s", k, v) == 2) {
            v[strcspn(v, "\r\n")] = 0;
            if (strcmp(k, "STOCK") == 0) {
                for (int i = 0; i < NUM_STOCKS; i++) {
                    if (strcmp(v, STOCKS[i]) == 0) {
                        s->stock_idx = i;
                        break;
                    }
                }
            } else if (strcmp(k, "GRAIN") == 0) {
                for (int i = 0; i < NUM_GRAINS; i++) {
                    if (strcmp(v, GRAINS[i]) == 0) {
                        s->grain_idx = i;
                        break;
                    }
                }
            } else if (strcmp(k, "HALATION") == 0) {
                s->halation = (strcmp(v, "ON") == 0 || strcmp(v, "1") == 0);
            }
        }
    }
    fclose(f);
}

static void save_state(const MenuState *s) {
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) return;

    fprintf(f, "# Sony a6400 The Ritual Configuration\n");
    fprintf(f, "# Edited in-camera via Ritual Menu\n");
    fprintf(f, "STOCK=%s\n", STOCKS[s->stock_idx]);
    fprintf(f, "GRAIN=%s\n", GRAINS[s->grain_idx]);
    fprintf(f, "HALATION=%s\n", s->halation ? "ON" : "OFF");
    fclose(f);
    sync();
}

static int count_processed_photos(void) {
    FILE *f = fopen(LOG_PATH, "r");
    if (!f) return 0;
    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strlen(line) > 2) count++;
    }
    fclose(f);
    return count;
}

static int is_daemon_running(void) {
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *de;
    int running = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] >= '0' && de->d_name[0] <= '9') {
            char cmdpath[64];
            snprintf(cmdpath, sizeof(cmdpath), "/proc/%s/cmdline", de->d_name);
            FILE *f = fopen(cmdpath, "r");
            if (f) {
                char buf[128] = {0};
                fread(buf, 1, sizeof(buf) - 1, f);
                fclose(f);
                if (strstr(buf, "ritual_daemon")) {
                    running = 1;
                    break;
                }
            }
        }
    }
    closedir(d);
    return running;
}

static void draw_menu(const MenuState *s) {
    printf(CLR_CLEAR);
    printf(CLR_AMBER CLR_BOLD);
    printf("  +--------------------------------------------------------------+\n");
    printf("  |            *** THE RITUAL - IN-CAMERA FILM LAB ***           |\n");
    printf("  |                   Sony ILCE-6400 Edition                     |\n");
    printf("  +--------------------------------------------------------------+\n" CLR_RESET);

    int daemon_on = is_daemon_running();
    int photos = count_processed_photos();

    printf("   Status: ");
    if (daemon_on) {
        printf(CLR_GREEN CLR_BOLD "[DAEMON ACTIVE - DEVELOPING LIVE]" CLR_RESET);
    } else {
        printf(CLR_RED CLR_BOLD "[DAEMON STOPPED]" CLR_RESET);
    }
    printf("  |  Photos Developed: " CLR_CYAN CLR_BOLD "%d" CLR_RESET "\n\n", photos);

    // Row 0: Stock
    if (s->selected_row == 0) {
        printf(CLR_BG_SEL CLR_GOLD CLR_BOLD " > [1] FILM EMULATION STOCK : < %-14s >              \n" CLR_RESET, STOCKS[s->stock_idx]);
        printf("       " CLR_AMBER "%s\n\n" CLR_RESET, STOCK_NAMES[s->stock_idx]);
    } else {
        printf("   [1] Film Emulation Stock :   %-14s\n", STOCKS[s->stock_idx]);
        printf("       " CLR_DIM "%s\n\n" CLR_RESET, STOCK_NAMES[s->stock_idx]);
    }

    // Row 1: Grain
    if (s->selected_row == 1) {
        printf(CLR_BG_SEL CLR_GOLD CLR_BOLD " > [2] ORGANIC FILM GRAIN   : < %-6s >                      \n" CLR_RESET, GRAINS[s->grain_idx]);
    } else {
        printf("   [2] Organic Film Grain   :   %-6s\n", GRAINS[s->grain_idx]);
    }
    printf("       " CLR_DIM "Simulates authentic silver-halide grain structure\n\n" CLR_RESET);

    // Row 2: Halation
    if (s->selected_row == 2) {
        printf(CLR_BG_SEL CLR_GOLD CLR_BOLD " > [3] RED HIGHLIGHT HALATION: < %-3s >                         \n" CLR_RESET, s->halation ? "ON " : "OFF");
    } else {
        printf("   [3] Red Highlight Halation:   %-3s\n", s->halation ? "ON " : "OFF");
    }
    printf("       " CLR_DIM "Simulates daylight/tungsten base-layer red bleed in highlights\n\n" CLR_RESET);

    // Row 3: Save & Apply
    if (s->selected_row == 3) {
        printf(CLR_BG_SEL CLR_GREEN CLR_BOLD " > [4] >>> SAVE & APPLY TO NEXT SHOTS <<<                       \n" CLR_RESET);
    } else {
        printf("   [4]     Save & Apply to Next Shots\n");
    }
    printf("\n");

    printf(CLR_AMBER "  +--------------------------------------------------------------+\n" CLR_RESET);
    printf("   Navigation:\n");
    printf("     " CLR_BOLD "[UP/DOWN]" CLR_RESET " Select item    " CLR_BOLD "[LEFT/RIGHT]" CLR_RESET " Change value\n");
    printf("     " CLR_BOLD "[ENTER]" CLR_RESET "   Save & Exit    " CLR_BOLD "[Q]" CLR_RESET "          Quit without saving\n");
    printf(CLR_AMBER "  +--------------------------------------------------------------+\n" CLR_RESET);
    fflush(stdout);
}

int main(int argc, char **argv) {
    MenuState state;
    load_state(&state);
    setup_terminal();

    draw_menu(&state);

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) break;

        if (c == 'q' || c == 'Q') {
            printf(CLR_CLEAR "Quit without saving.\n");
            break;
        } else if (c == 10 || c == 13) { // ENTER
            save_state(&state);
            printf(CLR_CLEAR CLR_GREEN CLR_BOLD "\n[+] Settings saved to /mnt/RITUAL.ini!\n" CLR_RESET);
            printf("    Film Stock : " CLR_GOLD "%s\n" CLR_RESET, STOCKS[state.stock_idx]);
            printf("    Grain      : " CLR_GOLD "%s\n" CLR_RESET, GRAINS[state.grain_idx]);
            printf("    Halation   : " CLR_GOLD "%s\n\n" CLR_RESET, state.halation ? "ON" : "OFF");
            printf("[+] Next photo taken on camera will develop with these settings.\n\n");
            break;
        } else if (c == '\033') { // Escape sequence
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1) {
                if (seq[0] == '[') {
                    switch (seq[1]) {
                        case 'A': // UP
                            if (state.selected_row > 0) state.selected_row--;
                            else state.selected_row = 3;
                            break;
                        case 'B': // DOWN
                            if (state.selected_row < 3) state.selected_row++;
                            else state.selected_row = 0;
                            break;
                        case 'C': // RIGHT
                            if (state.selected_row == 0) {
                                state.stock_idx = (state.stock_idx + 1) % NUM_STOCKS;
                            } else if (state.selected_row == 1) {
                                state.grain_idx = (state.grain_idx + 1) % NUM_GRAINS;
                            } else if (state.selected_row == 2) {
                                state.halation = !state.halation;
                            }
                            break;
                        case 'D': // LEFT
                            if (state.selected_row == 0) {
                                state.stock_idx = (state.stock_idx - 1 + NUM_STOCKS) % NUM_STOCKS;
                            } else if (state.selected_row == 1) {
                                state.grain_idx = (state.grain_idx - 1 + NUM_GRAINS) % NUM_GRAINS;
                            } else if (state.selected_row == 2) {
                                state.halation = !state.halation;
                            }
                            break;
                    }
                }
            }
        } else if (c == 'w' || c == 'k') { // UP alternate
            if (state.selected_row > 0) state.selected_row--;
            else state.selected_row = 3;
        } else if (c == 's' || c == 'j') { // DOWN alternate
            if (state.selected_row < 3) state.selected_row++;
            else state.selected_row = 0;
        } else if (c == 'd' || c == 'l') { // RIGHT alternate
            if (state.selected_row == 0) {
                state.stock_idx = (state.stock_idx + 1) % NUM_STOCKS;
            } else if (state.selected_row == 1) {
                state.grain_idx = (state.grain_idx + 1) % NUM_GRAINS;
            } else if (state.selected_row == 2) {
                state.halation = !state.halation;
            }
        } else if (c == 'a' || c == 'h') { // LEFT alternate
            if (state.selected_row == 0) {
                state.stock_idx = (state.stock_idx - 1 + NUM_STOCKS) % NUM_STOCKS;
            } else if (state.selected_row == 1) {
                state.grain_idx = (state.grain_idx - 1 + NUM_GRAINS) % NUM_GRAINS;
            } else if (state.selected_row == 2) {
                state.halation = !state.halation;
            }
        } else if (c == ' ') { // SPACE toggles current option
            if (state.selected_row == 0) {
                state.stock_idx = (state.stock_idx + 1) % NUM_STOCKS;
            } else if (state.selected_row == 1) {
                state.grain_idx = (state.grain_idx + 1) % NUM_GRAINS;
            } else if (state.selected_row == 2) {
                state.halation = !state.halation;
            }
        }

        draw_menu(&state);
    }

    return 0;
}
