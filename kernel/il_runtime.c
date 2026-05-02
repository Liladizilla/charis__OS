#include <kernel/il_runtime.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/serial.h>

static il_image_t* current_image = NULL;

void il_runtime_init(il_image_t* image) {
    current_image = image;
    vga_puts("IL Runtime initialized\n");
}

void il_runtime_exec(const char* entry_method) {
    // TODO: Find method by name and execute
    vga_puts("IL Runtime exec: ");
    vga_puts(entry_method);
    vga_puts("\n");

    // For now, just print
    vga_puts("C# kernel would run here\n");
}

void* il_alloc_obj(u32 vtable_idx) {
    if (!current_image) return NULL;
    if (vtable_idx >= current_image->vtable_count) return NULL;
    il_vtable_t* vt = &current_image->vtables[vtable_idx];
    usize size = sizeof(il_object_header_t) + vt->static_size;
    void* obj = kmalloc(size);
    if (!obj) return NULL;

    il_object_header_t* header = obj;
    header->vtable_idx = vtable_idx;
    header->flags = 0;
    header->size = size;
    header->ref_count = 1;

    return obj;
}

void* il_alloc_array(u32 elem_size, u32 count) {
    usize size = sizeof(il_object_header_t) + elem_size * count;
    void* arr = kmalloc(size);
    if (!arr) return NULL;

    il_object_header_t* header = arr;
    header->vtable_idx = 0; // Array vtable?
    header->flags = 0;
    header->size = size;
    header->ref_count = 1;

    return arr;
}

// Simple mark-and-sweep GC
void il_gc_collect(void) {
    // TODO: Implement GC
    vga_puts("GC run\n");
}

u64 il_native_call(u32 native_id, u64* args) {
    switch (native_id) {
        case 0: // Console.Write
            vga_putchar((char)args[0]);
            break;
        case 1: // Console.WriteLine
            vga_puts((const char*)args[0]);
            vga_putchar('\n');
            break;
        default:
            vga_puts("Unknown native call\n");
    }
    return 0;
}