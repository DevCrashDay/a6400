#include "fb.h"
#include "font.h"

int fb_open(struct fb *f, const char *dev, int writable)
{
    void *m;
    int fl = writable ? O_RDWR : O_RDONLY;

    memset(f, 0, sizeof(*f));
    f->fd = -1;
    if (!dev || !*dev)
        dev = "/dev/fb0";

    f->fd = sys_open(dev, fl, 0);
    if (f->fd < 0)
        return f->fd;

    if (sys_ioctl(f->fd, FBIOGET_VSCREENINFO, &f->var) < 0 ||
        sys_ioctl(f->fd, FBIOGET_FSCREENINFO, &f->fix) < 0) {
        sys_close(f->fd);
        f->fd = -1;
        return -1;
    }

    f->bpp = (f->var.bits_per_pixel + 7) / 8;
    if (f->bpp == 0)
        f->bpp = 4;
    if (f->var.xres == 0)
        f->var.xres = 640;
    if (f->var.yres == 0)
        f->var.yres = 480;

    f->stride = f->fix.line_length ? f->fix.line_length : f->var.xres * f->bpp;
    f->map_len = f->fix.smem_len ? f->fix.smem_len
                                 : f->stride * (f->var.yres_virtual ? f->var.yres_virtual : f->var.yres);

    m = sys_mmap(NULL, f->map_len, PROT_READ | (writable ? PROT_WRITE : 0), MAP_SHARED, f->fd, 0);
    if (!is_err((long)m))
        f->map = (u8 *)m;
    else
        f->map = NULL;

    return 0;
}

void fb_print_info(struct fb *f, int fd)
{
    fdprintf(fd, "fb: id='%s' res=%ux%u (virt %ux%u+%u+%u) bpp=%u (%uB) stride=%u smem=%u io=%s\n",
             f->fix.id,
             f->var.xres, f->var.yres,
             f->var.xres_virtual, f->var.yres_virtual,
             f->var.xoffset, f->var.yoffset,
             f->var.bits_per_pixel, f->bpp,
             f->stride, f->fix.smem_len,
             f->map ? "mmap" : "lseek+rw");
    fdprintf(fd, "fb: rgba=%u/%u,%u/%u,%u/%u,%u/%u\n",
             f->var.red.offset, f->var.red.length,
             f->var.green.offset, f->var.green.length,
             f->var.blue.offset, f->var.blue.length,
             f->var.transp.offset, f->var.transp.length);
}

int fb_read_rect(struct fb *f, u8 *dst)
{
    u32 r, row_bytes;
    if (!dst || f->rw == 0 || f->rh == 0)
        return -1;
    row_bytes = f->rw * f->bpp;
    for (r = 0; r < f->rh; r++) {
        size_t off = (size_t)(f->var.yoffset + f->ry + r) * f->stride +
                     (size_t)(f->var.xoffset + f->rx) * f->bpp;
        u8 *row_dst = dst + (size_t)r * row_bytes;
        if (f->map && off + row_bytes <= f->map_len) {
            memcpy(row_dst, f->map + off, row_bytes);
        } else {
            if (sys_lseek(f->fd, (long)off, 0) < 0)
                return -1;
            if (sys_read(f->fd, row_dst, row_bytes) != (ssize_t)row_bytes)
                return -1;
        }
    }
    return 0;
}

int fb_claim(struct fb *f, u32 x, u32 y, u32 w, u32 h)
{
    size_t sz;
    if (f->back) {
        xfree(f->back, (size_t)f->rw * f->rh * f->bpp);
        f->back = NULL;
    }
    if (f->saved) {
        xfree(f->saved, (size_t)f->rw * f->rh * f->bpp);
        f->saved = NULL;
    }
    f->have_saved = 0;

    if (x >= f->var.xres)
        x = 0;
    if (y >= f->var.yres)
        y = 0;
    if (x + w > f->var.xres)
        w = f->var.xres - x;
    if (y + h > f->var.yres)
        h = f->var.yres - y;
    if (w == 0 || h == 0)
        return -1;

    f->rx = x;
    f->ry = y;
    f->rw = w;
    f->rh = h;

    sz = (size_t)w * h * f->bpp;
    f->back = (u8 *)xalloc(sz);
    f->saved = (u8 *)xalloc(sz);
    if (!f->back || !f->saved) {
        if (f->back)
            xfree(f->back, sz);
        if (f->saved)
            xfree(f->saved, sz);
        f->back = f->saved = NULL;
        return -1;
    }

    if (fb_read_rect(f, f->saved) == 0) {
        f->have_saved = 1;
        memcpy(f->back, f->saved, sz);
    } else {
        memset(f->saved, 0, sz);
        memset(f->back, 0, sz);
    }
    return 0;
}

int fb_present(struct fb *f)
{
    u32 r, row_bytes;
    if (!f->back || f->rw == 0 || f->rh == 0)
        return -1;
    row_bytes = f->rw * f->bpp;
    for (r = 0; r < f->rh; r++) {
        size_t off = (size_t)(f->var.yoffset + f->ry + r) * f->stride +
                     (size_t)(f->var.xoffset + f->rx) * f->bpp;
        const u8 *row_src = f->back + (size_t)r * row_bytes;
        if (f->map && off + row_bytes <= f->map_len) {
            memcpy(f->map + off, row_src, row_bytes);
        } else {
            if (sys_lseek(f->fd, (long)off, 0) < 0)
                return -1;
            if (sys_write(f->fd, row_src, row_bytes) != (ssize_t)row_bytes)
                return -1;
        }
    }
    return 0;
}

void fb_restore(struct fb *f)
{
    if (f->have_saved && f->saved && f->back) {
        size_t sz = (size_t)f->rw * f->rh * f->bpp;
        memcpy(f->back, f->saved, sz);
        fb_present(f);
    }
}

void fb_close(struct fb *f)
{
    size_t sz = (size_t)f->rw * f->rh * f->bpp;
    if (f->back)
        xfree(f->back, sz);
    if (f->saved)
        xfree(f->saved, sz);
    if (f->map && f->map_len)
        sys_munmap(f->map, f->map_len);
    if (f->fd >= 0)
        sys_close(f->fd);
    memset(f, 0, sizeof(*f));
    f->fd = -1;
}

static inline u32 pack_chan(u8 v, const struct fb_bitfield *bf)
{
    if (bf->length == 0 || bf->length > 8)
        return 0;
    return ((u32)(v >> (8 - bf->length))) << bf->offset;
}

u32 fb_rgb(struct fb *f, u8 r, u8 g, u8 b)
{
    if (f->var.red.length || f->var.green.length || f->var.blue.length) {
        u32 px = pack_chan(r, &f->var.red) |
                 pack_chan(g, &f->var.green) |
                 pack_chan(b, &f->var.blue);
        if (f->var.transp.length && f->var.transp.length <= 8)
            px |= ((1u << f->var.transp.length) - 1u) << f->var.transp.offset;
        return px;
    }
    if (f->bpp == 2)
        return ((u32)(r >> 3) << 11) | ((u32)(g >> 2) << 5) | (u32)(b >> 3);
    return 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

static inline void put_px(struct fb *f, int x, int y, u32 px)
{
    u8 *p;
    if (x < 0 || y < 0 || (u32)x >= f->rw || (u32)y >= f->rh)
        return;
    p = f->back + ((size_t)y * f->rw + (size_t)x) * f->bpp;
    if (f->bpp == 4) {
        *(u32 *)p = px;
    } else if (f->bpp == 2) {
        *(u16 *)p = (u16)px;
    } else if (f->bpp == 3) {
        p[0] = (u8)(px & 0xff);
        p[1] = (u8)((px >> 8) & 0xff);
        p[2] = (u8)((px >> 16) & 0xff);
    } else if (f->bpp == 1) {
        p[0] = (u8)px;
    }
}

void gfx_fill(struct fb *f, int x, int y, int w, int h, u32 px)
{
    int x0, y0, x1, y1, r, c;
    if (!f->back || w <= 0 || h <= 0)
        return;
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w > (int)f->rw ? (int)f->rw : x + w;
    y1 = y + h > (int)f->rh ? (int)f->rh : y + h;
    if (x0 >= x1 || y0 >= y1)
        return;

    if (f->bpp == 4) {
        for (r = y0; r < y1; r++) {
            u32 *row = (u32 *)(f->back + ((size_t)r * f->rw + (size_t)x0) * 4);
            for (c = x0; c < x1; c++)
                *row++ = px;
        }
    } else if (f->bpp == 2) {
        u16 p16 = (u16)px;
        for (r = y0; r < y1; r++) {
            u16 *row = (u16 *)(f->back + ((size_t)r * f->rw + (size_t)x0) * 2);
            for (c = x0; c < x1; c++)
                *row++ = p16;
        }
    } else {
        for (r = y0; r < y1; r++)
            for (c = x0; c < x1; c++)
                put_px(f, c, r, px);
    }
}

void gfx_frame(struct fb *f, int x, int y, int w, int h, int t, u32 px)
{
    if (w <= 0 || h <= 0 || t <= 0)
        return;
    gfx_fill(f, x, y, w, t, px);
    gfx_fill(f, x, y + h - t, w, t, px);
    gfx_fill(f, x, y + t, t, h - 2 * t, px);
    gfx_fill(f, x + w - t, y + t, t, h - 2 * t, px);
}

int gfx_text_w(const char *s, int scale)
{
    if (!s)
        return 0;
    if (scale < 1)
        scale = 1;
    return (int)strlen(s) * FONT_W * scale;
}

int gfx_font_h(int scale)
{
    if (scale < 1)
        scale = 1;
    return FONT_H * scale;
}

int gfx_text(struct fb *f, int x, int y, const char *s, u32 fg, int scale, int maxw)
{
    int cx = 0, cw, gy, gx, sy, sx;
    if (!f->back || !s)
        return 0;
    if (scale < 1)
        scale = 1;
    cw = FONT_W * scale;

    for (; *s; s++) {
        u8 ch = (u8)*s;
        const unsigned short *glyph;
        if (maxw > 0 && cx + cw > maxw)
            break;
        if (ch < 32 || ch > 126)
            ch = '?';
        glyph = font_bits[ch - 32];
        for (gy = 0; gy < FONT_H; gy++) {
            unsigned short bits = glyph[gy];
            if (!bits)
                continue;
            for (gx = 0; gx < FONT_W; gx++) {
                if (bits & (1u << (15 - gx))) {
                    int px_x = x + cx + gx * scale;
                    int px_y = y + gy * scale;
                    if (scale == 1) {
                        put_px(f, px_x, px_y, fg);
                    } else {
                        for (sy = 0; sy < scale; sy++)
                            for (sx = 0; sx < scale; sx++)
                                put_px(f, px_x + sx, px_y + sy, fg);
                    }
                }
            }
        }
        cx += cw;
    }
    return cx;
}
