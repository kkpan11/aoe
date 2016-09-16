#ifndef AOE_GFX_H
#define AOE_GFX_H

#include <stdint.h>

struct rect {
	int left, right, top, bottom;
};

struct pal_entry {
	uint8_t r, g, b, flags;
};

extern struct pal_entry game_pal[256];

struct video_mode {
	unsigned hInst;
	unsigned window;
	struct pal_entry *palette;
	unsigned tblC[3];
	// REMAP typeof(LPDIRECTDRAW lplpDD) == unsigned
	unsigned lplpDD;
	unsigned tbl1C[3];
	int sys_memmap;
	unsigned width, height;
	unsigned num34;
	unsigned state;
	unsigned mode0;
	unsigned mode1;
	unsigned mode2;
	unsigned mode3;
	unsigned mode8_640_480;
	unsigned mode8_800_600;
	unsigned mode8_1024_768;
	unsigned mode8_1280_1024;
	unsigned mode8_320_200;
	unsigned mode8_320_240;
	unsigned mode16_320_200;
	unsigned mode16_320_240;
	unsigned mode16_640_480;
	unsigned mode16_800_600;
	unsigned mode16_1024_768;
	unsigned char gap78[1024];
	char byte478;
	char no_fullscreen;
	char gap47A[2];
};

struct display {
	unsigned bitdepth;
	unsigned width;
	unsigned height;
	unsigned frequency;
};

// NOTE returns 0 on success
int enum_display_modes(void *arg, int (*cmp)(struct display*, void*));

struct video_mode *video_mode_init(struct video_mode *this);
unsigned video_mode_fetch_bounds(struct video_mode *this, int query_interface);
int direct_draw_init(struct video_mode *this, unsigned hInst, unsigned window, struct pal_entry *palette, char opt0, char opt1, int width, int height, int sys_memmap);
void update_palette(struct pal_entry *tbl, unsigned start, unsigned n, struct pal_entry *src);
struct video_mode *video_mode_start_init(struct video_mode *this, const char *title, int a3, const char *a4, int a5);

#endif