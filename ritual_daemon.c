#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <time.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_SIMD
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifndef BLKFLSBUF
#define BLKFLSBUF _IO(0x12, 97)
#endif

#ifndef MNT_DETACH
#define MNT_DETACH 2
#endif

// Paths — state/lock/log files live in /tmp so /mnt can stay UNMOUNTED while shooting!
#define CONFIG_PATH_MNT   "/mnt/RITUAL.ini"
#define CONFIG_PATH_TMP   "/tmp/RITUAL.ini"
#define PROCESSED_LOG     "/tmp/ritual_processed.log"
#define PID_FILE          "/tmp/ritual_daemon.pid"
#define TRIGGER_FILE      "/tmp/ritual_develop_latest"
#define STATUS_FILE       "/tmp/ritual_status.txt"
#define LATEST_COPY_TMP   "/tmp/latest_ritual.jpg"
#define RITUAL_DIR        "/mnt/RITUAL"
#define DCIM_DIR          "/mnt/DCIM"

typedef enum {
    STOCK_PORTRA_400 = 0,
    STOCK_CINESTILL_800T,
    STOCK_FUJI_PROVIA,
    STOCK_KODAK_GOLD,
    STOCK_TRI_X_400
} FilmStock;

typedef struct {
    FilmStock stock;
    int grain_level;   // 0=OFF, 1=LOW, 2=MEDIUM, 3=HIGH
    int halation;      // 1=ON, 0=OFF
    int full_res;      // 0=fast embedded/downsampled (default), 1=full 24MP
} RitualConfig;

static uint8_t g_curve_toe[256];
static uint8_t g_curve_shoulder[256];
static uint8_t g_grain_bell[256];
static uint32_t g_rng_state = 0xA6400F17u;

static int g_mmc_irq_pid = -1;
static int g_bdev_mmca_pid = -1;

static inline uint32_t xorshift32(void) {
    uint32_t x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng_state = x;
    return x;
}

static inline uint8_t clamp_u8(int v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static void write_status(const char *msg) {
    FILE *f = fopen(STATUS_FILE ".tmp", "w");
    if (f) {
        fprintf(f, "%s\n", msg);
        fclose(f);
        rename(STATUS_FILE ".tmp", STATUS_FILE);
    }
    printf("[Ritual] %s\n", msg);
    fflush(stdout);
}

static void init_curves(void) {
    for (int i = 0; i < 256; i++) {
        float x = (float)i / 255.0f;

        // Soft film toe & shoulder S-curve
        float s = x * x * (3.0f - 2.0f * x);
        float lifted = 0.03f + 0.94f * (0.65f * s + 0.35f * x);
        int v_toe = (int)(lifted * 255.0f + 0.5f);
        g_curve_toe[i] = clamp_u8(v_toe);

        // High-contrast slide / B&W curve
        float c = 1.0f / (1.0f + expf(-7.5f * (x - 0.5f)));
        int v_sh = (int)(c * 255.0f + 0.5f);
        g_curve_shoulder[i] = clamp_u8(v_sh);

        // Midtone bell curve for film grain: 4 * x * (1 - x), scaled 0..255
        float bell = 4.0f * x * (1.0f - x);
        int v_bell = (int)(bell * 255.0f + 0.5f);
        g_grain_bell[i] = clamp_u8(v_bell);
    }
}

static void flush_mmca_buffers(void) {
    sync();
    int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
    if (fd >= 0) {
        (void)write(fd, "3\n", 2);
        close(fd);
    }
    int bfd = open("/dev/mmca1", O_RDONLY);
    if (bfd >= 0) {
        (void)ioctl(bfd, BLKFLSBUF, 0);
        close(bfd);
    }
}

// Unmount /mnt completely so Linux never holds stale FAT sectors while im.elf shoots photos
static void unmount_sd(void) {
    sync();
    if (umount("/mnt") != 0) {
        if (errno == EBUSY) {
            (void)umount2("/mnt", MNT_DETACH);
        }
    }
    flush_mmca_buffers();
}

// Freshly mount /dev/mmca1 on /mnt with errors=continue so Linux reads a 100% fresh FAT table
static int mount_sd_fresh(void) {
    unmount_sd();
    mkdir("/mnt", 0777);
    if (mount("/dev/mmca1", "/mnt", "vfat", MS_SYNCHRONOUS | MS_DIRSYNC, "errors=continue") != 0) {
        if (mount("/dev/mmca1", "/mnt", "vfat", 0, NULL) != 0) {
            (void)system("mount -t vfat -o rw,sync,dirsync,errors=continue /dev/mmca1 /mnt 2>/dev/null || mount -t vfat /dev/mmca1 /mnt 2>/dev/null");
        }
    }
    struct stat st;
    return (stat(DCIM_DIR, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
}

// Find PIDs of kernel SD card threads `irq/218-mmc0` and `bdev_mmca`
static void discover_sd_threads(void) {
    DIR *d = opendir("/proc");
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!isdigit((unsigned char)de->d_name[0])) continue;
        int pid = atoi(de->d_name);
        char path[64], buf[256];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fgets(buf, sizeof(buf), f)) {
            if (strstr(buf, "mmc0")) g_mmc_irq_pid = pid;
            else if (strstr(buf, "bdev_mmca")) g_bdev_mmca_pid = pid;
        }
        fclose(f);
    }
    closedir(d);
}

static uint64_t read_pid_switches(int pid) {
    if (pid <= 0) return 0;
    char path[64], line[128];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    uint64_t total = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long v = 0;
        if (sscanf(line, "voluntary_ctxt_switches: %llu", &v) == 1) total += v;
        else if (sscanf(line, "nonvoluntary_ctxt_switches: %llu", &v) == 1) total += v;
    }
    fclose(f);
    return total;
}

// Read total SD card I/O activity counter without touching /dev/mmca1
static uint64_t get_sd_io_activity(void) {
    if (g_mmc_irq_pid < 0 && g_bdev_mmca_pid < 0) {
        discover_sd_threads();
    }
    uint64_t total = 0;
    total += read_pid_switches(g_mmc_irq_pid);
    total += read_pid_switches(g_bdev_mmca_pid);

    FILE *f = fopen("/proc/interrupts", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "mmc") || strstr(line, "218:")) {
                const char *p = strchr(line, ':');
                if (p) {
                    p++;
                    for (int cpu = 0; cpu < 4; cpu++) {
                        while (*p == ' ' || *p == '\t') p++;
                        if (!isdigit((unsigned char)*p)) break;
                        total += (uint64_t)strtoull(p, (char **)&p, 10);
                    }
                }
            }
        }
        fclose(f);
    }
    return total;
}

static int parse_config_file(const char *path, RitualConfig *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == ';' || line[0] == '\n' || line[0] == '\r')
            continue;
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            val[strcspn(val, "\r\n ")] = 0;
            if (strcmp(key, "STOCK") == 0) {
                if (strstr(val, "CINESTILL")) cfg->stock = STOCK_CINESTILL_800T;
                else if (strstr(val, "PROVIA") || strstr(val, "FUJI")) cfg->stock = STOCK_FUJI_PROVIA;
                else if (strstr(val, "GOLD")) cfg->stock = STOCK_KODAK_GOLD;
                else if (strstr(val, "TRIX") || strstr(val, "TRI_X")) cfg->stock = STOCK_TRI_X_400;
                else cfg->stock = STOCK_PORTRA_400;
            } else if (strcmp(key, "GRAIN") == 0) {
                if (strcmp(val, "OFF") == 0 || strcmp(val, "0") == 0) cfg->grain_level = 0;
                else if (strcmp(val, "LOW") == 0) cfg->grain_level = 1;
                else if (strcmp(val, "HIGH") == 0) cfg->grain_level = 3;
                else cfg->grain_level = 2; // MEDIUM
            } else if (strcmp(key, "HALATION") == 0) {
                cfg->halation = (strcmp(val, "ON") == 0 || strcmp(val, "1") == 0);
            } else if (strcmp(key, "FULLRES") == 0) {
                cfg->full_res = (strcmp(val, "ON") == 0 || strcmp(val, "1") == 0);
            }
        }
    }
    fclose(f);
    return 1;
}

static void load_config(RitualConfig *cfg, int sd_mounted) {
    cfg->stock = STOCK_PORTRA_400;
    cfg->grain_level = 2;
    cfg->halation = 1;
    cfg->full_res = 0;

    if (sd_mounted) {
        (void)parse_config_file(CONFIG_PATH_MNT, cfg);
    }
    (void)parse_config_file(CONFIG_PATH_TMP, cfg);
}

static const char *stock_name(FilmStock s) {
    switch (s) {
        case STOCK_PORTRA_400:     return "Portra 400";
        case STOCK_CINESTILL_800T: return "CineStill 800T";
        case STOCK_FUJI_PROVIA:    return "Fuji Provia";
        case STOCK_KODAK_GOLD:     return "Kodak Gold";
        case STOCK_TRI_X_400:      return "Tri-X 400";
        default:                   return "Portra 400";
    }
}

static const char *stock_suffix(FilmStock s) {
    switch (s) {
        case STOCK_PORTRA_400:     return "PORTRA";
        case STOCK_CINESTILL_800T: return "CINESTILL";
        case STOCK_FUJI_PROVIA:    return "PROVIA";
        case STOCK_KODAK_GOLD:     return "GOLD";
        case STOCK_TRI_X_400:      return "TRIX";
        default:                   return "PORTRA";
    }
}

static void strip_extension(const char *filename, char *base_out, size_t cap) {
    strncpy(base_out, filename, cap - 1);
    base_out[cap - 1] = '\0';
    char *dot = strrchr(base_out, '.');
    if (dot) *dot = '\0';
}

// Apply real red-orange film halation glow around bright highlights (4x downsampled box blur)
static void apply_halation_glow(uint8_t *pixels, int w, int h, int strength) {
    int gw = w / 4;
    int gh = h / 4;
    if (gw < 8 || gh < 8) return;

    uint8_t *mask = (uint8_t *)malloc((size_t)(gw * gh));
    uint8_t *tmp  = (uint8_t *)malloc((size_t)(gw * gh));
    if (!mask || !tmp) {
        free(mask);
        free(tmp);
        return;
    }

    for (int y = 0; y < gh; y++) {
        const uint8_t *row = pixels + (size_t)(y * 4) * (size_t)w * 3;
        for (int x = 0; x < gw; x++) {
            const uint8_t *p = row + (size_t)(x * 4) * 3;
            int lum = (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
            mask[y * gw + x] = (lum > 195) ? (uint8_t)((lum - 195) * 4) : 0;
        }
    }

    const int rad = 3;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < gh; y++) {
            for (int x = 0; x < gw; x++) {
                int sum = 0, cnt = 0;
                int x0 = (x - rad < 0) ? 0 : x - rad;
                int x1 = (x + rad >= gw) ? gw - 1 : x + rad;
                for (int k = x0; k <= x1; k++) {
                    sum += mask[y * gw + k];
                    cnt++;
                }
                tmp[y * gw + x] = (uint8_t)(sum / cnt);
            }
        }
        for (int y = 0; y < gh; y++) {
            int y0 = (y - rad < 0) ? 0 : y - rad;
            int y1 = (y + rad >= gh) ? gh - 1 : y + rad;
            int cnt = y1 - y0 + 1;
            for (int x = 0; x < gw; x++) {
                int sum = 0;
                for (int k = y0; k <= y1; k++) {
                    sum += tmp[k * gw + x];
                }
                mask[y * gw + x] = (uint8_t)(sum / cnt);
            }
        }
    }

    for (int y = 0; y < h; y++) {
        int gy = y / 4;
        if (gy >= gh) gy = gh - 1;
        uint8_t *row = pixels + (size_t)y * (size_t)w * 3;
        const uint8_t *mrow = mask + gy * gw;
        for (int x = 0; x < w; x++) {
            int gx = x / 4;
            if (gx >= gw) gx = gw - 1;
            int glow = mrow[gx];
            if (glow > 2) {
                int dr = (glow * strength) >> 7;
                int dg = (dr * 90) >> 8;
                uint8_t *p = row + x * 3;
                p[0] = clamp_u8((int)p[0] + dr);
                p[1] = clamp_u8((int)p[1] + dg);
            }
        }
    }

    free(mask);
    free(tmp);
}

// Fast 100% Integer LUT + Grain + Halation Film Engine
static void apply_film_emulation(uint8_t *pixels, int w, int h, const RitualConfig *cfg) {
    uint8_t r_lut[256], g_lut[256], b_lut[256];

    for (int i = 0; i < 256; i++) {
        int r = i, g = i, b = i;
        switch (cfg->stock) {
            case STOCK_PORTRA_400:
                r = (i * 276) >> 8; r += 6;
                g = (i * 261) >> 8; g += 2;
                b = (i * 241) >> 8; b += 4;
                r = g_curve_toe[clamp_u8(r)];
                g = g_curve_toe[clamp_u8(g)];
                b = g_curve_toe[clamp_u8(b)];
                break;

            case STOCK_CINESTILL_800T:
                r = (i * 243) >> 8;
                g = (i * 251) >> 8;
                b = ((i * 286) >> 8) + 5;
                r = g_curve_toe[clamp_u8(r)];
                g = g_curve_toe[clamp_u8(g)];
                b = g_curve_toe[clamp_u8(b)];
                break;

            case STOCK_FUJI_PROVIA:
                r = (g_curve_shoulder[i] * 246) >> 8;
                g = (g_curve_shoulder[i] * 271) >> 8;
                b = (g_curve_shoulder[i] * 276) >> 8;
                break;

            case STOCK_KODAK_GOLD:
                r = ((i * 294) >> 8) + 8;
                g = ((i * 266) >> 8) + 4;
                b = (i * 225) >> 8;
                r = g_curve_toe[clamp_u8(r)];
                g = g_curve_toe[clamp_u8(g)];
                b = g_curve_toe[clamp_u8(b)];
                break;

            case STOCK_TRI_X_400:
            default:
                r = g = b = g_curve_shoulder[i];
                break;
        }
        r_lut[i] = clamp_u8(r);
        g_lut[i] = clamp_u8(g);
        b_lut[i] = clamp_u8(b);
    }

    int grain_amp = 0;
    if (cfg->grain_level == 1) grain_amp = 12;
    else if (cfg->grain_level == 2) grain_amp = 22;
    else if (cfg->grain_level == 3) grain_amp = 38;

    int total_pixels = w * h;
    int is_bw = (cfg->stock == STOCK_TRI_X_400);
    int is_provia = (cfg->stock == STOCK_FUJI_PROVIA);
    int is_cinestill = (cfg->stock == STOCK_CINESTILL_800T);

    for (int i = 0; i < total_pixels; i++) {
        uint8_t *p = pixels + i * 3;
        int r0 = p[0], g0 = p[1], b0 = p[2];
        int lum = (77 * r0 + 150 * g0 + 29 * b0) >> 8;

        int r, g, b;
        if (is_bw) {
            r = g = b = r_lut[lum];
        } else {
            r = r_lut[r0];
            g = g_lut[g0];
            b = b_lut[b0];

            if (is_provia) {
                int l2 = (77 * r + 150 * g + 29 * b) >> 8;
                r = l2 + (((r - l2) * 307) >> 8);
                g = l2 + (((g - l2) * 307) >> 8);
                b = l2 + (((b - l2) * 307) >> 8);
            } else if (is_cinestill && lum > 195) {
                r += ((lum - 195) * 160) >> 8;
            }
        }

        if (grain_amp > 0) {
            int bell = g_grain_bell[lum];
            int rnd = (int)(xorshift32() & 0xFF) - 128;
            int noise = (rnd * grain_amp * bell) >> 15;
            r += noise;
            g += noise;
            b += noise;
        }

        p[0] = clamp_u8(r);
        p[1] = clamp_u8(g);
        p[2] = clamp_u8(b);
    }

    if (cfg->halation && !is_bw) {
        int strength = is_cinestill ? 150 : 95;
        apply_halation_glow(pixels, w, h, strength);
    }
}

static size_t find_jpeg_eoi(const uint8_t *buf, size_t buf_len, size_t soi_pos) {
    if (soi_pos + 4 >= buf_len || buf[soi_pos] != 0xFF || buf[soi_pos + 1] != 0xD8)
        return 0;

    size_t pos = soi_pos + 2;
    int in_scan = 0;

    while (pos + 1 < buf_len) {
        if (!in_scan) {
            if (buf[pos] != 0xFF) {
                pos++;
                continue;
            }
            while (pos < buf_len && buf[pos] == 0xFF)
                pos++;
            if (pos >= buf_len) break;

            uint8_t marker = buf[pos++];
            if (marker == 0xD9) {
                return pos - soi_pos;
            }
            if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01 || marker == 0x00) {
                continue;
            }
            if (pos + 2 > buf_len) break;
            uint16_t seg_len = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
            if (seg_len < 2 || pos + seg_len > buf_len) break;
            pos += seg_len;
            if (marker == 0xDA) {
                in_scan = 1;
            }
        } else {
            if (buf[pos] == 0xFF && buf[pos + 1] == 0xD9) {
                return (pos + 2) - soi_pos;
            }
            pos++;
        }
    }
    return 0;
}

static uint8_t *extract_embedded_preview(const char *path, int is_arw, size_t *out_size) {
    *out_size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long file_size = ftell(f);
    if (file_size < 65536) {
        fclose(f);
        return NULL;
    }

    size_t scan_len = 0;
    long start_offset = 0;

    if (is_arw) {
        scan_len = (file_size > 3500000L) ? 3500000u : (size_t)file_size;
        start_offset = 0;
    } else {
        if (file_size < 1500000L) {
            fclose(f);
            return NULL;
        }
        scan_len = 1500000u;
        start_offset = file_size - (long)scan_len;
    }

    uint8_t *buf = (uint8_t *)malloc(scan_len);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    if (fseek(f, start_offset, SEEK_SET) != 0) {
        free(buf);
        fclose(f);
        return NULL;
    }
    size_t nread = fread(buf, 1, scan_len, f);
    fclose(f);

    if (nread < 32768) {
        free(buf);
        return NULL;
    }

    size_t best_start = 0;
    size_t best_len = 0;

    size_t i_start = is_arw ? 8 : 0;
    for (size_t i = i_start; i + 16 < nread; i++) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD8 && buf[i + 2] == 0xFF &&
            (buf[i + 3] == 0xE1 || buf[i + 3] == 0xE0 || buf[i + 3] == 0xDB)) {
            size_t jlen = find_jpeg_eoi(buf, nread, i);
            if (jlen > 40000 && jlen > best_len) {
                best_start = i;
                best_len = jlen;
                i += jlen - 1;
            }
        }
    }

    if (best_len > 40000) {
        uint8_t *jpeg = (uint8_t *)malloc(best_len);
        if (jpeg) {
            memcpy(jpeg, buf + best_start, best_len);
            *out_size = best_len;
        }
        free(buf);
        return jpeg;
    }

    free(buf);
    return NULL;
}

static void downsample_2x_inplace(uint8_t *pixels, int *w, int *h) {
    int nw = *w / 2;
    int nh = *h / 2;
    int stride = *w * 3;
    uint8_t *dst = pixels;

    for (int y = 0; y < nh; y++) {
        const uint8_t *r0 = pixels + (size_t)(y * 2) * (size_t)stride;
        const uint8_t *r1 = r0 + stride;
        for (int x = 0; x < nw; x++) {
            const uint8_t *p00 = r0 + x * 6;
            const uint8_t *p01 = p00 + 3;
            const uint8_t *p10 = r1 + x * 6;
            const uint8_t *p11 = p10 + 3;
            dst[0] = (uint8_t)((p00[0] + p01[0] + p10[0] + p11[0] + 2) >> 2);
            dst[1] = (uint8_t)((p00[1] + p01[1] + p10[1] + p11[1] + 2) >> 2);
            dst[2] = (uint8_t)((p00[2] + p01[2] + p10[2] + p11[2] + 2) >> 2);
            dst += 3;
        }
    }
    *w = nw;
    *h = nh;
}

static int is_already_processed(const char *rel_or_base) {
    char base[128];
    strip_extension(rel_or_base, base, sizeof(base));

    FILE *f = fopen(PROCESSED_LOG, "r");
    if (!f) return 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char lbase[128];
        strip_extension(line, lbase, sizeof(lbase));
        if (strcasecmp(lbase, base) == 0) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static void mark_processed(const char *rel_or_base) {
    char base[128];
    strip_extension(rel_or_base, base, sizeof(base));
    if (is_already_processed(base)) return;

    FILE *f = fopen(PROCESSED_LOG, "a");
    if (f) {
        fprintf(f, "%s\n", base);
        fflush(f);
        fclose(f);
    }
}

static uint8_t *decode_photo_path(const char *input_path, int full_res, int *w, int *h) {
    const char *ext = strrchr(input_path, '.');
    int is_arw = (ext && strcasecmp(ext, ".arw") == 0);
    int channels = 0;
    uint8_t *image_pixels = NULL;

    if (is_arw || !full_res) {
        size_t preview_sz = 0;
        uint8_t *preview_jpg = extract_embedded_preview(input_path, is_arw, &preview_sz);
        if (preview_jpg && preview_sz > 0) {
            image_pixels = stbi_load_from_memory(preview_jpg, (int)preview_sz, w, h, &channels, 3);
            free(preview_jpg);
        }
    }

    if (!image_pixels && !is_arw) {
        image_pixels = stbi_load(input_path, w, h, &channels, 3);
        if (image_pixels && !full_res && *w > 2400) {
            downsample_2x_inplace(image_pixels, w, h);
        }
    }
    return image_pixels;
}

typedef struct {
    char base_name[64];
    char arw_path[512];
    char jpg_path[512];
    time_t mtime;
} ShotEntry;

#define MAX_SHOTS 256

static void scan_shots_recursive(const char *dir_path, ShotEntry *shots, int *num_shots) {
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_shots_recursive(full_path, shots, num_shots);
        } else if (S_ISREG(st.st_mode) && st.st_size > 16384) {
            const char *ext = strrchr(entry->d_name, '.');
            if (!ext) continue;
            int is_arw = (strcasecmp(ext, ".arw") == 0);
            int is_jpg = (strcasecmp(ext, ".jpg") == 0);
            if (!is_arw && !is_jpg) continue;

            char base[64];
            strip_extension(entry->d_name, base, sizeof(base));

            int idx = -1;
            for (int i = 0; i < *num_shots; i++) {
                if (strcasecmp(shots[i].base_name, base) == 0) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) {
                if (*num_shots < MAX_SHOTS) {
                    idx = (*num_shots)++;
                    memset(&shots[idx], 0, sizeof(ShotEntry));
                    strncpy(shots[idx].base_name, base, sizeof(shots[idx].base_name) - 1);
                    shots[idx].mtime = st.st_mtime;
                } else {
                    // Replace oldest entry if full
                    int oldest = 0;
                    for (int i = 1; i < MAX_SHOTS; i++) {
                        if (strcasecmp(shots[i].base_name, shots[oldest].base_name) < 0)
                            oldest = i;
                    }
                    if (strcasecmp(base, shots[oldest].base_name) > 0) {
                        idx = oldest;
                        memset(&shots[idx], 0, sizeof(ShotEntry));
                        strncpy(shots[idx].base_name, base, sizeof(shots[idx].base_name) - 1);
                        shots[idx].mtime = st.st_mtime;
                    }
                }
            }
            if (idx >= 0) {
                if (is_arw) strncpy(shots[idx].arw_path, full_path, sizeof(shots[idx].arw_path) - 1);
                if (is_jpg) strncpy(shots[idx].jpg_path, full_path, sizeof(shots[idx].jpg_path) - 1);
                if (st.st_mtime > shots[idx].mtime) shots[idx].mtime = st.st_mtime;
            }
        }
    }
    closedir(d);
}

static int develop_shot(const ShotEntry *shot, const RitualConfig *cfg) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Developing %s (%s)...", shot->base_name, stock_name(cfg->stock));
    write_status(msg);

    int w = 0, h = 0;
    uint8_t *image_pixels = NULL;
    const char *used_src = NULL;

    // Prefer .ARW embedded preview when available (it sits in the first 1MB of the file),
    // with automatic fallback to .JPG (and vice-versa)
    if (shot->arw_path[0] != '\0') {
        image_pixels = decode_photo_path(shot->arw_path, cfg->full_res, &w, &h);
        if (image_pixels) used_src = shot->arw_path;
    }
    if (!image_pixels && shot->jpg_path[0] != '\0') {
        image_pixels = decode_photo_path(shot->jpg_path, cfg->full_res, &w, &h);
        if (image_pixels) used_src = shot->jpg_path;
    }

    if (!image_pixels) {
        snprintf(msg, sizeof(msg), "Decode failed: %s", shot->base_name);
        write_status(msg);
        mark_processed(shot->base_name);
        return 0;
    }

    printf("[Ritual] Decoded %s (%dx%d) -> applying %s (grain=%d, halation=%d)\n",
           used_src, w, h, stock_name(cfg->stock), cfg->grain_level, cfg->halation);
    fflush(stdout);

    apply_film_emulation(image_pixels, w, h, cfg);

    // Always write to /tmp/latest_ritual.jpg first (on /tmp, never fails), then copy/move to /mnt/RITUAL/
    int ok_tmp = stbi_write_jpg(LATEST_COPY_TMP, w, h, 3, image_pixels, 92);

    mkdir(RITUAL_DIR, 0777);
    char out_name[192], out_path[512];
    snprintf(out_name, sizeof(out_name), "%s_%s.JPG", shot->base_name, stock_suffix(cfg->stock));
    snprintf(out_path, sizeof(out_path), "%s/%s", RITUAL_DIR, out_name);

    int ok_mnt = stbi_write_jpg(out_path, w, h, 3, image_pixels, 92);
    stbi_image_free(image_pixels);

    mark_processed(shot->base_name);

    if (ok_mnt) {
        sync();
        snprintf(msg, sizeof(msg), "LUT Saved: %s", out_name);
        write_status(msg);
        return 1;
    } else if (ok_tmp) {
        snprintf(msg, sizeof(msg), "Saved to /tmp/latest_ritual.jpg (SD ro)");
        write_status(msg);
        return 1;
    } else {
        snprintf(msg, sizeof(msg), "Write failed: %s", out_name);
        write_status(msg);
        return 0;
    }
}

static int check_other_daemon_running(void) {
    FILE *f = fopen(PID_FILE, "r");
    if (!f) return 0;
    int old_pid = 0;
    if (fscanf(f, "%d", &old_pid) == 1 && old_pid > 0 && old_pid != (int)getpid()) {
        fclose(f);
        if (kill(old_pid, 0) == 0) {
            return old_pid;
        }
    } else {
        fclose(f);
    }
    return 0;
}

int main(int argc, char **argv) {
    (void)nice(10);

    // If invoked with `--latest`, if daemon is already running, signal it via TRIGGER_FILE and exit
    if (argc > 1 && (strcmp(argv[1], "--latest") == 0 || strcmp(argv[1], "latest") == 0)) {
        if (check_other_daemon_running() > 0) {
            FILE *tf = fopen(TRIGGER_FILE, "w");
            if (tf) {
                fprintf(tf, "1\n");
                fclose(tf);
            }
            return 0;
        }
        init_curves();
        if (!mount_sd_fresh()) {
            write_status("No SD card (/mnt/DCIM)");
            return 1;
        }
        RitualConfig cfg;
        load_config(&cfg, 1);
        ShotEntry *shots = (ShotEntry *)calloc(MAX_SHOTS, sizeof(ShotEntry));
        int num_shots = 0;
        scan_shots_recursive(DCIM_DIR, shots, &num_shots);
        int newest = -1;
        for (int i = 0; i < num_shots; i++) {
            if (newest < 0 || strcasecmp(shots[i].base_name, shots[newest].base_name) > 0)
                newest = i;
        }
        int rc = 1;
        if (newest >= 0) {
            rc = develop_shot(&shots[newest], &cfg) ? 0 : 1;
        } else {
            write_status("No photos found in /mnt/DCIM");
        }
        free(shots);
        unmount_sd();
        return rc;
    }

    // Single-instance daemon check
    int existing = check_other_daemon_running();
    if (existing > 0) {
        printf("[Ritual] Daemon already running (PID %d), exiting duplicate.\n", existing);
        return 0;
    }

    FILE *pf = fopen(PID_FILE, "w");
    if (pf) {
        fprintf(pf, "%d\n", (int)getpid());
        fclose(pf);
    }

    init_curves();
    discover_sd_threads();

    // Baseline existing photos on SD card at startup, then UNMOUNT /mnt so im.elf has exclusive SD access while shooting!
    if (mount_sd_fresh()) {
        RitualConfig cfg;
        load_config(&cfg, 1);
        ShotEntry *shots = (ShotEntry *)calloc(MAX_SHOTS, sizeof(ShotEntry));
        int num_shots = 0;
        scan_shots_recursive(DCIM_DIR, shots, &num_shots);

        int newest = -1;
        for (int i = 0; i < num_shots; i++) {
            if (newest < 0 || strcasecmp(shots[i].base_name, shots[newest].base_name) > 0)
                newest = i;
        }
        int baselined = 0;
        for (int i = 0; i < num_shots; i++) {
            if (!is_already_processed(shots[i].base_name)) {
                mark_processed(shots[i].base_name);
                baselined++;
            }
        }
        if (baselined > 0) {
            printf("[Ritual] Baselined %d pre-existing shots (latest: %s)\n",
                   baselined, newest >= 0 ? shots[newest].base_name : "none");
            fflush(stdout);
        }
        free(shots);
        unmount_sd();
    }

    write_status("Ritual active (take a photo or press OK)");

    uint64_t last_io = get_sd_io_activity();
    int sd_activity_pending = 0;
    int quiet_ticks = 0;

    while (1) {
        usleep(500000); // 500ms tick

        int manual_trigger = (access(TRIGGER_FILE, F_OK) == 0);
        uint64_t cur_io = get_sd_io_activity();
        uint64_t delta = (cur_io >= last_io) ? (cur_io - last_io) : 0;
        last_io = cur_io;

        if (delta > 5) {
            // SD card is actively being written by im.elf (photo capture in progress)
            sd_activity_pending = 1;
            quiet_ticks = 0;
            continue;
        }

        if (sd_activity_pending) {
            quiet_ticks++;
        }

        // Wait until SD card has been completely quiet for 2.0 seconds (4 x 500ms) after write activity
        if (manual_trigger || (sd_activity_pending && quiet_ticks >= 4)) {
            if (manual_trigger) {
                unlink(TRIGGER_FILE);
            }
            sd_activity_pending = 0;
            quiet_ticks = 0;

            if (mount_sd_fresh()) {
                RitualConfig cfg;
                load_config(&cfg, 1);

                // Sync /tmp/RITUAL.ini to /mnt/RITUAL.ini while mounted
                FILE *ft = fopen(CONFIG_PATH_TMP, "r");
                if (ft) {
                    char cbuf[512];
                    size_t cn = fread(cbuf, 1, sizeof(cbuf), ft);
                    fclose(ft);
                    if (cn > 0) {
                        FILE *fm = fopen(CONFIG_PATH_MNT, "w");
                        if (fm) {
                            fwrite(cbuf, 1, cn, fm);
                            fflush(fm);
                            fsync(fileno(fm));
                            fclose(fm);
                        }
                    }
                }

                ShotEntry *shots = (ShotEntry *)calloc(MAX_SHOTS, sizeof(ShotEntry));
                int num_shots = 0;
                scan_shots_recursive(DCIM_DIR, shots, &num_shots);

                if (manual_trigger) {
                    // Develop the single newest photo on the card with current LUT
                    int newest = -1;
                    for (int i = 0; i < num_shots; i++) {
                        if (newest < 0 || strcasecmp(shots[i].base_name, shots[newest].base_name) > 0)
                            newest = i;
                    }
                    if (newest >= 0) {
                        develop_shot(&shots[newest], &cfg);
                    } else {
                        write_status("No photos found in /mnt/DCIM");
                    }
                } else {
                    // Develop all newly taken unprocessed shots
                    for (int i = 0; i < num_shots; i++) {
                        if (!is_already_processed(shots[i].base_name)) {
                            develop_shot(&shots[i], &cfg);
                        }
                    }
                }

                free(shots);
                unmount_sd();
            }

            // Re-baseline SD I/O counter after our own mount/unmount completes
            usleep(200000);
            last_io = get_sd_io_activity();
        }
    }

    return 0;
}
