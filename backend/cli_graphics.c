/*
	cli_graphics.c -- apl_cli's stand-in for the Qt front end's
	graphics bridge (graph_bridge.cpp), so grfuncs.c links into apl_cli
	and the graphics functions can be regression-tested (Phase 7a of
	MIDI_SEQUENCER_NOTES.md; first for ⎕WCLICK, tests/wclick.apl).

	What is modelled: a table of windows with the state the window
	functions report -- open/closed, hidden, minimized, title, tag,
	size and position, pen, font size -- following the contracts in
	graph_bridge.h (handles reused lowest first, ⎕WNUMS in creation
	order, ⎕WCTRL 0/1 doing nothing to a hidden window, and so on).

	What is not: nothing is drawn. ⎕WPIXEL reads every point inside a
	window as white (0xFFFFFF), whatever was drawn; ⎕WTEXTW is 8
	pixels a character; images never load (⎕WILOAD's "can't load"),
	can't be saved, and ⎕FDIALOG is always cancelled (after printing
	what it was given -- see gw_file_dialog()). The desktop is
	1920x1080. A test that depends on any of these is testing this
	file, not the interpreter.

	Window events: ⎕WCLICK waits in the event hub (evhub.c), which this
	file puts in test mode at startup (cli_graphics_init(), from
	apl_cli's main()): clicks, closes, Escape and Ctrl-C come from
	⎕EVPOST, and a posted close drops the window from this table when
	it is delivered, as a real close would.
*/

#include <stdio.h>
#include <stdlib.h>	/* getenv() -- gw_file_dialog() */
#include <string.h>
#include <stdint.h>

#include "graph_bridge.h"
#include "evhub.h"

#define MAXWIN	64
#define DESK_W	1920
#define DESK_H	1080
#define FONT_PX	14	/* GraphWindow's kDefaultFontPixelSize */

static struct win {
	int	open, hidden, minimized;
	long	order;		/* creation order, for ⎕WNUMS */
	long	w, h, x, y;
	long	pen[4];		/* r, g, b, width */
	long	font;
	char	title[256], tag[256];
} wins[MAXWIN + 1];		/* index = handle; 0 unused */

static long next_order;

static struct win *
find(long handle)
{
	if (handle < 1 || handle > MAXWIN || !wins[handle].open) return NULL;
	return &wins[handle];
}

static void
copy_str(char *dst, const char *src, size_t size)
{
	if (!src) src = "";
	strncpy(dst, src, size - 1);
	dst[size - 1] = 0;
}

/* ── The hub's hooks ───────────────────────────────────────────────── */

static void
test_close(long handle)
{
	gw_close(handle);
}

void
cli_graphics_init(void)
{
	evhub_set_vis(gw_vis);
	evhub_set_test(test_close);
}

/* ── graph_bridge.h ────────────────────────────────────────────────── */

long
gw_open(const char *title, long x, long y, long w, long h, long visible)
{
	long k;

	if (w <= 0 || h <= 0) return -1;
	for (k = 1; k <= MAXWIN && wins[k].open; k++)
		;
	if (k > MAXWIN) return -1;
	memset(&wins[k], 0, sizeof wins[k]);
	wins[k].open = 1;
	wins[k].hidden = !visible;
	wins[k].order = ++next_order;
	wins[k].w = w;
	wins[k].h = h;
	wins[k].x = x;
	wins[k].y = y;
	wins[k].pen[3] = 1;
	wins[k].font = FONT_PX;
	copy_str(wins[k].title, title, sizeof wins[k].title);
	return k;
}

long
gw_load_image(const char *filename, long x, long y, long visible)
{
	(void)filename; (void)x; (void)y; (void)visible;
	return -1;
}

void
gw_close(long handle)
{
	struct win *p = find(handle);
	if (p) p->open = 0;
}

/* handles in creation order, optionally only those whose tag matches */
static long
list(const char *pattern, int32_t *buf, long max)
{
	long n = 0, k, best, last = 0;
	size_t plen = 0;
	int prefix = 0;

	if (pattern)
	{
		plen = strlen(pattern);
		prefix = plen > 0 && pattern[plen - 1] == '*';
		if (prefix) plen--;
	}
	while (n < max)
	{
		best = 0;
		for (k = 1; k <= MAXWIN; k++)
			if (wins[k].open && wins[k].order > last
			    && (!best || wins[k].order < wins[best].order))
				best = k;
		if (!best) break;
		last = wins[best].order;
		if (pattern && (prefix ? strncmp(wins[best].tag, pattern, plen) != 0
				       : strcmp(wins[best].tag, pattern) != 0))
			continue;
		buf[n++] = (int32_t)best;
	}
	return n;
}

long
gw_nums(int32_t *buf, long maxCount)
{
	return list(NULL, buf, maxCount);
}

long
gw_nums_matching(const char *pattern, int32_t *buf, long maxCount)
{
	return list(pattern ? pattern : "", buf, maxCount);
}

long
gw_size(long handle, int32_t *buf)
{
	struct win *p;

	buf[0] = buf[1] = buf[2] = buf[3] = 0;
	if (handle == -1)
	{
		buf[0] = DESK_W;
		buf[1] = DESK_H;
		return 1;
	}
	if (!(p = find(handle))) return 0;
	buf[0] = (int32_t)p->w;
	buf[1] = (int32_t)p->h;
	buf[2] = (int32_t)p->x;
	buf[3] = (int32_t)p->y;
	return 1;
}

void
gw_title(long handle, const char *title)
{
	struct win *p = find(handle);
	if (p) copy_str(p->title, title, sizeof p->title);
}

void
gw_tag(long handle, const char *tag)
{
	struct win *p = find(handle);
	if (p) copy_str(p->tag, tag, sizeof p->tag);
}

void
gw_tagof(long handle, char *buf, long bufsize)
{
	struct win *p = find(handle);
	if (bufsize <= 0) return;
	copy_str(buf, p ? p->tag : "", (size_t)bufsize);
}

void
gw_ctrl(long handle, long opcode, long x, long y)
{
	struct win *p = find(handle);

	if (!p) return;
	switch (opcode)
	{
		case 0: if (!p->hidden) p->minimized = 1; break;
		case 1: if (!p->hidden) p->minimized = 0; break;
		case 4: p->x = x; p->y = y; break;
		case 5: p->hidden = 1; break;
		/* a window hidden while minimized comes back normal */
		case 6: if (p->hidden) { p->hidden = 0; p->minimized = 0; } break;
	}
}

void
gw_pen(long handle, long r, long g, long b, long width)
{
	struct win *p = find(handle);

	if (!p) return;
	if (r != -1) { p->pen[0] = r; p->pen[1] = g; p->pen[2] = b; }
	if (width != -1) p->pen[3] = width < 1 ? 1 : width;
}

long
gw_pen_query(long handle, int32_t *buf)
{
	struct win *p = find(handle);
	int i;

	if (!p) return 0;
	for (i = 0; i < 4; i++) buf[i] = (int32_t)p->pen[i];
	return 1;
}

void gw_poly(long handle, const int32_t *ops, long count)
{ (void)handle; (void)ops; (void)count; }
void gw_points(long handle, const int32_t *data, long count, long ncols)
{ (void)handle; (void)data; (void)count; (void)ncols; }
void gw_clear(long handle, long packedColor)
{ (void)handle; (void)packedColor; }
void gw_fpoly(long handle, const int32_t *ops, long count)
{ (void)handle; (void)ops; (void)count; }

long
gw_vis(const int32_t *handles, long count, int32_t *buf)
{
	struct win *p;
	long i;

	for (i = 0; i < count; i++)
	{
		p = find(handles[i]);
		buf[i] = !p ? -1 : p->hidden ? 0 : p->minimized ? 2 : 1;
	}
	return count;
}

long
gw_pixel(long handle, const int32_t *points, long count, int32_t *buf)
{
	struct win *p = find(handle);
	long i;

	if (!p) return 0;
	for (i = 0; i + 1 < count; i += 2)
		buf[i / 2] = (points[i] >= 0 && points[i] < p->w
			      && points[i + 1] >= 0 && points[i + 1] < p->h)
			     ? 0xFFFFFF : -1;
	return 1;
}

void gw_text(const char *text, long handle, long x, long y)
{ (void)text; (void)handle; (void)x; (void)y; }

long
gw_font_query(long handle)
{
	struct win *p = find(handle);
	return p ? p->font : -1;
}

long
gw_font(long handle, long size)
{
	struct win *p = find(handle);
	long prior;

	if (!p) return -1;
	prior = p->font;
	if (size != -1) p->font = size;
	return prior;
}

long
gw_font_face(long handle, const char *spec)
{
	struct win *p = find(handle);
	(void)spec;
	return p ? p->font : -1;
}

void
gw_font_face_query(long handle, char *buf, long bufsize)
{
	if (bufsize <= 0) return;
	copy_str(buf, find(handle) ? "default" : "", (size_t)bufsize);
}

long
gw_text_width(const char *text, long handle)
{
	if (!find(handle)) return -1;
	return 8 * (long)strlen(text ? text : "");
}

void gw_fill(long handle, long x, long y, long tol)
{ (void)handle; (void)x; (void)y; (void)tol; }

void
gw_blit(long dst, long src, long dx, long dy, long sx, long sy, long sw, long sh)
{
	(void)dst; (void)src; (void)dx; (void)dy;
	(void)sx; (void)sy; (void)sw; (void)sh;
}

long
gw_reload_image(long handle, const char *filename)
{
	(void)filename;
	return find(handle) ? -1 : -2;
}

long
gw_query_image(const char *filename, int32_t *buf)
{
	(void)filename; (void)buf;
	return 0;
}

long
gw_save_image(long handle, const char *filename, long overwrite)
{
	(void)filename; (void)overwrite;
	return find(handle) ? -1 : -2;
}

/* ⎕FDIALOG is still always cancelled, but first prints one line showing
   what the dialog would have been given (Oct 21, 2026), so a test can
   check how grfuncs.c split and resolved the filter's folder prefix:

	[FDIALOG title=<title> dir=<folder> filter=<filter>]

   A folder under the test's scratch $APLHOME is shown as $APLHOME/...
   so the line reads the same wherever the tests run, and the folder is
   shown in lower case: on Windows the as-typed MIDI/ is found on the
   first try, on Linux only by the lower-case retry, and the transcripts
   should still match (a folder not found at all shows as dir= on
   both). Printed with printf() and flushed, since nothing else in this
   file writes output. */
void
gw_file_dialog(const char *title, const char *dir, const char *filter,
	       char *buf, long bufsize)
{
	const char *home = getenv("APLHOME");
	size_t hl = home ? strlen(home) : 0;
	const char *rest;

	if (!title) title = "";
	if (!dir) dir = "";
	if (!filter) filter = "";
	printf("[FDIALOG title=%s dir=", title);
	rest = dir;
	if (hl && strncmp(dir, home, hl) == 0)
	{
		printf("$APLHOME");
		rest = dir + hl;
	}
	for (; *rest; rest++)
		putchar(*rest >= 'A' && *rest <= 'Z' ? *rest + 32 : *rest);
	printf(" filter=%s]\n", filter);
	fflush(stdout);
	if (bufsize > 0) buf[0] = 0;
}
