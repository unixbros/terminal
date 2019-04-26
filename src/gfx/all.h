struct video_fcn_tbl {
	/* XXX fix explicit args */

	/* oneshot */
	void (*init)();
	void (*cleanup)();

	/* output */
	void (*redraw)();
	void (*set_cell)();
	void (*clr_cell)();
	void (*set_attr)();
	void (*resize)();

	/* input callbacks */
	void (*cb_keypress)();
	void (*cb_buttonpress)();
};

