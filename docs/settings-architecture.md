# CharisOS Settings Architecture

## Purpose

Settings is the user-facing control plane for the operating system. It must expose real, validated system capabilities, not a catalogue of switches that merely change labels or persist values which no service consumes.

The intended experience is a complete, searchable, keyboard-accessible settings application covering the display, desktop, devices, applications, accounts, privacy and system lifecycle. The navigation below is the product target. Each item moves through the readiness states in this document before it is allowed to look like an active control.

## Product rules

1. **A setting has an owner.** Every control maps to one kernel service, device driver, desktop service or application policy. The UI does not implement hardware behavior itself.
2. **Detect before displaying.** Pages query capabilities and device state. Unsupported hardware produces a clear unavailable state and a reason, not a working-looking toggle.
3. **Apply, verify, persist.** A change is validated, applied by its owning service, read back, and only then persisted. On failure, the old value is restored and the user sees the failure.
4. **Keep runtime state and preference state distinct.** A saved preference is not proof that a device accepted it. Pages show actual active mode and selected preference separately when they differ.
5. **Use least authority.** App permissions are explicit capabilities with revocation and audit trails. Account credentials and secrets are never stored as plain text in the general settings file.
6. **Recover safely.** Configuration needs a schema version, bounded parsing, migration, validation and an atomic/rollback-safe write path before the settings surface becomes large.
7. **No fake cloud controls.** Google, Microsoft and Apple sign-in require a complete secure identity flow, trusted TLS, callback validation, token protection and revocation. A password field that pretends to be OAuth is not an implementation.
8. **No client-side polling loops.** State changes should arrive through the kernel event path or service notifications; polling is only a bounded fallback where a device interface has no event mechanism.

## Target navigation and coverage

The Settings application will use grouped categories, a searchable page index, a page title and description, capability status, and consistent detail controls. Categories are listed here in their intended long-term form.

### Personalization

#### Appearance
- System style: dark, light and automatic when a reliable day/night policy exists.
- Accent color and a bounded set of high-contrast palettes.
- Wallpaper/background gallery, preview, fit/fill/center options, per-workspace selection and safe fallback.
- Window chrome, desktop typography and cursor appearance.
- Optional per-user appearance preferences once local user profiles exist.

#### Display
- Orientation and supported rotation modes.
- Enumerated resolutions and refresh rates, with a safe test/revert flow.
- Text scaling and global interface scaling.
- Night light with a real color-transform pipeline and schedule.
- Brightness only when a real monitor/backlight control interface is detected.
- Color profiles, calibration and per-display color state.
- Screen-edge actions and hot corners, with explicit conflicts and disable controls.
- Display arrangement, primary display and per-monitor scaling for genuinely enumerated monitors.

#### Accessibility
- Text size, UI scale, contrast and reduced-motion behavior.
- Pointer size, visibility and tracking options.
- Keyboard navigation, sticky/slow keys and shortcut discoverability.
- Magnification and a screen-reader/text-to-speech service when implemented.
- Per-application accessibility overrides where the app API supports them.

#### Wellbeing
- Screen-time summaries based on actual session/activity events.
- Per-user limits and scheduled quiet periods.
- Break reminders with configurable interval and duration.
- Clear pause/override controls and no hidden surveillance or remote reporting.

### Desktop and devices

#### Multitasking and workspaces
- Multiple named virtual workspaces.
- Window overview, task switching and application switching preferences.
- Snap zones, tiling and screen-edge behavior.
- Per-workspace wallpaper and window placement.
- Multi-monitor workspace behavior and focus movement between displays.
- Keyboard shortcuts for all core actions.

#### Mouse and touchpad
- Pointer speed, acceleration and button mapping.
- Scroll direction and speed.
- Tap-to-click, natural scrolling, palm rejection and gestures only when a touchpad driver reports support.
- Device-specific settings for each detected pointer.
- Pointer location and size options for accessibility.

#### Keyboard
- Input sources/layouts and explicit source switching.
- Modifier and shortcut configuration with conflict detection.
- Special-character entry and compose/dead-key support.
- Repeat delay/rate and input behavior.
- Per-device layout and hot-plug handling once the input stack supports it.

#### Sound
- Output and input device inventory and selection.
- Master, application and per-device volume.
- Mute, balance and alert/notification sound policy.
- Microphone access and input level where a real capture device exists.
- Audio format, sample-rate and latency information when exposed by the audio service.

#### Power
- Power mode/performance profile.
- Screen dim and display-off timers.
- Idle and suspend behavior.
- Battery health, charge state and power-source information when ACPI/device telemetry exists.
- Lid and power-button actions on supported hardware.
- Shutdown, restart and suspend states must reflect actual hardware/firmware behavior. Hibernate is hidden until a recoverable image and restore path exist.

### Applications and services

#### Apps
- Installed application inventory and per-app information.
- Default applications and protocol/file-type handlers.
- Startup behavior and background permissions when lifecycle controls exist.
- Per-app resource and capability permissions.
- Uninstall/update actions only when a signed package and transaction system exists.

#### Notifications
- Per-application notification permission and delivery policy.
- Quiet hours, priority and notification history.
- Per-category sound/banner/badge controls when the notification service exists.
- No notification switch is shown as active before applications can submit notifications through the service.

#### Search
- Search providers and result scopes.
- Applications, settings and local files as separate sources.
- Opt-in online results only when a privacy-preserving network provider exists.
- Per-source enable/disable, safe result actions and clear query/history controls.

#### Accounts
- Local users and sign-in state.
- Online account providers: Google, Microsoft and Apple, each with a genuine standards-based authorization flow.
- Token storage, expiry, disconnect and revoke operations.
- No provider is displayed as connectable until its end-to-end flow and secure token store are implemented.

#### Sharing
- Local file and media sharing with explicit source, recipient and permission controls.
- Share links with expiry and revocation only when a secure transfer service exists.
- Network discovery and transfer protocol support must be explicit; no implicit public sharing.

### System

#### Privacy and security
- Per-application access to files, network and supported devices.
- Device privacy indicators and permission history where those device classes exist.
- Firewall/network exposure policy when an enforceable network filtering service exists.
- Secure boot/system-integrity status, update trust and recovery status.
- Lock screen/session security and credential management when implemented.
- Audit and diagnostic data controls with clear retention rules.

#### Region and language
- Region, locale, date/number formats and measurement units.
- Language selection and translation packs.
- Keyboard input sources remain a separate, discoverable control but are reachable from this page.

#### Date and time
- Clock display, time zone and calendar preferences.
- Manual clock adjustment with validation.
- Network time synchronization only after a trusted NTP/service implementation exists.
- Firmware/RTC time, timezone conversion and synchronized wall-clock state must remain clearly distinguished.

#### Users
- Local user creation, removal and profile management.
- Administrator privileges and session policies.
- Password/credential changes and session locking only when the authentication subsystem supports them.
- Per-user home, settings and permissions.

#### Remote access
- Remote desktop with authenticated sessions, explicit inbound firewall policy and session visibility.
- Secure Shell server/client only after a maintained SSH implementation, host-key management, user authentication and service isolation exist.
- Both features default off and are absent as active controls until security review and tests pass.

#### About and software updates
- OS version, build, kernel, architecture and hardware inventory.
- Driver/device information and diagnostic export.
- Signed software updates, integrity verification, staged installation, rollback and recovery status.
- Update channels and scheduling only after a package/update transaction system exists.

## Current implementation reality

This section is deliberately strict. The repository contains useful foundations, but many user-facing services are not ready.

| Area | Current evidence | Status |
|---|---|---|
| Display modes | Bootloader-provided framebuffer; no mode enumeration, EDID pipeline, refresh-rate control or rotation service | Needs display driver/compositor work |
| Multi-monitor | A four-entry monitor data structure exists, but initialization uses a fixed fallback and there is no real monitor discovery/arrangement pipeline | Placeholder, not multi-monitor support |
| Font and UI scale | Font family, text scale and UI scale are persisted; QEMU verifies large-font Settings reflow and persistence | Implemented foundation |
| Appearance and wallpaper | Theme/wallpaper preference keys exist; build supplies a validated CWP1 Multiboot2 RGB565 wallpaper module | Partial; gallery and switching need multiple assets and UI |
| Night light/color profiles | No display color-transform or calibration service identified | Not implemented |
| Sound | PC-speaker beep path; HDA discovery/initialization is incomplete; general PCM playback is not supported | Partial driver foundation, no general sound settings |
| Power | CPU idle uses HLT; power state API reports states; hibernate is explicitly not implemented and full ACPI suspend/resume is absent | Partial |
| Multitasking | Real window manager supports bounded windows, focus, movement, resize, minimize, maximize/restore, taskbar and Alt+Tab | Implemented foundation; no virtual workspaces, overview or snap/tiling |
| Mouse/touchpad | PS/2 mouse and cursor input work; no touchpad/device settings service | Partial |
| Keyboard | PS/2 Set-1 scancode input and a fixed character map; no input-source/layout manager or special-character UI | Partial |
| Accessibility | Text/UI scale options exist | Partial; other services absent |
| Notifications/search/default apps | No full notification center, provider-based search or default-handler registry identified | Not implemented |
| Accounts/sharing/wellbeing | No online identity providers, local sharing service or wellbeing policy engine identified | Not implemented |
| Privacy/security | Capability framework and selected enforcement exist, but overall capability enforcement and isolation remain incomplete | Partial; not a complete security boundary |
| Region/time | CMOS RTC supplies firmware time; timezone conversion and trusted network time are not implemented | Partial |
| Users/remote access/updates | No complete user manager, remote desktop/SSH service or signed update transaction system identified | Not implemented |
| About | Native About window exists | Implemented foundation |

The UI must not expose unsupported settings as enabled. For a device-dependent category, it should show the detected device, available capabilities and a concise reason a control is unavailable.

## Engineering sequence

### S0 — Settings platform
- Replace the three-item mock navigation with a real category/page model.
- Add a settings registry with stable IDs, type, range/enum, default, owning service, capability requirement and persistence policy.
- Add a capability query path so pages render supported controls only.
- Add a central apply/read-back/persist/rollback transaction.
- Version and migrate configuration; validate keys and values and make writes recoverable.
- Add search within Settings, keyboard navigation, focus, page history and clear unavailable states.
- Add host tests for schema validation, migration, invalid values, apply failures and rollback.

### S1 — Real desktop preferences
- Appearance styles and accent colors backed by the shared UI palette.
- Multiple bundled CC0 wallpapers, preview, selection and fallback.
- Desktop/session switching controls, workspace primitives, app switching and screen-edge actions.
- Accessibility controls that can be fully honored by the current renderer and input stack.
- QEMU screenshots at each supported font/UI scale and framebuffer size.

### S2 — Input, display and device services
- Hardware capability discovery and a truthful display-mode inventory.
- Display mode test/revert, orientation, monitor arrangement and multi-monitor composition only with a working display backend.
- Keyboard layout/input-source service, special-character entry and configurable shortcut registry.
- Pointer configuration and touchpad settings only for devices with working drivers.
- Complete HDA/PCM device enumeration, playback/capture routing and per-app volume before shipping sound controls.
- ACPI power-source/battery reporting and safe suspend/resume before shipping those controls.

### S3 — Application and desktop services
- Application registry, default handlers and capability manifests.
- Notification broker, per-app permissions, quiet hours and history.
- Search-provider registry with explicit local/online scopes.
- File/media sharing service with authentication, expiry and revoke.
- Wellbeing scheduler and per-user session accounting.

### S4 — Identity, security and system lifecycle
- Local user/profile and credential management.
- Privacy dashboard and per-app/device permission enforcement.
- Secure provider authorization and token vault for supported online accounts.
- Region/locale and timezone-aware wall clock with trusted time synchronization.
- Remote desktop and SSH only after authentication, isolation, audit and network policy are independently tested.
- Signed updates, atomic activation, rollback and recovery.

## Acceptance criteria

A Settings feature is done only when:
- The control is visible only when its capability is available.
- The selected value is validated and owned by a real service.
- Apply/read-back/failure/rollback behavior is tested.
- Preferences survive reboot when persistent storage is present, and the UI clearly identifies session-only state otherwise.
- Keyboard and pointer interaction both work at supported scales.
- Host regression tests and QEMU integration tests cover the change.
- README and TODO describe the tested behavior accurately.
