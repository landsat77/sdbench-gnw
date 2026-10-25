/*
 * SD Bench — measures the SD card of a Game & Watch running Retro-Go SD,
 * as a GWHB homebrew (/homebrews/SDBench.bin).
 *
 * Six tests, through the firmware's own file functions (what games use):
 *
 *   1. sequential write, 64 KB blocks, 32 MB file (the time to close it
 *      counts: that is when the last data reach the card)
 *   2. sequential read of that file, 64 KB blocks
 *   3. sequential read, 512-byte blocks (2 MB): the cost of small reads
 *   4. random 4 KB reads in the file (IOPS, latency)
 *   5. random 4 KB writes in the file, then close
 *   6. small files: create, write 4 KB, close (100 files), then delete them
 *
 * The 32 MB file is far bigger than any RAM cache, so reads come from the
 * card. Each test draws a live chart of its speed; the results screen
 * compares them, and they are appended to /sdbench.txt on the card. The
 * test files are deleted at the end (and when a test is cancelled with B).
 *
 * Timing: the CPU's cycle counter (DWT), microseconds; drawing is not
 * counted.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "common.h"
#include "gw_lcd.h"
#include "gw_audio.h"
#include "rom_manager.h"
#include "odroid_system.h"
#include "appid.h"
#include "odroid_overlay.h"
#include "odroid_settings.h"
#include "gw_malloc.h"
#include "rg_storage.h"

#ifndef HOST_BUILD
#include "gw_core_bridge.h"
#include "gw_core_i18n.h"
uint32_t get_SystemCoreClock(void);
#else
#include <sys/stat.h>
#include "host_compat.h"
#include "gw_core_i18n.h"
#endif

#include "gfx.h"

#define APP_ID APPID_HOMEBREW
#define FPS 60
#define SAMPLE_RATE 16000
#define AUDIO_LENGTH (SAMPLE_RATE / FPS)
#define VERSION_TEXT "v1.0.1"

#ifdef HOST_BUILD
#define ROOT "host_sd"
#else
#define ROOT RG_STORAGE_ROOT
#endif
#define TEST_FILE ROOT "/sdbench.tmp"
#define FILES_DIR ROOT "/sdbench_files"
#define RESULTS_FILE ROOT "/sdbench.txt"

#define MB (1024 * 1024)
#define BIG_BLOCK (64 * 1024)
#define FILE_SIZE (32 * MB)
#define SMALL_READ_SIZE (2 * MB)
#define RANDOM_READS 1024
#define RANDOM_WRITES 512
#define SMALL_FILES 100
#define MAX_SAMPLES 128

/* --- colours --- */

#define C_BG_TOP RGB(16, 22, 46)
#define C_BG_BOTTOM RGB(6, 8, 20)
#define C_PANEL RGB(22, 29, 58)
#define C_PANEL_EDGE RGB(44, 54, 100)
#define C_TEXT RGB(236, 240, 255)
#define C_DIM RGB(138, 147, 184)
#define C_FAINT RGB(70, 80, 120)
#define C_GOOD RGB(74, 222, 128)
#define C_BAD RGB(248, 113, 113)

/* --- texts --- */

#define T(name, en, fr) static const gw_i18n_entry_t name[] = { { "en", en }, { "fr", fr }, GW_I18N_END }
T(t_subtitle, "SD card speed test", "Test de vitesse de la carte SD");
T(t_start, "Start", "Lancer");
T(t_results_btn, "Results", "Résultats");
T(t_cancel, "Cancel", "Annuler");
T(t_again, "Again", "Relancer");
T(t_home, "Back", "Retour");
T(t_note, "Writes then deletes a 32 MB file", "Écrit puis efface un fichier de 32 Mo");
T(t_results, "Results", "Résultats");
T(t_saved, "Saved to /sdbench.txt", "Enregistré dans /sdbench.txt");
T(t_not_saved, "Could not write /sdbench.txt", "Impossible d'écrire /sdbench.txt");
T(t_test_of, "Test %d of %d", "Test %d sur %d");
T(t_average, "average", "moyenne");
T(t_now, "now", "instant");
T(t_cancelled, "Cancelled", "Annulé");
T(t_error, "Error", "Erreur");
T(t_err_open, "Cannot create the test file", "Impossible de créer le fichier de test");
T(t_err_full, "Card full or write error", "Carte pleine ou erreur d'écriture");
T(t_err_read, "Read error", "Erreur de lecture");
T(t_err_files, "Cannot create the small files", "Impossible de créer les petits fichiers");
T(t_mbs, "MB/s", "Mo/s");
T(t_files_s, "files/s", "fichiers/s");
T(t_iops, "IOPS", "IOPS");
T(t_wait, "Deleting the test files...", "Suppression des fichiers de test...");

typedef enum { UNIT_MBS, UNIT_IOPS, UNIT_FILES } unit_t;

typedef struct {
	const gw_i18n_entry_t *name;
	const gw_i18n_entry_t *detail;
	uint16_t colour;
	unit_t unit;
	const char *log_name;
} test_info_t;

T(n_seq_write, "Sequential write", "Écriture séquentielle");
T(d_seq_write, "64 KB · 32 MB", "64 Ko · 32 Mo");
T(n_seq_read, "Sequential read", "Lecture séquentielle");
T(d_seq_read, "64 KB · 32 MB", "64 Ko · 32 Mo");
T(n_small_read, "Small reads", "Petites lectures");
T(d_small_read, "512 B · 2 MB", "512 o · 2 Mo");
T(n_rnd_read, "Random reads", "Lectures aléatoires");
T(d_rnd_read, "4 KB · 1024×", "4 Ko · 1024×");
T(n_rnd_write, "Random writes", "Écritures aléatoires");
T(d_rnd_write, "4 KB · 512×", "4 Ko · 512×");
T(n_files, "Small files", "Petits fichiers");
T(d_files, "4 KB · 100 files", "4 Ko · 100 fichiers");

enum { TEST_SEQ_WRITE, TEST_SEQ_READ, TEST_SMALL_READ, TEST_RND_READ, TEST_RND_WRITE, TEST_FILES, TEST_COUNT };

static const test_info_t tests[TEST_COUNT] = {
	{ n_seq_write, d_seq_write, RGB(255, 159, 67), UNIT_MBS, "Sequential write 64 KB" },
	{ n_seq_read, d_seq_read, RGB(46, 216, 255), UNIT_MBS, "Sequential read 64 KB" },
	{ n_small_read, d_small_read, RGB(167, 139, 250), UNIT_MBS, "Sequential read 512 B" },
	{ n_rnd_read, d_rnd_read, RGB(74, 222, 128), UNIT_IOPS, "Random read 4 KB" },
	{ n_rnd_write, d_rnd_write, RGB(244, 114, 182), UNIT_IOPS, "Random write 4 KB" },
	{ n_files, d_files, RGB(250, 204, 21), UNIT_FILES, "Small files (create+4 KB)" },
};

typedef struct {
	bool done;
	double mbs;		/* MB/s */
	double ops;		/* IOPS or files/s */
	double latency_ms;	/* average per operation (random tests) */
	float samples[MAX_SAMPLES];	/* speed per slice, in the test's unit */
	int sample_count;
} result_t;

static result_t results[TEST_COUNT];
static bool have_results, results_saved;

/* --- buffers --- */

static uint8_t block[BIG_BLOCK] __attribute__((aligned(32)));

/* --- time --- */

#ifdef HOST_BUILD
static uint64_t time_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}
static void timer_init(void) {}
#else
static uint32_t cycles_per_us = 280, last_cycles;
static uint64_t total_cycles;

static void timer_init(void)
{
	common_emu_enable_dwt_cycles();
	uint32_t clock = get_SystemCoreClock();
	if (clock >= 1000000) cycles_per_us = clock / 1000000;
	last_cycles = common_emu_get_dwt_cycles();
}

/* Call at least every 15 s (the 32-bit counter wraps). */
static uint64_t time_us(void)
{
	uint32_t now = common_emu_get_dwt_cycles();
	total_cycles += (uint32_t)(now - last_cycles);
	last_cycles = now;
	return total_cycles / cycles_per_us;
}
#endif

/* xorshift: random offsets and test data */
static uint32_t rng_state = 0x2545F491;
static uint32_t rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

/* --- input --- */

static odroid_gamepad_state_t pad, last_pad;

static bool pressed(int key)
{
	return pad.values[key] && !last_pad.values[key];
}

static void read_pad(void)
{
	last_pad = pad;
	odroid_input_read_gamepad(&pad);
}

/* --- drawing helpers --- */

static void background(void)
{
	/* lcd_swap() takes effect at the next vblank: until then the "active"
	 * buffer is still the one on screen (drawing into it flickered). */
	while (lcd_is_swap_pending()) wdog_refresh();
	gfx_target(lcd_get_active_buffer());
	gfx_vgradient(0, 0, GFX_W, GFX_H, C_BG_TOP, C_BG_BOTTOM);
}

/* A small SD card icon. */
static void sd_icon(int x, int y, uint16_t c)
{
	gfx_round_rect(x, y, 16, 20, 2, c);
	gfx_fill(x + 11, y, 6, 5, C_BG_TOP);
	gfx_line(x + 10.5f, y, x + 16, y + 5.5f, c);
	for (int i = 0; i < 4; i++) gfx_fill(x + 3 + i * 3, y + 3, 2, 4, C_BG_TOP);
}

static void header(const char *right)
{
	sd_icon(12, 9, RGB(46, 216, 255));
	gfx_text(gfx_medium, 34, 10, "SD Bench", C_TEXT);
	if (right) gfx_text_right(gfx_small, GFX_W - 12, 13, right, C_DIM);
	gfx_hgradient(12, 34, GFX_W - 24, 1, C_PANEL_EDGE, C_BG_TOP);
}

/* A button hint: a rounded key cap with its letter, then the label. */
static int hint(int x, int y, const char *key, const char *label)
{
	int w = gfx_text_width(gfx_medium, key) + 10;
	gfx_round_rect(x, y, w, 18, 9, RGB(232, 236, 252));
	gfx_text_center(gfx_medium, x + w / 2, y + 1, key, RGB(16, 22, 46));
	x += w + 6;
	x += gfx_text(gfx_small, x, y + 3, label, C_TEXT);
	return x + 14;
}

/* "12.34": the firmware's printf has no floating point (%f prints nothing). */
typedef struct { char s[16]; } num_t;

static num_t num(double v, int decimals)
{
	static const unsigned long scale[] = { 1, 10, 100, 1000 };
	num_t n;
	if (!(v > 0)) v = 0;
	if (v > 4.0e9 / scale[decimals]) v = 4.0e9 / scale[decimals];
	unsigned long q = (unsigned long)(v * scale[decimals] + 0.5);
	unsigned long i = q / scale[decimals], f = q % scale[decimals];
	if (decimals == 0) snprintf(n.s, sizeof n.s, "%lu", i);
	else if (decimals == 1) snprintf(n.s, sizeof n.s, "%lu.%lu", i, f);
	else if (decimals == 2) snprintf(n.s, sizeof n.s, "%lu.%02lu", i, f);
	else snprintf(n.s, sizeof n.s, "%lu.%03lu", i, f);
	return n;
}

static void format_value(char *s, size_t n, const result_t *r, unit_t unit)
{
	if (unit == UNIT_MBS) snprintf(s, n, "%s %s", num(r->mbs, 2).s, gw_i18n(t_mbs));
	else if (unit == UNIT_IOPS) snprintf(s, n, "%s %s", num(r->ops, 0).s, gw_i18n(t_iops));
	else snprintf(s, n, "%s %s", num(r->ops, 1).s, gw_i18n(t_files_s));
}

/* A rounded "nice" maximum for a chart axis. */
static float nice_max(float v)
{
	static const float steps[] = { 1, 2, 2.5f, 5, 10 };
	float scale = 1;
	if (v <= 0) return 1;
	while (v > 10 * scale) scale *= 10;
	while (v < scale) scale /= 10;
	for (int i = 0; i < 5; i++)
		if (v <= steps[i] * scale) return steps[i] * scale;
	return 10 * scale;
}

/* The live chart: area under the speed curve, gradient filled. */
static void chart(int x, int y, int w, int h, const result_t *r, int total, uint16_t colour)
{
	gfx_round_rect(x, y, w, h, 8, C_PANEL);
	gfx_round_frame(x, y, w, h, 8, C_PANEL_EDGE);

	int px = x + 34, pw = w - 44, py = y + 10, ph = h - 22;
	float top = 0;
	for (int i = 0; i < r->sample_count; i++)
		if (r->samples[i] > top) top = r->samples[i];
	top = nice_max(top * 1.1f);

	char label[16];
	for (int g = 0; g <= 4; g++) {
		int gy = py + ph - ph * g / 4;
		gfx_dotted_hline(px, gy, pw, C_FAINT, 200);
		float v = top * g / 4;
		if (v >= 1000000) snprintf(label, sizeof label, "%sM", num(v / 1000000, 1).s);
		else if (v >= 10000) snprintf(label, sizeof label, "%sk", num(v / 1000, 0).s);
		else if (v >= 1000) snprintf(label, sizeof label, "%sk", num(v / 1000, 1).s);
		else snprintf(label, sizeof label, "%s", num(v, v >= 10 || v == 0 ? 0 : 1).s);
		gfx_text_right(gfx_small, px - 5, gy - 6, label, C_DIM);
	}
	if (r->sample_count < 1 || total < 2) return;

	float step = (float)pw / (total - 1);
	float prev_x = 0, prev_y = 0;
	for (int i = 0; i < r->sample_count; i++) {
		float sx = px + i * step, sy = py + ph - r->samples[i] / top * ph;
		if (i > 0) {
			/* fill the columns between the previous point and this one */
			for (int cx = (int)prev_x; cx < (int)sx; cx++) {
				float t = (cx - prev_x) / (sx - prev_x);
				int cy = (int)(prev_y + (sy - prev_y) * t);
				int bottom = py + ph;
				for (int yy = cy; yy < bottom; yy++) {
					int a = 150 - (yy - cy) * 130 / (bottom - cy + 1);
					uint16_t *p = (uint16_t *)lcd_get_active_buffer() + yy * GFX_W + cx;
					*p = gfx_mix(*p, colour, a);
				}
			}
			gfx_line(prev_x, prev_y, sx, sy, colour);
			gfx_line(prev_x, prev_y - 1, sx, sy - 1, colour);
		}
		prev_x = sx;
		prev_y = sy;
	}
	/* the newest point */
	gfx_round_rect((int)prev_x - 3, (int)prev_y - 3, 7, 7, 3, C_TEXT);
	gfx_round_rect((int)prev_x - 2, (int)prev_y - 2, 5, 5, 2, colour);
}

static void progress_bar(int x, int y, int w, float done, uint16_t colour)
{
	gfx_round_rect(x, y, w, 8, 4, C_PANEL);
	int fw = (int)(w * (done < 0 ? 0 : done > 1 ? 1 : done));
	if (fw >= 8) {
		gfx_round_rect(x, y, fw, 8, 4, colour);
		gfx_blend_rect(x + 4, y + 1, fw - 8, 2, C_TEXT, 70);
	}
}

#ifdef HOST_BUILD
/* PC tests: SDB_SHOTS=dir saves every SDB_SHOT_EVERY-th picture (PPM). */
#include <stdlib.h>
static void host_shot(void)
{
	static int n;
	const char *dir = getenv("SDB_SHOTS");
	int every = getenv("SDB_SHOT_EVERY") ? atoi(getenv("SDB_SHOT_EVERY")) : 1;
	if (!dir || (n++ % (every > 0 ? every : 1))) return;
	char path[512];
	snprintf(path, sizeof path, "%s/shot_%05d.ppm", dir, n - 1);
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H);
	const uint16_t *p = lcd_get_active_buffer();
	for (int i = 0; i < GFX_W * GFX_H; i++) {
		uint8_t rgb[3] = { (uint8_t)((p[i] >> 11) << 3), (uint8_t)(((p[i] >> 5) & 63) << 2), (uint8_t)((p[i] & 31) << 3) };
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
}
#endif

static void present(void)
{
	common_ingame_overlay();
#ifdef HOST_BUILD
	host_shot();
#endif
	lcd_swap();
}

/* --- screens --- */

static void draw_home(void)
{
	background();
	header(VERSION_TEXT);
	gfx_text(gfx_small, 12, 42, gw_i18n(t_subtitle), C_DIM);

	int y = 62;
	gfx_round_rect(12, y, GFX_W - 24, 6 * 21 + 10, 8, C_PANEL);
	gfx_round_frame(12, y, GFX_W - 24, 6 * 21 + 10, 8, C_PANEL_EDGE);
	for (int i = 0; i < TEST_COUNT; i++) {
		int ty = y + 6 + i * 21;
		gfx_round_rect(24, ty + 5, 8, 8, 4, tests[i].colour);
		gfx_text(gfx_medium, 40, ty, gw_i18n(tests[i].name), C_TEXT);
		gfx_text_right(gfx_small, GFX_W - 24, ty + 3, gw_i18n(tests[i].detail), C_DIM);
	}

	gfx_text_center(gfx_small, GFX_W / 2, 200, gw_i18n(t_note), C_DIM);
	int x = 12;
	x = hint(x, 216, "A", gw_i18n(t_start));
	if (have_results) hint(x, 216, "B", gw_i18n(t_results_btn));
	present();
}

static void draw_running(int t, float done, const char *now_text)
{
	const test_info_t *info = &tests[t];
	const result_t *r = &results[t];
	char s[64], v[32];

	background();
	snprintf(s, sizeof s, gw_i18n(t_test_of), t + 1, TEST_COUNT);
	header(s);

	gfx_round_rect(12, 45, 8, 8, 4, info->colour);
	gfx_text(gfx_medium, 26, 41, gw_i18n(info->name), C_TEXT);
	gfx_text_right(gfx_small, GFX_W - 12, 44, gw_i18n(info->detail), C_DIM);

	/* the running average, big, with its unit; the last sample beside */
	format_value(v, sizeof v, r, info->unit);
	char *unit = strchr(v, ' ');
	if (unit) *unit++ = 0;
	int x = 12 + gfx_text(gfx_large, 12, 58, v, info->colour);
	gfx_text(gfx_small, x + 6, 60, gw_i18n(t_average), C_FAINT);
	if (unit) gfx_text(gfx_medium, x + 6, 72, unit, C_DIM);
	snprintf(s, sizeof s, "%s %s", gw_i18n(t_now), now_text);
	gfx_text_right(gfx_small, GFX_W - 12, 75, s, C_DIM);

	chart(12, 96, GFX_W - 24, 100, r, MAX_SAMPLES, info->colour);
	progress_bar(12, 204, GFX_W - 24 - 40, done, info->colour);
	snprintf(s, sizeof s, "%d%%", (int)(done * 100 + 0.5f));
	gfx_text_right(gfx_small, GFX_W - 12, 201, s, C_TEXT);
	hint(12, 216, "B", gw_i18n(t_cancel));
	present();
}

static void draw_results(void)
{
	char s[64];
	background();
	header(gw_i18n(t_results));

	double max_mbs = 0.001;
	for (int i = 0; i < TEST_COUNT; i++)
		if (results[i].done && results[i].mbs > max_mbs) max_mbs = results[i].mbs;

	for (int i = 0; i < TEST_COUNT; i++) {
		const result_t *r = &results[i];
		int y = 42 + i * 27;
		int nx = 14 + gfx_text(gfx_small, 14, y + 2, gw_i18n(tests[i].name), C_DIM);
		int bx = 14, bw = GFX_W - 28, by = y + 17;
		gfx_round_rect(bx, by, bw, 6, 3, C_PANEL);
		if (r->done) {
			/* random and small-file tests are compared on MB/s too, so the
			   bars show how much slower small accesses are */
			double frac = r->mbs / max_mbs;
			int fw = (int)(bw * frac);
			if (fw < 6) fw = 6;
			gfx_round_rect(bx, by, fw, 6, 3, tests[i].colour);
			format_value(s, sizeof s, r, tests[i].unit);
			int value_x = GFX_W - 14 - gfx_text_width(gfx_medium, s);
			gfx_text(gfx_medium, value_x, y, s, C_TEXT);
			if (tests[i].unit != UNIT_MBS) {
				/* the same in MB/s, if there is room */
				snprintf(s, sizeof s, "· %s %s", num(r->mbs, 2).s, gw_i18n(t_mbs));
				if (nx + 6 + gfx_text_width(gfx_small, s) < value_x - 8)
					gfx_text(gfx_small, nx + 6, y + 2, s, C_FAINT);
			}
		} else {
			gfx_text_right(gfx_medium, GFX_W - 14, y, "-", C_DIM);
		}
	}

	gfx_text(gfx_small, 14, 205, gw_i18n(results_saved ? t_saved : t_not_saved),
	         results_saved ? C_GOOD : C_BAD);
	int x = 12;
	x = hint(x, 220, "A", gw_i18n(t_again));
	hint(x, 220, "B", gw_i18n(t_home));
	present();
}

static void draw_message(const gw_i18n_entry_t *title, const gw_i18n_entry_t *text, uint16_t colour)
{
	background();
	header(NULL);
	gfx_round_rect(30, 80, GFX_W - 60, 80, 10, C_PANEL);
	gfx_round_frame(30, 80, GFX_W - 60, 80, 10, colour);
	if (title) gfx_text_center(gfx_medium, GFX_W / 2, 96, gw_i18n(title), colour);
	if (text) gfx_text_center(gfx_small, GFX_W / 2, 124, gw_i18n(text), C_TEXT);
	present();
}

/* --- the tests --- */

typedef struct {
	int test;
	uint64_t busy_us;	/* time spent in file functions */
	uint64_t bytes, ops;
	uint64_t slice_us, slice_bytes, slice_ops;
	int slice_target;	/* operations per chart sample */
	int slice_count;
	bool cancelled;
} run_t;

static run_t run;

static void run_begin(int t, int slice_ops)
{
	memset(&run, 0, sizeof run);
	memset(&results[t], 0, sizeof results[t]);
	run.test = t;
	run.slice_target = slice_ops;
	draw_running(t, 0, "-");
}

static double value_of(uint64_t bytes, uint64_t ops, uint64_t us, unit_t unit)
{
	if (!us) return 0;
	if (unit == UNIT_MBS) return (double)bytes / MB / (us / 1e6);
	return (double)ops / (us / 1e6);
}

static void update_result(void)
{
	result_t *r = &results[run.test];
	r->mbs = value_of(run.bytes, run.ops, run.busy_us, UNIT_MBS);
	r->ops = value_of(run.bytes, run.ops, run.busy_us, UNIT_IOPS);
	r->latency_ms = run.ops ? run.busy_us / 1000.0 / run.ops : 0;
}

/* One operation done: count it; every slice_target operations, add a chart
   sample, redraw and look at B. Returns false when cancelled. */
static bool step(uint64_t us, uint64_t bytes, float done)
{
	run.busy_us += us;
	run.bytes += bytes;
	run.ops++;
	run.slice_us += us;
	run.slice_bytes += bytes;
	run.slice_ops++;
	wdog_refresh();
	if (run.slice_ops < (uint64_t)run.slice_target && done < 1) return true;

	result_t *r = &results[run.test];
	unit_t unit = tests[run.test].unit;
	double now = value_of(run.slice_bytes, run.slice_ops, run.slice_us, unit);
	if (r->sample_count < MAX_SAMPLES) r->samples[r->sample_count++] = (float)now;
	update_result();
	run.slice_us = run.slice_bytes = run.slice_ops = 0;

	char s[32];
	snprintf(s, sizeof s, "%s", num(now, unit == UNIT_MBS ? 2 : 0).s);
	draw_running(run.test, done, s);
	read_pad();
	if (pressed(ODROID_INPUT_B)) run.cancelled = true;
	return !run.cancelled;
}

static int test_seq_write(void)
{
	const int blocks = FILE_SIZE / BIG_BLOCK;
	run_begin(TEST_SEQ_WRITE, blocks / MAX_SAMPLES);
	for (int i = 0; i < BIG_BLOCK / 4; i++) ((uint32_t *)block)[i] = rng();

	uint64_t t0 = time_us();
	FILE *f = fopen(TEST_FILE, "wb");
	uint64_t open_us = time_us() - t0;
	if (!f) return -1;
	run.busy_us += open_us;
	for (int i = 0; i < blocks; i++) {
		((uint32_t *)block)[0] = i;
		t0 = time_us();
		size_t n = fwrite(block, 1, BIG_BLOCK, f);
		uint64_t us = time_us() - t0;
		if (n != BIG_BLOCK) { fclose(f); return -2; }
		if (i == blocks - 1) {
			/* the close writes what is still cached: it belongs to the test */
			t0 = time_us();
			fclose(f);
			us += time_us() - t0;
			f = NULL;
		}
		if (!step(us, BIG_BLOCK, (float)(i + 1) / blocks)) break;
	}
	if (f) fclose(f);
	return 0;
}

static int test_seq_read(void)
{
	const int blocks = FILE_SIZE / BIG_BLOCK;
	run_begin(TEST_SEQ_READ, blocks / MAX_SAMPLES);
	FILE *f = fopen(TEST_FILE, "rb");
	if (!f) return -3;
	for (int i = 0; i < blocks; i++) {
		uint64_t t0 = time_us();
		size_t n = fread(block, 1, BIG_BLOCK, f);
		uint64_t us = time_us() - t0;
		if (n != BIG_BLOCK) { fclose(f); return -3; }
		if (!step(us, BIG_BLOCK, (float)(i + 1) / blocks)) break;
	}
	fclose(f);
	return 0;
}

static int test_small_read(void)
{
	const int reads = SMALL_READ_SIZE / 512;
	run_begin(TEST_SMALL_READ, reads / MAX_SAMPLES);
	FILE *f = fopen(TEST_FILE, "rb");
	if (!f) return -3;
	/* from the middle of the file: not what the previous test just read */
	fseek(f, FILE_SIZE / 2, SEEK_SET);
	for (int i = 0; i < reads; i++) {
		uint64_t t0 = time_us();
		size_t n = fread(block, 1, 512, f);
		uint64_t us = time_us() - t0;
		if (n != 512) { fclose(f); return -3; }
		if (!step(us, 512, (float)(i + 1) / reads)) break;
	}
	fclose(f);
	return 0;
}

static int test_random(int t, int count, bool write)
{
	run_begin(t, count / MAX_SAMPLES);
	FILE *f = fopen(TEST_FILE, write ? "r+b" : "rb");
	if (!f) return write ? -2 : -3;
	const uint32_t slots = FILE_SIZE / 4096;
	for (int i = 0; i < count; i++) {
		long offset = (long)(rng() % slots) * 4096;
		uint64_t t0 = time_us();
		size_t n;
		if (fseek(f, offset, SEEK_SET) != 0) n = 0;
		else n = write ? fwrite(block, 1, 4096, f) : fread(block, 1, 4096, f);
		uint64_t us = time_us() - t0;
		if (n != 4096) { fclose(f); return write ? -2 : -3; }
		if (write && i == count - 1) {
			t0 = time_us();
			fclose(f);
			us += time_us() - t0;
			f = NULL;
		}
		if (!step(us, 4096, (float)(i + 1) / count)) break;
	}
	if (f) fclose(f);
	return 0;
}

static void small_file_name(char *s, size_t n, int i)
{
	snprintf(s, n, FILES_DIR "/f%03d.tmp", i);
}

static void make_dir(const char *path)
{
#ifdef HOST_BUILD
	mkdir(path, 0777);
#else
	rg_storage_mkdir(path);
#endif
}

static int test_files(void)
{
	run_begin(TEST_FILES, SMALL_FILES / 50);
	make_dir(FILES_DIR);
	char name[64];
	int made = 0, error = 0;
	for (int i = 0; i < SMALL_FILES; i++) {
		small_file_name(name, sizeof name, i);
		uint64_t t0 = time_us();
		FILE *f = fopen(name, "wb");
		size_t n = f ? fwrite(block, 1, 4096, f) : 0;
		if (f) fclose(f);
		uint64_t us = time_us() - t0;
		if (!f || n != 4096) { error = -4; break; }
		made++;
		if (!step(us, 4096, (float)(i + 1) / SMALL_FILES)) break;
	}
	return error;
}

/* Delete everything the tests made. */
static void clean_up(void)
{
	char name[64];
	for (int i = 0; i < SMALL_FILES; i++) {
		small_file_name(name, sizeof name, i);
		remove(name);
		if (i % 16 == 0) wdog_refresh();
	}
	remove(FILES_DIR);
	remove(TEST_FILE);
}

static bool save_results(void)
{
	FILE *f = fopen(RESULTS_FILE, "a");
	if (!f) return false;
	char date[48] = "";
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	if (tm) snprintf(date, sizeof date, "%04d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1,
	                 tm->tm_mday, tm->tm_hour, tm->tm_min);
	fprintf(f, "SD Bench %s - %s\n", VERSION_TEXT, date);
	for (int i = 0; i < TEST_COUNT; i++) {
		const result_t *r = &results[i];
		if (!r->done) {
			fprintf(f, "  %-28s -\n", tests[i].log_name);
			continue;
		}
		if (tests[i].unit == UNIT_MBS)
			fprintf(f, "  %-28s %8s MB/s\n", tests[i].log_name, num(r->mbs, 2).s);
		else if (tests[i].unit == UNIT_IOPS)
			fprintf(f, "  %-28s %8s IOPS  %6s MB/s  %6s ms\n", tests[i].log_name, num(r->ops, 0).s,
			        num(r->mbs, 2).s, num(r->latency_ms, 2).s);
		else
			fprintf(f, "  %-28s %8s files/s  %6s MB/s\n", tests[i].log_name, num(r->ops, 1).s,
			        num(r->mbs, 2).s);
	}
	fprintf(f, "\n");
	fclose(f);
	return true;
}

/* All the tests; false when cancelled or failed (message shown). */
static bool run_all(void)
{
	memset(results, 0, sizeof results);
	audio_clear_buffers();
	timer_init();
	rng_state = 0x2545F491;

	int error = 0;
	for (int t = 0; t < TEST_COUNT && !error && !run.cancelled; t++) {
		switch (t) {
		case TEST_SEQ_WRITE: error = test_seq_write(); break;
		case TEST_SEQ_READ: error = test_seq_read(); break;
		case TEST_SMALL_READ: error = test_small_read(); break;
		case TEST_RND_READ: error = test_random(t, RANDOM_READS, false); break;
		case TEST_RND_WRITE: error = test_random(t, RANDOM_WRITES, true); break;
		case TEST_FILES: error = test_files(); break;
		}
		if (!error && !run.cancelled) {
			update_result();
			results[t].done = true;
		}
	}

	draw_message(NULL, t_wait, C_DIM);
	clean_up();

	if (error) {
		const gw_i18n_entry_t *text = error == -1 ? t_err_open : error == -2 ? t_err_full
		                              : error == -3 ? t_err_read : t_err_files;
		draw_message(t_error, text, C_BAD);
	} else if (run.cancelled) {
		draw_message(t_cancelled, NULL, C_DIM);
	}
	have_results = false;
	for (int t = 0; t < TEST_COUNT; t++) have_results |= results[t].done;
	results_saved = have_results && save_results();
	if (error || run.cancelled) {
		/* leave the message up for a moment */
		for (int i = 0; i < 90; i++) {
			wdog_refresh();
			lcd_wait_for_vblank();
		}
	}
	return !error && !run.cancelled;
}

/* --- system callbacks --- */

static bool LoadState(const char *path) { (void)path; return false; }
static bool SaveState(const char *path) { (void)path; return false; }
static void Shutdown(void) {}
static void SramSave(void) {}

static void (*redraw)(void);

static void *Screenshot(void)
{
	lcd_wait_for_vblank();
	if (redraw) redraw();
	return lcd_get_active_buffer();
}

static void SleepWake(void)
{
	odroid_audio_init(SAMPLE_RATE);
	audio_clear_buffers();
	audio_start_playing(AUDIO_LENGTH);
}

static void silence(void)
{
	if (!common_emu_sound_loop_is_muted()) {
		int16_t *buf = audio_get_active_buffer();
		uint16_t len = audio_get_buffer_length();
		if (buf && len) memset(buf, 0, len * sizeof(int16_t));
	}
	common_emu_sound_sync(false);
}

/* --- main --- */

void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
	odroid_gamepad_state_t joystick;
	odroid_dialog_choice_t options[1] = { ODROID_DIALOG_CHOICE_LAST };
	(void)load_state;
	(void)save_slot;

#ifndef HOST_BUILD
	gw_core_bridge_init();
#endif
	common_emu_state.pause_after_frames = start_paused ? 2 : 0;
	common_emu_state.frame_time_10us = (uint16_t)(100000 / FPS + 0.5f);
	lcd_set_refresh_rate(FPS);
	odroid_system_init(APP_ID, SAMPLE_RATE);
	odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, &Shutdown, &SleepWake, &SramSave, NULL);
	audio_start_playing(AUDIO_LENGTH);
	lcd_clear_buffers();
#ifdef HOST_BUILD
	make_dir(ROOT);
#endif

	redraw = draw_home;
	while (1) {
		wdog_refresh();
		bool draw_frame = common_emu_frame_loop();
		odroid_input_read_gamepad(&joystick);
		common_emu_input_loop(&joystick, options, redraw);
		last_pad = pad;
		pad = joystick;
#ifdef HOST_BUILD
		/* PC tests: SDB_AUTOSTART runs the tests once, shows the results, quits. */
		if (getenv("SDB_AUTOSTART")) {
			static int frames;
			frames++;
			if (frames == 20) pad.values[ODROID_INPUT_A] = 1;
			if (redraw == draw_results && ++frames > 60) exit(0);
		}
#endif

		if (redraw == draw_home) {
			if (pressed(ODROID_INPUT_A)) {
				bool ok = run_all();
				redraw = have_results && (ok || results_saved) ? draw_results : draw_home;
				odroid_input_read_gamepad(&pad);
				last_pad = pad;	/* no repeat of the button that ended it */
				continue;
			}
			if (pressed(ODROID_INPUT_B) && have_results) redraw = draw_results;
		} else if (redraw == draw_results) {
			if (pressed(ODROID_INPUT_A)) {
				run_all();
				redraw = have_results ? draw_results : draw_home;
				odroid_input_read_gamepad(&pad);
				last_pad = pad;
				continue;
			}
			if (pressed(ODROID_INPUT_B)) redraw = draw_home;
		}

		if (draw_frame) redraw();
		silence();
	}
}
