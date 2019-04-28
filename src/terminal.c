#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include "gfx/xcb.h"
#include "config.h"


void
tprintf(const char *fmt, ...) {
	va_list args;
	char buf[BUFSIZ], *p;

	va_start(args, fmt);
	vsprintf(buf, fmt, args);

	p = buf;
	while (*p) {
		switch (*p) {
		default:
			set_cell(t.cursor.x, t.cursor.y, p);
			next_cell();
		}

		p += utf_len(p);
	}
}

/* callbacks */
void
loop(void) {
	/* mock sleep to emulate poll() */
	usleep(6000);
}

void
keypress(int key, int state) {
	warnx("keypress '%lc'", key);
}

void
buttonpress(int y, int x) {
	warnx("x: %d, y: %d", x, y);
}

int
main(int argc, char **argv) {
	gfx.init();
	tprintf("hello world shit works apparently testing tosts hello world abcdefghijk 1234567890");
	gfx.loop();
	gfx.cleanup();
	return 0;
}
