#include <stdio.h>
#include "all.h"

void
xcb_init(void) {
	puts("mock init");
}

void
xcb_cleanup(void) {
	puts("mock cleanup");
}

struct video_fcn_tbl gfx = {
	.init    = xcb_init,
	.cleanup = xcb_cleanup
};
