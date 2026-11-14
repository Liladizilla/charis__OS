# CharisOS boot identity, display and application platform

## Purpose

This document defines the next platform milestone. It is an implementation contract and status boundary, not a claim that the current kernel already provides features that are still absent.

CharisOS targets a broad global audience: everyday desktop users, gamers and streamers, software developers, creators, students, small businesses and enterprise deployments. The kernel and core OS remain implemented in C and NASM Assembly. SDKs may expose stable language bindings later, but they must call a documented native ABI rather than adding unrelated languages to the kernel.

## 1. Brand identity and boot experience

The official mark is a custom calligraphic capital C pierced vertically by a sword descending from above. The letter and sword must read as one emblem, remain recognizable in monochrome and at small sizes, and have a clean transparent/black-background variant for firmware and boot display.

The same identity is used by the firmware-facing boot splash, installer, first-run experience, login, recovery environment, About panel and official documentation. The boot renderer must not depend on the desktop compositor or user-space services.

### Boot state machine

1. Firmware handoff: accept the bootloader-provided framebuffer and record the selected display mode.
2. Identity splash: render the CharisOS emblem centered with a restrained progress indicator.
3. Kernel initialization: report truthful milestones for CPU, memory map, physical/virtual memory, interrupt controllers, storage discovery, input, display and security initialization.
4. Service startup: start only services required for the selected boot target; report actual service states.
5. Session handoff: show login or the configured automatic session, then start the desktop.
6. Failure/recovery: display a readable error code, preserve serial diagnostics and offer recovery where the boot target supports it.

The boot sequence must never simulate file installation during normal startup. A fast boot is a feature, not a UX defect. A short minimum splash duration may be used only to avoid a one-frame flash, not to fabricate work.

### Installer state machine

The installer is a separate, explicit workflow and must not be conflated with kernel boot:

1. Select language, keyboard and accessibility options.
2. Detect hardware, memory, storage and available installation targets.
3. Explain the selected disk and destructive operations; require explicit confirmation before partition or format changes.
4. Create/validate partitions and filesystems using real storage drivers.
5. Install the base system and verify each written component.
6. Install/configure the bootloader and create a recovery entry.
7. Create the first user, security credentials and initial device configuration.
8. Verify the installation and reboot into first-boot setup.

Every progress item is driven by a completed operation or measured byte/step count. Failed operations stop the workflow, show the real error and leave recoverable state. No pretend package names, fake file-copy logs or arbitrary 0–100% animations.

First-boot setup handles user profile, locale/time zone, network, accessibility, display, input, privacy and optional data migration. It persists choices transactionally and can resume after interruption.

## 2. Display and orientation architecture

### Principles

- Discover the modes the firmware or native display driver actually supports; never advertise a mode merely because a settings menu lists it.
- Prefer a panel's native mode when reliable EDID data is available. Otherwise choose a safe supported fallback, preserving aspect ratio where possible.
- Keep framebuffer pitch/stride, pixel format, physical address, width and height distinct. Never assume pitch equals width.
- Support portrait, landscape, square, ultrawide and custom orientations as explicit display configurations. Orientation and resolution are separate concepts.
- A mode change is transactional: validate the candidate, allocate/prepare resources, switch, verify and commit; revert on failure.
- Keep a known-good mode and a timed confirmation/revert path so an unsupported mode cannot strand the user at a blank screen.

### Orientation profiles

- Automatic: follow the panel's native orientation and rotation metadata where available.
- Landscape: horizontal desktop and conventional monitor layouts.
- Portrait: vertical monitors, reading, coding and streaming side displays.
- Square: square panels and kiosk/creative workflows.
- Ultrawide: wide and super-ultrawide layouts.
- Custom: user-defined rotation and a resolution selected only from supported hardware modes.

Examples for UI testing include 1024x768 safe fallback, 1280x720, 1920x1080, 2560x1440, 3840x2160, 1080x1920 portrait, 720x1280 portrait and square 1080x1080. 1024x720 can be a custom test target only when the actual mode is exposed by the device. These examples are test fixtures, not a promise that every monitor or GPU supports them.

### Implementation stages

1. Correct the current display abstraction: it must report the real primary framebuffer, not invent a 640x480 VGA monitor or a pointer at 0xA0000.
2. Preserve the bootloader's actual framebuffer geometry, pitch and pixel layout; enumerate bootloader-provided modes when the boot protocol permits it.
3. Add validated EDID parsing and monitor identity/capability discovery.
4. Implement the display driver boundary and real mode-setting backend for supported hardware. Until then, Settings may expose only the active mode and supported choices from the bootloader.
5. Add rotation/compositor transforms, per-monitor scaling, workspace placement and multi-monitor topology after single-display correctness is established.
6. Add refresh-rate selection only when timing data and a driver can actually set and verify it. Do not infer a refresh rate from resolution.

Current limitation: the GRUB configuration requests 1024x768x32. The existing kernel consumes a Multiboot2 framebuffer handoff, but the display.c monitor list is a placeholder and there is no complete native GPU/display mode-setting stack. Therefore orientation, arbitrary resolution switching, refresh-rate switching and multi-monitor rendering are not yet complete features.

## 3. Native application and package formats

### Native executable: CXE

The proposed native executable container uses the `.cxe` extension and a versioned, endian-defined, fixed-width header. It is not an ELF rename. The format must support:

- Magic, format version, architecture and ABI version.
- Entry point and bounded section/segment table.
- Read-only code, read-only data, writable data and zero-initialized memory declarations.
- Required alignment and explicit memory/stack limits.
- Relocation metadata only for supported relocation types.
- Import/export table with versioned symbol names.
- Declared application capabilities and requested resources.
- Optional debug/symbol metadata kept outside the privileged load path.
- Integrity digest and signature metadata, with trust policy enforced by the package/loader layer.

The loader validates every offset, size, alignment, overlap, permission and arithmetic operation before mapping pages. Writable+executable mappings are denied by default. Unknown versions, architectures, flags, relocation types and capability bits fail closed. A malformed executable must not partially replace the running process.

Existing ELF64 support remains a development and migration path while CXE is designed and implemented. Do not remove working ELF tests until CXE has equivalent loader, rollback, permission and execution coverage.

### Application package: CAPP

The proposed `.capp` package is a signed, versioned application bundle containing one or more CXE binaries, assets, icons, localization, dependency declarations, update metadata and a manifest. It is installed transactionally into an application store. Installation verifies the signature, dependency graph, available space and requested permissions before modifying system state.

Package permissions are declarative requests, not automatic grants. The user or administrator can approve, deny, inspect and revoke capabilities. Updates stage into a new version, verify, then atomically switch the active version; rollback retains the previous known-good version until health checks pass.

Do not execute code directly from an untrusted archive. The package parser must reject path traversal, duplicate normalized paths, symlinks escaping the package root, decompression bombs, integer overflow and undeclared payloads.

### Compatibility

- CXE/CAPP are the native target and the long-term application contract.
- ELF64 is supported during migration and for low-level development tools, subject to the same permission policy.
- Compatibility layers for other operating systems are separate, optional projects; they must not expand the trusted kernel by default.
- Source projects and SDK examples should remain reproducible and buildable offline where dependencies permit.

## 4. CharisOS SDK family

One stable core SDK provides the system ABI, CXE toolchain integration, package manifest validation, capability declarations, event/input APIs, filesystem access, diagnostics, localization, accessibility, UI primitives and test harnesses. Domain SDKs build on that core instead of inventing incompatible platform APIs.

### Game and streaming SDK

- Window/surface lifecycle, input, controller/gamepad, audio, timer and high-resolution clock APIs.
- Graphics abstraction with capability queries and a staged path from software rendering to accelerated backends.
- Asset streaming, resource budgets, frame pacing, telemetry and crash reporting.
- Capture/streaming interfaces with explicit screen/audio consent and revocable permissions.
- Deterministic test harnesses and profiling tools.

The SDK must not promise hardware acceleration, low latency or anti-cheat guarantees until the relevant kernel drivers and isolation boundaries exist.

### Developer SDK

- Native C ABI and headers, linker/loader integration, CXE build and inspection tools.
- Debug symbols, debugger protocol, tracing, crash dumps, profiling and reproducible builds.
- Package creation/signing, capability inspection, test VM and API compatibility checks.
- Documentation, templates and stable versioned APIs.

### Business and enterprise SDK

- Structured data, database/client integration, document and report surfaces, notifications and workflow APIs.
- Identity integration, role-based policy mapping, audit events, managed configuration and deployment channels.
- Signed internal package distribution, staged updates, backup/restore and administrative diagnostics.
- Clear data ownership and export formats; no mandatory cloud account for local applications.

### AI and automation SDK

- Provider-neutral model interface with local-first execution and explicit remote-provider consent.
- Permission-scoped tool access, auditable actions, resource budgets and cancellation.
- Model/runtime discovery and capability reporting; no privileged kernel execution for AI-generated code.
- Offline behavior and deterministic fallback when no model/provider is configured.

SDKs are a platform roadmap, not four disconnected codebases. All share the core ABI, package model, capability system, documentation and conformance tests.

## 5. Delivery gates

A feature is complete only when all applicable gates pass:

1. Spec and threat model reviewed.
2. C/NASM implementation compiles with warnings as errors.
3. Host tests cover malformed and boundary inputs.
4. QEMU tests exercise success, failure and rollback paths.
5. Bootable ISO and documented manual test are available.
6. Settings and diagnostics report real system state.
7. Documentation and screenshots match the shipped build.
8. No unimplemented feature is presented as working.

## 6. Ordered milestone plan

### M0 — Complete current CWP2 wallpaper work
Finish CI, inspect QEMU captures, correct any regressions, merge only after required checks pass.

### M1 — Boot identity and truthful startup
Replace the text-art banner with the official C+sword mark; add a framebuffer splash state machine, boot-stage diagnostics, clean handoff and failure display. Preserve serial logs and text-only fallback.

### M2 — Installer and first-boot separation
Design an installer state machine and storage safety contract. First implement detection and dry-run validation; only add destructive partition/format actions after storage drivers, rollback and explicit confirmation are proven.

### M3 — Real display foundation
Remove placeholder monitor state, define a single-source-of-truth display object, preserve pitch/pixel format, add supported-mode discovery and safe fallback. Then implement orientation transforms and real mode setting.

### M4 — Native executable specification and loader
Freeze CXE v1 header/segment/relocation/permission rules, write a parser and fuzzable host validator, then implement a transactional loader. Keep ELF regression coverage.

### M5 — CAPP package manager
Define manifest, signature, dependency, permission, atomic install/update and rollback behavior. Implement parser and host tests before a privileged installer.

### M6 — Core SDK and developer tools
Ship stable headers, toolchain integration, debugger/diagnostics, sample apps, documentation and compatibility tests.

### M7 — Domain SDKs
Deliver game/streaming, business/enterprise and AI/automation SDKs on the same core ABI. Validate real hardware dependencies before advertising acceleration or enterprise guarantees.

### M8 — Global hardware and release readiness
Expand graphics, storage, USB, networking, audio, input, power management, localization, accessibility, security review and reproducible hardware test coverage.
