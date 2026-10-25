#include <math.h>
#include <string.h>
#include "gfx.h"
#include "font_data.h"

const gfx_font_t *const gfx_small = &font_small;
const gfx_font_t *const gfx_medium = &font_medium;
const gfx_font_t *const gfx_large = &font_large;

static uint16_t *fb;

/* No libm on the device beyond the firmware's few functions. */
static inline int ifloor(float v) { int i = (int)v; return (float)i > v ? i - 1 : i; }
static inline float fabs_(float v) { return v < 0 ? -v : v; }

void gfx_target(uint16_t *buffer)
{
	fb = buffer;
}

uint16_t gfx_mix(uint16_t a, uint16_t b, int alpha)
{
	if (alpha <= 0) return a;
	if (alpha >= 255) return b;
	int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
	int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
	int r = ar + (((br - ar) * alpha) >> 8);
	int g = ag + (((bg - ag) * alpha) >> 8);
	int bl = ab + (((bb - ab) * alpha) >> 8);
	return (uint16_t)(r << 11 | g << 5 | bl);
}

static inline void blend(int x, int y, uint16_t c, int alpha)
{
	if ((unsigned)x >= GFX_W || (unsigned)y >= GFX_H) return;
	uint16_t *p = fb + y * GFX_W + x;
	*p = gfx_mix(*p, c, alpha);
}

static int clip(int *x, int *y, int *w, int *h)
{
	if (*x < 0) { *w += *x; *x = 0; }
	if (*y < 0) { *h += *y; *y = 0; }
	if (*x + *w > GFX_W) *w = GFX_W - *x;
	if (*y + *h > GFX_H) *h = GFX_H - *y;
	return *w > 0 && *h > 0;
}

void gfx_fill(int x, int y, int w, int h, uint16_t c)
{
	if (!clip(&x, &y, &w, &h)) return;
	for (int j = 0; j < h; j++) {
		uint16_t *p = fb + (y + j) * GFX_W + x;
		for (int i = 0; i < w; i++) p[i] = c;
	}
}

void gfx_blend_rect(int x, int y, int w, int h, uint16_t c, int alpha)
{
	if (!clip(&x, &y, &w, &h)) return;
	for (int j = 0; j < h; j++) {
		uint16_t *p = fb + (y + j) * GFX_W + x;
		for (int i = 0; i < w; i++) p[i] = gfx_mix(p[i], c, alpha);
	}
}

void gfx_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom)
{
	for (int j = 0; j < h; j++)
		gfx_fill(x, y + j, w, 1, gfx_mix(top, bottom, h > 1 ? j * 255 / (h - 1) : 0));
}

void gfx_hgradient(int x, int y, int w, int h, uint16_t left, uint16_t right)
{
	for (int i = 0; i < w; i++)
		gfx_fill(x + i, y, 1, h, gfx_mix(left, right, w > 1 ? i * 255 / (w - 1) : 0));
}

/* Coverage of a corner pixel (dx, dy from the corner circle's centre). */
static int corner_alpha(float dx, float dy, float r)
{
	float d = sqrtf(dx * dx + dy * dy) - r;
	if (d <= -0.5f) return 255;
	if (d >= 0.5f) return 0;
	return (int)((0.5f - d) * 255);
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t c)
{
	if (r * 2 > h) r = h / 2;
	if (r * 2 > w) r = w / 2;
	gfx_fill(x + r, y, w - 2 * r, h, c);
	gfx_fill(x, y + r, r, h - 2 * r, c);
	gfx_fill(x + w - r, y + r, r, h - 2 * r, c);
	for (int j = 0; j < r; j++)
		for (int i = 0; i < r; i++) {
			int a = corner_alpha(r - i - 0.5f, r - j - 0.5f, r);
			blend(x + i, y + j, c, a);
			blend(x + w - 1 - i, y + j, c, a);
			blend(x + i, y + h - 1 - j, c, a);
			blend(x + w - 1 - i, y + h - 1 - j, c, a);
		}
}

void gfx_round_frame(int x, int y, int w, int h, int r, uint16_t c)
{
	gfx_hline(x + r, y, w - 2 * r, c);
	gfx_hline(x + r, y + h - 1, w - 2 * r, c);
	gfx_vline(x, y + r, h - 2 * r, c);
	gfx_vline(x + w - 1, y + r, h - 2 * r, c);
	for (int j = 0; j < r; j++)
		for (int i = 0; i < r; i++) {
			float dx = r - i - 0.5f, dy = r - j - 0.5f;
			float d = fabs_(sqrtf(dx * dx + dy * dy) - (r - 0.5f));
			int a = d >= 1 ? 0 : (int)((1 - d) * 255);
			blend(x + i, y + j, c, a);
			blend(x + w - 1 - i, y + j, c, a);
			blend(x + i, y + h - 1 - j, c, a);
			blend(x + w - 1 - i, y + h - 1 - j, c, a);
		}
}

void gfx_hline(int x, int y, int w, uint16_t c) { gfx_fill(x, y, w, 1, c); }
void gfx_vline(int x, int y, int h, uint16_t c) { gfx_fill(x, y, 1, h, c); }

void gfx_dotted_hline(int x, int y, int w, uint16_t c, int alpha)
{
	for (int i = 0; i < w; i += 3) blend(x + i, y, c, alpha);
}

/* Xiaolin Wu's line. */
void gfx_line(float x0, float y0, float x1, float y1, uint16_t c)
{
	int steep = fabs_(y1 - y0) > fabs_(x1 - x0);
	float t;
	if (steep) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
	if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
	float dx = x1 - x0, gradient = dx == 0 ? 1 : (y1 - y0) / dx;
	float y = y0 + gradient * ((float)ifloor(x0 + 0.5f) - x0);
	for (int x = ifloor(x0 + 0.5f); x <= ifloor(x1 + 0.5f); x++) {
		int yi = ifloor(y);
		int a = (int)((y - yi) * 255);
		if (steep) { blend(yi, x, c, 255 - a); blend(yi + 1, x, c, a); }
		else { blend(x, yi, c, 255 - a); blend(x, yi + 1, c, a); }
		y += gradient;
	}
}

/* --- text --- */

static uint32_t next_code(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t c = *p++;
	if (c >= 0xC0 && c < 0xE0 && (*p & 0xC0) == 0x80) c = (c & 0x1F) << 6 | (*p++ & 0x3F);
	else if (c >= 0xE0 && c < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
		c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F);
		p += 2;
	}
	*s = (const char *)p;
	return c;
}

static const gfx_glyph_t *find(const gfx_font_t *f, uint32_t code)
{
	for (int i = 0; i < f->count; i++)
		if (f->glyphs[i].code == code) return &f->glyphs[i];
	return code == '?' ? &f->glyphs[0] : find(f, '?');
}

int gfx_text(const gfx_font_t *f, int x, int y, const char *s, uint16_t c)
{
	int x0 = x;
	while (*s) {
		const gfx_glyph_t *g = find(f, next_code(&s));
		const uint8_t *bits = f->bits + g->offset;
		for (int j = 0, n = 0; j < g->h; j++)
			for (int i = 0; i < g->w; i++, n++) {
				int a = (bits[n >> 1] >> ((n & 1) ? 0 : 4)) & 15;
				if (a) blend(x + g->x + i, y + g->y + j, c, a * 17);
			}
		x += g->advance;
	}
	return x - x0;
}

int gfx_text_width(const gfx_font_t *f, const char *s)
{
	int w = 0;
	while (*s) w += find(f, next_code(&s))->advance;
	return w;
}

void gfx_text_right(const gfx_font_t *f, int right, int y, const char *s, uint16_t c)
{
	gfx_text(f, right - gfx_text_width(f, s), y, s, c);
}

void gfx_text_center(const gfx_font_t *f, int cx, int y, const char *s, uint16_t c)
{
	gfx_text(f, cx - gfx_text_width(f, s) / 2, y, s, c);
}
