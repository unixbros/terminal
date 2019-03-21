# terminal

```c
struct terminal_attributes {
    uint16_t utf;
    uint8_t  fg;
    uint8_t  bg;
    uint8_t  attr;
}

struct terminal_attributes buffer[width * height];
```

Display functions stored in:
* `opengl.c`
* `xcb.c`
* `metal.m`
* `linuxfb.c`
