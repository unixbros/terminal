#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <err.h>
#include "all.h"

struct video_fcn_tbl gfx;

struct font {
	xcb_font_t ptr;
	int descent, height, width;
	uint16_t char_max, char_min;
	xcb_charinfo_t *width_lut;
};

struct gfxinternals {
	xcb_connection_t *conn;
	xcb_screen_t *scr;
	xcb_window_t win;
	xcb_gcontext_t gc;
	struct font *font;
	char fontline[1024]; /* XXX different size? */
	xcb_key_symbols_t *keysyms;

	off_t width, height;
	struct tattr *map;
	int ttydead;
	int wants_redraw;
} t;

void loop(void);
void keypress(int, int);
void buttonpress(int, int);

int
utf_len(char *str) {
	uint8_t *utf = (uint8_t *)str;

	if (utf[0] < 0x80)
		return 1;
	else if ((utf[0] & 0xe0) == 0xc0)
		return 2;
	else if ((utf[0] & 0xf0) == 0xe0)
		return 3;
	else if ((utf[0] & 0xf8) == 0xf0)
		return 4;
	else if ((utf[0] & 0xfc) == 0xf8)
		return 5;
	else if ((utf[0] & 0xfe) == 0xfc)
		return 6;

	return 1;
}

uint16_t
utf_combine(char *str) {
	uint16_t c;
	uint8_t *utf = (uint8_t *)str;

	switch (utf_len(str)) {
	case 1:
		c = *utf;
		break;
	case 2:
		c = (*utf & 0x1f) << 6 | (utf[1] & 0x3f);
		break;
	case 3:
		c = (*utf & 0xf) << 12 | (utf[1] & 0x3f) << 6 | (utf[2] & 0x3f);
		break;
	case 4:
	case 5:
	case 6:
		c = 0xfffd;
		break;
	}

	return c >> 8 | c << 8;
}

void
resize(int x, int y) {
	uint32_t mask, values[3];
	struct tattr *ret;
	struct winsize ws;

	mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
	values[0] = t.font->width  * x;
	values[1] = t.font->height * y;
	xcb_configure_window(t.conn, t.win, mask, values);

	ret = malloc(sizeof(struct tattr) * (x * y));
	if (ret == NULL)
		err(1, "malloc");

	memset(ret, 0, sizeof(struct tattr) * (x * y));

	if (t.map != NULL) {
		memcpy(ret, t.map, t.width * t.height);
		free(t.map);
	}

	ws.ws_col = t.width  = x;
	ws.ws_row = t.height = y;

	t.map = ret;

	/* XXX: also store window pixel size */

	/* XXX change terminal device size, and send SIGWINCH
	(void)ioctl(d, TIOCSWINSZ, &ws);
	(void)kill(t.pid, SIGWINCH);
	*/
}

void
redraw(void) {
	/* stub */
}

xcb_keysym_t
xcb_get_keysym(xcb_keycode_t keycode, uint16_t state)
{
	if (!(t.keysyms = xcb_key_symbols_alloc(t.conn)))
		return 0;

	xcb_keysym_t sym = xcb_key_symbols_get_keysym(t.keysyms, keycode, state);

	return sym;
}

void
set_cell(int x, int y, char *str) {
	uint16_t c;
	uint8_t *utf = (uint8_t *)str;

	off_t pos;

	pos = x + (y * t.width);
	c = utf_combine(str);
	t.map[pos].utf = c;
	/* XXX fix later
	term.map[pos].fg = t.fg;
	term.map[pos].bg = t.bg;
	*/
}

struct font *
load_font(xcb_gcontext_t gc, const char *name) {
	xcb_query_font_cookie_t queryreq;
	xcb_query_font_reply_t *font_info;
	xcb_void_cookie_t cookie;
	xcb_font_t font;
	struct font *r;

	font = xcb_generate_id(t.conn);

	cookie = xcb_open_font_checked(t.conn, font, strlen(name), name);
	if (xcb_request_check(t.conn, cookie))
		err(1, "could not load font '%s'", name);

	r = malloc(sizeof(struct font));
	if (r == NULL)
		err(1, "malloc");

	queryreq = xcb_query_font(t.conn, font);
	font_info = xcb_query_font_reply(t.conn, queryreq, NULL);

	r->ptr = font;
	r->descent = font_info->font_descent;
	r->height = font_info->font_ascent + font_info->font_descent;
	r->width = font_info->max_bounds.character_width;
	r->char_max = font_info->max_byte1 << 8 | font_info->max_char_or_byte2;
	r->char_min = font_info->min_byte1 << 8 | font_info->min_char_or_byte2;

	xcb_change_gc(t.conn, t.gc, XCB_GC_FONT, &t.font);

	free(font_info);
	return r;
}

void
xcb_loop(void) {
	xcb_generic_event_t *ev;

	while (!t.ttydead) {
		ev = xcb_poll_for_event(t.conn);
		if (ev == NULL) {
			/* XXX: also handle X connection breaking */
			if (xcb_connection_has_error(t.conn))
				err(1, "connection to X11 broken");

			gfx.cb_loop();
		} else {
			switch (ev->response_type & ~0x80) {
			case XCB_EXPOSE: {
				t.wants_redraw = 1;
			} break;
			case XCB_KEY_PRESS: {
				xcb_key_press_event_t *e;
				xcb_keysym_t keysym, key;

				e = (xcb_key_press_event_t *)ev;
				key = xcb_get_keysym(e->detail, 0);

				gfx.cb_keypress(key, e->state);
			} break;
			case XCB_BUTTON_PRESS: {
				xcb_button_press_event_t *e;

				e = (xcb_button_press_event_t *)ev;
				gfx.cb_buttonpress(e->root_x, e->root_y);
			} break;
			case XCB_CONFIGURE_NOTIFY: {
				/* resize? */
			} break;
			}
		}
	}
}

void
xcb_init(void) {
	uint32_t mask, values[3];

	(void)setlocale(LC_ALL, "");

	/* XXX load config file */
	strcpy(t.fontline, "-*-fixed-medium-r-*-*-10-*-*-*-*-*-*-1");

	t.conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(t.conn))
		err(1, "xcb_connection_has_error");

	t.scr = xcb_setup_roots_iterator(xcb_get_setup(t.conn)).data;

	mask = XCB_CW_EVENT_MASK;
	values[0]  = XCB_EVENT_MASK_EXPOSURE;
	values[0] |= XCB_EVENT_MASK_KEY_PRESS;
	values[0] |= XCB_EVENT_MASK_BUTTON_PRESS;
	values[0] |= XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;

	t.win = xcb_generate_id(t.conn);

	xcb_create_window(t.conn, XCB_COPY_FROM_PARENT, t.win,
				t.scr->root, 0, 0, 1, 1, 10,
				XCB_WINDOW_CLASS_INPUT_OUTPUT,
				t.scr->root_visual, mask, values);

	xcb_map_window(t.conn, t.win);

	mask = XCB_GC_GRAPHICS_EXPOSURES;
	values[0] = 0;
	t.gc = xcb_generate_id(t.conn);
	xcb_create_gc(t.conn, t.gc, t.win, mask, values);

	t.font = load_font(t.gc, t.fontline);
	resize(80, 24);

	xcb_flush(t.conn);
}

void
xcb_cleanup(void) {
	xcb_disconnect(t.conn);
	xcb_key_symbols_free(t.keysyms);
	free(t.map);
	free(t.font);
}

struct video_fcn_tbl gfx = {
	.init    = xcb_init,
	.cleanup = xcb_cleanup,
	.loop    = xcb_loop,

	.set_cell = set_cell,
	.resize   = resize,
	.redraw   = redraw,

	.cb_loop        = loop,
	.cb_keypress    = keypress,
	.cb_buttonpress = buttonpress,
};
