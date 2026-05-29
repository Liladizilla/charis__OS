#include <kernel/apps.h>
#include <kernel/wm.h>
#include <kernel/graphics.h>
#include <kernel/vga.h>

static const char* app_names[] = {
    "Terminal",
    "File Manager", 
    "Text Editor",
    "Calculator",
    "Settings"
};

static void (*app_funcs[])(void) = {
    app_terminal_main,
    app_filemanager_main,
    app_texteditor_main,
    app_calculator_main,
    app_settings_main
};

#define APP_COUNT (sizeof(app_names) / sizeof(app_names[0]))

void apps_init(void) {
    vga_puts("Applications initialized\n");
}

void app_terminal_main(void) {
    window_t* win = wm_create_window("Terminal", 50, 50, 500, 300);
    (void)win;
    graphics_set_color(0x0D1117);
    graphics_rect(58, 58, 484, 272, true);
    psf_draw_string(66, 66, "CharisOS Terminal", 0x58A6FF, 0x0D1117);
    wm_render();
    
    while (1) {
        asm volatile("hlt");
    }
}

void app_filemanager_main(void) {
    window_t* win = wm_create_window("File Manager", 100, 80, 400, 350);
    (void)win;
    graphics_set_color(0x0D1117);
    graphics_rect(108, 108, 384, 322, true);
    psf_draw_string(116, 116, "File Manager", 0x58A6FF, 0x0D1117);
    wm_render();
    
    while (1) {
        asm volatile("hlt");
    }
}

void app_texteditor_main(void) {
    window_t* win = wm_create_window("Text Editor", 80, 60, 500, 400);
    (void)win;
    graphics_set_color(0x0D1117);
    graphics_rect(88, 88, 484, 372, true);
    psf_draw_string(96, 96, "Text Editor", 0x58A6FF, 0x0D1117);
    wm_render();
    
    while (1) {
        asm volatile("hlt");
    }
}

void app_calculator_main(void) {
    window_t* win = wm_create_window("Calculator", 200, 200, 250, 300);
    (void)win;
    graphics_set_color(0x0D1117);
    graphics_rect(208, 228, 234, 272, true);
    psf_draw_string(216, 236, "Calculator", 0x58A6FF, 0x0D1117);
    wm_render();
    
    while (1) {
        asm volatile("hlt");
    }
}

void app_settings_main(void) {
    window_t* win = wm_create_window("Settings", 150, 150, 300, 250);
    (void)win;
    graphics_set_color(0x0D1117);
    graphics_rect(158, 178, 284, 222, true);
    psf_draw_string(166, 186, "Settings", 0x58A6FF, 0x0D1117);
    wm_render();
    
    while (1) {
        asm volatile("hlt");
    }
}