/*
 * timelapse_mux.c - In-Camera MJPEG AVI Video Assembler for Sony a6400
 *
 * Muxes JPEG sequences into a standard, universally playable AVI (MJPEG)
 * container with zero transcoding overhead (pure disk I/O speed, < 2MB RAM).
 *
 * Usage:
 *   timelapse_mux -i <input_dir_or_glob> -o <output.avi> [-fps 24] [--delete-source]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

typedef struct {
    uint32_t ckid;
    uint32_t flags;
    uint32_t offset;
    uint32_t length;
} AVIIndexEntry;

static int get_jpeg_dimensions(const char *path, uint32_t *width, uint32_t *height) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint8_t header[2];
    if (fread(header, 1, 2, f) != 2 || header[0] != 0xFF || header[1] != 0xD8) {
        fclose(f);
        return -1;
    }

    while (!feof(f)) {
        uint8_t marker[2];
        if (fread(marker, 1, 2, f) != 2) break;
        if (marker[0] != 0xFF) break;

        // Skip stuffing bytes
        while (marker[1] == 0xFF) {
            marker[1] = fgetc(f);
        }

        uint8_t m = marker[1];
        if (m == 0xD9 || m == 0xDA) break; // EOI or SOS

        uint8_t len_bytes[2];
        if (fread(len_bytes, 1, 2, f) != 2) break;
        uint16_t len = ((uint16_t)len_bytes[0] << 8) | len_bytes[1];
        if (len < 2) break;

        // SOF0 (0xC0), SOF1 (0xC1), SOF2 (0xC2)
        if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            uint8_t sof[5];
            if (fread(sof, 1, 5, f) == 5) {
                *height = ((uint32_t)sof[1] << 8) | sof[2];
                *width  = ((uint32_t)sof[3] << 8) | sof[4];
                fclose(f);
                return 0;
            }
            break;
        } else {
            fseek(f, len - 2, SEEK_CUR);
        }
    }

    fclose(f);
    return -1;
}

static int compare_strings(const void *a, const void *b) {
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    return strcmp(sa, sb);
}

int main(int argc, char **argv) {
    const char *input_dir = NULL;
    const char *output_file = NULL;
    int fps = 24;
    int delete_source = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            input_dir = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-fps") == 0 && i + 1 < argc) {
            fps = atoi(argv[++i]);
            if (fps <= 0 || fps > 120) fps = 24;
        } else if (strcmp(argv[i], "--delete-source") == 0) {
            delete_source = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: timelapse_mux -i <input_dir> -o <output.avi> [-fps 24] [--delete-source]\n");
            return 0;
        }
    }

    if (!input_dir || !output_file) {
        fprintf(stderr, "Error: Missing required arguments -i and -o\n");
        fprintf(stderr, "Usage: timelapse_mux -i <input_dir> -o <output.avi> [-fps 24] [--delete-source]\n");
        return 1;
    }

    DIR *d = opendir(input_dir);
    if (!d) {
        perror("opendir");
        return 1;
    }

    struct dirent *de;
    size_t file_cap = 1024;
    size_t file_count = 0;
    char **file_list = malloc(file_cap * sizeof(char *));

    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        const char *ext = strrchr(de->d_name, '.');
        if (!ext) continue;
        if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
            if (file_count >= file_cap) {
                file_cap *= 2;
                file_list = realloc(file_list, file_cap * sizeof(char *));
            }
            char path_buf[1024];
            snprintf(path_buf, sizeof(path_buf), "%s/%s", input_dir, de->d_name);
            file_list[file_count++] = strdup(path_buf);
        }
    }
    closedir(d);

    if (file_count == 0) {
        fprintf(stderr, "No JPEG images found in %s\n", input_dir);
        free(file_list);
        return 1;
    }

    qsort(file_list, file_count, sizeof(char *), compare_strings);
    printf("[+] Found %zu frames in %s\n", file_count, input_dir);

    uint32_t width = 0, height = 0;
    if (get_jpeg_dimensions(file_list[0], &width, &height) != 0) {
        fprintf(stderr, "Failed to read dimensions of first image: %s\n", file_list[0]);
        for (size_t i = 0; i < file_count; i++) free(file_list[i]);
        free(file_list);
        return 1;
    }
    printf("[+] Frame dimensions: %ux%u @ %d fps\n", width, height, fps);

    FILE *out = fopen(output_file, "wb+");
    if (!out) {
        perror("fopen output");
        for (size_t i = 0; i < file_count; i++) free(file_list[i]);
        free(file_list);
        return 1;
    }

    // Allocate index table
    AVIIndexEntry *idx = malloc(file_count * sizeof(AVIIndexEntry));
    if (!idx) {
        fclose(out);
        for (size_t i = 0; i < file_count; i++) free(file_list[i]);
        free(file_list);
        return 1;
    }

    // Placeholder RIFF header
    uint8_t zero_header[2048];
    memset(zero_header, 0, sizeof(zero_header));
    fwrite(zero_header, 1, 2048, out);

    // movi chunk starts at offset 2048
    long movi_start_offset = ftell(out);
    uint32_t movi_fourcc = FOURCC('L', 'I', 'S', 'T');
    uint32_t movi_size_placeholder = 0;
    uint32_t movi_type = FOURCC('m', 'o', 'v', 'i');
    fwrite(&movi_fourcc, 4, 1, out);
    fwrite(&movi_size_placeholder, 4, 1, out);
    fwrite(&movi_type, 4, 1, out);

    long movi_data_start = ftell(out);
    uint32_t max_frame_size = 0;
    char buffer[65536];

    for (size_t i = 0; i < file_count; i++) {
        FILE *in = fopen(file_list[i], "rb");
        if (!in) {
            fprintf(stderr, "Warning: Failed to open %s\n", file_list[i]);
            continue;
        }

        fseek(in, 0, SEEK_END);
        uint32_t jsize = (uint32_t)ftell(in);
        fseek(in, 0, SEEK_SET);

        if (jsize > max_frame_size) max_frame_size = jsize;

        long current_pos = ftell(out);
        idx[i].ckid = FOURCC('0', '0', 'd', 'c');
        idx[i].flags = 0x10; // AVIIF_KEYFRAME
        idx[i].offset = (uint32_t)(current_pos - movi_data_start);
        idx[i].length = jsize;

        uint32_t chunk_id = FOURCC('0', '0', 'd', 'c');
        fwrite(&chunk_id, 4, 1, out);
        fwrite(&jsize, 4, 1, out);

        size_t bytes_left = jsize;
        while (bytes_left > 0) {
            size_t to_read = bytes_left > sizeof(buffer) ? sizeof(buffer) : bytes_left;
            size_t n = fread(buffer, 1, to_read, in);
            if (n <= 0) break;
            fwrite(buffer, 1, n, out);
            bytes_left -= n;
        }
        fclose(in);

        // Word alignment padding if needed
        if (jsize % 2 != 0) {
            fputc(0, out);
        }

        if ((i + 1) % 50 == 0 || i + 1 == file_count) {
            printf("[+] Muxing progress: %zu / %zu frames (%.1f%%)\n",
                   i + 1, file_count, (float)(i + 1) * 100.0f / (float)file_count);
        }
    }

    long movi_end = ftell(out);
    uint32_t movi_size = (uint32_t)(movi_end - movi_start_offset - 8);

    // Write idx1 chunk
    uint32_t idx1_fourcc = FOURCC('i', 'd', 'x', '1');
    uint32_t idx1_size = (uint32_t)(file_count * sizeof(AVIIndexEntry));
    fwrite(&idx1_fourcc, 4, 1, out);
    fwrite(&idx1_size, 4, 1, out);
    fwrite(idx, sizeof(AVIIndexEntry), file_count, out);

    long file_size = ftell(out);
    uint32_t riff_size = (uint32_t)(file_size - 8);

    // Now write the real header at offset 0
    fseek(out, 0, SEEK_SET);

    uint32_t riff_id = FOURCC('R', 'I', 'F', 'F');
    uint32_t avi_id  = FOURCC('A', 'V', 'I', ' ');
    fwrite(&riff_id, 4, 1, out);
    fwrite(&riff_size, 4, 1, out);
    fwrite(&avi_id, 4, 1, out);

    // LIST 'hdrl'
    uint32_t list_id = FOURCC('L', 'I', 'S', 'T');
    uint32_t hdrl_size = 4 + (8 + 56) + (8 + (4 + (8 + 56) + (8 + 40)));
    uint32_t hdrl_id = FOURCC('h', 'd', 'r', 'l');
    fwrite(&list_id, 4, 1, out);
    fwrite(&hdrl_size, 4, 1, out);
    fwrite(&hdrl_id, 4, 1, out);

    // 'avih' (56 bytes)
    uint32_t avih_id = FOURCC('a', 'v', 'i', 'h');
    uint32_t avih_sz = 56;
    fwrite(&avih_id, 4, 1, out);
    fwrite(&avih_sz, 4, 1, out);

    uint32_t us_per_frame = 1000000 / fps;
    uint32_t max_bytes_sec = max_frame_size * fps;
    uint32_t flags = 0x10 | 0x800; // HASINDEX | TRUSTCKTYPE
    uint32_t total_frames = (uint32_t)file_count;
    uint32_t streams = 1;
    uint32_t zero = 0;

    fwrite(&us_per_frame, 4, 1, out);
    fwrite(&max_bytes_sec, 4, 1, out);
    fwrite(&zero, 4, 1, out); // padding granularity
    fwrite(&flags, 4, 1, out);
    fwrite(&total_frames, 4, 1, out);
    fwrite(&zero, 4, 1, out); // initial frames
    fwrite(&streams, 4, 1, out);
    fwrite(&max_frame_size, 4, 1, out);
    fwrite(&width, 4, 1, out);
    fwrite(&height, 4, 1, out);
    for (int k = 0; k < 4; k++) fwrite(&zero, 4, 1, out);

    // LIST 'strl'
    uint32_t strl_size = 4 + (8 + 56) + (8 + 40);
    uint32_t strl_id = FOURCC('s', 't', 'r', 'l');
    fwrite(&list_id, 4, 1, out);
    fwrite(&strl_size, 4, 1, out);
    fwrite(&strl_id, 4, 1, out);

    // 'strh' (56 bytes)
    uint32_t strh_id = FOURCC('s', 't', 'r', 'h');
    uint32_t strh_sz = 56;
    fwrite(&strh_id, 4, 1, out);
    fwrite(&strh_sz, 4, 1, out);

    uint32_t fccType = FOURCC('v', 'i', 'd', 's');
    uint32_t fccHandler = FOURCC('M', 'J', 'P', 'G');
    uint32_t scale = 1;
    uint32_t rate = fps;
    uint32_t quality = 0xFFFFFFFF; // -1

    fwrite(&fccType, 4, 1, out);
    fwrite(&fccHandler, 4, 1, out);
    fwrite(&zero, 4, 1, out); // flags
    fwrite(&zero, 4, 1, out); // priority, language
    fwrite(&zero, 4, 1, out); // initial frames
    fwrite(&scale, 4, 1, out);
    fwrite(&rate, 4, 1, out);
    fwrite(&zero, 4, 1, out); // start
    fwrite(&total_frames, 4, 1, out); // length
    fwrite(&max_frame_size, 4, 1, out);
    fwrite(&quality, 4, 1, out);
    fwrite(&zero, 4, 1, out); // sample size
    // rcFrame (16 bits each: left, top, right, bottom)
    uint16_t rc[4] = { 0, 0, (uint16_t)width, (uint16_t)height };
    fwrite(rc, 2, 4, out);

    // 'strf' (BITMAPINFOHEADER, 40 bytes)
    uint32_t strf_id = FOURCC('s', 't', 'r', 'f');
    uint32_t strf_sz = 40;
    fwrite(&strf_id, 4, 1, out);
    fwrite(&strf_sz, 4, 1, out);

    uint32_t biSize = 40;
    int32_t  biWidth = (int32_t)width;
    int32_t  biHeight = (int32_t)height;
    uint16_t biPlanes = 1;
    uint16_t biBitCount = 24;
    uint32_t biCompression = FOURCC('M', 'J', 'P', 'G');
    uint32_t biSizeImage = width * height * 3;
    uint32_t biXPels = 0, biYPels = 0, biClrUsed = 0, biClrImp = 0;

    fwrite(&biSize, 4, 1, out);
    fwrite(&biWidth, 4, 1, out);
    fwrite(&biHeight, 4, 1, out);
    fwrite(&biPlanes, 2, 1, out);
    fwrite(&biBitCount, 2, 1, out);
    fwrite(&biCompression, 4, 1, out);
    fwrite(&biSizeImage, 4, 1, out);
    fwrite(&biXPels, 4, 1, out);
    fwrite(&biYPels, 4, 1, out);
    fwrite(&biClrUsed, 4, 1, out);
    fwrite(&biClrImp, 4, 1, out);

    // Fill padding until offset 2048 with JUNK chunk
    long current_hdr_end = ftell(out);
    if (current_hdr_end < 2048) {
        uint32_t junk_id = FOURCC('J', 'U', 'N', 'K');
        uint32_t junk_sz = (uint32_t)(2048 - current_hdr_end - 8);
        fwrite(&junk_id, 4, 1, out);
        fwrite(&junk_sz, 4, 1, out);
        while (ftell(out) < 2048) {
            fputc(0, out);
        }
    }

    // Update movi size
    fseek(out, movi_start_offset + 4, SEEK_SET);
    fwrite(&movi_size, 4, 1, out);

    fclose(out);
    free(idx);

    printf("[+] Timelapse video saved: %s (%ld bytes)\n", output_file, file_size);

    if (delete_source) {
        printf("[+] Removing source frames (--delete-source specified)...\n");
        for (size_t i = 0; i < file_count; i++) {
            unlink(file_list[i]);
        }
    }

    for (size_t i = 0; i < file_count; i++) free(file_list[i]);
    free(file_list);

    return 0;
}
