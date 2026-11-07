# CharisOS desktop shell

## Current desktop

The native framebuffer shell provides a graphite workspace with a system overview and application access.

- Workspace heading and concise desktop guidance.
- Live overview cards for uptime, available managed memory and active kernel tasks.
- Files desktop shortcut.
- Pinned Browser, Files, Terminal and Settings taskbar launchers.
- Start menu with application entries.
- Software pointer and keyboard navigation.

The overview values are sampled from kernel timer, physical memory manager and diagnostics services. They are informational kernel metrics, not a substitute for a full hardware monitor.

## Native applications

### Files

The Files application presents the mounted FAT32 root directory in a native file-manager layout. It supports bounded root enumeration, selection, keyboard navigation and F5 refresh. The current filesystem browser is read-only and limited to FAT32 8.3 root entries. Nested directories, long filenames, file preview and write operations are not implemented.

### Terminal

The Terminal is a native framebuffer command surface with a session header, console canvas, line gutter, differentiated prompt/output colours and a separate command input row. Commands currently supported:

- `help`, `clear`, `echo TEXT`
- `uname`, `version`, `uptime`
- `mem`, `tasks`, `settings`

This is a built-in kernel command environment, not yet a general-purpose POSIX shell or isolated userspace terminal.

## Settings

The Settings application currently exposes appearance preferences in a structured panel:

| Setting | Values | Behavior |
|---|---|---|
| Font family | Proportional, monospace, bitmap serif | Changes bitmap glyph selection |
| Text size | 100%, 200%, 300% | Changes text scale |
| Interface scale | 80%, 100%, 120% | Changes desktop geometry and control sizes |

Changes apply immediately and are saved to the root FAT32 file `/CHARISOS.CFG` when a writable volume is mounted. Without writable storage, the panel reports that preferences are session-only.

## Input

- Click the Start button to open the launcher.
- Click the Files desktop shortcut or pinned application icons.
- Use F10 to open Files and F12 to open Terminal.
- Use Escape to close the active modal application.
- Use the Settings minus/plus controls to change appearance preferences.

## Known limitations

- Browser networking and remote web transport are not complete.
- Files remains a read-only root-directory browser with FAT32 8.3 names.
- Terminal command coverage is intentionally small.
- The shell is still a kernel task rather than a fully isolated userspace desktop session.
- Physical hardware and UEFI interaction testing remain outstanding.
