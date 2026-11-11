# Desktop shell

## Architecture and rendering ownership

The active desktop is a single framebuffer shell running as a kernel task. It owns the wallpaper, workspace shortcuts, Start menu, taskbar, software pointer and application input routing. The shell redraws the framebuffer when UI state changes; it is not yet a general-purpose multi-window compositor.

Terminal, Files, Browser, Settings and About are native framebuffer surfaces. The shell remains the rendering owner for these applications. Their lifecycle is registered with the window manager through bufferless surface records, so focus, visibility, hit testing and destruction use bounded WM metadata without allocating duplicate content buffers.

The WM now exposes bounded geometry and lifecycle operations for the next integration stage: move, transactional resize, minimize, restore, visibility changes and focus cycling. These operations are covered by host-side tests. They do not yet move the shell-owned application renderers, provide independent application framebuffers or establish a complete compositor.

## Workspace and taskbar

The workspace uses a restrained graphite palette and a small Files shortcut. The taskbar provides Start and pinned Files, Terminal and Settings launch targets. Keyboard launch paths remain available:
- F10 opens Files.
- F11 opens Settings.
- F12 opens Terminal.
- Escape closes the active modal surface.
- The Super key toggles the Start menu.

## Native applications

### Files
Files provides a bounded FAT32 root-directory listing, selection, keyboard navigation and refresh. It is read-only and currently limited to root entries and 8.3 names.

### Terminal
Terminal is a native framebuffer command environment with a console canvas, prompt and input handling. Supported commands include `help`, `clear`, `echo TEXT`, `uname`, `version`, `uptime`, `mem`, `tasks` and `settings`. It is not yet a general-purpose POSIX shell or isolated userspace terminal.

### Settings
Settings exposes font family, text size and global interface scale. Changes apply immediately and persist to `/CHARISOS.CFG` on supported writable primary ATA FAT32 storage. Other categories are not yet backed by implemented services.

### Browser and About
Browser provides local shell pages and limited remote text-preview plumbing. It is not a full standards-compliant browser. About is informational.

## Window-manager integration status

Host-side CI covers allocation failure cleanup, dimensions and buffer caps, title sanitization, focus and hit testing, visibility, focus recovery, maximum window count, slot reuse, bufferless surface lifecycle, bounded movement, transactional resize, minimize/restore and focus cycling.

The shell still activates one modal application at a time. The new WM operations are metadata-level foundations only: independent shell rendering, titlebar controls, Alt+Tab integration, resize handles, per-window repaint and QEMU pointer interaction remain incomplete.

## Known limitations

- The shell is a kernel task, not an isolated userspace desktop session.
- There is no complete multi-window desktop, independent application rendering or per-window compositor.
- Files is read-only and limited to FAT32 root entries with 8.3 names.
- Terminal command coverage is intentionally small.
- Browser remote rendering is not a complete standards-compliant browser.
- Settings categories beyond display preferences are not yet functional.
- Physical hardware and broad UEFI interaction testing remain outstanding.
