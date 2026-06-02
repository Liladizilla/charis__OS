/* widgets.h - Widget toolkit for Phase 6 */
#pragma once
#include <kernel/types.h>

typedef enum {
    WIDGET_LABEL,
    WIDGET_BUTTON,
    WIDGET_TEXTBOX,
    WIDGET_PANEL
} widget_type_t;

typedef struct widget {
    widget_type_t type;
    int x, y, w, h;
    char text[128];
    u32 fg_color;
    u32 bg_color;
    bool visible;
    bool enabled;
    bool hover;
    bool pressed;
    bool focused;
    bool bordered;
    
    /* Callbacks */
    void (*on_click)(struct widget* w);
    
    struct widget* next;
} widget_t;

/* Widget creation and management */
widget_t* widget_create(widget_type_t type, int x, int y, int w, int h, const char* text);
void widget_destroy(widget_t* widget);

/* Drawing functions (offscreen buffer support) */
void widget_draw(widget_t* widget, u32* buffer, int buf_w);
void widget_draw_label(widget_t* widget, u32* buffer, int buf_w);
void widget_draw_button(widget_t* widget, u32* buffer, int buf_w);
void widget_draw_textbox(widget_t* widget, u32* buffer, int buf_w);
void widget_draw_panel(widget_t* widget, u32* buffer, int buf_w);

/* Event handling */
bool widget_handle_mouse(widget_t* widget, int mx, int my, int button, bool pressed);
bool widget_handle_key(widget_t* widget, char key);

/* Utility functions */
void widget_set_text(widget_t* widget, const char* text);
void widget_set_colors(widget_t* widget, u32 fg, u32 bg);