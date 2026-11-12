# Charis Design Language

Status: design baseline for the native framebuffer shell. This document describes
the intended visual and interaction rules; it does not claim that every component
or behavior is already implemented.

## Principles

1. Content first, chrome second.
2. One accent color, used for focus and selection.
3. The same component must look and behave the same in every application.
4. Layout must adapt to text scale and framebuffer size.
5. User-facing text uses plain language. Kernel and filesystem details belong in diagnostics.
6. Prefer durable, restrained surfaces over decorative effects.

## Visual tokens

The current implementation source of truth is `include/kernel/ui_theme.h`.
Applications must use these shared tokens instead of defining local palettes.

| Token | Current value | Use |
|---|---:|---|
| Background | `#0F1216` | Desktop background |
| Panel | `#171B21` | Windows and major surfaces |
| Raised panel | `#1F252D` | Controls and taskbar |
| Surface | `#29313B` | Rows and secondary surfaces |
| Border | `#2E3742` | Separation |
| Primary text | `#E8ECF1` | Main content |
| Muted text | `#A3ADBA` | Supporting content |
| Accent | `#5E9BD6` | Focus, selection and active state |
| Success | `#5FB37C` | Successful operations only |
| Warning | `#D9A441` | Warnings only |
| Danger | `#E0625A` | Destructive/error states only |

Do not use per-application rainbow edge stripes, purple as an app accent,
unexplained status colors, glow effects, or decorative gradients.

## Typography

- The current kernel desktop still uses a PSF bitmap font. Its scaled rendering
  is a known limitation, not a finished accessibility solution.
- Do not claim proportional/vector typography until a real font rasterizer and
  metrics are integrated and tested.
- UI text should use sentence case, readable weights, consistent baselines and
  measured widths. Avoid tracking-heavy uppercase micro-labels.
- Monospace is reserved for terminal output and technical diagnostics.

## Layout and scaling

The target toolkit uses a measure pass followed by an arrange pass. Containers
must support minimum/preferred sizes, padding, margins, gaps, horizontal and
vertical flow, grids, stacking and scrolling. Text must wrap at word boundaries
or be visibly ellipsized. Children must not draw outside their parent.

Every screen must eventually pass at 100%, 125%, 150% and 200% UI scale at
1024x768, 1280x800 and 1920x1080. Until then, unsupported combinations must be
reported as limitations rather than described as accessible or responsive.

## Desktop and application behavior

- Desktop shell and application content are separate layers.
- The desktop target is a quiet wallpaper, a small icon set, a launcher and a
  taskbar. System metrics belong in System Monitor, not on the desktop.
- All applications share one titlebar and window-control model.
- Modal dialogs dim the surface behind them. Window focus, z-order, minimize,
  restore and keyboard switching must be consistent.
- Files must describe current storage limits honestly. Read-only media must not
  imply write support.
- Browser, editor and other unfinished applications must not appear as working
  launch targets.

## Interaction quality gates

- Keyboard navigation and visible focus are required for interactive controls.
- Hit-test geometry must match rendered geometry.
- Clipping must be applied before drawing child content.
- Every empty, error, unavailable and read-only state needs deliberate copy.
- Repeated open/close, focus changes, scale changes and redraws must not leave
  stale pixels or input targets.
- Screenshots are captured from QEMU and reviewed at each supported scale and
  resolution before a UI change is considered complete.

## Current implementation limits

The current shell is a single-framebuffer, modal-app environment. The desktop
is a minimal workspace with a Files shortcut and taskbar; Files is a read-only
FAT32 root browser; Terminal is a built-in kernel command environment; Settings
exposes display/font preferences. The WM source now has bounded allocation,
clipping and focus/lifecycle safeguards, but it is not yet the active desktop's
complete general-purpose window manager. There is no installer, writable user
home, browser engine, package manager or finished userspace. See [TODO.md](../TODO.md) for the authoritative status.
