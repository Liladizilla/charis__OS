#include <kernel/widgets.h>
#include <kernel/graphics.h>
#include <kernel/psf.h>
#include <kernel/memory.h>

static widget_t* widget_list = NULL;

widget_t* widget_create(widget_type_t type, int x, int y, int w, int h, const char* text) {
    widget_t* widget = (widget_t*)kmalloc(sizeof(widget_t));
    if (!widget) return NULL;
    
    kmemset(widget, 0, sizeof(widget_t));
    widget->type = type;
    widget->x = x;
    widget->y = y;
    widget->w = w;
    widget->h = h;
    if (text) {
        kstrncpy(widget->text, text, 127);
    }
    widget->visible = true;
    widget->enabled = true;
    
    /* Add to widget list */
    widget->next = widget_list;
    widget_list = widget;
    
    return widget;
}

void widget_destroy(widget_t* widget) {
    if (!widget) return;
    
    /* Remove from list */
    if (widget_list == widget) {
        widget_list = widget->next;
    } else {
        widget_t* curr = widget_list;
        while (curr && curr->next != widget) {
            curr = curr->next;
        }
        if (curr) {
            curr->next = widget->next;
        }
    }
    
    kfree(widget);
}

void widget_draw(widget_t* widget, u32* buffer, int buf_w) {
    if (!widget || !widget->visible || !buffer) return;
    
    switch (widget->type) {
        case WIDGET_LABEL:
            widget_draw_label(widget, buffer, buf_w);
            break;
        case WIDGET_BUTTON:
            widget_draw_button(widget, buffer, buf_w);
            break;
        case WIDGET_TEXTBOX:
            widget_draw_textbox(widget, buffer, buf_w);
            break;
        case WIDGET_PANEL:
            widget_draw_panel(widget, buffer, buf_w);
            break;
    }
}

static int widget_get_buffer_height(int buf_w) {
    if (g_framebuffer.initialized && g_framebuffer.width > 0) {
        return g_framebuffer.height;
    }
    return buf_w;
}

void widget_draw_label(widget_t* widget, u32* buffer, int buf_w) {
    int buf_h = widget_get_buffer_height(buf_w);
    for (int y = widget->y; y < widget->y + widget->h && y < buf_h; y++) {
        for (int x = widget->x; x < widget->x + widget->w && x < buf_w; x++) {
            if (x >= 0 && y >= 0) {
                buffer[y * buf_w + x] = widget->bg_color;
            }
        }
    }
    
    /* Draw text */
    if (widget->text[0]) {
        psf_draw_string_buffer(widget->x, widget->y, widget->text, 
                             widget->fg_color, widget->bg_color, buffer, buf_w);
    }
}

void widget_draw_button(widget_t* widget, u32* buffer, int buf_w) {
    u32 bg = widget->bg_color;
    
    /* Hover effect */
    if (widget->hover) {
        bg = 0x21262D; /* Slightly brighter for hover */
    }
    
    /* Pressed effect */
    if (widget->pressed) {
        bg = 0x1A1F26;
    }
    
    /* Fill button background */
    for (int y = widget->y; y < widget->y + widget->h && y < buf_w; y++) {
        for (int x = widget->x; x < widget->x + widget->w && x < buf_w; x++) {
            if (x >= 0 && y >= 0) {
                buffer[y * buf_w + x] = bg;
            }
        }
    }
    
    /* Draw border */
    graphics_set_color(0x30363D);
    for (int x = widget->x; x < widget->x + widget->w && x < buf_w; x++) {
        if (x >= 0 && widget->y >= 0) {
            buffer[widget->y * buf_w + x] = 0x30363D;
        }
        if (x >= 0 && widget->y + widget->h - 1 >= 0 && widget->y + widget->h - 1 < buf_w) {
            buffer[(widget->y + widget->h - 1) * buf_w + x] = 0x30363D;
        }
    }
    for (int y = widget->y; y < widget->y + widget->h && y < buf_w; y++) {
        if (widget->x >= 0 && widget->x < buf_w) {
            buffer[y * buf_w + widget->x] = 0x30363D;
        }
        if (widget->x + widget->w - 1 >= 0 && widget->x + widget->w - 1 < buf_w) {
            buffer[y * buf_w + (widget->x + widget->w - 1)] = 0x30363D;
        }
    }
    
    /* Draw label text centered */
    if (widget->text[0]) {
        int text_x = widget->x + 8;
        int text_y = widget->y + (widget->h - 16) / 2;
        psf_draw_string_buffer(text_x, text_y, widget->text, 
                             widget->fg_color, bg, buffer, buf_w);
    }
}

void widget_draw_textbox(widget_t* widget, u32* buffer, int buf_w) {
    /* Fill background */
    for (int y = widget->y; y < widget->y + widget->h && y < buf_w; y++) {
        for (int x = widget->x; x < widget->x + widget->w && x < buf_w; x++) {
            if (x >= 0 && y >= 0) {
                buffer[y * buf_w + x] = widget->bg_color;
            }
        }
    }
    
    /* Draw border */
    graphics_set_color(0x30363D);
    for (int x = widget->x; x < widget->x + widget->w && x < buf_w; x++) {
        if (x >= 0 && widget->y >= 0) {
            buffer[widget->y * buf_w + x] = 0x30363D;
        }
        if (x >= 0 && widget->y + widget->h - 1 >= 0 && widget->y + widget->h - 1 < buf_w) {
            buffer[(widget->y + widget->h - 1) * buf_w + x] = 0x30363D;
        }
    }
    for (int y = widget->y; y < widget->y + widget->h && y < buf_w; y++) {
        if (widget->x >= 0 && widget->x < buf_w) {
            buffer[y * buf_w + widget->x] = 0x30363D;
        }
        if (widget->x + widget->w - 1 >= 0 && widget->x + widget->w - 1 < buf_w) {
            buffer[y * buf_w + (widget->x + widget->w - 1)] = 0x30363D;
        }
    }
    
    /* Draw text */
    if (widget->text[0]) {
        psf_draw_string_buffer(widget->x + 4, widget->y + 4, widget->text, 
                             widget->fg_color, widget->bg_color, buffer, buf_w);
    }
    
    /* Draw cursor if focused */
    if (widget->focused) {
        int cursor_x = widget->x + 4 + kstrlen(widget->text) * 8;
        if (cursor_x < widget->x + widget->w - 4) {
            graphics_set_color(widget->fg_color);
            graphics_line(cursor_x, widget->y + 4, cursor_x, widget->y + widget->h - 8);
        }
    }
}

void widget_draw_panel(widget_t* widget, u32* buffer, int buf_w) {
    /* Fill panel background */
    for (int y = widget->y; y < widget->y + widget->h && y < buf_w; y++) {
        for (int x = widget->x; x < widget->x + widget->w && x < buf_w; x++) {
            if (x >= 0 && y >= 0) {
                buffer[y * buf_w + x] = widget->bg_color;
            }
        }
    }
    
    /* Draw border if enabled */
    if (widget->bordered) {
        graphics_set_color(0x30363D);
        graphics_rect(widget->x, widget->y, widget->w, widget->h, false);
    }
}

bool widget_handle_mouse(widget_t* widget, int mx, int my, int button, bool pressed) {
    if (!widget || !widget->enabled) return false;
    
    bool inside = (mx >= widget->x && mx < widget->x + widget->w &&
                   my >= widget->y && my < widget->y + widget->h);
    
    widget->hover = inside;
    
    if (widget->type == WIDGET_BUTTON && inside && button == 1) {
        if (pressed) {
            widget->pressed = true;
        } else if (widget->pressed && widget->on_click) {
            widget->pressed = false;
            widget->on_click(widget);
            return true;
        }
    }
    
    if (widget->type == WIDGET_TEXTBOX && inside && button == 1 && pressed) {
        widget->focused = true;
        /* First textbox to get focus becomes the focused widget */
        widget_t* curr = widget_list;
        while (curr) {
            if (curr != widget) curr->focused = false;
            curr = curr->next;
        }
    }
    
    return false;
}

bool widget_handle_key(widget_t* widget, char key) {
    if (!widget || !widget->focused || widget->type != WIDGET_TEXTBOX) return false;
    
    if (key == '\b' || key == 127) {
        int len = (int)kstrlen(widget->text);
        if (len > 0) {
            widget->text[len - 1] = '\0';
        }
        return true;
    }
    
    /* Handle printable characters */
    int len = (int)kstrlen(widget->text);
    if (len < 127) {
        widget->text[len] = key;
        widget->text[len + 1] = '\0';
    }
    
    return true;
}

void widget_set_text(widget_t* widget, const char* text) {
    if (!widget || !text) return;
    kstrncpy(widget->text, text, 127);
}

void widget_set_colors(widget_t* widget, u32 fg, u32 bg) {
    if (!widget) return;
    widget->fg_color = fg;
    widget->bg_color = bg;
}