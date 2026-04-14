/* gen_icon.c - Generate a 32x32 yellow acid smiley .ico for NARINA */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define SZ 32

typedef struct { uint8_t r, g, b, a; } Pixel;
static Pixel img[SZ][SZ];

static void put(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x >= 0 && x < SZ && y >= 0 && y < SZ)
        img[y][x] = (Pixel){r, g, b, a};
}

static void fill_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b) {
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++)
            if (dx*dx + dy*dy <= radius*radius)
                put(cx+dx, cy+dy, r, g, b, 255);
}

static void circle_outline(int cx, int cy, int radius, int thick, uint8_t r, uint8_t g, uint8_t b) {
    for (int dy = -(radius+thick); dy <= radius+thick; dy++)
        for (int dx = -(radius+thick); dx <= radius+thick; dx++) {
            int d2 = dx*dx + dy*dy;
            if (d2 >= (radius-thick)*(radius-thick) && d2 <= (radius+thick)*(radius+thick))
                put(cx+dx, cy+dy, r, g, b, 255);
        }
}

int main(void) {
    /* Clear to transparent */
    for (int y = 0; y < SZ; y++)
        for (int x = 0; x < SZ; x++)
            img[y][x] = (Pixel){0, 0, 0, 0};

    /* Yellow filled face */
    fill_circle(15, 15, 13, 255, 220, 20);

    /* Darker yellow edge */
    circle_outline(15, 15, 13, 1, 200, 170, 0);

    /* Eyes - black X shapes (acid/rave style) */
    for (int i = -2; i <= 2; i++) {
        put(10+i, 10+i, 20, 20, 20, 255);
        put(10+i, 10-i, 20, 20, 20, 255);
        put(10+i, 11+i, 20, 20, 20, 255);
        put(10+i, 11-i, 20, 20, 20, 255);

        put(20+i, 10+i, 20, 20, 20, 255);
        put(20+i, 10-i, 20, 20, 20, 255);
        put(20+i, 11+i, 20, 20, 20, 255);
        put(20+i, 11-i, 20, 20, 20, 255);
    }

    /* Wide grin - black */
    for (int x = 8; x <= 22; x++) {
        double t = (x - 15.0) / 7.0;
        int y = (int)(19 + 3.5 * t * t + 0.5);
        put(x, y, 20, 20, 20, 255);
        put(x, y+1, 20, 20, 20, 255);
    }

    /* Tongue - red/pink */
    fill_circle(15, 24, 2, 220, 50, 70);

    /* ---- Write .ico ---- */
    FILE *f = fopen("narina.ico", "wb");
    if (!f) { fprintf(stderr, "Cannot open narina.ico\n"); return 1; }

    uint8_t ico_hdr[6] = {0,0, 1,0, 1,0};
    fwrite(ico_hdr, 1, 6, f);

    uint32_t bmp_size = 40 + SZ*SZ*4 + SZ*SZ/8;
    uint8_t dir[16] = {
        SZ, SZ, 0, 0, 1, 0, 32, 0,
        (uint8_t)(bmp_size), (uint8_t)(bmp_size>>8),
        (uint8_t)(bmp_size>>16), (uint8_t)(bmp_size>>24),
        22, 0, 0, 0
    };
    fwrite(dir, 1, 16, f);

    uint8_t bih[40];
    memset(bih, 0, 40);
    bih[0] = 40;
    bih[4] = SZ;
    bih[8] = SZ*2;
    bih[12] = 1;
    bih[14] = 32;
    fwrite(bih, 1, 40, f);

    for (int y = SZ - 1; y >= 0; y--)
        for (int x = 0; x < SZ; x++) {
            uint8_t bgra[4] = { img[y][x].b, img[y][x].g, img[y][x].r, img[y][x].a };
            fwrite(bgra, 1, 4, f);
        }

    uint8_t zero_row[SZ/8];
    memset(zero_row, 0, sizeof(zero_row));
    for (int y = 0; y < SZ; y++)
        fwrite(zero_row, 1, sizeof(zero_row), f);

    fclose(f);
    printf("Generated narina.ico (%d bytes)\n", 22 + (int)bmp_size);
    return 0;
}
