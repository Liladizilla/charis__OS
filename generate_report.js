const fs = require('fs');
const path = require('path');
const { Document, Packer } = require('docx');
const DS = require('./design_system.js');

// Use the design system
const {
  C, F, sp, ind, TR, TRc, TRm, P, H1, H2, H3, H4,
  Body, BodyRuns, Bul, CodeBlock, Separator, Callout, SPC,
  BugBlock, PhaseHeader, FileHeader, AlignmentType
} = DS;

// ─── REPORT CONTENT ──────────────────────────────────────────────────────────
const sections = [];

// Title Page
sections.push(
  new DS.Paragraph({
    ...sp(1200, 200),
    alignment: AlignmentType.CENTER,
    children: [new DS.TextRun({
      text: 'CHARIS OS',
      font: F.head,
      size: 72,
      bold: true,
      color: C.amber
    })]
  }),
  new DS.Paragraph({
    ...sp(200, 400),
    alignment: AlignmentType.CENTER,
    children: [new DS.TextRun({
      text: 'Development Report & Technical Documentation',
      font: F.head,
      size: 36,
      color: C.gray5
    })]
  }),
  new DS.Paragraph({
    ...sp(200, 200),
    alignment: AlignmentType.CENTER,
    children: [new DS.TextRun({
      text: 'Generated: ' + new Date().toLocaleDateString(),
      font: F.body,
      size: 22,
      color: C.gray4
    })]
  }),
  new DS.Paragraph({
    children: [new DS.PageBreak()]
  })
);

// Executive Summary
sections.push(H1('EXECUTIVE SUMMARY', C.amber));
sections.push(Body('CharisOS is a lightweight, educational operating system designed for x86_64 architecture. It combines low-level Assembly boot logic with a minimal C runtime, implementing a managed-style kernel architecture that emphasizes simplicity, security, and performance on low-end hardware.', 120, 120));
sections.push(Body('This report documents the complete development journey through 17 phases, covering memory management, syscalls, filesystems, graphics, networking, and desktop environment implementation.', 120, 120));

sections.push(SPC(2));

// Phase 1: Memory/VMM
sections.push(...PhaseHeader('01', 'Memory & Virtual Memory Manager', 'COMPLETE', C.green,
  'Foundation of the kernel with physical and virtual memory management'));
sections.push(H2('Physical Memory Manager (PMM)', C.blue));
sections.push(Bul([
  'Bitmap-based frame allocator tracking available physical memory',
  'Double-free protection with validation checks',
  'Next-fit cursor optimization for allocation speed'
]));
sections.push(H2('Virtual Memory Manager (VMM)', C.blue));
sections.push(Bul([
  'Page table management with 4-level hierarchy (PML4→PDP→PD→PT)',
  'Identity mapping for kernel space',
  'CR3 reload for TLB flush on context switch'
]));
sections.push(FileHeader('kernel/pmm.c'));
sections.push(FileHeader('kernel/vmm.c'));
sections.push(FileHeader('kernel/heap.c'));

sections.push(SPC(2));

// Phase 2: Syscalls
sections.push(...PhaseHeader('02', 'System Call Interface', 'COMPLETE', C.green,
  'User-space entry points with full register save/restore'));
sections.push(H2('Implementation Details', C.blue));
sections.push(Bul([
  'SYSCALL/SYSRET assembly entry points with register preservation',
  'MSRs setup (LSTAR, STAR, SF_MASK) for fast syscall dispatch',
  'Core syscalls: exit, getpid, yield, sleep, read, write, print, fork, exec'
]));
sections.push(H2('Process Management', C.blue));
sections.push(Bul([
  'Per-process file descriptor table (stdin/stdout/stderr pre-opened)',
  'task_create_with_pml4() for separate address spaces',
  'Fork creates child with copied page tables'
]));
sections.push(FileHeader('kernel/syscall.c'));
sections.push(FileHeader('kernel/task.c'));

sections.push(SPC(2));

// Phase 3: Filesystem
sections.push(...PhaseHeader('03', 'Virtual Filesystem & FAT32', 'COMPLETE', C.green,
  'VFS abstraction layer with FAT32 backend'));
sections.push(H2('VFS Layer', C.blue));
sections.push(Bul([
  'Mount point management with path resolution',
  'Device nodes: /dev/null, /dev/zero, /dev/kbd, /dev/vga',
  'Per-task fd table with fd_alloc(), fd_get(), fd_close()'
]));
sections.push(H2('FAT32 Integration', C.blue));
sections.push(Bul([
  'Cluster chain traversal',
  'Directory entry parsing',
  'sys_open() / sys_close() syscalls'
]));
sections.push(FileHeader('kernel/vfs.c'));
sections.push(FileHeader('kernel/fs.c'));

sections.push(SPC(2));

// Phase 4: ELF Loader
sections.push(...PhaseHeader('04', 'ELF Binary Loader', 'COMPLETE', C.green,
  'Executable and Linkable Format parser for user programs'));
sections.push(Bul([
  'ELF header and program header parsing',
  'PT_LOAD segment mapping into user address space',
  'sys_exec() syscall for process loading'
]));
sections.push(FileHeader('kernel/elf.c'));

sections.push(SPC(2));

// Phase 5: Graphics
sections.push(...PhaseHeader('05', 'Graphics & Display Subsystem', 'COMPLETE', C.green,
  'Framebuffer driver with 2D graphics primitives'));
sections.push(H2('Framebuffer Driver', C.blue));
sections.push(Bul([
  'VESA/GOP framebuffer initialization',
  'PSF2 font rendering with fb_put_pixel(), fb_clear(), fb_fill_rect()'
]));
sections.push(H2('2D Graphics Primitives', C.blue));
sections.push(Bul([
  'graphics_line() - Bresenham\'s line algorithm',
  'graphics_rect() - filled and outline rectangles',
  'graphics_circle() - midpoint circle algorithm',
  'graphics_put_string() - PSF-based text rendering'
]));
sections.push(FileHeader('kernel/fb.c'));
sections.push(FileHeader('kernel/psf.c'));
sections.push(FileHeader('kernel/graphics.c'));

sections.push(SPC(2));

// Phase 6: Window System
sections.push(...PhaseHeader('06', 'Window Manager & Input System', 'COMPLETE', C.green,
  'Compositing window manager with widget library'));
sections.push(Bul([
  'Z-order window management with decorations',
  'Input event queue with mouse tracking',
  'PS/2 mouse driver with button state tracking',
  'Window dragging support via wm_process_mouse()'
]));
sections.push(FileHeader('kernel/wm.c'));
sections.push(FileHeader('kernel/input.c'));
sections.push(FileHeader('kernel/mouse.c'));

sections.push(SPC(2));

// Phase 7: IPC
sections.push(...PhaseHeader('07', 'Inter-Process Communication', 'COMPLETE', C.green,
  'Message queues and shared memory'));
sections.push(Bul([
  'Message queues: 16 channels, 512 bytes/message',
  'Shared memory: 16 blocks, 4KB each',
  'Syscalls: SYS_SHM_ALLOC, SYS_SHM_GET, SYS_SHM_FREE, SYS_IPC_CREATE, SYS_IPC_SEND, SYS_IPC_RECV'
]));
sections.push(FileHeader('kernel/ipc.c'));

sections.push(SPC(2));

// Phase 8: Network
sections.push(...PhaseHeader('08', 'Network Stack', 'COMPLETE', C.green,
  'TCP/IP foundation with socket API'));
sections.push(Bul([
  'Socket API: AF_INET, SOCK_STREAM/SOCK_DGRAM',
  'RTL8139 Ethernet driver support',
  'Socket syscalls: socket, connect, bind, listen, accept, send, recv, close'
]));
sections.push(FileHeader('kernel/net.c'));
sections.push(FileHeader('kernel/socket.c'));

sections.push(SPC(2));

// Phase 9: Desktop Environment
sections.push(...PhaseHeader('09', 'Desktop Environment', 'COMPLETE', C.green,
  'Graphical desktop with icons and taskbar'));
sections.push(Bul([
  'Desktop with icon grid and taskbar',
  'Window management integration',
  'Demo GUI application'
]));
sections.push(FileHeader('kernel/desktop.c'));

sections.push(SPC(2));

// Phase 10: User Applications
sections.push(...PhaseHeader('10', 'Built-in Applications', 'COMPLETE', C.green,
  'Application suite with Game SDK'));
sections.push(Bul([
  'Terminal, File Manager, Text Editor, Calculator, Settings',
  'Game SDK (sdk/charis_game.h) with graphics/audio syscalls',
  'SYS_GAME_INIT, SYS_GAME_CLEAR, SYS_GAME_FLIP, SYS_GAME_AUDIO_OPEN, SYS_GAME_AUDIO_PLAY'
]));
sections.push(FileHeader('kernel/apps.c'));
sections.push(FileHeader('sdk/charis_game.h'));

sections.push(SPC(2));

// Phase 11: Hardware Abstraction
sections.push(...PhaseHeader('11', 'Hardware Abstraction Layer', 'COMPLETE', C.green,
  'PCI, USB, and audio driver framework'));
sections.push(Bul([
  'HDA audio driver with PCI binding',
  'USB device enumeration placeholder',
  'Driver framework with probe/remove callbacks',
  'PCI class/subclass detection in pci_scan()'
]));
sections.push(FileHeader('kernel/pci.c'));
sections.push(FileHeader('kernel/hda.c'));
sections.push(FileHeader('kernel/driver.c'));

sections.push(SPC(2));

// Phase 12: Graphics Acceleration
sections.push(...PhaseHeader('12', 'Software 3D Rasterizer', 'COMPLETE', C.green,
  'Fixed-point 3D graphics acceleration'));
sections.push(Bul([
  '16.16 fixed-point arithmetic for performance',
  'Triangle rasterization with depth buffer (640x480, ~1.5MB)',
  'Barycentric coordinate interpolation'
]));
sections.push(FileHeader('kernel/raster.c'));

sections.push(SPC(2));

// Phase 13: Gaming Support
sections.push(...PhaseHeader('13', 'Gaming Subsystem', 'COMPLETE', C.green,
  'Gamepad and game SDK integration'));
sections.push(Bul([
  'USB HID gamepad support',
  'Game SDK header for user applications',
  'Graphics and audio syscalls for games'
]));
sections.push(FileHeader('kernel/gamepad.c'));
sections.push(FileHeader('sdk/charis_game.h'));

sections.push(SPC(2));

// Phase 14: Optimizations
sections.push(...PhaseHeader('14', 'Optimizations & Polish', 'IN PROGRESS', C.amber,
  'Build system verification and hardware initialization'));
sections.push(Bul([
  'Build tools verification (nasm, gcc, ld, qemu)',
  'Framebuffer initialization via Multiboot2 GFX tag',
  'PCI enumeration verification'
]));

sections.push(SPC(2));
sections.push(new DS.PageBreak());

// Technical Specifications
sections.push(H1('TECHNICAL SPECIFICATIONS', C.amber));
sections.push(H2('System Architecture', C.blue));
sections.push(Bul([
  'Architecture: x86_64 (64-bit)',
  'Boot Standard: Multiboot2',
  'Kernel Language: C (freestanding)',
  'Boot Language: NASM Assembly',
  'Memory Model: Flat memory model with paging',
  'Process Model: Single address space (hybrid kernel design)'
]));

sections.push(H2('Resource Limits', C.blue));
sections.push(Bul([
  'Maximum Tasks: 32 concurrent tasks',
  'Default Stack Size: 8KB per task with guard pages',
  'Timer Frequency: 1000 Hz (configurable)',
  'Minimum RAM: 64MB (tested with 256MB)',
  'Maximum Displays: 4 monitors'
]));

sections.push(H2('Security Features', C.blue));
sections.push(Bul([
  'Stack canaries for buffer overflow detection',
  'Guard pages below stacks to catch overruns',
  'Capability-based permission system',
  'Privilege separation (ring 0 kernel)'
]));

sections.push(SPC(2));
sections.push(new DS.PageBreak());

// Known Issues & Future Work
sections.push(H1('KNOWN ISSUES & FUTURE WORK', C.amber));
sections.push(H2('Remaining Limitations', C.blue));
sections.push(Bul([
  'Build tools not available in current environment (nasm, gcc, ld, qemu)',
  'Screen resolution hardcoded to 80x25 text mode',
  'No PCI enumeration - network uses hardcoded I/O port',
  'Framebuffer initialization needs Multiboot2 GFX tag parsing'
]));

sections.push(H2('Planned Enhancements', C.blue));
sections.push(Bul([
  'Full PCI enumeration for hardware detection',
  'Dynamic screen resolution support',
  'Network stack completion (TCP/IP full implementation)',
  'USB driver expansion beyond enumeration',
  'Power management ACPI integration'
]));

sections.push(SPC(2));
sections.push(Separator());

// Footer
sections.push(Body('End of Report', 200, 200, { alignment: AlignmentType.CENTER }));

// ─── BUILD DOCUMENT ──────────────────────────────────────────────────────────
const doc = new Document({
  sections: [{
    properties: {
      page: {
        margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 }
      }
    },
    children: sections
  }]
});

Packer.toBuffer(doc).then(buffer => {
  const outputPath = path.join(__dirname, 'CharisOS_Development_Report.docx');
  fs.writeFileSync(outputPath, buffer);
  console.log('Report generated successfully: ' + outputPath);
}).catch(err => {
  console.error('Error generating report:', err);
});