# TODO - CharisOS Phase 1 Memory/VMM Refactor

## Step 1: Create new memory module files
- [ ] Add `kernel/heap.c` with moved `kmalloc/kfree` implementation from `kernel/memory.c`
- [ ] Add `kernel/bootmem.c` skeleton for multiboot parsing (or helpers)

## Step 2: Refactor `kernel/memory.c`
- [ ] Replace contents with thin `memory_init(info)` orchestrator calling:
  - pmm init
  - vmm init
  - heap_init

## Step 3: Fix/lock public headers
- [ ] Update `include/kernel/memory.h` to declare only kernel heap + memory_init + kmalloc/kfree + flags
- [ ] Ensure `include/kernel/pmm.h` and `include/kernel/vmm.h` match new strict APIs

## Step 4: VMM correctness fixes
- [ ] In `kernel/vmm.c`, replace `alloc_pt()` with guaranteed page-table mapping strategy (no hardcoded higher-half assumption)
- [ ] Implement `get_or_create_table()` and `walk_page_tables()` helpers
- [ ] Refactor `vmm_map_page()` to reload parent entries and recompute table pointers after inserts

## Step 5: VMM unit tests
- [ ] Add `kernel/vmm_test.c` (or similar) with tests:
  - map existing page
  - map missing page
  - remap same page
  - unmap page
  - map user page
- [ ] Add test runner call after `vmm_init()` (behind debug flag)

## Step 6: Minimal PMM safety improvements
- [ ] Add `pmm_free_page()` validation (range + double-free check)
- [ ] Add next-fit cursor (`last_free_hint`) to reduce allocation scan cost

## Step 7: Build integration
- [ ] Update `Makefile` to compile new files (`heap.c`, `bootmem.c`, `vmm_test.c`)
- [ ] Build and run QEMU; verify boot completes and tests report pass

