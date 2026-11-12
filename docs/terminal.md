# CharisOS Terminal

## Rendering model

The native Terminal uses an explicit fixed-cell renderer. It does not temporarily change the shared desktop font preference; each terminal draw operation selects monospace glyph advances directly. Whitespace consumes the same cell advance as visible characters, so ASCII alignment does not drift.

The console canvas is near-black and avoids editor-style line-number gutters. The CharisOS mark is a 19-column, 12-row ASCII C rendered in the amber accent. The banner is compact enough to fit the default 760-pixel window at the maximum 3× text scale with the built-in 8-pixel glyph cell and one-pixel cell spacing.

`tests/terminal_ascii_layout_test.c` verifies every row is exactly 19 columns and that the mark fits the default terminal content width at 3× scale. CI also compiles the full freestanding kernel with the monospace renderer.

## Limits

The terminal is still a kernel-owned command environment with a small built-in command set. It does not yet have a general process-backed shell, pipes/PTY job control, arbitrary executable launching, shell scripting, command history persistence or a complete userspace runtime. The visual changes do not imply those capabilities exist.

