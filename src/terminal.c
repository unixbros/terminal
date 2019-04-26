#include "gfx/xcb.h"

void
loop(void) {
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
	gfx.loop();
	gfx.cleanup();
	return 0;
}
