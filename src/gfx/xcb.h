#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcbext.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <err.h>
#include "all.h"

struct video_fcn_tbl gfx;
extern struct config c;

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
	struct xy cursor;

	int width, height;
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
	values[0] = (t.font->width  * (x + 1)) + (c.padding * 2);
	values[1] = (t.font->height * (y + 1)) + (c.padding * 2);
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

/*
 * thanks for wmdia for doing what xcb devs can't
 */
xcb_void_cookie_t
xcb_poly_text_16_simple(xcb_connection_t *c, xcb_drawable_t drawable,
		xcb_gcontext_t gc, int16_t x, int16_t y, uint32_t len,
		const uint16_t *str)
{
	struct iovec xcb_parts[7];
	static const xcb_protocol_request_t xcb_req = {
		5,                /* count  */
		0,                /* ext    */
		XCB_POLY_TEXT_16, /* opcode */
		1                 /* isvoid */
	};
	uint8_t xcb_lendelta[2];
	xcb_void_cookie_t xcb_ret;
	xcb_poly_text_8_request_t xcb_out;

	xcb_out.pad0 = 0;
	xcb_out.drawable = drawable;
	xcb_out.gc = gc;
	xcb_out.x = x;
	xcb_out.y = y;

	xcb_lendelta[0] = len;
	xcb_lendelta[1] = 0;

	xcb_parts[2].iov_base = (char *)&xcb_out;
	xcb_parts[2].iov_len = sizeof(xcb_out);
	xcb_parts[3].iov_base = 0;
	xcb_parts[3].iov_len = -xcb_parts[2].iov_len & 3;

	xcb_parts[4].iov_base = xcb_lendelta;
	xcb_parts[4].iov_len = sizeof(xcb_lendelta);
	xcb_parts[5].iov_base = (char *)str;
	xcb_parts[5].iov_len = len * sizeof(int16_t);

	xcb_parts[6].iov_base = 0;
	xcb_parts[6].iov_len = -(xcb_parts[4].iov_len + xcb_parts[5].iov_len)
		& 3;

	xcb_ret.sequence = xcb_send_request(c, 0, xcb_parts + 2, &xcb_req);

	return xcb_ret;
}

void
redraw(void) {
	int x, y, i;

	warnx("redrawing");
	t.wants_redraw = 0;

	i = 0;
	for (x = 0; x < t.width; x++)
		for (y = 0; y < t.height; y++)
			if (t.map[x + (y * t.width)].draw)
				xcb_poly_text_16_simple(t.conn, t.win, t.gc,
					((x + 1) * t.font->width) + c.padding,
					((y + 1) * t.font->height) + c.padding,
					1, &t.map[x + (y * t.width)].utf);

	xcb_flush(t.conn);
}

xcb_keysym_t
xcb_get_keysym(xcb_keycode_t keycode, uint16_t state) {
	if (!(t.keysyms = xcb_key_symbols_alloc(t.conn)))
		return 0;

	return xcb_key_symbols_get_keysym(t.keysyms, keycode, state);
}

void
next_cell(void) {
	if (t.cursor.x + 1 >= t.width) {
		t.cursor.y++;
		t.cursor.x = 0;
	} else
		t.cursor.x++;

	/* if t.cursor.y >= t.height; scroll */
}

void
set_cell(int x, int y, char *str) {
	uint16_t c;
	uint8_t *utf = (uint8_t *)str;
	int pos;

	pos = x + (y * t.width);
	t.map[pos].utf = utf_combine(str);
	t.map[pos].draw = 1;
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

			if (t.wants_redraw)
				redraw();

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
				key = xcb_get_keysym(e->detail, e->state);

				gfx.cb_keypress(key, e->state);
			} break;
			case XCB_BUTTON_PRESS: {
				xcb_button_press_event_t *e;

				e = (xcb_button_press_event_t *)ev;
				gfx.cb_buttonpress(e->root_x / t.font->width,
						e->root_y / t.font->height);
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

	mask = XCB_CW_EVENT_MASK | XCB_CW_BACK_PIXEL;
	values[0]  = c.bg;
	values[1]  = XCB_EVENT_MASK_EXPOSURE;
	values[1] |= XCB_EVENT_MASK_KEY_PRESS;
	values[1] |= XCB_EVENT_MASK_BUTTON_PRESS;
	values[1] |= XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;

	t.win = xcb_generate_id(t.conn);

	xcb_create_window(t.conn, XCB_COPY_FROM_PARENT, t.win,
				t.scr->root, 0, 0, 1, 1, 10,
				XCB_WINDOW_CLASS_INPUT_OUTPUT,
				t.scr->root_visual, mask, values);

	xcb_map_window(t.conn, t.win);

	mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
	values[0] = c.fg;
	values[1] = 0;
	t.gc = xcb_generate_id(t.conn);
	xcb_create_gc(t.conn, t.gc, t.win, mask, values);

	t.font = load_font(t.gc, t.fontline);
	resize(80, 24);
	xcb_flush(t.conn);

	t.cursor.x = t.cursor.y = 0;
	redraw();
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
