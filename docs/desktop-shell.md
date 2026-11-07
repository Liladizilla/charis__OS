# Desktop shell

## Desktop workspace

The native framebuffer desktop uses a shared graphite palette with restrained blue, amber, green and violet application accents. The workspace contains:
- Product identity and a welcome surface.
- Live system status for free managed RAM, active kernel tasks and uptime.
- Four application tiles for Browser, Files, Terminal and Settings.
- A pinned taskbar and Start launcher.
- A software pointer and keyboard launch paths.

The system card reads kernel diagnostics and memory-manager counters. These are current kernel metrics, not a complete hardware monitor. The desktop is still a single framebuffer shell, not yet a multi-window compositor.

## Native applications

### Files

Files uses a three-part layout: volume locations, the root-directory list and a selection inspector. It supports bounded FAT32 root enumeration, keyboard navigation, selection and F5 refresh. The current filesystem browser is read-only and limited to FAT32 8.3 root entries. Nested directories, long filenames, file preview and write operations are not implemented. The inspector reports metadata only; it does not claim to open or edit a file.

### Terminal

Terminal is a native framebuffer command environment with a session title bar, local-session strip, dark console canvas, line gutter, differentiated prompt/output colours and a dedicated input row. Its text is rendered at a dedicated 2x scale for readability. Commands currently supported:
- `help`, `clear`, `echo TEXT`
- `uname`, `version`, `uptime`
- `mem`, `tasks`, `settings`

This is a built-in kernel command environment, not yet a general-purpose POSIX shell or isolated userspace terminal.

## Settings

Settings uses a navigation rail and a focused Display preferences page. Current controls:
- Font family: Proportional, Monospace, Bitmap serif.
- Text size: 100%, 200%, 300%.
- Interface scale: 80%, 100%, 120%.

Changes apply immediately and are saved to `/CHARISOS.CFG` when a writable FAT32 volume is mounted. Without writable storage, the panel reports that preferences are session-only. The navigation categories are visual structure only until their corresponding settings services are implemented.

## Input

- Click an application tile or pinned taskbar icon to launch it.
- Click Start to open the application launcher.
- F10 opens Files and F12 opens Terminal.
- Escape closes the active modal application.
- Settings controls apply font family, text size and interface scale changes.

## Known limitations

- Browser networking and remote web transport are not complete.
- Files remains a read-only root-directory browser with FAT32 8.3 names.
- Terminal command coverage is intentionally small.
- The shell is still a kernel task rather than a fully isolated userspace desktop session.
- There is no general window manager yet: applications are modal surfaces, not independently movable, resizable, overlapping windows.
- Physical hardware and UEFI interaction testing remain outstanding.
