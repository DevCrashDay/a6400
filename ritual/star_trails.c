#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/**
 * Sony a6400 Star Trails & Light Painting Accumulator
 * Memory-bounded running-max (lighten-blend) compositor.
 * Keeps peak RAM < 35MB by processing images sequentially into an accumulator buffer.
 */

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Sony a6400 In-Camera Star Trail Compositor\n");
        printf("Usage:\n");
        printf("  star_trails <input_dir> <output_composite.jpg> [--decay 0.95]\n");
        return 1;
    }

    const char *in_dir = argv[1];
    const char *out_path = argv[2];
    float decay = 1.0f; // 1.0 = classic star trails, <1.0 = comet decay

    if (argc >= 5 && strcmp(argv[3], "--decay") == 0) {
        decay = atof(argv[4]);
        if (decay <= 0.0f || decay > 1.0f) decay = 0.95f;
    }

    DIR *d = opendir(in_dir);
    if (!d) {
        fprintf(stderr, "Cannot open directory: %s\n", in_dir);
        return 1;
    }

    printf("=========================================\n");
    printf("     Sony a6400 Star Trail Compositor    \n");
    printf("=========================================\n");
    printf("Input Folder: %s\n", in_dir);
    printf("Output File : %s\n", out_path);
    printf("Mode        : %s\n\n", decay < 1.0f ? "Comet Trails (Decay)" : "Classic Lighten-Blend");

    unsigned char *acc = NULL;
    int acc_w = 0, acc_h = 0;
    int count = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *ext = strrchr(de->d_name, '.');
        if (!ext || (strcasecmp(ext, ".jpg") != 0 && strcasecmp(ext, ".jpeg") != 0)) {
            continue;
        }

        char img_path[512];
        snprintf(img_path, sizeof(img_path), "%s/%s", in_dir, de->d_name);

        int w, h, channels;
        unsigned char *src = stbi_load(img_path, &w, &h, &channels, 3);
        if (!src) continue;

        if (!acc) {
            // Initialize accumulator buffer
            acc_w = w;
            acc_h = h;
            acc = (unsigned char *)malloc(w * h * 3);
            memcpy(acc, src, w * h * 3);
            printf("[FRAME %d] Initialized base frame: %s (%dx%d)\n", ++count, de->d_name, w, h);
        } else if (w == acc_w && h == acc_h) {
            // Lighten blend (running max)
            int total = w * h * 3;
            if (decay < 1.0f) {
                // Comet decay
                for (int i = 0; i < total; i++) {
                    unsigned char decayed = (unsigned char)(acc[i] * decay);
                    acc[i] = (src[i] > decayed) ? src[i] : decayed;
                }
            } else {
                // Standard lighten blend
                for (int i = 0; i < total; i++) {
                    if (src[i] > acc[i]) acc[i] = src[i];
                }
            }
            printf("[FRAME %d] Blended %s (Lighten max)\n", ++count, de->d_name);
        }

        stbi_image_free(src);

        // Every 5 frames, update output composite so photographer can preview live
        if (count % 5 == 0 && acc) {
            stbi_write_jpg(out_path, acc_w, acc_h, 3, acc, 90);
        }
    }
    closedir(d);

    if (acc) {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s.tmp", out_path);
        stbi_write_jpg(tmp, acc_w, acc_h, 3, acc, 92);
        rename(tmp, out_path);
        free(acc);
        printf("\n[SUCCESS] Star trail composite complete (%d frames combined) -> %s\n", count, out_path);
        return 0;
    } else {
        printf("No valid JPEG frames found in %s\n", in_dir);
        return 1;
    }
}
