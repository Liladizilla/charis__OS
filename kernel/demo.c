// demo.c - GUI demo application for CharisOS
#include <kernel/syscall.h>

static void demo_window_paint(void* win) {
    (void)win;
    // Demo will draw a simple UI
    sys_print("Demo window painting\n");
}

void demo_main(void) {
    sys_print("CharisOS GUI Demo starting...\n");
    
    // Create a demo window
    sys_print("Creating window...\n");
    
    // This would use wm_create_window via syscall in a real implementation
    // For now, just demonstrate basic graphics
    
    while (1) {
        sys_yield();
    }
}