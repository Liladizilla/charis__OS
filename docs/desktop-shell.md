# CharisOS desktop shell

## Current desktop

The native framebuffer shell provides:

- A dark desktop with a compact Start/taskbar surface.
- Browser, Files and Terminal desktop shortcuts.
- Four taskbar launchers: Browser, Files, Terminal and Settings.
- A live System Overview card with uptime, managed RAM, active/blocked task counts, current task/PID, context switches, interrupt count and syscall count.
- A notification card.
- A Settings panel that reflows with font size and interface scale.

## Settings

The current controls are:

| Setting | Values | Behavior |
|---|---|---|
| Font family | Proportional, monospace, bitmap serif | Changes bitmap glyph selection |
| Font size | 1x, 2x, 3x | Changes text scale |
| UI scale | 80%, 100%, 120% | Changes desktop geometry and control sizes |

Changes apply immediately and are saved to the root FAT32 file `/CHARISOS.CFG`. If a writable FAT32 volume is mounted, the kernel creates the file when needed and reloads it on the next boot. Without writable FAT32 storage, the Settings panel explicitly reports that the current session is not persistent.

## Input

- Click the Start button to open the launcher.
- Click a desktop shortcut to open its app or status notice.
- Use the pinned gear icon to open Settings directly.
- Use the minus/plus controls to change font family, font size and global scale.

## Diagnostics

The System Overview values are collected from kernel diagnostics at redraw time. Free RAM is queried through the PMM's cached O(1) counter so the UI does not scan the physical-page bitmap on every frame.

## Security-related shell behavior

The desktop is a kernel task. User processes use private user code and stack frames; the scheduler updates TSS.RSP0 to the selected task's private kernel stack before each handoff. System-wide IPC, socket, audio, framebuffer-clear and diagnostics syscalls remain kernel-only until per-process ownership and capability-backed handles are implemented.

## Known limitations

- Browser opens, but Files and Terminal are still status notices rather than complete applications.
- Preferences persist only when a writable FAT32 volume is mounted; the current VFS supports root-directory 8.3 filenames, not nested configuration directories.
- ELF execution is denied to untrusted tasks until the loader and process-image lifecycle are fully enabled.
- Manual keyboard/mouse testing on physical hardware and UEFI boot validation remain outstanding.
