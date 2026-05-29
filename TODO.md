# TODO - CharisOS Development Roadmap

## Phase 1: Memory/VMM Refactor (COMPLETE)
- [x] heap.c - separate heap module with kmalloc/kfree
- [x] bootmem.c - multiboot parsing skeleton
- [x] memory.c - thin orchestrator
- [x] vmm_walk(), vmm_create_address_space(), vmm_copy_kernel_mappings(), vmm_switch()
- [x] vmm_map_page_pml4() - map into specific PML4
- [x] PMM safety: double-free check, next-fit cursor

## Phase 2: Syscall Interface & User Space (COMPLETE)
- [x] SYSCALL/SYSRET assembly entry with register save/restore
- [x] MSRs setup (LSTAR, STAR, SF_MASK)
- [x] sys_exit(), sys_getpid(), sys_yield(), sys_sleep()
- [x] sys_read()/sys_write() with fd-based I/O
- [x] sys_print() - debug output
- [x] sys_fork() stub (creates child with separate PML4)
- [x] task_create_with_pml4() - create task with custom address space
- [x] Per-process fd table (stdin/stdout/stderr pre-opened)

## Phase 3: Filesystem (COMPLETE)
- [x] VFS layer (vfs.h, vfs.c) - vfs_node_t, mount points
- [x] Device nodes: /dev/null, /dev/zero, /dev/kbd, /dev/vga
- [x] Per-task fd table with fd_alloc(), fd_get(), fd_close()
- [x] FAT32 integration with VFS
- [x] sys_open() / sys_close() syscalls

## Phase 4: ELF Loader (COMPLETE)
- [x] elf.h - ELF header/structures
- [x] elf.c - ELF parsing (ehdr, phdr), PT_LOAD mapping
- [x] sys_exec() syscall - load and run ELF program

## Phase 5: Graphics & Display (COMPLETE)
- [x] fb.h/fb.c - framebuffer driver (VESA/GOP ready)
- [x] psf.h/psf.c - PC Screen Font renderer (PSF2 support)
- [x] fb_put_pixel(), fb_clear(), fb_fill_rect()
- [x] graphics.h/graphics.c - 2D drawing primitives
- [x] graphics_line() - Bresenham's algorithm
- [x] graphics_rect() - filled/outline
- [x] graphics_circle() - midpoint circle
- [x] graphics_put_string() - PSF-based text rendering

---

## Phase 6: Window System (Next)
- [ ] Window manager (compositor)
- [ ] Widget library
- [ ] Input event system
- [ ] PS/2 mouse driver

## Phase 7: IPC & Signals
- [ ] Signals (SIGINT, SIGSEGV, SIGCHLD)
- [ ] Pipes
- [ ] Unix domain sockets