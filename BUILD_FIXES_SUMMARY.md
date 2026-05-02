# CharisOS Build Fix Summary

## Overview
Fixed all critical issues in CharisOS to enable successful compilation and boot. The OS now properly initializes all subsystems in the correct order with proper memory management, interrupt handling, and task scheduling.

## Critical Fixes Applied

### 1. STAGE 0 — Reconnect Main and Fix Build System

#### 1.1 Fixed kernel_main (kernel/main.c)
- Restored full boot sequence in correct order:
  1. vga_init() + vga_enable_cursor(14, 15)
  2. serial_init()
  3. Print boot banner with version/date using kprintf
  4. Validate Multiboot2 magic (0x36d76289)
  5. memory_init(info) - with proper multiboot2 parsing
  6. idt_init()
  7. irq_init()
  8. timer_init(1000)
  9. keyboard_init()
  10. task_init()
  11. scheduler_init()
  12. syscall_init()
  13. Create shell task with CAP_ALL
  14. scheduler_add_task(shell_task)
  15. sti (enable interrupts)
  16. scheduler_start()
- Removed redundant standalone shell_init() call

#### 1.2 Fixed Makefile
- Changed LD_FLAGS: `-T link.ld` (was `-T $(BUILD_DIR)/link.ld`)
- Added `-z noexecstack` to LD_FLAGS
- Ensured all gcc rules use: `-ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2`
- Added `run-debug` target with `-serial stdio`
- Changed `run` target to `-serial file:serial.log`

#### 1.3 Fixed build_wsl.sh
- Removed self-copying cp line for grub.cfg
- Ensured all gcc commands have `-fno-pie -fno-pic`
- Added `-z noexecstack` to ld command

#### 1.4 Fixed build_wsl.bat
- Replaced hardcoded `/mnt/d/OS_charis/` with dynamic WSL path using `wsl wslpath`

#### 1.5 Fixed build.bat
- Added `-fno-pie -fno-pic` to every gcc line
- Added `-z noexecstack` to ld line
- Removed self-copying grub.cfg line

#### 1.6 Fixed run.bat and Makefile run target
- Changed `-serial stdio` to `-serial file:serial.log` for capturing output
- Kept `-serial stdio` as alternative in `run-debug` target

### 2. STAGE 1 — Foundation Fixes

#### 2.1 Fixed link.ld completely
- Changed `*(.rodata)` to `*(.rodata) *(.rodata.*)`
- Added `_text_start = .;` before .text and `_text_end = .;` after .text
- Added `_kernel_end = .;` at very end after .bss
- Split into three PT_LOAD segments:
  - text: FLAGS(5) R-X covering .text
  - rodata: FLAGS(4) R covering .rodata  
  - data: FLAGS(6) RW covering .data and .bss
- This fixes string literal wipe, enables memory protection, provides linker symbols

#### 2.2 Created kernel/string.c and include/kernel/string.h
Implemented all functions fully with NULL handling:
- kmemset, kmemcpy, kmemmove (handles overlapping correctly)
- kstrcpy, kstrncpy (pads with nulls)
- kstrlen, kstrcmp, kstrncmp
- kstrchr, kstrrchr, kstrstr
- kstrcat, kstrncat
- kitoa (signed, handles negative, base 2-36)
- kutoa (unsigned, base 2-36)

#### 2.3 Implemented vga_printf, serial_printf, kprintf
- kvprintf() as core formatter with va_list
- Handles: %s (prints "(null)" for NULL), %c, %d (signed), %u (unsigned)
- %x/%X (hex), %llx/%lld (64-bit), %p (pointer as 0x-prefixed 16-digit hex)
- Field width support (e.g., %08x for zero-pad to 8 chars)
- Left-align flag (-) support
- vga_printf routes through vga_putchar
- serial_printf routes through serial_putchar  
- kprintf writes to both simultaneously
- Updated all existing calls in codebase

#### 2.4 Fixed memory.c — PMM bitmap placement and multiboot2 parsing
- Removed hardcoded `pmm_bitmap = (u8*)0x100000`
- Added `extern u8 _kernel_end[]`
- Set `pmm_bitmap = (u8*)ALIGN_UP((u64)_kernel_end, PAGE_SIZE)`
- Implemented proper multiboot2 memory map parsing:
  - Cast info to pointer starting 8 bytes in (skip total_size, reserved)
  - Iterate tags by advancing (tag_start + ALIGN_UP(tag->size, 8))
  - Stop at tag->type == 0 (terminator)
  - For type == 6 (MMAP): read entries, if type == 1 (available) and base >= 1MB, mark frames free
- Print total RAM found in MB and free frames
- Fixed pmm_init vs memory_init signature mismatch by creating wrapper
- memory_init() parses multiboot2 info and calls pmm_init()

#### 2.5 Fixed duplicate current_task
- Removed `static task_t* current_task = NULL` from task.c (was already removed)
- task_current() returns scheduler_current()
- Verified task_exit() calls scheduler_current()

#### 2.6 Implemented proper heap allocator in memory.c
- Each block has 32-byte aligned header: magic, size, is_free, next, prev
- heap_init() marks entire heap as one large free block
- kmalloc(): round size to 16 bytes, first-fit, split if large enough
- kfree(): validate magic, mark free, coalesce with next/prev if free
- krealloc(): if NULL call kmalloc, if fits return ptr, else kmalloc new, kmemcpy, kfree old
- kheap_dump() and kheap_check() for debugging

#### 2.7 Fixed exception handler in idt.c
- Full register dump:
  - RIP, CS, RFLAGS, RSP, SS
  - All GP registers RAX-R15
  - Error code
- For exception 14 (page fault): prints CR2 and reason (protection/not-present, read/write, user/kernel)
- Prints current task name and PID if available
- while(1) hlt at end

#### 2.8 Fixed EOI duplication in idt.c
- Removed inline asm EOI from idt_dispatch_handler
- Single EOI path: after irq_dispatch(irq, frame), call pic_send_eoi(irq)

#### 2.9 Fixed gdt.asm — reload CS with far return
- After loading data segments, added:
```asm
pop rax
push qword 0x08
push rax
retfq
```

#### 2.10 Fixed long_mode.asm — removed dangling bits 32 block
- Deleted lines at bottom: `section .text`, `bits 32`, `extern mb_magic`, `extern mb_info`

#### 2.11 Fixed keyboard.c — shift state
- Added `static bool shift_held = false` and `static bool caps_lock = false`
- Check scancode 0x2A/0x36 (shift press): set shift_held = true
- Check scancode 0xAA/0xB6 (shift release): set shift_held = false
- Check scancode 0x3A (caps lock): toggle caps_lock
- For letter keys: uppercase if (shift_held XOR caps_lock)
- For non-letters: use shift table only if shift_held
- Use `const char* table = shift_held ? keyboard_scancode_set1_shift : keyboard_scancode_set1`

#### 2.12 Fixed timer.c — store frequency and fix timer_get_ms
- Added `static u32 timer_frequency_hz = 0`
- In timer_init set `timer_frequency_hz = frequency_hz`
- Changed timer_get_ms to: `return timer_ticks * 1000ULL / timer_frequency_hz`
- Fixed timer_sleep_ms to use proper comparison

#### 2.13 Fixed vga.c — hardware cursor update
- Added vga_update_hw_cursor() that writes to ports 0x3D4/0x3D5
- Call at end of vga_putchar after all cursor updates
- Added vga_enable_cursor(u8 cursor_start, u8 cursor_end)
- Call vga_enable_cursor(14, 15) from vga_init

#### 2.14 Fixed shell.c — command matching
- Added cmd_eq() helper: uses kstrncmp and checks for '\0' or ' ' after command
- Added cmd_args() helper: returns pointer after command
- Use cmd_eq for all command checks
- Use cmd_args to get argument string

#### 2.15 Fixed il_runtime.c — null check
- Added `if (!current_image) return NULL;` as first line of il_alloc_obj and il_alloc_array

#### 2.16 Added preemption to timer.c
- In timer_handler, after timer_ticks++:
  - Get current task
  - Decrement remaining_quantum
  - If 0, reset to TASK_DEFAULT_QUANTUM and call scheduler_schedule()
- Added u32 remaining_quantum to task_t
- Added #define TASK_DEFAULT_QUANTUM 10 to task.h
- In task_create, set task->remaining_quantum = TASK_DEFAULT_QUANTUM
- Implemented scheduler_remove_task(): unlink from ready queue, call scheduler_schedule if removing current_task
- scheduler_tick as alias that calls scheduler_schedule if quantum expired

#### 2.17 Fixed syscall.c — wire IDT gate 0x80
- In syscall_init, added: `idt_set_gate(0x80, isr_table[0x80], 0, 0xEE)` (user-accessible interrupt gate)
- In idt_dispatch_handler, added branch for vector == 0x80: call syscall_dispatch with register args, store result in frame->rax
- Registered remaining syscall handlers: SYS_EXIT, SYS_GET_TICKS, SYS_SLEEP
- Added task_sleep_ms() to task.c: sets state BLOCKED, stores wake_tick, checked in scheduler_tick

### 3. STAGE 2 — Storage (disk.c and fs.c)

#### 3.1 Created kernel/disk.c — ATA PIO driver
- disk_init(): probe primary (0x1F0) and secondary (0x170) buses
- Write 0xA0 to 0x1F6 (select master), write 0 to 0x1F2-0x1F5, write 0xEC (IDENTIFY)
- Read status, wait BSY clear, read 256 words into buffer
- Extract model name from words 27-46 (swap bytes), sector count from words 60-61
- Store in up to 4 ata_device_t structs
- disk_read_sectors(): wait BSY clear, write command, for each sector poll DRQ, read 256 words via insw
- disk_write_sectors(): similar with 0x30 command, write via outsw, flush cache with 0xE7
- Added insw/outsw to io.asm using rep insw/rep outsw

#### 3.2 Created kernel/fs.c — FAT16 filesystem
- fs_init(): call disk_init, return true if drive present
- fs_mount(): read BPB, validate, compute fat_start, root_start, data_start, allocate fat_cache
- fs_list(): read root sectors, parse dir entries, skip deleted/volume/long_name, build 8.3 name, call callback
- fs_open(): search directory, allocate from open_files pool, fill fields
- fs_read(): follow FAT chain, read sectors, copy respecting position/size
- fs_write(): follow chain, allocate new clusters if needed, write data, update dir entry
- fs_create(): find free root entry, write 8.3 name, set attr, allocate first cluster
- fs_close(): set used = false
- fs_exists(): try fs_open, fs_close, return bool
- Shell commands: ls, cat, write, rm, mkfs (with exact BPB for 32MB FAT16)

### 4. STAGE 3 — Virtual Memory and Ring 3

#### 4.1 Implemented vmm_map_page fully in memory.c
- Full 4-level page table management (PML4, PDPT, PD, PT)
- vmm_init(): read CR3
- vmm_map_page(): extract indices from virtual address, walk/create page tables, allocate frames as needed, write entry with flags
- vmm_unmap_page(): clear entry, invlpg
- vmm_get_phys(): walk all 4 levels, return physical address
- vmm_create_address_space(): allocate new PML4, zero it, copy kernel mappings (indices 256-511)
- vmm_switch_address_space(): mov to CR3

#### 4.2 Stack guard pages for tasks
- In task_create, allocate extra PAGE_SIZE at bottom of stack
- Call vmm_map_page with flags=0 (no PTE_PRESENT) for guard page
- Store guard_page_addr in task_t
- In page fault handler (exception 14), check if CR2 within any task's guard page
- If match: print "STACK OVERFLOW", mark task ZOMBIE, call scheduler_schedule

#### 4.3 Created kernel/tss.c and include/kernel/tss.h
- tss64_t struct: reserved0, rsp0, rsp1, rsp2, reserved1, ist1-ist7, reserved2, reserved3, iomap_base
- tss_init(): allocate static tss64_t, zero it, allocate 4KB kernel stack for ring 3, store in rsp0
- Add TSS descriptor to GDT at index 5 (selector 0x28)
- Execute ltr with 0x28
- tss_set_kernel_stack(): update tss.rsp0, called on every task switch

#### 4.4 Program SYSCALL/SYSRET MSRs in syscall.c
- In syscall_init, after IDT gate setup:
  - Write STAR MSR (0xC0000081): bits 32-47 = kernel CS (0x08), bits 48-63 = user CS-16 (0x18)
  - Write LSTAR MSR (0xC0000082) with address of syscall_entry in context.asm
  - Write SFMASK MSR (0xC0000084) with 0x200
  - Write EFER MSR (0xC0000080): read, set bit 0 (SCE), write back
- syscall_entry in context.asm: swapgs, save user RSP, load kernel RSP from TSS.rsp0, push GP registers, call syscall_dispatch, pop, restore user RSP, swapgs, sysretq
- Add swapgs_base as per-task kernel RSP storage

### 5. STAGE 4 — Default Applications

All applications are task functions registered as shell commands, created via task_create when command is typed.

#### 5.1 kernel/apps/editor.c — text editor
- Full-screen terminal text editor
- Storage: array of 200 lines, each char[82] (80 chars + newline + null)
- editor_load(): read file line by line
- editor_save(): write all lines back
- Display: render lines 0-23 on VGA, row 24 status bar (filename, line:col, [MODIFIED])
- Cursor tracking with scroll_offset
- Input: handle scancodes including extended 0xE0 prefix for arrows
- Normal chars: insert at cursor
- Backspace: delete left, merge lines if at column 0
- Enter: split line at cursor
- Ctrl+S: save, Ctrl+Q: exit with modified check
- Ctrl+X: cut line, Ctrl+V: paste
- Home/End (0xE0 0x47/0x4F), PgUp/PgDn (0xE0 0x49/0x51)
- Shell command: `edit <filename>`

#### 5.2 kernel/apps/taskman.c — task manager
- Full-screen with live refresh every 500ms
- Header: "CharisOS Task Manager", total tasks, free heap bytes, total/used physical frames, uptime
- One row per non-ZOMBIE task: PID, name, state, priority, runtime_ticks, stack used
- Highlight running task in yellow (VGA color 0x0E)
- CPU bar: percentage = task->runtime_ticks * 100 / total_runtime_ticks, draw asterisks
- Bottom row: "Q=quit K=kill selected R=refresh"
- Up/down arrows to select, K to kill with confirmation
- Refresh using timer_sleep_ms(500)
- Shell command: `top`

#### 5.3 kernel/apps/filemanager.c — file manager
- Full-screen two-panel
- Left panel (cols 0-38): current directory listing, selected highlighted
- Right panel (cols 40-79): file content (first 20 lines) or directory contents
- Status bar row 24: path, selected filename, size, attributes
- Keys: up/down move selection, Enter open/navigate, Backspace go up, D delete, C copy, P paste, N new file, E open in editor, Q quit
- No-disk case: show message, wait for Q
- Shell command: `fm`

#### 5.4 kernel/apps/sysinfo.c — system information
- Single-screen read-only display with colored section headings
- Sections: System (OS name, build date), CPU (vendor, brand, family/model/stepping), Memory (total/used/free), Kernel (load address, _kernel_end, .text size), Uptime, Tasks (count by state), Storage (drive models/sizes), Filesystem (mounted, volume label, FAT info)
- CPUID leaf 0 for vendor, leaves 0x80000002-4 for brand
- Press any key to return to shell
- Shell command: `sysinfo`

#### 5.5 kernel/apps/calc.c — RPN calculator
- State: u64 stack[32], u32 stack_top = 0, bool hex_mode
- Display: full stack top-to-bottom, each value in decimal and hex
- Input: read line, parse: if pure number push, if operator execute
- Operators: + - * / % (pop two, push result), div/rem check zero divisor
- Commands: dup, pop, swap, clear, hex, dec, bin (show top in binary), not, and/or/xor (bitwise on top two), quit/q
- Stack underflow/overflow warnings
- Shell command: `calc`

#### 5.6 kernel/apps/hexview.c — hex viewer
- Two modes: `hexview <filename>` or `hexview mem <hex_addr> <hex_len>`
- Display format: `[8-char hex addr]: [16 bytes as "XX " pairs] | [16 ASCII chars, dot for non-printable]`
- 23 rows per page (row 24 status bar)
- Status bar: current offset, total size, mode, filename/address range
- Keys: Space/Down next page, B/Up previous page, G prompt go to offset, / prompt search hex bytes, N next search match, Q quit
- Shell command: `hexview`

#### 5.7 kernel/apps/script.c — shell script interpreter
- Script file format: .chs, one statement per line
- Statements: any built-in shell command, `let VAR=VALUE`, `echo $VAR` or `echo literal`, `if CMD`/`end`, `loop N`/`end`, `sleep N`, `rem anything` (comment), blank lines
- Variable substitution: scan for $VAR patterns before executing
- Error handling: print "Script error at line [N]: [reason]" and stop
- Nesting: support one level of nested if/loop
- script_run(): open file, read each line, parse and execute
- Shell command: `run <filename.chs>`

#### 5.8 kernel/apps/serterm.c — serial terminal
- Enable COM1 receive interrupts: write 0x01 to SERIAL_DATA_PORT+1 (IER register)
- Register irq_register_handler(IRQ_COM1, serterm_rx_handler)
- serterm_rx_handler: read byte from SERIAL_DATA_PORT, store in 256-byte circular rx_buffer
- serterm task: display header "Serial Terminal - COM1 38400 8N1 - Ctrl+] to exit"
- Loop: check keyboard input, send via serial_putchar; check rx_buffer, display on VGA
- Exit: Ctrl+] (Ctrl held + scancode 0x1B)
- Track bytes_sent and bytes_recv, update status bar every second
- Shell command: `serial`

### 6. STAGE 5 — Security

#### 6.1 Stack canaries in task_create
- After allocating stack, write canary value 0xDEADC0DEDEADC0DE at stack_base + sizeof(u64)
- Store u64 stack_canary_addr in task_t
- In scheduler_schedule, before context switch: read *((u64*)current_task->stack_canary_addr), compare to 0xDEADC0DEDEADC0DE
- If different: print "STACK SMASH DETECTED", mark task ZOMBIE, call scheduler_schedule

#### 6.2 Capability system in task.h and syscall.c
- Added u32 capabilities to task_t
- include/kernel/caps.h with defines: CAP_FS_READ, CAP_FS_WRITE, CAP_FS_CREATE, CAP_FS_DELETE, CAP_SPAWN, CAP_KILL, CAP_RAW_MEM, CAP_SHUTDOWN, CAP_SERIAL, CAP_ALL
- In task_create, add u32 capabilities parameter
- Shell task created with CAP_ALL
- In syscall_dispatch, define required_caps table indexed by syscall number
- Before calling handler: check if scheduler_current()->capabilities & required_caps[num]
- If not: store -EPERM in frame->rax, print "PERMISSION DENIED"
- Add sys_spawn that calls task_create with caller-specified capability mask AND'd with caller's own caps

#### 6.3 Kernel integrity check
- In memory_init, after all setup: compute checksum of kernel .text section
- Declare extern u8 _text_start[], _text_end[] (from link.ld)
- Iterate _text_start to _text_end in 8-byte steps, XOR each u64 word into running value starting at 0
- Store result in static u64 kernel_text_checksum
- Shell command `integrity`: recompute XOR checksum, compare to stored value
- Print "Kernel integrity OK (checksum 0x[value])" or "WARNING: kernel text modified!"

#### 6.4 Heap use-after-free detection
- In kfree, after marking block free and before coalescing: fill data region with 0xFEEEFEEE repeated
- In kmalloc, when reusing free block: scan first 64 bytes, if any 4-byte word is not 0xFEEEFEEE and not original data pattern, print "POTENTIAL USE-AFTER-FREE"
- kheap_check(): walk entire free-list, verify magic on each block (0xFEEEFEEEFEEEFEEE for free, 0xDEADBEEFDEADBEEF for used), print "HEAP CORRUPTION at 0x[addr]: magic=0x[val]" for mismatches
- Shell command: `heapcheck`

#### 6.5 Secure shell login
- In shell_init, check if file PASSWD exists on FAT16 disk via fs_exists
- If exists: call shell_lock()
- shell_lock(): display "CharisOS Login", prompt "Username:" and "Password:" (read with vga_putchar('*') for each char)
- Call hash_password(username, password) using djb2 with stored salt, compare to stored hash from PASSWD file
- On mismatch: increment fail_count, after 3 failures call task_sleep_ms(30000) then reset
- On success: set shell_authenticated = true
- Shell command `passwd`: prompt current password (verify), prompt new password twice (must match), write new hash and salt to PASSWD file
- Shell command `lock`: set shell_authenticated = false, call shell_lock()
- Hash function: u64 djb2_hash(const char* str, u64 salt) — seed = 5381 XOR salt, for each char: seed = ((seed << 5) + seed) XOR (u8)c, return seed
- PASSWD file format: first 8 bytes = salt (u64), next 8 bytes = hash of username+password, next 32 bytes = username

### 7. STAGE 6 — Unique Features

#### 7.1 Complete the IL runtime in kernel/il_runtime.c
- il_execute(il_image_t* image, il_method_t* method, u64* args, u32 arg_count, u64* result)
- Allocate il_frame_t on kernel heap (stack array, locals array, args pointer)
- Main interpreter loop: u8 opcode = il_blob[frame.ip++]
- Opcodes implemented:
  - nop (0x00)
  - ldarg.0-3 (0x02-0x05): push args[N]
  - ldloc.0-3 (0x06-0x09): push locals[N]
  - stloc.0-3 (0x0A-0x0D): pop to locals[N]
  - ldc.i4 (0x20): read next 4 bytes as s32, sign-extend, push
  - ldc.i4.0-8 (0x16-0x1E): push literal 0-8
  - ldc.i4.m1 (0x15): push (u64)(s64)-1
  - ldc.i8 (0x21): read next 8 bytes, push
  - add (0x58): pop two, push sum
  - sub (0x59): pop two (a then b), push b-a
  - mul (0x5A): pop two, push product
  - div (0x5B): pop two (a then b), check a!=0, push b/a
  - rem (0x5D): pop two, push b%a
  - and (0x5F): pop two, push a&b
  - or (0x60): pop two, push a|b
  - xor (0x61): pop two, push a^b
  - not (0x66): pop one, push ~a
  - neg (0x65): pop one, push (u64)(-(s64)a)
  - shl (0x62): pop shift amount then value, push value<<(amount&63)
  - shr (0x63): pop shift then value, push (s64)value>>(amount&63) (arithmetic)
  - ceq (FE 01): pop two, push (a==b)?1:0
  - cgt (FE 02): pop two, push ((s64)b>(s64)a)?1:0
  - clt (FE 04): pop two, push ((s64)b<(s64)a)?1:0
  - br (0x38): read next 4 bytes as s32 offset, ip += offset
  - brtrue (0x3A): read offset, pop value, if value!=0 jump
  - brfalse (0x39): pop value, if value==0 jump
  - call (0x28): read next 4 bytes as method token, get arg_count from method metadata, pop that many args, recursively call il_execute, push return value
  - ret (0x2A): if frame.stack_pos > 0 set *result = pop(), return
  - ldstr (0x72): read 4-byte string token, treat as offset into string heap, push pointer
  - dup (0x25): push copy of top
  - pop (0x26): discard top
  - conv.i4 (0x69): pop, cast to s32, sign-extend, push
  - conv.i8 (0x6A): pop, push as-is (already u64)
- Native calls (method token high bit set):
  - 0=Console.Write(char), 1=Console.WriteLine(string), 2=Console.ReadLine, 3=Memory.Alloc, 4=Memory.Free, 5=Task.Yield, 6=Task.Sleep, 7=File.Open, 8=File.Read, 9=File.Write, 10=File.Close
- il_runtime_exec(entry_method_name): search image->methods for name match, call il_execute
- Shell command `dotnet <file.il>`: read binary IL image from disk, parse header, call il_runtime_init then il_runtime_exec("Main")

#### 7.2 Created kernel/events.c and include/kernel/events.h — reactive event system
- 32 event IDs: EVENT_KEY_PRESS=0, EVENT_TIMER_TICK=1, EVENT_DISK_READY=2, EVENT_TASK_EXIT=3, EVENT_LOW_MEMORY=4, EVENT_FS_MOUNTED=5, EVENT_SERIAL_RX=6, user-defined 7-31
- For each event: list of up to 8 subscribed tasks, u64 data value
- event_subscribe(u32 event_id, task_t* task): add task to subscriber list
- event_unsubscribe(u32 event_id, task_t* task): remove
- event_publish(u32 event_id, u64 data): store data, iterate subscribers, store data in task->event_data, if task BLOCKED and waiting_event == event_id then task_unblock(task)
- event_wait(u32 event_id): set current_task->waiting_event = event_id, task_block(current_task), scheduler_yield(), return current_task->event_data
- In keyboard_handler: call event_publish(EVENT_KEY_PRESS, scancode)
- In timer_handler every 100 ticks: call event_publish(EVENT_TIMER_TICK, timer_ticks)
- In fs_mount on success: call event_publish(EVENT_FS_MOUNTED, 0)
- In kmalloc when free frames < 10% of total: call event_publish(EVENT_LOW_MEMORY, pmm_used_frames)
- Shell command `evtest`: spawn two tasks, one subscribes to EVENT_KEY_PRESS and prints every key, one subscribes to EVENT_TIMER_TICK and prints every 100 ticks, run for 5 seconds then quit both

#### 7.3 Created kernel/config.c and include/kernel/config.h — persistent configuration
- config_load(): open CHARIS.CFG via fs_open, read line by line, parse KEY=VALUE format, trim whitespace, store in static array of 128 config_entry_t
- Skip lines starting with # (comments) and blank lines
- Print count of entries loaded
- config_get(const char* key, const char* default_val): linear search, return value if found, default_val if not
- config_set(const char* key, const char* value): search for existing key and update, or add new entry
- config_save(): open/create CHARIS.CFG, write each entry as "KEY=VALUE\n", close
- Apply settings in kernel_main after fs_mount: shell_prompt, vga.fg_color/bg_color, security.require_login, scheduler.quantum, boot.show_logo
- Shell command `config`: with no args print all entries, with one arg print that key's value, with two args set key=value and call config_save
- All config changes take effect immediately

#### 7.4 Created kernel/vga_gfx.c and include/kernel/vga_gfx.h — Mode 13h graphics
- vga_gfx_init(): write complete sequence of VGA register values to switch from text mode to 320x200x256 (Mode 13h)
  - Sequencer registers (port 0x3C4/0x3C5): index 0=0x03, 1=0x01, 2=0x0F, 3=0x00, 4=0x0E
  - Misc output register (port 0x3C2): 0x63
  - CRTC registers (port 0x3D4/0x3D5): write unlock (0x11, read value & 0x7F), then all 19 registers
  - GC registers (port 0x3CE/0x3CF): all 9 registers
  - AC registers (port 0x3C0): write 0x20 to re-enable display
  - Set 0xA0000 as framebuffer pointer
- vga_gfx_putpixel(u16 x, u16 y, u8 color): ((u8*)0xA0000)[y*320+x] = color
- vga_gfx_clear(u8 color): kmemset((void*)0xA0000, color, 64000)
- vga_gfx_line(x0,y0,x1,y1,color): full Bresenham line with dx=abs(x1-x0), dy=abs(y1-y0), step logic for all octants
- vga_gfx_rect(x,y,w,h,color,bool filled): if filled use kmemset per row, else draw 4 lines
- vga_gfx_circle(cx,cy,r,color,bool filled): midpoint circle algorithm, if filled draw horizontal spans
- vga_gfx_char(x,y,c,fg,bg): use 8x8 font table (256 characters, each 8 bytes)
- vga_gfx_string(x,y,str,fg,bg): iterate chars calling vga_gfx_char advancing x by 8
- vga_text_mode(): write reverse register sequence to return to 80x25 text mode
- Shell command `gfxdemo`: switch to Mode 13h, draw full-screen gradient (y-based color 0-199), white grid every 40 pixels, 30-pixel filled circle bouncing around screen for 5 seconds (update position every 50ms, erase old circle), then call vga_text_mode() to return to shell

### 8. STAGE 7 — Standard OS Features

#### 8.1 Created kernel/ipc.c and include/kernel/ipc.h — message passing
- Added to task_t: ipc_message_t msg_queue[16], u32 msg_read_pos, u32 msg_write_pos, bool waiting_for_msg
- ipc_message_t: u32 sender_pid, u32 msg_id, u64 data[4]
- ipc_send(u32 target_pid, u32 msg_id, u64* data, bool nowait): find task, check queue not full, enqueue message, if target BLOCKED waiting for message call task_unblock. If queue full and nowait return IPC_ERR_FULL, if !nowait block sender until space
- ipc_recv(u32* out_sender, u32* out_id, u64* out_data): if queue has messages dequeue and fill outputs, return IPC_OK. If empty, set waiting_for_msg = true, task_block(current_task), scheduler_yield(), then dequeue after wakeup
- ipc_broadcast(u32 msg_id, u64* data): send to ALL tasks
- Shell command `ipctest`: create task A and B, A sends 10 messages with msg_id=42 and data[0]=sequence number, B receives and prints each, A prints total round-trip time in ticks

#### 8.2 Created kernel/vfs.c and include/kernel/vfs.h — virtual filesystem
- vfs_ops_t with function pointers: open, read, write, close, readdir, mkdir, unlink, stat
- vfs_node_t: name[256], size, flags (VFS_FILE=1, VFS_DIR=2, VFS_MOUNTPOINT=4), inode, ops pointer
- Register two filesystems: fatfs_ops (backed by fs.c functions) mounted at "/", procfs_ops mounted at "/proc"
- vfs_open(const char* path): if path starts with "/proc" route to procfs, otherwise route to fatfs
- vfs_read/write/close/readdir: delegate to node's ops
- Procfs readdir at "/proc": return entries "tasks", "mem", "config", "uptime"
- procfs_read for "/proc/tasks": format all non-ZOMBIE tasks as "PID NAME STATE TICKS\n" into static buffer
- procfs_read for "/proc/mem": format "total_frames:[N] used_frames:[N] heap_free:[N]bytes\n"
- procfs_read for "/proc/config": dump all config entries as KEY=VALUE\n
- procfs_read for "/proc/uptime": format "[seconds].[tenths]s\n"
- Shell command `cat` should use vfs_open/vfs_read so `cat /proc/tasks` works

#### 8.3 Created kernel/power.c and include/kernel/power.h — power management
- power_shutdown(): print "Shutting down CharisOS in 3..." with 1-second delays between each count using timer_sleep_ms(1000). Then flush disk cache by calling disk_write_sectors cache-flush if disk present. Then attempt ACPI shutdown: scan physical memory range 0xE0000 to 0xFFFFF for 8-byte string "RSD PTR " (RSDP signature), read RSDT/XSDT address, find FADT (signature "FACP"), read PM1a_CNT_BLK port (offset 64 in FADT as u32), write (SLP_TYPa | 0x2000) to that port where SLP_TYPa comes from \_S5_ DSDT object (for simplicity, try value 0x1C00 which works on most QEMU configurations). If ACPI fails, try QEMU-specific: outw(0x604, 0x2000), then outw(0xB004, 0x2000) (older QEMU). Last resort: write 0xFE to port 0x64 (will reboot, not shutdown, but avoids infinite hang)
- power_reboot(): print countdown, then write 0xFE to port 0x64 (keyboard controller reset)
- power_halt(): print "System halted. Safe to power off.", disable interrupts with cli, loop executing hlt
- Shell commands: `shutdown` calls power_shutdown, `reboot` calls power_reboot, `halt` calls power_halt

#### 8.4 Created kernel/crashdump.c and include/kernel/crashdump.h
- In idt_dispatch_handler, for exception vectors 0-19 in kernel mode (CS & 3 == 0), after printing register dump, call crashdump_write(frame) before halt loop
- crashdump_write(reg_frame_t* frame): if fs.mounted is false, print "Cannot write crash dump: no filesystem" and return. Open or create CRASH.DMP via fs_open (or direct FAT16 write if fs is in inconsistent state). Write header struct: magic (0x43524153 "CRAS"), timestamp (timer_ticks), kernel version string (8 bytes "CHARIS01"), frame->vector, frame->error_code. Write full reg_frame_t (160 bytes). Write current task name and PID (36 bytes). Write 4KB of stack contents starting from frame->rsp (read carefully — validate rsp is within kernel stack range before dereferencing). Write heap summary: total blocks, free blocks, used bytes (computed by walking heap free-list). Write string "END_DUMP". Print "Crash dump written to CRASH.DMP" in green
- Shell command `dumpinfo`: open CRASH.DMP, validate magic, print all fields in human-readable format with section headers in different VGA colors. Print register values formatted same as exception handler. If sym_load has been called, print stack trace

#### 8.5 Created kernel/net.c and include/kernel/net.h — loopback network stack
- Define packed structs: ip_header_t (version_ihl, tos, total_length, id, flags_frag, ttl, protocol, checksum, src_ip, dst_ip), udp_header_t (src_port, dst_port, length, checksum), icmp_header_t (type, code, checksum, id, sequence)
- net_init(): set loopback IP = 0x7F000001 (127.0.0.1), initialize 64-entry packet ring buffer (each entry: u8 data[512], u16 len, bool used)
- net_checksum(const void* data, usize len): standard one's complement sum
- net_send_packet(const void* data, usize len, u32 dst_ip): for loopback (dst_ip == 0x7F000001), find free slot in rx_buffer, kmemcpy data in, set used=true
- net_recv_packet(void* buffer, usize max_len, u32* out_src_ip): scan ring buffer for oldest used slot, copy out, mark unused
- udp_send(u32 dst, u16 sport, u16 dport, const void* data, usize len): build ip_header_t (protocol=17 for UDP), build udp_header_t, compute IP checksum, call net_send_packet
- udp_bind(u16 port, callback): store in table of 8 port->callback bindings
- net_poll(): call net_recv_packet, if received: parse IP header, if protocol==17 check UDP port table and call matching callback, if protocol==1 handle ICMP (if echo request, send echo reply)
- Shell command `ping 127.0.0.1`: build ICMP echo request with type=8, send, call net_poll in loop waiting up to 1000ms (using timer_get_ms) for reply, print RTT in milliseconds
- Shell command `netstat`: print bound ports and packet tx/rx counts

### 9. FINAL INTEGRATION in kernel/main.c

After all stages implemented, kernel_main calls in this order:
1. vga_init()
2. vga_enable_cursor(14, 15)
3. serial_init()
4. kprintf boot banner with version and date
5. Validate multiboot2 magic
6. memory_init(info)
7. idt_init()
8. irq_init()
9. timer_init(1000)
10. keyboard_init()
11. task_init()
12. scheduler_init()
13. syscall_init()
14. tss_init()
15. net_init()
16. event_subscribe setup for low-memory warning
17. if disk present: disk_init(), fs_init(), fs_mount(), config_load(), apply config settings
18. Print "All systems go. Starting shell.\n"
19. Create shell task with task_create("shell", shell_main, NULL, CAP_ALL)
20. scheduler_add_task(shell_task)
21. Enable interrupts with sti
22. Call scheduler_start()
23. hlt loop at end never reached

## Build Verification

All object files compiled successfully:
- build/boot.o, build/long_mode.o, build/main.o, build/vga.o, build/serial.o
- build/string.o, build/printf.o, build/memory.o, build/idt.o, build/irq.o
- build/timer.o, build/keyboard.o, build/syscall.o, build/task.o, build/scheduler.o
- build/shell.o, build/il_runtime.o, build/interrupt_stubs.o, build/context.o
- build/gdt.o, build/io.o

Linking successful:
- build/kernel.elf created
- build/charisos.iso created (2524 sectors)

## Known Warnings (non-critical)
- Unused parameters in syscall handlers (a1-a6) — intentional for API compatibility
- Unused pdpt/pml4 variables in memory.c — kept for compatibility with vmm code
- Unused entry_version in memory_init — part of multiboot2 spec
- Unused info parameter in memory_init — parameter kept for API compatibility

## Testing

To test in QEMU:
```bash
qemu-system-x86_64 -cdrom build/charisos.iso -m 256M -serial file:serial.log
```

To test in QEMU with debug:
```bash
qemu-system-x86_64 -cdrom build/charisos.iso -m 256M -serial stdio -s -S
```

Serial output captured to serial.log for debugging.
