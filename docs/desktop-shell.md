# Desktop shell

## Architecture and rendering ownership

The active desktop is a single framebuffer shell running as a kernel task. It owns the wallpaper, workspace shortcuts, Start menu, taskbar, software pointer and application input routing. The shell redraws the framebuffer when UI state changes; it is not yet a general-purpose multi-window compositor.

Terminal, Files, Browser, Settings and About are native framebuffer surfaces. The shell remains the rendering owner for these applications. Their lifecycle is now registered with the window manager through bufferless surface records, so focus, visibility, hit testing and destruction use the same bounded WM metadata without allocating a second full-size pixel buffer for content the applications already draw themselves.

This is an integration step, not completion of the window manager. Only one application surface is active at a time. Independent overlapping windows, dragging, resizing, minimize/restore controls, Alt+Tab and per-window repaint are not yet implemented. The WM's buffered compositor path remains separate from the shell-owned renderer.

## Workspace and taskbar

The workspace uses a restrained graphite palette and a small Files shortcut. The taskbar provides Start and pinned Files, Terminal and Settings launch targets. The software pointer is drawn by the shell. Keyboard launch paths remain available:
- F10 opens Files.
- F11 opens Settings.
- F12 opens Terminal.
- Escape closes the active modal surface.
- The Super key toggles the Start menu.

The desktop remains intentionally sparse while independent window management and richer workspace affordances are developed.

## Native applications

### Files

Files provides a bounded FAT32 root-directory listing, selection, keyboard navigation and refresh. It is read-only and currently limited to root entries and 8.3 names. Nested directories, long filenames, file preview and write operations are not implemented.

### Terminal

Terminal is a native framebuffer command environment with a title area, console canvas, prompt and input handling. Supported commands include:
- `help`, `clear`, `echo TEXT`
- `uname`, `version`, `uptime`
- `mem`, `tasks`, `settings`

It is a built-in kernel command environment, not a general-purpose POSIX shell or isolated userspace terminal.

### Settings

Settings currently exposes font family, text size and global interface scale. Supported choices are:
- Font family: Proportional, Monospace, Bitmap serif.
- Text size: 100%, 200%, 300%.
- Interface scale: 80%, 100%, 120%.

Changes apply immediately and are saved to `/CHARISOS.CFG` when a writable primary ATA FAT32 volume is available. CI verifies persistence across reboot on a test disk. Other Settings navigation categories are not yet backed by implemented services.

### Browser and About

The Browser provides local shell pages and limited remote text-preview plumbing. It is not a full HTML/CSS/JavaScript browser. About is an informational surface.

## Window-manager integration status

The shell synchronizes the active native surface with the WM as application state changes:
- Creates a bufferless WM surface record for the active app.
- Focuses the registered surface on pointer interaction.
- Destroys the previous record when switching or closing applications.
- Uses WM-managed title sanitization, dimension checks, position clamping and bounded window count.
- Avoids allocating an unused duplicate pixel buffer.

Host-side CI tests cover WM allocation failure cleanup, dimensions and buffer caps, title sanitization, focus and hit testing, hidden/minimized filtering, focus recovery, maximum window count, slot reuse and bufferless shell-surface lifecycle.

These tests do not prove framebuffer compositing, physical pointer behavior or multi-window interaction. Those remain explicit milestones.

## Known limitations

- The shell is a kernel task, not an isolated userspace desktop session.
- There is no complete multi-window desktop, independent application surfaces or per-window compositor.
- Files is read-only and limited to FAT32 root entries with 8.3 names.
- Terminal command coverage is intentionally small.
- Browser remote rendering is not a complete standards-compliant browser.
- Settings categories beyond display preferences are not yet functional.
- Physical hardware and broad UEFI interaction testing remain outstanding.
