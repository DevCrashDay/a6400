#ifndef FB_H
#define FB_H
#include "rt.h"

struct fb_bitfield {
    u32 offset, length, msb_right;
};

struct fb_var_screeninfo {
    u32 xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
    u32 bits_per_pixel, grayscale;
    struct fb_bitfield red, green, blue, transp;
    u32 nonstd, activate, height, width, accel_flags, pixclock;
    u32 left_margin, right_margin, upper_margin, lower_margin;
    u32 hsync_len, vsync_len, sync, vmode, rotate, colorspace;
    u32 reserved[4];
};

struct fb_fix_screeninfo {
    char id[16];
    unsigned long smem_start;
    u32 smem_len, type, type_aux, visual;
    u16 xpanstep, ypanstep, ywrapstep;
    u32 line_length;
    unsigned long mmio_start;
    u32 mmio_len, accel;
    u16 capabilities, reserved[2];
};

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

/* A rectangle of the screen we own. We draw into `back` (RAM) and
 * copy it to the device on present(); the pixels that were there before
 * are kept in `saved` so they can be put back on exit. */
struct fb {
    int fd;
    u8 *map;      /* pointer to start of framebuffer pixels */
    u8 *map_base; /* page-aligned base passed to sys_munmap */
    size_t map_len;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    u32 bpp; /* bytes per pixel */
    u32 stride;
    /* owned rectangle (visible-screen coordinates) */
    u32 rx, ry, rw, rh;
    u8 *back, *saved;
    int have_saved;
};

int fb_open(struct fb *f, const char *dev, int writable);
void fb_print_info(struct fb *f, int fd);
int fb_claim(struct fb *f, u32 x, u32 y, u32 w, u32 h);
int fb_read_rect(struct fb *f, u8 *dst);
int fb_present(struct fb *f);
void fb_restore(struct fb *f);
void fb_close(struct fb *f);

u32 fb_rgb(struct fb *f, u8 r, u8 g, u8 b);
void gfx_fill(struct fb *f, int x, int y, int w, int h, u32 px);
void gfx_frame(struct fb *f, int x, int y, int w, int h, int t, u32 px);
int gfx_text(struct fb *f, int x, int y, const char *s, u32 fg, int scale, int maxw);
int gfx_text_w(const char *s, int scale);
int gfx_font_h(int scale);

#endif
