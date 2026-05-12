/*
 * Comprehensive test suite for ezdib.
 *
 * Each test is a static function returning 0 (pass) or 1 (fail).
 * The test name is passed as argv[1]; the dispatch table at the bottom
 * maps names to functions so CTest can invoke them individually.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "ezdib.h"

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

#define FAIL(msg) \
    do { fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, (msg)); return 1; } while(0)

#define CHECK(cond, msg) \
    do { if (!(cond)) FAIL(msg); } while(0)

#define CHECKF(cond, fmt, ...) \
    do { if (!(cond)) { fprintf(stderr, "  FAIL line %d: " fmt "\n", __LINE__, __VA_ARGS__); return 1; } } while(0)

/* Path for temporary BMP files used by I/O tests */
#define TMP_BMP "/tmp/ezdib_test_%s.bmp"

static char tmp_path[256];
static void tmp(const char *tag) { snprintf(tmp_path, sizeof tmp_path, TMP_BMP, tag); }

static long avg_channel(HEZDIMAGE img, int x, int y, int w, int h, int shift)
{
    long sum = 0; int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            sum += (ezd_get_pixel(img, x+i, y+j) >> shift) & 0xff;
    return sum / (w * h);
}

static int count_set_pixels(HEZDIMAGE img, int x, int y, int w, int h)
{
    int n = 0, i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (ezd_get_pixel(img, x+i, y+j)) n++;
    return n;
}


/* =======================================================================
 * IMAGE LIFECYCLE
 * ===================================================================== */

static int test_create_24bpp(void)
{
    HEZDIMAGE img = ezd_create(320, -240, 24, 0);
    CHECK(img, "ezd_create returned NULL");
    CHECK(ezd_get_width(img)  == 320, "wrong width");
    CHECK(ezd_get_height(img) == -240, "wrong height (sign lost)");
    CHECK(ezd_get_bpp(img)    == 24, "wrong bpp");
    CHECK(ezd_get_image_size(img) > 0, "image size is zero");
    CHECK(ezd_get_image_ptr(img)  != NULL, "pixel buffer is NULL");
    ezd_destroy(img);
    return 0;
}

static int test_create_32bpp(void)
{
    HEZDIMAGE img = ezd_create(100, -100, 32, 0);
    CHECK(img, "ezd_create returned NULL");
    CHECK(ezd_get_bpp(img) == 32, "wrong bpp");
    /* 32bpp: image size = width * height * 4 */
    CHECK(ezd_get_image_size(img) == 100 * 100 * 4, "wrong image size");
    ezd_destroy(img);
    return 0;
}

static int test_create_1bpp(void)
{
    HEZDIMAGE img = ezd_create(64, -32, 1, 0);
    CHECK(img, "ezd_create returned NULL");
    CHECK(ezd_get_bpp(img) == 1, "wrong bpp");
    /* 1bpp scan line = ceil(64/8) = 8 bytes, padded to 4 = 8; height = 32 */
    CHECK(ezd_get_image_size(img) == 8 * 32, "wrong image size");
    ezd_destroy(img);
    return 0;
}

static int test_create_negative_height(void)
{
    /* Negative height = top-down; drawing uses same coordinates either way */
    HEZDIMAGE td = ezd_create(10, -10, 24, 0);
    HEZDIMAGE bu = ezd_create(10,  10, 24, 0);
    CHECK(td && bu, "create failed");
    /* Both accept set_pixel at (0,0) */
    CHECK(ezd_set_pixel(td, 0, 0, 0xff0000), "set_pixel top-down failed");
    CHECK(ezd_set_pixel(bu, 0, 0, 0xff0000), "set_pixel bottom-up failed");
    CHECK(ezd_get_pixel(td, 0, 0) == 0xff0000, "get_pixel top-down wrong");
    CHECK(ezd_get_pixel(bu, 0, 0) == 0xff0000, "get_pixel bottom-up wrong");
    ezd_destroy(td); ezd_destroy(bu);
    return 0;
}

static int test_create_user_buffer(void)
{
    char hdr[EZD_HEADER_SIZE];
    char pixels[64 * 32 * 3];
    HEZDIMAGE img = ezd_initialize(hdr, sizeof hdr, 64, -32, 24,
                                   EZD_FLAG_USER_IMAGE_BUFFER);
    CHECK(img, "ezd_initialize returned NULL");
    CHECK(ezd_set_image_buffer(img, pixels, sizeof pixels), "set_image_buffer failed");
    CHECK(ezd_set_pixel(img, 10, 10, 0x00ff00), "set_pixel failed");
    CHECK(ezd_get_pixel(img, 10, 10) == 0x00ff00, "get_pixel wrong");
    /* do NOT call ezd_destroy — we own the buffer */
    return 0;
}

static int test_create_overflow(void)
{
    /* Dimensions that would overflow int when computing image size */
    CHECK(ezd_create(0x7fffffff, 0x7fffffff, 32, 0) == NULL, "huge image should fail");
    CHECK(ezd_create(INT_MIN, -1, 24, 0) == NULL, "INT_MIN width should fail");
    CHECK(ezd_create(0, 10, 24, 0) == NULL, "zero width should fail");
    CHECK(ezd_create(10, 0, 24, 0) == NULL, "zero height should fail");
    CHECK(ezd_create(10, -10, 8, 0) == NULL, "unsupported bpp should fail");
    return 0;
}

static int test_header_size(void)
{
    /* Runtime value must fit within the static EZD_HEADER_SIZE guarantee */
    int s = ezd_header_size();
    CHECK(s > 0,               "header_size is zero");
    CHECK(s <= EZD_HEADER_SIZE, "header_size exceeds EZD_HEADER_SIZE constant");
    return 0;
}

static int test_save_load_24bpp(void)
{
    int i, j;
    HEZDIMAGE src = ezd_create(32, -32, 24, 0);
    HEZDIMAGE dst = NULL;
    CHECK(src, "create failed");
    /* Write a distinct colour pattern */
    for (j = 0; j < 32; j++)
        for (i = 0; i < 32; i++)
            ezd_set_pixel(src, i, j, (i * 8) | ((j * 8) << 8));
    tmp("24bpp");
    CHECK(ezd_save(src, tmp_path), "save failed");
    dst = ezd_load(tmp_path);
    CHECK(dst, "load returned NULL");
    /* Pixel-identical round-trip */
    for (j = 0; j < 32; j++)
        for (i = 0; i < 32; i++)
            CHECKF(ezd_get_pixel(src,i,j) == ezd_get_pixel(dst,i,j),
                   "pixel mismatch at (%d,%d)", i, j);
    ezd_destroy(src); ezd_destroy(dst);
    remove(tmp_path);
    return 0;
}

static int test_save_load_32bpp(void)
{
    HEZDIMAGE src = ezd_create(16, -16, 32, 0);
    HEZDIMAGE dst = NULL;
    int i, j;
    CHECK(src, "create failed");
    for (j = 0; j < 16; j++)
        for (i = 0; i < 16; i++)
            ezd_set_pixel(src, i, j, i | (j << 8) | (0x80 << 16));
    tmp("32bpp");
    CHECK(ezd_save(src, tmp_path), "save failed");
    dst = ezd_load(tmp_path);
    CHECK(dst, "load returned NULL");
    for (j = 0; j < 16; j++)
        for (i = 0; i < 16; i++)
            CHECKF(ezd_get_pixel(src,i,j) == ezd_get_pixel(dst,i,j),
                   "pixel mismatch at (%d,%d)", i, j);
    ezd_destroy(src); ezd_destroy(dst);
    remove(tmp_path);
    return 0;
}

static int test_save_load_1bpp(void)
{
    HEZDIMAGE src = ezd_create(16, -16, 1, 0);
    HEZDIMAGE dst = NULL;
    int i, j;
    CHECK(src, "create failed");
    ezd_set_color_threshold(src, 0x7f);
    for (j = 0; j < 16; j++)
        for (i = 0; i < 16; i++)
            ezd_set_pixel(src, i, j, ((i+j) & 1) ? 0xffffff : 0x000000);
    tmp("1bpp");
    CHECK(ezd_save(src, tmp_path), "save failed");
    dst = ezd_load(tmp_path);
    CHECK(dst, "load returned NULL");
    CHECK(ezd_get_bpp(dst) == 1, "loaded image bpp wrong");
    ezd_destroy(src); ezd_destroy(dst);
    remove(tmp_path);
    return 0;
}

static int test_save_1bpp_file_size(void)
{
    FILE *fp;
    unsigned char hdr[6];
    unsigned int file_size;
    HEZDIMAGE img = ezd_create(16, -16, 1, 0);
    CHECK(img, "create failed");
    tmp("1bpp_size");
    CHECK(ezd_save(img, tmp_path), "save failed");
    fp = fopen(tmp_path, "rb");
    CHECK(fp != NULL, "open saved bmp failed");
    CHECK(fread(hdr, 1, sizeof hdr, fp) == sizeof hdr, "short bmp header");
    fclose(fp);
    file_size = (unsigned int)hdr[2]
              | ((unsigned int)hdr[3] << 8)
              | ((unsigned int)hdr[4] << 16)
              | ((unsigned int)hdr[5] << 24);
    CHECKF(file_size == 14u + 40u + 8u + (unsigned int)ezd_get_image_size(img),
           "wrong BMP file size: %u", file_size);
    ezd_destroy(img);
    remove(tmp_path);
    return 0;
}

static int test_load_nonexistent(void)
{
    CHECK(ezd_load("/tmp/ezdib_no_such_file_xyz.bmp") == NULL,
          "load of missing file should return NULL");
    return 0;
}


/* =======================================================================
 * PIXEL OPERATIONS AND COLOR FORMAT
 * ===================================================================== */

static int test_color_format(void)
{
    /* Colors are encoded as R | (G<<8) | (B<<16) */
    HEZDIMAGE img = ezd_create(4, -4, 24, 0);
    CHECK(img, "create failed");
    ezd_set_pixel(img, 0, 0, 0x0000ff);  /* R=255 */
    ezd_set_pixel(img, 1, 0, 0x00ff00);  /* G=255 */
    ezd_set_pixel(img, 2, 0, 0xff0000);  /* B=255 */
    ezd_set_pixel(img, 3, 0, 0xffffff);  /* white */
    CHECK((ezd_get_pixel(img,0,0) & 0xff)       == 0xff, "R channel wrong");
    CHECK(((ezd_get_pixel(img,1,0) >> 8) & 0xff) == 0xff, "G channel wrong");
    CHECK(((ezd_get_pixel(img,2,0) >>16) & 0xff) == 0xff, "B channel wrong");
    CHECK(ezd_get_pixel(img,3,0) == 0xffffff, "white wrong");
    ezd_destroy(img);
    return 0;
}

static int test_pixels_24bpp(void)
{
    HEZDIMAGE img = ezd_create(8, -8, 24, 0);
    int x, y;
    CHECK(img, "create failed");
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            int col = (x * 0x11) | ((y * 0x11) << 8) | (0x55 << 16);
            CHECK(ezd_set_pixel(img, x, y, col), "set_pixel failed");
            CHECKF(ezd_get_pixel(img,x,y) == col,
                   "round-trip fail at (%d,%d)", x, y);
        }
    ezd_destroy(img);
    return 0;
}

static int test_pixels_32bpp(void)
{
    HEZDIMAGE img = ezd_create(8, -8, 32, 0);
    int x, y;
    CHECK(img, "create failed");
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            int col = x | (y << 8) | (0x33 << 16) | (0xaa << 24);
            CHECK(ezd_set_pixel(img, x, y, col), "set_pixel failed");
            CHECKF(ezd_get_pixel(img,x,y) == col,
                   "round-trip fail at (%d,%d)", x, y);
        }
    ezd_destroy(img);
    return 0;
}

static int test_pixels_1bpp(void)
{
    HEZDIMAGE img = ezd_create(16, -16, 1, 0);
    int x, y;
    CHECK(img, "create failed");
    ezd_set_color_threshold(img, 0x7f);
    /* Set alternating pixels */
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            int on = (x + y) & 1;
            CHECK(ezd_set_pixel(img, x, y, on ? 0xffffff : 0), "set_pixel failed");
        }
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            int expect_on = (x + y) & 1;
            int got = ezd_get_pixel(img, x, y);
            /* 1bpp get_pixel returns the palette colour, not 0/1 */
            CHECKF((got != 0) == (expect_on != 0),
                   "1bpp pixel wrong at (%d,%d): got %d", x, y, got);
        }
    ezd_destroy(img);
    return 0;
}

static int test_pixel_oob(void)
{
    HEZDIMAGE img = ezd_create(10, -10, 24, 0);
    CHECK(img, "create failed");
    /* Out-of-bounds writes must not crash and return 0 */
    CHECK(ezd_set_pixel(img, -1,  0, 0xff) == 0, "oob x<0 should return 0");
    CHECK(ezd_set_pixel(img, 10,  0, 0xff) == 0, "oob x>=w should return 0");
    CHECK(ezd_set_pixel(img,  0, -1, 0xff) == 0, "oob y<0 should return 0");
    CHECK(ezd_set_pixel(img,  0, 10, 0xff) == 0, "oob y>=h should return 0");
    CHECK(ezd_get_pixel(img, -1,  0) == 0, "oob get_pixel should return 0");
    ezd_destroy(img);
    return 0;
}

static int test_fill_24bpp(void)
{
    HEZDIMAGE img = ezd_create(20, -20, 24, 0);
    int x, y;
    CHECK(img, "create failed");
    CHECK(ezd_fill(img, 0x123456), "fill failed");
    for (y = 0; y < 20; y++)
        for (x = 0; x < 20; x++)
            CHECKF(ezd_get_pixel(img,x,y) == 0x123456,
                   "fill pixel wrong at (%d,%d)", x, y);
    ezd_destroy(img);
    return 0;
}

static int test_fill_32bpp(void)
{
    HEZDIMAGE img = ezd_create(16, -16, 32, 0);
    int x, y;
    CHECK(img, "create failed");
    CHECK(ezd_fill(img, 0xdeadbeef), "fill failed");
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            CHECKF((ezd_get_pixel(img,x,y) & 0xffffff) == (0xdeadbeef & 0xffffff),
                   "fill pixel wrong at (%d,%d)", x, y);
    ezd_destroy(img);
    return 0;
}

static int test_palette_ops(void)
{
    HEZDIMAGE img = ezd_create(4, -4, 1, 0);
    CHECK(img, "create failed");
    CHECK(ezd_get_palette_size(img) == 2, "palette size wrong for 1bpp");
    CHECK(ezd_set_palette_color(img, 0, 0x000000), "set palette[0] failed");
    CHECK(ezd_set_palette_color(img, 1, 0x00ff00), "set palette[1] failed");
    CHECK(ezd_get_palette_color(img, 0, 0) == 0x000000, "get palette[0] wrong");
    CHECK(ezd_get_palette_color(img, 1, 0) == 0x00ff00, "get palette[1] wrong");
    CHECK(ezd_get_palette(img) != NULL, "get_palette returned NULL");
    ezd_destroy(img);
    return 0;
}

static int test_color_threshold(void)
{
    HEZDIMAGE img = ezd_create(4, -4, 1, 0);
    CHECK(img, "create failed");
    ezd_set_color_threshold(img, 0x80);
    /* Color 0x90 is above threshold → should set the bit */
    ezd_set_pixel(img, 0, 0, 0x900000);
    CHECK(ezd_get_pixel(img, 0, 0) != 0, "above-threshold pixel not set");
    /* Color 0x70 is below threshold → should clear the bit */
    ezd_set_pixel(img, 1, 0, 0x700000);
    CHECK(ezd_get_pixel(img, 1, 0) == 0, "below-threshold pixel should be 0");
    ezd_destroy(img);
    return 0;
}


/* =======================================================================
 * LINES
 * ===================================================================== */

static int test_line_horizontal(void)
{
    HEZDIMAGE img = ezd_create(40, -10, 24, 0);
    int x;
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    CHECK(ezd_line(img, 5, 4, 34, 4, 0xffffff), "line failed");
    /* Every pixel from x=5 to x=34 at y=4 must be set */
    for (x = 5; x <= 34; x++)
        CHECKF(ezd_get_pixel(img,x,4) == 0xffffff,
               "horizontal line missing pixel at x=%d", x);
    /* Adjacent rows must be untouched */
    CHECK(ezd_get_pixel(img,10,3) == 0, "row above touched");
    CHECK(ezd_get_pixel(img,10,5) == 0, "row below touched");
    ezd_destroy(img);
    return 0;
}

static int test_line_vertical(void)
{
    HEZDIMAGE img = ezd_create(10, -40, 24, 0);
    int y;
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    CHECK(ezd_line(img, 5, 3, 5, 36, 0xff0000), "line failed");
    for (y = 3; y <= 36; y++)
        CHECKF(ezd_get_pixel(img,5,y) == 0xff0000,
               "vertical line missing pixel at y=%d", y);
    CHECK(ezd_get_pixel(img,4,10) == 0, "column left touched");
    CHECK(ezd_get_pixel(img,6,10) == 0, "column right touched");
    ezd_destroy(img);
    return 0;
}

static int test_line_diagonal(void)
{
    HEZDIMAGE img = ezd_create(20, -20, 24, 0);
    int i;
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    /* 45-degree diagonal: every step moves x and y by 1 */
    CHECK(ezd_line(img, 0, 0, 19, 19, 0x00ff00), "line failed");
    for (i = 0; i <= 19; i++)
        CHECKF(ezd_get_pixel(img,i,i) == 0x00ff00,
               "diagonal missing pixel at (%d,%d)", i, i);
    ezd_destroy(img);
    return 0;
}

static int test_line_clipped(void)
{
    HEZDIMAGE img = ezd_create(20, -20, 24, 0);
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    /* Line from (-5,10) to (24,10): only x=0..19 at y=10 should be set */
    CHECK(ezd_line(img, -5, 10, 24, 10, 0xffffff), "line failed");
    CHECK(ezd_get_pixel(img,  0, 10) != 0, "clipped line: x=0 missing");
    CHECK(ezd_get_pixel(img, 19, 10) != 0, "clipped line: x=19 missing");
    ezd_destroy(img);
    return 0;
}

static int test_line_offscreen(void)
{
    HEZDIMAGE img = ezd_create(10, -10, 24, 0);
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    /* Line entirely off-screen — must not crash and should return 1 */
    CHECK(ezd_line(img, -50, -50, -10, -10, 0xffffff) != -1,
          "off-screen line crashed");
    CHECK(count_set_pixels(img,0,0,10,10) == 0, "off-screen line drew pixels");
    ezd_destroy(img);
    return 0;
}

static int test_line_1bpp(void)
{
    HEZDIMAGE img = ezd_create(32, -8, 1, 0);
    int x;
    CHECK(img, "create failed");
    ezd_set_color_threshold(img, 0x7f);
    ezd_fill(img, 0);
    CHECK(ezd_line(img, 0, 4, 31, 4, 0xffffff), "line failed");
    for (x = 0; x <= 31; x++)
        CHECKF(ezd_get_pixel(img,x,4) != 0,
               "1bpp horizontal line missing at x=%d", x);
    ezd_destroy(img);
    return 0;
}


/* =======================================================================
 * RECTANGLES
 * ===================================================================== */

static int test_fill_rect_24bpp(void)
{
    HEZDIMAGE img = ezd_create(30, -30, 24, 0);
    int x, y;
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    CHECK(ezd_fill_rect(img, 5, 5, 24, 24, 0x0000ff), "fill_rect failed");
    /* Interior must be fill colour */
    for (y = 5; y <= 24; y++)
        for (x = 5; x <= 24; x++)
            CHECKF(ezd_get_pixel(img,x,y) == 0x0000ff,
                   "fill_rect interior wrong at (%d,%d)", x, y);
    /* Border pixels outside must be 0 */
    CHECK(ezd_get_pixel(img, 4,  5) == 0, "outside left not 0");
    CHECK(ezd_get_pixel(img,25, 10) == 0, "outside right not 0");
    CHECK(ezd_get_pixel(img,10,  4) == 0, "outside top not 0");
    CHECK(ezd_get_pixel(img,10, 25) == 0, "outside bottom not 0");
    ezd_destroy(img);
    return 0;
}

static int test_fill_rect_32bpp(void)
{
    HEZDIMAGE img = ezd_create(20, -20, 32, 0);
    int x, y;
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    CHECK(ezd_fill_rect(img, 2, 2, 17, 17, 0xabcdef), "fill_rect failed");
    for (y = 2; y <= 17; y++)
        for (x = 2; x <= 17; x++)
            CHECKF((ezd_get_pixel(img,x,y) & 0xffffff) == 0xabcdef,
                   "fill_rect 32bpp wrong at (%d,%d)", x, y);
    ezd_destroy(img);
    return 0;
}

static int test_fill_rect_1bpp(void)
{
    HEZDIMAGE img = ezd_create(32, -32, 1, 0);
    int x, y;
    CHECK(img, "create failed");
    ezd_set_color_threshold(img, 0x7f);
    ezd_fill(img, 0);
    CHECK(ezd_fill_rect(img, 4, 4, 27, 27, 0xffffff), "fill_rect 1bpp failed");
    for (y = 4; y <= 27; y++)
        for (x = 4; x <= 27; x++)
            CHECKF(ezd_get_pixel(img,x,y) != 0,
                   "fill_rect 1bpp interior off at (%d,%d)", x, y);
    CHECK(ezd_get_pixel(img, 3, 15) == 0, "outside left should be 0");
    CHECK(ezd_get_pixel(img, 28,15) == 0, "outside right should be 0");
    ezd_destroy(img);
    return 0;
}

static int test_rect_outline(void)
{
    HEZDIMAGE img = ezd_create(20, -20, 24, 0);
    int x, y;
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    CHECK(ezd_rect(img, 3, 3, 16, 16, 0xffffff), "rect failed");
    /* All four edges must be drawn */
    for (x = 3; x <= 16; x++) {
        CHECKF(ezd_get_pixel(img,x, 3) != 0, "top edge missing at x=%d", x);
        CHECKF(ezd_get_pixel(img,x,16) != 0, "bottom edge missing at x=%d", x);
    }
    for (y = 3; y <= 16; y++) {
        CHECKF(ezd_get_pixel(img, 3,y) != 0, "left edge missing at y=%d", y);
        CHECKF(ezd_get_pixel(img,16,y) != 0, "right edge missing at y=%d", y);
    }
    /* Interior centre must be untouched */
    CHECK(ezd_get_pixel(img,10,10) == 0, "interior was touched");
    ezd_destroy(img);
    return 0;
}

static int test_fill_rect_coords_swapped(void)
{
    /* fill_rect should normalise swapped corners */
    HEZDIMAGE a = ezd_create(20,-20,24,0);
    HEZDIMAGE b = ezd_create(20,-20,24,0);
    int x, y;
    CHECK(a && b, "create failed");
    ezd_fill(a, 0); ezd_fill(b, 0);
    ezd_fill_rect(a, 5,  5, 14, 14, 0xffffff);
    ezd_fill_rect(b, 14, 14,  5,  5, 0xffffff); /* reversed coords */
    for (y = 0; y < 20; y++)
        for (x = 0; x < 20; x++)
            CHECKF(ezd_get_pixel(a,x,y) == ezd_get_pixel(b,x,y),
                   "swapped-coord mismatch at (%d,%d)", x, y);
    ezd_destroy(a); ezd_destroy(b);
    return 0;
}


/* =======================================================================
 * CIRCLES AND ARCS
 * ===================================================================== */

static int test_circle_24bpp(void)
{
    /* Draw a circle of radius 10 centred at (20,20) and verify the four
       extreme points are set. */
    HEZDIMAGE img = ezd_create(40, -40, 24, 0);
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    CHECK(ezd_circle(img, 20, 20, 10, 0xffffff), "circle failed");
    CHECK(ezd_get_pixel(img, 20, 10) != 0, "top of circle missing");
    CHECK(ezd_get_pixel(img, 20, 30) != 0, "bottom of circle missing");
    CHECK(ezd_get_pixel(img, 10, 20) != 0, "left of circle missing");
    CHECK(ezd_get_pixel(img, 30, 20) != 0, "right of circle missing");
    /* Centre must be untouched */
    CHECK(ezd_get_pixel(img, 20, 20) == 0, "centre was touched");
    ezd_destroy(img);
    return 0;
}

static int test_circle_32bpp(void)
{
    HEZDIMAGE img = ezd_create(40, -40, 32, 0);
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    CHECK(ezd_circle(img, 20, 20, 8, 0x00ff00), "circle failed");
    CHECK(ezd_get_pixel(img, 20, 12) != 0, "top missing");
    CHECK(ezd_get_pixel(img, 20, 28) != 0, "bottom missing");
    ezd_destroy(img);
    return 0;
}

static int test_circle_zero_radius(void)
{
    /* Radius 0 → single pixel at the centre */
    HEZDIMAGE img = ezd_create(10, -10, 24, 0);
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    CHECK(ezd_circle(img, 5, 5, 0, 0xffffff), "circle r=0 failed");
    CHECK(ezd_get_pixel(img, 5, 5) != 0, "centre pixel not set");
    CHECK(count_set_pixels(img,0,0,10,10) == 1, "more than one pixel set");
    ezd_destroy(img);
    return 0;
}

static int test_arc_partial(void)
{
#if defined(EZD_MATH_NONE)
    return 0; /* arc drawing disabled in this build */
#else
    /* Quarter arc from 0 to π/2 (positive x-axis to positive y-axis).
       Check ±1-pixel neighbourhoods around the expected endpoints to
       tolerate the integer-truncation inherent in trig → pixel mapping. */
    HEZDIMAGE img = ezd_create(60, -60, 24, 0);
    double pi_2 = 1.5707963268;
    int x, y, found_right = 0, found_bottom = 0, found_left = 0;
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    CHECK(ezd_arc(img, 30, 30, 15, 0.0, pi_2, 0xffffff), "arc failed");
    /* Right endpoint (angle=0) should be near (45, 30) */
    for (y = 29; y <= 31; y++) for (x = 43; x <= 45; x++)
        if (ezd_get_pixel(img,x,y)) found_right = 1;
    /* Bottom endpoint (angle=π/2) should be near (30, 45) */
    for (y = 43; y <= 45; y++) for (x = 29; x <= 31; x++)
        if (ezd_get_pixel(img,x,y)) found_bottom = 1;
    /* Left point (angle=π) should NOT be drawn */
    for (y = 29; y <= 31; y++) for (x = 14; x <= 16; x++)
        if (ezd_get_pixel(img,x,y)) found_left = 1;
    CHECK(found_right,  "arc right endpoint neighbourhood not drawn");
    CHECK(found_bottom, "arc bottom endpoint neighbourhood not drawn");
    CHECK(!found_left,  "arc left point (outside range) should not be drawn");
    ezd_destroy(img);
    return 0;
#endif
}

static int test_arc_full_covers_circle(void)
{
#if defined(EZD_MATH_NONE)
    return 0;
#else
    /* A full-circle arc must draw something at each of the four cardinal
       directions.  Check ±1-pixel neighbourhoods to tolerate truncation. */
    double pi2 = 6.2831853072;
    HEZDIMAGE a = ezd_create(40,-40,24,0);
    HEZDIMAGE c = ezd_create(40,-40,24,0);
    int x, y, ft=0, fb=0, fl=0, fr=0;
    CHECK(a && c, "create failed");
    ezd_fill(a, 0); ezd_fill(c, 0);
    ezd_arc(a, 20, 20, 10, 0.0, pi2, 0xffffff);
    ezd_circle(c, 20, 20, 10, 0xffffff);
    /* Arc: check ±1 neighbourhood at top/bottom/left/right extremes */
    for (y=9;y<=11;y++)  for (x=19;x<=21;x++) if(ezd_get_pixel(a,x,y)) ft=1;
    for (y=29;y<=31;y++) for (x=19;x<=21;x++) if(ezd_get_pixel(a,x,y)) fb=1;
    for (y=19;y<=21;y++) for (x=9;x<=11;x++)  if(ezd_get_pixel(a,x,y)) fl=1;
    for (y=19;y<=21;y++) for (x=29;x<=31;x++) if(ezd_get_pixel(a,x,y)) fr=1;
    CHECK(ft, "arc: top cardinal missing");
    CHECK(fb, "arc: bottom cardinal missing");
    CHECK(fl, "arc: left cardinal missing");
    CHECK(fr, "arc: right cardinal missing");
    /* Circle (Bresenham): cardinal pixels are exact */
    CHECK(ezd_get_pixel(c,20,10), "circle: top missing");
    CHECK(ezd_get_pixel(c,20,30), "circle: bottom missing");
    CHECK(ezd_get_pixel(c,10,20), "circle: left missing");
    CHECK(ezd_get_pixel(c,30,20), "circle: right missing");
    ezd_destroy(a); ezd_destroy(c);
    return 0;
#endif
}


/* =======================================================================
 * FLOOD FILL
 * ===================================================================== */

static int test_flood_fill_24bpp(void)
{
    /* Draw a filled circle as boundary, flood-fill from the centre */
    HEZDIMAGE img = ezd_create(60, -60, 24, 0);
    int px, py;
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    ezd_circle(img, 30, 30, 15, 0xffffff); /* white border */
    CHECK(ezd_flood_fill(img, 30, 30, 0xffffff, 0x0000ff), "flood_fill failed");
    /* Centre must now be the fill colour */
    CHECK((ezd_get_pixel(img,30,30) & 0xffffff) == 0x0000ff, "centre not filled");
    /* Border must still be white */
    CHECK((ezd_get_pixel(img,30,15) & 0xffffff) == 0xffffff, "border overwritten");
    /* Outside the circle must still be black */
    CHECK(ezd_get_pixel(img, 5, 5) == 0, "outside was filled");
    ezd_destroy(img);
    return 0;
}

static int test_flood_fill_32bpp(void)
{
    HEZDIMAGE img = ezd_create(40, -40, 32, 0);
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    ezd_rect(img, 5, 5, 34, 34, 0xffffff);
    CHECK(ezd_flood_fill(img, 20, 20, 0xffffff, 0x00ff00), "flood_fill failed");
    CHECK((ezd_get_pixel(img,20,20) & 0xffffff) == 0x00ff00, "centre not filled");
    CHECK((ezd_get_pixel(img, 0, 0) & 0xffffff) == 0, "outside was filled");
    ezd_destroy(img);
    return 0;
}

static int test_flood_fill_does_not_cross_border(void)
{
    /* Fill should stop at the border colour and not bleed through */
    HEZDIMAGE img = ezd_create(30, -30, 24, 0);
    int x, y;
    CHECK(img, "create failed");
    ezd_fill(img, 0);
    /* Solid vertical wall at x=15 */
    for (y = 0; y < 30; y++) ezd_set_pixel(img, 15, y, 0xff0000);
    ezd_flood_fill(img, 5, 5, 0xff0000, 0x00ff00);
    /* Right side of wall must still be black */
    for (y = 0; y < 30; y++)
        CHECKF(ezd_get_pixel(img,20,y) == 0,
               "flood fill crossed wall at y=%d", y);
    ezd_destroy(img);
    return 0;
}

static int test_flood_fill_already_fill_colour(void)
{
    /* Flood-fill into a pixel that is already the fill colour should succeed
       without looping indefinitely */
    HEZDIMAGE img = ezd_create(10, -10, 24, 0);
    CHECK(img, "create failed");
    ezd_fill(img, 0x0000ff);
    /* Seed pixel is already fill colour — function should return quickly */
    CHECK(ezd_flood_fill(img, 5, 5, 0xffffff, 0x0000ff) != -1, "flood_fill crashed");
    ezd_destroy(img);
    return 0;
}


/* =======================================================================
 * FONT AND TEXT
 * ===================================================================== */

static int test_font_load_small(void)
{
    HEZDFONT f = ezd_load_font(EZD_FONT_TYPE_SMALL, 0, 0);
    CHECK(f, "small font load failed");
    ezd_destroy_font(f);
    return 0;
}

static int test_font_load_medium(void)
{
    HEZDFONT f = ezd_load_font(EZD_FONT_TYPE_MEDIUM, 0, 0);
    CHECK(f, "medium font load failed");
    ezd_destroy_font(f);
    return 0;
}

static int test_font_rejects_malformed(void)
{
    static const char bad_short[] = { 'A', 8, 8, 0 };
    static const char bad_unterminated[] = { 'A', 1, 1, 0x80 };
    CHECK(ezd_load_font(bad_short, sizeof bad_short, 0) == NULL,
          "short glyph bitmap should fail");
    CHECK(ezd_load_font(bad_unterminated, sizeof bad_unterminated, 0) == NULL,
          "unterminated font table should fail");
    CHECK(ezd_load_font(bad_unterminated, 0, 0) == NULL,
          "unsized custom font table should fail");
    return 0;
}

static int test_text_renders_pixels(void)
{
    HEZDIMAGE img = ezd_create(200, -20, 24, 0);
    HEZDFONT  fnt = ezd_load_font(EZD_FONT_TYPE_MEDIUM, 0, 0);
    CHECK(img && fnt, "setup failed");
    ezd_fill(img, 0);
    CHECK(ezd_text(img, fnt, "HELLO", -1, 0, 2, 0xffffff), "text failed");
    /* At least some pixels must have been set */
    CHECK(count_set_pixels(img,0,0,200,20) > 0, "text drew no pixels");
    ezd_destroy_font(fnt); ezd_destroy(img);
    return 0;
}

static int test_text_size(void)
{
    HEZDFONT fnt = ezd_load_font(EZD_FONT_TYPE_MEDIUM, 0, 0);
    int w = 0, h = 0;
    CHECK(fnt, "font load failed");
    CHECK(ezd_text_size(fnt, "ABC", -1, &w, &h) > 0, "text_size returned 0");
    CHECK(w > 0, "text_size width is 0");
    CHECK(h > 0, "text_size height is 0");
    ezd_destroy_font(fnt);
    return 0;
}

static int test_text_size_wider_for_longer_string(void)
{
    HEZDFONT fnt = ezd_load_font(EZD_FONT_TYPE_SMALL, 0, 0);
    int w1=0,h1=0, w2=0,h2=0;
    CHECK(fnt, "font load failed");
    ezd_text_size(fnt, "A",    -1, &w1, &h1);
    ezd_text_size(fnt, "ABCD", -1, &w2, &h2);
    CHECK(w2 > w1, "longer string should be wider");
    CHECK(h1 == h2, "single-line strings should have same height");
    ezd_destroy_font(fnt);
    return 0;
}

static int test_text_newline(void)
{
    HEZDFONT fnt = ezd_load_font(EZD_FONT_TYPE_SMALL, 0, 0);
    int w1=0,h1=0, w2=0,h2=0;
    CHECK(fnt, "font load failed");
    ezd_text_size(fnt, "A",     -1, &w1, &h1);
    ezd_text_size(fnt, "A\nB",  -1, &w2, &h2);
    CHECK(h2 > h1, "newline should increase height");
    ezd_destroy_font(fnt);
    return 0;
}

static int test_text_null_terminated(void)
{
    HEZDIMAGE img = ezd_create(100, -20, 24, 0);
    HEZDFONT  fnt = ezd_load_font(EZD_FONT_TYPE_SMALL, 0, 0);
    CHECK(img && fnt, "setup failed");
    ezd_fill(img, 0);
    /* len=-1 means null-terminated; should draw the same as explicit len */
    CHECK(ezd_text(img, fnt, "HI", -1, 0, 2, 0xffffff), "text len=-1 failed");
    CHECK(count_set_pixels(img,0,0,100,20) > 0, "no pixels drawn");
    ezd_destroy_font(fnt); ezd_destroy(img);
    return 0;
}

static int test_text_bottom_up_top_edge(void)
{
    HEZDIMAGE img = ezd_create(100, 20, 24, 0);
    HEZDFONT  fnt = ezd_load_font(EZD_FONT_TYPE_SMALL, 0, 0);
    CHECK(img && fnt, "setup failed");
    ezd_fill(img, 0);
    CHECK(ezd_text(img, fnt, "HI", -1, 0, 0, 0xffffff),
          "text at top edge should be a no-op, not a failure");
    CHECK(count_set_pixels(img,0,0,100,20) == 0,
          "top-edge bottom-up text should not draw out-of-bounds glyph");
    CHECK(ezd_text(img, fnt, "HI", -1, 0, 6, 0xffffff),
          "bottom-up text with enough space failed");
    CHECK(count_set_pixels(img,0,0,100,20) > 0, "bottom-up text drew no pixels");
    ezd_destroy_font(fnt); ezd_destroy(img);
    return 0;
}


/* =======================================================================
 * SCALING
 * ===================================================================== */

static int test_scale_nearest_1to1_24bpp(void)
{
    HEZDIMAGE src = ezd_create(32,-32,24,0);
    HEZDIMAGE dst = ezd_create(32,-32,24,0);
    int x, y;
    CHECK(src && dst, "create failed");
    for (y=0;y<32;y++) for (x=0;x<32;x++)
        ezd_set_pixel(src,x,y,(x*8)|(y<<8));
    CHECK(ezd_scale(src,0,0,31,31, dst,0,0,31,31, EZD_SCALE_NEAREST), "scale failed");
    for (y=0;y<32;y++) for (x=0;x<32;x++)
        CHECKF(ezd_get_pixel(src,x,y)==ezd_get_pixel(dst,x,y),
               "pixel mismatch at (%d,%d)",x,y);
    ezd_destroy(src); ezd_destroy(dst);
    return 0;
}

static int test_scale_nearest_1to1_32bpp(void)
{
    HEZDIMAGE src = ezd_create(16,-16,32,0);
    HEZDIMAGE dst = ezd_create(16,-16,32,0);
    int x, y;
    CHECK(src && dst, "create failed");
    for (y=0;y<16;y++) for (x=0;x<16;x++)
        ezd_set_pixel(src,x,y,x|(y<<8)|(0xab<<16));
    CHECK(ezd_scale(src,0,0,15,15, dst,0,0,15,15, EZD_SCALE_NEAREST), "scale failed");
    for (y=0;y<16;y++) for (x=0;x<16;x++)
        CHECKF(ezd_get_pixel(src,x,y)==ezd_get_pixel(dst,x,y),
               "pixel mismatch at (%d,%d)",x,y);
    ezd_destroy(src); ezd_destroy(dst);
    return 0;
}

static int test_scale_nearest_pixels_are_source_values(void)
{
    /* After downscaling a checkerboard, every output pixel must be one of
       the two source colours (nearest-neighbour never interpolates). */
    HEZDIMAGE src = ezd_create(64,-64,24,0);
    HEZDIMAGE dst = ezd_create( 8, -8,24,0);
    int x, y, bad = 0;
    CHECK(src && dst, "create failed");
    for (y=0;y<64;y++) for (x=0;x<64;x++)
        ezd_set_pixel(src,x,y,((x+y)&1)?0xff0000:0x0000ff);
    CHECK(ezd_scale(src,0,0,63,63, dst,0,0,7,7, EZD_SCALE_NEAREST), "scale failed");
    for (y=0;y<8;y++) for (x=0;x<8;x++) {
        int p = ezd_get_pixel(dst,x,y);
        if (p!=0x0000ff && p!=0xff0000) bad++;
    }
    CHECKF(bad==0, "%d output pixels are not source values", bad);
    ezd_destroy(src); ezd_destroy(dst);
    return 0;
}

static int test_scale_upscale_solid_colour(void)
{
    /* All three modes must preserve a solid colour under upscaling */
    int q, bpp;
    for (bpp=24; bpp<=32; bpp+=8) {
        for (q=0; q<=2; q++) {
            HEZDIMAGE s = ezd_create(4,-4,bpp,0);
            HEZDIMAGE d = ezd_create(16,-16,bpp,0);
            long r, g, b;
            CHECK(s && d, "create failed");
            ezd_fill(s, 0x00ff00); /* solid green */
            ezd_fill(d, 0);
            CHECK(ezd_scale(s,0,0,3,3, d,0,0,15,15, q), "scale failed");
            r=avg_channel(d,0,0,16,16,0);
            g=avg_channel(d,0,0,16,16,8);
            b=avg_channel(d,0,0,16,16,16);
            CHECKF(r==0 && g==255 && b==0,
                   "solid upscale wrong bpp=%d q=%d R=%ld G=%ld B=%ld",bpp,q,r,g,b);
            ezd_destroy(s); ezd_destroy(d);
        }
    }
    return 0;
}

static int test_scale_bilinear_gradient_monotone(void)
{
    /* Bilinear upscale of a left-to-right gradient must stay monotone */
    HEZDIMAGE src = ezd_create(4,-1,24,0);
    HEZDIMAGE dst = ezd_create(16,-1,24,0);
    int i, prev = -1;
    CHECK(src && dst, "create failed");
    ezd_set_pixel(src,0,0,0x000000); ezd_set_pixel(src,1,0,0x555555);
    ezd_set_pixel(src,2,0,0xaaaaaa); ezd_set_pixel(src,3,0,0xffffff);
    CHECK(ezd_scale(src,0,0,3,0, dst,0,0,15,0, EZD_SCALE_BILINEAR), "scale failed");
    for (i=0;i<16;i++) {
        int v = ezd_get_pixel(dst,i,0) & 0xff;
        CHECKF(v >= prev, "bilinear gradient not monotone at x=%d (v=%d prev=%d)",i,v,prev);
        prev = v;
    }
    ezd_destroy(src); ezd_destroy(dst);
    return 0;
}

static int test_scale_area_average_24bpp(void)
{
    /* Area averaging a 50/50 red-blue checkerboard must produce ~(127,0,127) */
    HEZDIMAGE src = ezd_create(64,-64,24,0);
    HEZDIMAGE dst = ezd_create( 8, -8,24,0);
    long r, b;
    int x, y;
    CHECK(src && dst, "create failed");
    for (y=0;y<64;y++) for (x=0;x<64;x++)
        ezd_set_pixel(src,x,y,((x+y)&1)?0xff0000:0x0000ff);
    CHECK(ezd_scale(src,0,0,63,63, dst,0,0,7,7, EZD_SCALE_AREA), "scale failed");
    r = avg_channel(dst,0,0,8,8,0);
    b = avg_channel(dst,0,0,8,8,16);
    CHECKF(r>=120 && r<=135, "area avg R=%ld (expected ~127)", r);
    CHECKF(b>=120 && b<=135, "area avg B=%ld (expected ~127)", b);
    ezd_destroy(src); ezd_destroy(dst);
    return 0;
}

static int test_scale_area_average_32bpp(void)
{
    HEZDIMAGE src = ezd_create(32,-32,32,0);
    HEZDIMAGE dst = ezd_create( 4, -4,32,0);
    long r, b;
    int x, y;
    CHECK(src && dst, "create failed");
    for (y=0;y<32;y++) for (x=0;x<32;x++)
        ezd_set_pixel(src,x,y,((x+y)&1)?0xff0000:0x0000ff);
    CHECK(ezd_scale(src,0,0,31,31, dst,0,0,3,3, EZD_SCALE_AREA), "scale failed");
    r = avg_channel(dst,0,0,4,4,0);
    b = avg_channel(dst,0,0,4,4,16);
    CHECKF(r>=110 && r<=145, "area 32bpp R=%ld (expected ~127)", r);
    CHECKF(b>=110 && b<=145, "area 32bpp B=%ld (expected ~127)", b);
    ezd_destroy(src); ezd_destroy(dst);
    return 0;
}

static int test_scale_1bpp_nearest(void)
{
    HEZDIMAGE src = ezd_create(16,-16,1,0);
    HEZDIMAGE dst = ezd_create( 8, -8,1,0);
    int x, y;
    CHECK(src && dst, "create failed");
    ezd_set_color_threshold(src, 0x7f);
    ezd_set_color_threshold(dst, 0x7f);
    ezd_fill(src, 0xffffff);
    CHECK(ezd_scale(src,0,0,15,15, dst,0,0,7,7, EZD_SCALE_NEAREST), "scale failed");
    /* All output pixels should remain set */
    for (y=0;y<8;y++) for (x=0;x<8;x++)
        CHECKF(ezd_get_pixel(dst,x,y)!=0, "1bpp scale lost pixel at (%d,%d)",x,y);
    ezd_destroy(src); ezd_destroy(dst);
    return 0;
}

static int test_scale_mismatched_bpp(void)
{
    HEZDIMAGE s24 = ezd_create(16,-16,24,0);
    HEZDIMAGE d32 = ezd_create(16,-16,32,0);
    CHECK(s24 && d32, "create failed");
    /* Must return 0 (failure) for mismatched bit depths */
    CHECK(ezd_scale(s24,0,0,15,15, d32,0,0,15,15, EZD_SCALE_NEAREST) == 0,
          "mismatched bpp should fail");
    ezd_destroy(s24); ezd_destroy(d32);
    return 0;
}

static int test_scale_sub_region(void)
{
    /* Scale only the top-left quadrant of a 4-colour image */
    HEZDIMAGE src = ezd_create(8,-8,24,0);
    HEZDIMAGE dst = ezd_create(4,-4,24,0);
    long r, g;
    CHECK(src && dst, "create failed");
    /* Top-left quadrant: solid red */
    ezd_fill_rect(src, 0, 0, 3, 3, 0x0000ff);
    /* Bottom-right quadrant: solid green */
    ezd_fill_rect(src, 4, 4, 7, 7, 0x00ff00);
    ezd_fill(dst, 0);
    /* Scale only the red quadrant */
    CHECK(ezd_scale(src,0,0,3,3, dst,0,0,3,3, EZD_SCALE_NEAREST), "scale failed");
    r = avg_channel(dst,0,0,4,4,0);  /* R channel */
    g = avg_channel(dst,0,0,4,4,8);  /* G channel */
    CHECKF(r==255 && g==0, "sub-region scale wrong: R=%ld G=%ld", r, g);
    ezd_destroy(src); ezd_destroy(dst);
    return 0;
}


/* =======================================================================
 * MATH FUNCTIONS
 * ===================================================================== */

/* Precision tolerance: within 1/512 (~0.002), tighter than one pixel step */
#define MATH_TOL 0.002

static int math_near(double a, double b) { double d=a-b; return d>=-MATH_TOL && d<=MATH_TOL; }

static int test_sin_cos_zero(void)
{
    CHECK(math_near(ezd_sin(0.0),  0.0), "sin(0) != 0");
    CHECK(math_near(ezd_cos(0.0),  1.0), "cos(0) != 1");
    return 0;
}

static int test_sin_cos_pi_over_2(void)
{
    double pi_2 = 1.5707963268;
    CHECK(math_near(ezd_sin(pi_2),  1.0), "sin(π/2) != 1");
    CHECK(math_near(ezd_cos(pi_2),  0.0), "cos(π/2) != 0");
    return 0;
}

static int test_sin_cos_pi(void)
{
    double pi = 3.1415926536;
    CHECK(math_near(ezd_sin(pi),  0.0), "sin(π) != 0");
    CHECK(math_near(ezd_cos(pi), -1.0), "cos(π) != -1");
    return 0;
}

static int test_sin_cos_identity(void)
{
    /* sin²(x) + cos²(x) == 1 for several angles */
    double angles[] = { 0.1, 0.5, 1.0, 1.5, 2.3, 3.7, 5.0, 6.1 };
    int i;
    for (i = 0; i < 8; i++) {
        double s = ezd_sin(angles[i]);
        double c = ezd_cos(angles[i]);
        double sum = s*s + c*c;
        CHECKF(math_near(sum, 1.0), "sin²+cos²=%f at angle %f", sum, angles[i]);
    }
    return 0;
}

static int test_sin_cos_symmetry(void)
{
    /* sin is odd: sin(-x) = -sin(x); cos is even: cos(-x) = cos(x) */
    double x = 1.23456;
    CHECK(math_near(ezd_sin(-x), -ezd_sin(x)), "sin is not odd");
    CHECK(math_near(ezd_cos(-x),  ezd_cos(x)), "cos is not even");
    return 0;
}

static int test_sin_cos_wrap(void)
{
    /* sin/cos must handle angles outside (-π, π] via normalisation */
    double pi2 = 6.2831853072;
    /* sin(2π + x) == sin(x) */
    CHECK(math_near(ezd_sin(pi2 + 0.5), ezd_sin(0.5)), "sin wrap failed");
    CHECK(math_near(ezd_cos(pi2 + 0.5), ezd_cos(0.5)), "cos wrap failed");
    return 0;
}


/* =======================================================================
 * ROBUSTNESS
 * ===================================================================== */

static int test_null_handle(void)
{
    /* Every function that takes HEZDIMAGE must handle NULL without crashing */
    CHECK(ezd_set_pixel(NULL, 0, 0, 0)  == 0, "set_pixel(NULL) crashed");
    CHECK(ezd_get_pixel(NULL, 0, 0)     == 0, "get_pixel(NULL) crashed");
    CHECK(ezd_fill(NULL, 0)             == 0, "fill(NULL) crashed");
    CHECK(ezd_line(NULL,0,0,1,1,0)      == 0, "line(NULL) crashed");
    CHECK(ezd_fill_rect(NULL,0,0,1,1,0) == 0, "fill_rect(NULL) crashed");
    CHECK(ezd_rect(NULL,0,0,1,1,0)      == 0, "rect(NULL) crashed");
    CHECK(ezd_circle(NULL,5,5,3,0)      == 0, "circle(NULL) crashed");
    CHECK(ezd_save(NULL, "/tmp/x.bmp")  == 0, "save(NULL) crashed");
    CHECK(ezd_get_width(NULL)           == 0, "get_width(NULL) crashed");
    CHECK(ezd_get_height(NULL)          == 0, "get_height(NULL) crashed");
    CHECK(ezd_get_bpp(NULL)             == 0, "get_bpp(NULL) crashed");
    CHECK(ezd_get_image_size(NULL)      == 0, "get_image_size(NULL) crashed");
    CHECK(ezd_get_image_ptr(NULL)       == NULL, "get_image_ptr(NULL) crashed");
    ezd_destroy(NULL); /* must not crash */
    return 0;
}

typedef struct { int count; int last_col; } CbCtx;

static int cb_count(void *u, int x, int y, int c, int f)
{
    CbCtx *p = (CbCtx*)u;
    p->count++; p->last_col = c;
    (void)x; (void)y; (void)f;
    return 1;
}

static int test_pixel_callback(void)
{
    /* Verify that a pixel callback receives every pixel during fill */
    CbCtx ctx = { 0, 0 };
    char hdr[EZD_HEADER_SIZE];
    HEZDIMAGE img = ezd_initialize(hdr, sizeof hdr, 4, -4, 24,
                                   EZD_FLAG_USER_IMAGE_BUFFER);
    CHECK(img, "initialize failed");
    CHECK(ezd_set_pixel_callback(img, cb_count, &ctx), "set callback failed");
    ezd_fill(img, 0xabcdef);
    CHECK(ctx.count == 16, "fill should call callback 4x4=16 times");
    CHECK(ctx.last_col == 0xabcdef, "callback received wrong colour");
    return 0;
}

static int test_scale_clipped_rects(void)
{
    /* Rects that extend beyond image bounds should be clipped, not crash */
    HEZDIMAGE src = ezd_create(16,-16,24,0);
    HEZDIMAGE dst = ezd_create(16,-16,24,0);
    CHECK(src && dst, "create failed");
    ezd_fill(src, 0x0000ff);
    /* Source rect extends beyond image */
    CHECK(ezd_scale(src,-10,-10,100,100, dst,0,0,15,15, EZD_SCALE_NEAREST),
          "scale with oversized src rect failed");
    ezd_destroy(src); ezd_destroy(dst);
    return 0;
}


/* =======================================================================
 * DISPATCH TABLE
 * ===================================================================== */

typedef struct { const char *name; int (*fn)(void); } Test;

static const Test tests[] = {
    /* Image lifecycle */
    { "create_24bpp",               test_create_24bpp               },
    { "create_32bpp",               test_create_32bpp               },
    { "create_1bpp",                test_create_1bpp                },
    { "create_negative_height",     test_create_negative_height     },
    { "create_user_buffer",         test_create_user_buffer         },
    { "create_overflow",            test_create_overflow            },
    { "header_size",                test_header_size                },
    { "save_load_24bpp",            test_save_load_24bpp            },
    { "save_load_32bpp",            test_save_load_32bpp            },
    { "save_load_1bpp",             test_save_load_1bpp             },
    { "save_1bpp_file_size",        test_save_1bpp_file_size        },
    { "load_nonexistent",           test_load_nonexistent           },
    /* Pixels and colour */
    { "color_format",               test_color_format               },
    { "pixels_24bpp",               test_pixels_24bpp               },
    { "pixels_32bpp",               test_pixels_32bpp               },
    { "pixels_1bpp",                test_pixels_1bpp                },
    { "pixel_oob",                  test_pixel_oob                  },
    { "fill_24bpp",                 test_fill_24bpp                 },
    { "fill_32bpp",                 test_fill_32bpp                 },
    { "palette_ops",                test_palette_ops                },
    { "color_threshold",            test_color_threshold            },
    /* Lines */
    { "line_horizontal",            test_line_horizontal            },
    { "line_vertical",              test_line_vertical              },
    { "line_diagonal",              test_line_diagonal              },
    { "line_clipped",               test_line_clipped               },
    { "line_offscreen",             test_line_offscreen             },
    { "line_1bpp",                  test_line_1bpp                  },
    /* Rectangles */
    { "fill_rect_24bpp",            test_fill_rect_24bpp            },
    { "fill_rect_32bpp",            test_fill_rect_32bpp            },
    { "fill_rect_1bpp",             test_fill_rect_1bpp             },
    { "rect_outline",               test_rect_outline               },
    { "fill_rect_coords_swapped",   test_fill_rect_coords_swapped   },
    /* Circles and arcs */
    { "circle_24bpp",               test_circle_24bpp               },
    { "circle_32bpp",               test_circle_32bpp               },
    { "circle_zero_radius",         test_circle_zero_radius         },
    { "arc_partial",                test_arc_partial                },
    { "arc_full_covers_circle",     test_arc_full_covers_circle     },
    /* Flood fill */
    { "flood_fill_24bpp",           test_flood_fill_24bpp           },
    { "flood_fill_32bpp",           test_flood_fill_32bpp           },
    { "flood_fill_no_bleed",        test_flood_fill_does_not_cross_border },
    { "flood_fill_already_colour",  test_flood_fill_already_fill_colour   },
    /* Fonts and text */
    { "font_load_small",            test_font_load_small            },
    { "font_load_medium",           test_font_load_medium           },
    { "font_rejects_malformed",     test_font_rejects_malformed     },
    { "text_renders_pixels",        test_text_renders_pixels        },
    { "text_size",                  test_text_size                  },
    { "text_size_wider",            test_text_size_wider_for_longer_string },
    { "text_newline",               test_text_newline               },
    { "text_null_terminated",       test_text_null_terminated       },
    { "text_bottom_up_top_edge",    test_text_bottom_up_top_edge    },
    /* Scaling */
    { "scale_nearest_1to1_24bpp",   test_scale_nearest_1to1_24bpp  },
    { "scale_nearest_1to1_32bpp",   test_scale_nearest_1to1_32bpp  },
    { "scale_nearest_source_vals",  test_scale_nearest_pixels_are_source_values },
    { "scale_upscale_solid",        test_scale_upscale_solid_colour },
    { "scale_bilinear_monotone",    test_scale_bilinear_gradient_monotone },
    { "scale_area_24bpp",           test_scale_area_average_24bpp  },
    { "scale_area_32bpp",           test_scale_area_average_32bpp  },
    { "scale_1bpp_nearest",         test_scale_1bpp_nearest         },
    { "scale_mismatched_bpp",       test_scale_mismatched_bpp       },
    { "scale_sub_region",           test_scale_sub_region           },
    /* Math */
    { "sin_cos_zero",               test_sin_cos_zero               },
    { "sin_cos_pi_over_2",          test_sin_cos_pi_over_2          },
    { "sin_cos_pi",                 test_sin_cos_pi                 },
    { "sin_cos_identity",           test_sin_cos_identity           },
    { "sin_cos_symmetry",           test_sin_cos_symmetry           },
    { "sin_cos_wrap",               test_sin_cos_wrap               },
    /* Robustness */
    { "null_handle",                test_null_handle                },
    { "pixel_callback",             test_pixel_callback             },
    { "scale_clipped_rects",        test_scale_clipped_rects        },
    { NULL, NULL }
};

int main(int argc, char *argv[])
{
    int i;
    if (argc < 2) {
        /* With no arguments, list all test names (useful for scripting) */
        for (i = 0; tests[i].name; i++)
            printf("%s\n", tests[i].name);
        return 0;
    }
    for (i = 0; tests[i].name; i++) {
        if (strcmp(argv[1], tests[i].name) == 0) {
            int result = tests[i].fn();
            if (result == 0)
                printf("PASS: %s\n", argv[1]);
            else
                fprintf(stderr, "FAIL: %s\n", argv[1]);
            return result;
        }
    }
    fprintf(stderr, "Unknown test: %s\n", argv[1]);
    return 1;
}
