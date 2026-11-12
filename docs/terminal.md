# CharisOS Terminal

## Rendering model

The native Terminal uses an explicit fixed-cell renderer. It does not temporarily change the shared desktop font preference; each terminal draw operation selects monospace glyph advances directly. Whitespace consumes the same cell advance as visible characters, so ASCII alignment does not drift.

The console canvas is near-black and avoids editor-style line-number gutters. The CharisOS mark is a 13-column, 6-row ASCII C rendered in amber at a fixed 2× glyph scale. It is drawn in a dedicated identity header above the transcript, so `clear`, command output and scrollback cannot erase the logo. The adjacent identity lines follow the selected monospace text scale and collapse to a centered mark on narrow windows. The 14-character tagline fits beside the mark at the maximum 3× text scale.

`tests/terminal_ascii_layout_test.c` verifies every row is exactly 19 columns and that the mark fits the default terminal content width at 3× scale. CI also compiles the full freestanding kernel with the monospace renderer.

## Limits

The terminal is still a kernel-owned command environment with a small built-in command set. It does not yet have a general process-backed shell, pipes/PTY job control, arbitrary executable launching, shell scripting, command history persistence or a complete userspace runtime. The visual changes do not imply those capabilities exist.

