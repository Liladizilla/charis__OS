# CharisOS: Progress and Problems Report

Date: 5 October 2026
Repo: https://github.com/Liladizilla/charis__OS (branch `main`, commit `858a84f`, 1 October 2026)

How to read the tags:
- **[verified]** I built it, booted it in QEMU, or read the code myself in this pass.
- **[repo docs]** Taken from your PROGRESS.md / TODO.md. I did not test it.
- **[earlier audit]** From your July audit notes. I did not re-check it today.

---

## 1. Snapshot

| Item | Value |
|---|---|
| Remote history | 1 commit on `main`, so there is no history to bisect a regression against |
| Source size | about 14,300 lines of C and NASM (60 kernel files; 2,000 of those lines are the font table) |
| Kernel binary | 153 KB (`kernel.elf`) |
| ISO | 5.3 MB (`charisos.iso`, mostly GRUB) |
| Build | Succeeds [verified]. Roughly 25 compiler warnings across about 14 files |
| Boot | GRUB, then 1024x768 framebuffer, then first-run setup wizard, then desktop [verified] |
| CI | GitHub Action builds the images and compiles the VMM self-tests. It does not boot the OS |

Size note: the ISO being 5 MB is normal for a young kernel. The "college project" feeling does not come from size. It comes from three gaps: no persistent writable storage, no real user programs, and an empty desktop.

---

## 2. What works [verified]

- Boots through GRUB into 64-bit long mode, maps the framebuffer, no kernel faults in the serial log.
- Memory: physical and virtual memory managers, heap.
- Interrupts, PIC, timer, keyboard.
- Graphics: framebuffer, bitmap font, rectangles, text, gradient wallpaper.
- First-run setup wizard (welcome, run-or-install, and more steps), driven fully by keyboard.
- Desktop shell: taskbar, start button, start menu with 5 entries (Terminal, File Manager, Text Editor, Settings, About), uptime clock.
- With the pending fixes (section 4): mouse movement and clicks work, clicking the start button opens the menu.

---

## 3. Problems found

### 3.1 Bugs fixed on my branch (not on your GitHub yet)

Branch `fix/mouse-cascade-irq`, commit `5f759ad`, delivered as a patch and a bundle. **Nothing has been pushed to your repo.**

1. **Mouse never delivered events.** The slave PIC connects through IRQ 2 on the master, and IRQ 2 was never unmasked. The keyboard (IRQ 1) worked, the mouse (IRQ 12) was cut off. `kernel/irq.c`.
2. **Mouse was never switched on.** Enable commands were sent to the keyboard port instead of the mouse. They need the 0xD4 prefix. `kernel/mouse.c` init rewritten with proper polling.
3. **Downward mouse movement was discarded.** Bit 5 (Y sign) was treated as an overflow flag. Overflow is bits 6 and 7.
4. **Cursor left a trail of arrows** in the wizard. Added save-under cursor and a backdrop repaint.
5. **Clock showed `00:0020u`.** Now `HH:MM:SS`.

### 3.2 Documentation does not match the code

- PROGRESS.md marks Phases 8 to 12 "Complete" [repo docs]. TODO.md lists **34 unchecked items** [verified count]. Both files describe the same project and disagree.
- The built-in apps are marked complete in PROGRESS.md. TODO.md says they only create windows with no function [repo docs]. In the OS I only saw them as start menu labels.
- `usb.c` is 28 lines and described in PROGRESS.md as an "enumeration placeholder" [verified].
- The wizard itself says "Install is not implemented" and "DHCP is not implemented yet" [verified in `wizard.c`]. Yet it offers "Install to disk" as a choice.
- `SECURITY.md` is still the GitHub template text, and the supported version table says 1.0.0 [verified].

### 3.3 Missing or stubbed features [verified in code, or TODO.md]

- No real storage story: ATA is read-only, no identify, FAT32 is read-only. This is the real meaning of "needs real space": the OS cannot save anything.
- Installer does not exist (see wizard text above).
- `fork()` is explicitly not implemented. `wait()/waitpid()` missing, so no zombie reaping.
- Network: `net_handle_tcpip` is a stub, RX returns 0, no ARP/ICMP/DHCP.
- Compositor effects (frosted and acrylic materials) are stubs.
- Desktop icons are "colored square placeholders" [`desktop.c` comment].
- Wallpaper is one gradient style. Settings do not persist.
- Game SDK headers exist, no game exists.
- Window manager noted as untested beyond the 32-task limit.

### 3.4 Build and code quality [verified]

- About 25 warnings, including: unused parameters in the ELF loader (`argv`, `argc`, `envp` are ignored, so programs cannot receive arguments), `-Wparentheses` in `fb_init` (`!width || !height || bpp != 16 && ...`), comparisons that can never be true in `fs.c` and `psf.c`, unused variables in `graphics.c` and `pci.c`, dead code in `demo.c` and `apps.c` (the app tables are defined but unused), and a `font_data.c` initializer missing braces.
- No `-Werror`, so new warnings pile up silently.
- Windows scripts (`build.bat`, `run.bat`) show line-ending noise in git. Add a `.gitattributes` with `*.bat text eol=crlf` and `*.sh text eol=lf`.
- CI installs `libvirt-daemon-system`, which it does not need, and never boots the ISO, so a boot regression like the mouse bug would pass CI.

### 3.5 Security [earlier audit, not re-checked today]

- Capability enforcement is a no-op stub.
- Security tokens use predictable XOR patterns.
- Path security is a two-string blocklist.
- Stack protector exists but uses a fixed canary constant.

These are fine for a hobby kernel today. They matter before you ship to "all users".

---

## 4. Test coverage

- Added `tools/smoke_test.sh` on my branch. It boots the ISO headless, completes the wizard by keyboard, moves and clicks the mouse, saves screenshots, and fails on kernel faults. All 4 checks pass.
- Not tested: real hardware, KVM, UEFI boot, USB mice, long uptime, memory pressure, rapid input, opening and closing apps repeatedly. The "stretch test on all sides" you asked for does not exist yet.

---

## 5. Recommended order of work

1. **Push the fixes.** Apply the patch, push the branch, open a PR, merge when the smoke test passes on your machine.
2. **Make CI boot the OS.** Run `tools/smoke_test.sh` in GitHub Actions and upload the screenshots as artifacts.
3. **Stop the docs lying.** Merge PROGRESS.md and TODO.md into one honest status file with a single source of truth.
4. **Real storage.** Writable ATA, FAT32 write, a 512 MB+ disk image in the Makefile, and a working installer. Nothing else gives you "real space".
5. **Stability harness.** Soak test, input storms, app open/close loops, memory leak counters in the serial log, run under 64 MB and 2 CPUs.
6. **Desktop.** Icons, widgets, several wallpapers, window chrome, then more apps. Make the five existing apps functional before adding new ones.
7. **Warnings to zero, then `-Werror`.**
8. **Security hardening** (real entropy, real capability checks) before public release.
9. **Download website** from `CHARISOS_WEBSITE_PROMPT.md`, once a tagged release exists.

---

## 6. Not reviewed in this pass

README.md, `docs/ARCHITECTURE.md`, the bootloader assembly (`boot/boot.asm`), the network driver internals, HDA audio, the IL runtime, and every app's behaviour beyond what the start menu shows.
