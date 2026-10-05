// demo.c - GUI demo application for CharisOS
#include <kernel/syscall.h>
#include <kernel/syscall_wrappers.h>

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