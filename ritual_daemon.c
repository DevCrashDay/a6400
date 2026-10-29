#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
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

// Configuration paths
#define CONFIG_PATH_MNT   "/mnt/RITUAL.ini"
#define CONFIG_PATH_TMP   "/tmp/RITUAL.ini"
#define PROCESSED_LOG     "/mnt/RITUAL/.processed.log"
#define RITUAL_DIR        "/mnt/RITUAL"
#define DCIM_DIR          "/mnt/DCIM"
#define STATUS_FILE       "/tmp/ritual_status.txt"

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

// Flush Linux VFS dentry/inode/page caches and invalidate /dev/mmca1 buffer cache
// so new files written by Sony's im.elf (bdev_mmca) appear immediately in /mnt/DCIM.
static void refresh_sd_cache(void) {
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

static void ensure_sd_mounted(void) {
    struct stat st;
    if (stat(DCIM_DIR, &st) == 0 && S_ISDIR(st.st_mode))
        return;
    mkdir("/mnt", 0777);
    if (mount("/dev/mmca1", "/mnt", "vfat", 0, NULL) != 0) {
        (void)system("mount -t vfat /dev/mmca1 /mnt 2>/dev/null");
    }
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

static void load_config(RitualConfig *cfg) {
    cfg->stock = STOCK_PORTRA_400;
    cfg->grain_level = 2;
    cfg->halation = 1;
    cfg->full_res = 0;

    // Load from /mnt/RITUAL.ini first, then let /tmp/RITUAL.ini override if newer/present
    int have_mnt = parse_config_file(CONFIG_PATH_MNT, cfg);
    int have_tmp = parse_config_file(CONFIG_PATH_TMP, cfg);

    if (!have_mnt && !have_tmp) {
        FILE *fw = fopen(CONFIG_PATH_MNT, "w");
        if (fw) {
            fprintf(fw,
                "# Sony a6400 The Ritual Configuration\n"
                "STOCK=PORTRA_400\n"
                "GRAIN=MEDIUM\n"
                "HALATION=ON\n");
            fflush(fw);
            fsync(fileno(fw));
            fclose(fw);
        }
    }
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

    // Extract highlight threshold mask on 4x4 grid
    for (int y = 0; y < gh; y++) {
        const uint8_t *row = pixels + (size_t)(y * 4) * (size_t)w * 3;
        for (int x = 0; x < gw; x++) {
            const uint8_t *p = row + (size_t)(x * 4) * 3;
            int lum = (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
            mask[y * gw + x] = (lum > 195) ? (uint8_t)((lum - 195) * 4) : 0;
        }
    }

    // Two-pass separable box blur (radius 3 on 1/4 grid = 12px effective glow radius)
    const int rad = 3;
    for (int pass = 0; pass < 2; pass++) {
        // Horizontal blur: mask -> tmp
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
        // Vertical blur: tmp -> mask
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

    // Blend warm red-orange halation back into full-resolution image
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
                int dg = (dr * 90) >> 8; // warm orange-red ratio
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
                // Warm golden skin tones, lifted shadow toe
                r = (i * 276) >> 8; r += 6;
                g = (i * 261) >> 8; g += 2;
                b = (i * 241) >> 8; b += 4;
                r = g_curve_toe[clamp_u8(r)];
                g = g_curve_toe[clamp_u8(g)];
                b = g_curve_toe[clamp_u8(b)];
                break;

            case STOCK_CINESTILL_800T:
                // Tungsten cool shadows/mids, high contrast shoulder
                r = (i * 243) >> 8;
                g = (i * 251) >> 8;
                b = ((i * 286) >> 8) + 5;
                r = g_curve_toe[clamp_u8(r)];
                g = g_curve_toe[clamp_u8(g)];
                b = g_curve_toe[clamp_u8(b)];
                break;

            case STOCK_FUJI_PROVIA:
                // Rich slide contrast, vibrant greens & blues
                r = (g_curve_shoulder[i] * 246) >> 8;
                g = (g_curve_shoulder[i] * 271) >> 8;
                b = (g_curve_shoulder[i] * 276) >> 8;
                break;

            case STOCK_KODAK_GOLD:
                // Classic warm vintage gold
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

    // Grain amplitude in integer units (0, 12, 22, 38)
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
                // Extra 20% saturation boost for Fuji Provia slide film
                int l2 = (77 * r + 150 * g + 29 * b) >> 8;
                r = l2 + (((r - l2) * 307) >> 8);
                g = l2 + (((g - l2) * 307) >> 8);
                b = l2 + (((b - l2) * 307) >> 8);
            } else if (is_cinestill && lum > 195) {
                // Warm highlight shift on CineStill 800T
                r += ((lum - 195) * 160) >> 8;
            }
        }

        if (grain_amp > 0) {
            int bell = g_grain_bell[lum];
            int rnd = (int)(xorshift32() & 0xFF) - 128; // -128..127
            int noise = (rnd * grain_amp * bell) >> 15;
            r += noise;
            g += noise;
            b += noise;
        }

        p[0] = clamp_u8(r);
        p[1] = clamp_u8(g);
        p[2] = clamp_u8(b);
    }

    // Apply spatial highlight halation bloom if enabled (stronger for CineStill 800T)
    if (cfg->halation && !is_bw) {
        int strength = is_cinestill ? 150 : 95;
        apply_halation_glow(pixels, w, h, strength);
    }
}

// Find the true end (FF D9) of a JPEG starting at `buf + soi_pos` by stepping over
// APPn/COM/DQT/DHT/SOF headers using their 2-byte length until SOS (FF DA),
// then finding the first unstuffed FF D9 after SOS.
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
                // Entered entropy-coded scan; scan for FF D9
                in_scan = 1;
            }
        } else {
            if (buf[pos] == 0xFF) {
                uint8_t next = buf[pos + 1];
                if (next == 0xD9) {
                    return (pos + 2) - soi_pos;
                }
            }
            pos++;
        }
    }
    return 0;
}

// Extract the embedded 1616x1080 preview JPEG from a Sony .ARW file (within first 3.5MB)
// or the embedded 1616x1080 MPF preview JPEG from the tail of a Sony .JPG file.
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
        // Sony ARW stores the 1616x1080 preview JPEG within the first 3.5 MB
        scan_len = (file_size > 3500000L) ? 3500000u : (size_t)file_size;
        start_offset = 0;
    } else {
        // Sony camera .JPG (MPF format) appends a 1616x1080 preview JPEG in the last ~1.5 MB
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

    fseek(f, start_offset, SEEK_SET);
    size_t nread = fread(buf, 1, scan_len, f);
    fclose(f);

    if (nread < 32768) {
        free(buf);
        return NULL;
    }

    size_t best_start = 0;
    size_t best_len = 0;

    // Scan for JPEG SOI markers (FF D8 FF E0/E1/DB)
    size_t i_start = is_arw ? 8 : 0;
    for (size_t i = i_start; i + 16 < nread; i++) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD8 && buf[i + 2] == 0xFF &&
            (buf[i + 3] == 0xE1 || buf[i + 3] == 0xE0 || buf[i + 3] == 0xDB)) {
            size_t jlen = find_jpeg_eoi(buf, nread, i);
            // Embedded 1616x1080 preview is typically 150 KB .. 2.5 MB (thumbnails are < 30 KB)
            if (jlen > 40000 && jlen > best_len) {
                best_start = i;
                best_len = jlen;
                // Jump past this JPEG stream so we don't re-scan its interior
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

// Fast 2x2 box-filter downsample in-place when decoding a huge primary JPEG without MPF preview
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

static int is_already_processed(const char *rel_name) {
    FILE *f = fopen(PROCESSED_LOG, "r");
    if (!f) return 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strcasecmp(line, rel_name) == 0) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static void mark_processed(const char *rel_name) {
    if (is_already_processed(rel_name)) return;
    FILE *f = fopen(PROCESSED_LOG, "a");
    if (f) {
        fprintf(f, "%s\n", rel_name);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }
}

// Clock-independent check that a newly created photo has finished flushing to the SD card
static int wait_file_stable(const char *full_path) {
    struct stat st1, st2;
    if (stat(full_path, &st1) != 0 || st1.st_size < 16384)
        return 0;
    usleep(350000);
    refresh_sd_cache();
    if (stat(full_path, &st2) != 0 || st2.st_size != st1.st_size || st2.st_size < 16384)
        return 0;
    return 1;
}

static int develop_image(const char *input_path, const char *rel_name, const RitualConfig *cfg) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Developing %s (%s)...", rel_name, stock_name(cfg->stock));
    write_status(msg);

    const char *ext = strrchr(input_path, '.');
    int is_arw = (ext && strcasecmp(ext, ".arw") == 0);

    int w = 0, h = 0, channels = 0;
    uint8_t *image_pixels = NULL;

    // 1. Try fast embedded 1616x1080 preview first (always for .ARW, and for .JPG unless FULLRES=ON)
    if (is_arw || !cfg->full_res) {
        size_t preview_sz = 0;
        uint8_t *preview_jpg = extract_embedded_preview(input_path, is_arw, &preview_sz);
        if (preview_jpg && preview_sz > 0) {
            image_pixels = stbi_load_from_memory(preview_jpg, (int)preview_sz, &w, &h, &channels, 3);
            free(preview_jpg);
        }
    }

    // 2. Fallback to primary JPEG stream if not ARW and no MPF preview was extracted
    if (!image_pixels && !is_arw) {
        image_pixels = stbi_load(input_path, &w, &h, &channels, 3);
        if (image_pixels && !cfg->full_res && w > 2400) {
            downsample_2x_inplace(image_pixels, &w, &h);
        }
    }

    if (!image_pixels) {
        snprintf(msg, sizeof(msg), "Decode failed: %s", rel_name);
        write_status(msg);
        mark_processed(rel_name);
        return 0;
    }

    printf("[Ritual] Decoded %s: %dx%d -> applying %s (grain=%d, halation=%d)\n",
           rel_name, w, h, stock_name(cfg->stock), cfg->grain_level, cfg->halation);
    fflush(stdout);

    // Apply integer LUT + grain + halation
    apply_film_emulation(image_pixels, w, h, cfg);

    mkdir(RITUAL_DIR, 0777);

    char base_name[128];
    strncpy(base_name, rel_name, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char out_name[192];
    snprintf(out_name, sizeof(out_name), "%s_%s.JPG", base_name, stock_suffix(cfg->stock));

    char out_path[512], tmp_path[520];
    snprintf(out_path, sizeof(out_path), "%s/%s", RITUAL_DIR, out_name);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", out_path);

    int ok = stbi_write_jpg(tmp_path, w, h, 3, image_pixels, 92);
    stbi_image_free(image_pixels);

    if (ok) {
        rename(tmp_path, out_path);
        mark_processed(rel_name);
        sync();
        snprintf(msg, sizeof(msg), "LUT Saved: %s", out_name);
        write_status(msg);
        return 1;
    } else {
        unlink(tmp_path);
        snprintf(msg, sizeof(msg), "Write failed: %s", out_name);
        write_status(msg);
        return 0;
    }
}

typedef struct {
    char full_path[512];
    char rel_name[128];
    time_t mtime;
} PhotoCandidate;

static void collect_photos(const char *dir_path, int only_unprocessed,
                           PhotoCandidate *newest, int *count, int mark_older) {
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
            collect_photos(full_path, only_unprocessed, newest, count, mark_older);
        } else if (S_ISREG(st.st_mode) && st.st_size > 16384) {
            const char *ext = strrchr(entry->d_name, '.');
            if (!ext) continue;
            if (strcasecmp(ext, ".jpg") != 0 && strcasecmp(ext, ".arw") != 0)
                continue;

            if (only_unprocessed && is_already_processed(entry->d_name))
                continue;

            (*count)++;
            // Compare by filename (e.g. DSC00125 > DSC00124) and mtime; prefer .JPG over .ARW for same shot
            int is_newer = 0;
            if (newest->rel_name[0] == '\0') {
                is_newer = 1;
            } else {
                int cmp = strcasecmp(entry->d_name, newest->rel_name);
                if (st.st_mtime > newest->mtime || (st.st_mtime == newest->mtime && cmp > 0) || cmp > 0) {
                    is_newer = 1;
                }
            }

            if (is_newer) {
                if (mark_older && newest->rel_name[0] != '\0') {
                    mark_processed(newest->rel_name);
                }
                strncpy(newest->full_path, full_path, sizeof(newest->full_path) - 1);
                strncpy(newest->rel_name, entry->d_name, sizeof(newest->rel_name) - 1);
                newest->mtime = st.st_mtime;
            } else if (mark_older) {
                mark_processed(entry->d_name);
            }
        }
    }
    closedir(d);
}

static void scan_and_develop(const char *dir_path, const RitualConfig *cfg) {
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_and_develop(full_path, cfg);
            } else if (S_ISREG(st.st_mode)) {
                const char *ext = strrchr(entry->d_name, '.');
                if (ext && (strcasecmp(ext, ".arw") == 0 || strcasecmp(ext, ".jpg") == 0)) {
                    if (!is_already_processed(entry->d_name)) {
                        if (wait_file_stable(full_path)) {
                            develop_image(full_path, entry->d_name, cfg);
                        }
                    }
                }
            }
        }
    }
    closedir(d);
}

int main(int argc, char **argv) {
    // Lower CPU priority via syscall so live view & shooting are never impacted
    (void)nice(10);

    init_curves();
    ensure_sd_mounted();
    refresh_sd_cache();
    mkdir(RITUAL_DIR, 0777);

    RitualConfig cfg;
    load_config(&cfg);

    // One-shot mode: `ritual_daemon --latest` develops the most recent photo in /mnt/DCIM immediately
    if (argc > 1 && (strcmp(argv[1], "--latest") == 0 || strcmp(argv[1], "latest") == 0)) {
        PhotoCandidate newest;
        memset(&newest, 0, sizeof(newest));
        int count = 0;
        collect_photos(DCIM_DIR, 0, &newest, &count, 0);
        if (newest.rel_name[0] != '\0') {
            develop_image(newest.full_path, newest.rel_name, &cfg);
            return 0;
        } else {
            write_status("No photos found in /mnt/DCIM");
            return 1;
        }
    }

    write_status("Ritual daemon active (watching /mnt/DCIM)");

    // On initial daemon startup, if there is a backlog of old unprocessed photos on the SD card,
    // baseline the older ones so we don't spend 20 minutes processing old photos from weeks ago,
    // and only leave the single newest photo (plus any future photos taken) to be developed!
    {
        PhotoCandidate newest;
        memset(&newest, 0, sizeof(newest));
        int count = 0;
        collect_photos(DCIM_DIR, 1, &newest, &count, 1);
        if (count > 1 && newest.rel_name[0] != '\0') {
            printf("[Ritual] Baselined %d older pre-existing photos; keeping newest: %s\n",
                   count - 1, newest.rel_name);
            fflush(stdout);
        }
    }

    while (1) {
        ensure_sd_mounted();
        refresh_sd_cache();
        load_config(&cfg);

        struct stat st;
        if (stat(DCIM_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
            scan_and_develop(DCIM_DIR, &cfg);
        }

        sleep(2);
    }

    return 0;
}
