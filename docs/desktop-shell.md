# Desktop shell

## Architecture and rendering ownership

The active desktop is a framebuffer shell running as a kernel task. It owns the wallpaper, workspace shortcuts, Start menu, taskbar and software pointer. It paints the desktop first, then asks the window manager to composite independently registered application surfaces in back-to-front z-order.

Terminal, Files, Browser, Settings and About each have a bufferless window record. The WM owns window identity, geometry, z-order, clipping, visibility, minimize/restore and focus. The shell owns each app's state and renderer through paint and event callbacks. Applications draw directly into their own clipped framebuffer rectangle, so the WM does not allocate a second pixel buffer for these shell-owned surfaces.

## Window interaction

- Clicking a visible window focuses and raises it.
- Unclaimed titlebar space can be dragged with the left mouse button. Application close controls consume their own clicks and do not start a drag.
- Clicking a pinned taskbar icon opens/focuses its window; clicking the focused icon minimizes it; clicking a minimized icon restores it.
- The taskbar marks open windows and uses the accent rule for the focused application.
- Alt+Tab cycles visible, non-minimized windows. Subsequent keyboard and pointer events are delivered only to the focused window.
- Escape is dispatched to the focused application unless the launcher is open.

The WM exposes bounded move, pointer resize, keyboard resize, maximize and transactional restore operations. The lower-right pointer grip and Alt+F8 keyboard-resize mode share the same app-specific minimum sizes and work-area limits. In keyboard mode, arrows adjust the size, Enter commits and Escape restores the original bounds. Super+Up maximizes, Super+Down restores or minimizes, and Alt+F4 closes the focused app. Snap/tiling and workspace management are not implemented yet. The current compositor redraws the desktop and all visible surfaces on a shell repaint; dirty-region repaint and independent userspace framebuffers remain future work.

## Workspace and taskbar

The workspace uses a compact two-column shortcut grid for Files, Terminal, Settings and Browser, with a pinned taskbar for the same core applications. About is launched from the Start menu. Focus and minimized state are visible on the taskbar.

Keyboard launch paths:
- F9 opens/focuses Browser.
- F10 opens/focuses Files.
- F11 opens/focuses Settings.
- F12 opens/focuses Terminal.
- Alt+Tab cycles the visible application windows.
- Super+Up maximizes the focused application.
- Super+Down restores a maximized window, or minimizes a normal window.
- Alt+F8 starts keyboard resize mode for the focused window; arrows adjust it, Enter commits and Escape restores the original bounds.
- Alt+F4 closes the focused application.
- The Super key toggles the Start menu.
- Escape is sent to the focused application.

## Native applications

### Files
Files provides a bounded FAT32 root-directory listing, selection, keyboard navigation and refresh. It remains read-only and currently limited to root entries and 8.3 names.

### Terminal
Terminal is a native framebuffer command environment with a console canvas, prompt and input handling. Supported commands include `help`, `clear`, `echo TEXT`, `uname`, `version`, `uptime`, `mem`, `tasks` and `settings`. It is not yet a general-purpose POSIX shell or isolated userspace terminal.

### Settings
Settings is an independent window exposing font family, text size and global interface scale. Changes apply immediately and persist to `/CHARISOS.CFG` on supported writable primary ATA FAT32 storage. The Personalization and System categories remain visual placeholders rather than functional services.

### Browser and About
Browser provides local shell pages and limited remote text-preview plumbing. It is not a full standards-compliant browser. About is an informational window.

## Validation

Host-side CI covers allocation failure cleanup, dimensions and buffer caps, title sanitization, focus and hit testing, visibility, focus recovery, maximum window count, slot reuse, bufferless surface lifecycle, bounded movement, transactional resize, minimize/restore, maximize/restore geometry, failed buffer allocation rollback, focus cycling, compositor clipping/z-order and focus-only event dispatch.

The QEMU capture path exercises Files and Terminal open at the same time, switches focus with Alt+Tab, confirms keyboard input reaches Files only when Files is focused, executes `help` in Terminal, maximizes Terminal with Super+Up, restores its original bounds with Super+Down, then uses Alt+F8 and the arrow keys to resize it from 640×404 to 720×464 before validating Settings persistence across reboot. CI checks the matching serial markers for all window state transitions. Pointer-driven resize remains implemented, but QEMU's HMP mouse injection has not been reliable enough to claim pointer-drag verification.

## Known limitations

- The shell and application renderers are kernel-owned, not isolated userspace processes.
- The workspace now exposes four direct application shortcuts, but still needs richer workspace affordances and resolution/scale validation before release-quality design.
- Snap/tiling controls, window menus and dirty-region rendering are not implemented.
- Pointer-driven resize uses a lower-right grip with per-app minimum dimensions and work-area clamping; QEMU pointer-drag verification is still pending.
- Files is read-only and limited to FAT32 root entries with 8.3 names.
- Terminal command coverage is intentionally small.
- Browser remote rendering is not a standards-compliant browser; TLS, full HTML/CSS and JavaScript execution are not implemented.
- Settings categories beyond display preferences are not yet functional.
- Physical hardware and broad UEFI interaction testing remain outstanding.