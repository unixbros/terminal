struct tattr {
	uint16_t utf;
	uint8_t fg;
	uint8_t bg;
	uint8_t attr;
};

struct video_fcn_tbl {
	/* XXX fix explicit args */

	/* oneshot */
	void (*init)(void);
	void (*cleanup)(void);
	void (*loop)(void);

	/* output */
	void (*redraw)(void);
	void (*resize)(int, int);
	void (*set_cell)(int, int, char *);
	void (*clr_cell)(int, int);
	void (*set_attr)(int, int, struct tattr);

	/* input callbacks */
	void (*cb_keypress)();
	void (*cb_buttonpress)();
	void (*cb_loop)(void);
};

