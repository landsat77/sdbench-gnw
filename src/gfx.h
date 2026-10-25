/*
 * Small RGB565 drawing library for SD Bench: rectangles, gradients, blended
 * pixels, anti-aliased lines and text (fonts from tools/make_font.py).
 * Draws into a 320 x 240 frame buffer given with gfx_target().
 */

#ifndef SDB_GFX_H
#define SDB_GFX_H

#include <stdint.h>

#define GFX_W 320
#define GFX_H 240

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

typedef struct {
	uint16_t code;		/* Unicode code point */
	uint8_t advance;	/* pen advance */
	uint8_t w, h;		/* bitmap size */
	int8_t x, y;		/* bitmap offset from the pen (y from the line's top) */
	uint32_t offset;	/* in bits[] (4-bit alpha, 2 pixels a byte) */
} gfx_glyph_t;

typedef struct {
	uint8_t height, ascent;
	uint16_t count;
	const gfx_glyph_t *glyphs;
	const uint8_t *bits;
} gfx_font_t;

extern const gfx_font_t *const gfx_small, *const gfx_medium, *const gfx_large;

void gfx_target(uint16_t *fb);
uint16_t gfx_mix(uint16_t a, uint16_t b, int alpha);	/* alpha 0-255 = b over a */

void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_blend_rect(int x, int y, int w, int h, uint16_t c, int alpha);
void gfx_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom);
void gfx_hgradient(int x, int y, int w, int h, uint16_t left, uint16_t right);
void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t c);
void gfx_round_frame(int x, int y, int w, int h, int r, uint16_t c);
void gfx_hline(int x, int y, int w, uint16_t c);
void gfx_vline(int x, int y, int h, uint16_t c);
void gfx_dotted_hline(int x, int y, int w, uint16_t c, int alpha);
void gfx_line(float x0, float y0, float x1, float y1, uint16_t c);	/* anti-aliased */

/* Text: y is the top of the line; returns the width. UTF-8. */
int gfx_text(const gfx_font_t *f, int x, int y, const char *s, uint16_t c);
int gfx_text_width(const gfx_font_t *f, const char *s);
void gfx_text_right(const gfx_font_t *f, int right, int y, const char *s, uint16_t c);
void gfx_text_center(const gfx_font_t *f, int cx, int y, const char *s, uint16_t c);

#endif
