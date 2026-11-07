/* ui_theme.h - Calm Precision shared palette.
 *
 * Opaque ARGB colours. Keep desktop, launcher, windows and setup screens on
 * these tokens rather than introducing local one-off shades.
 */
#pragma once

#define CHARIS_UI_BACKGROUND        0xFF0F1216u
#define CHARIS_UI_SHADOW            0xFF080B10u
#define CHARIS_UI_PANEL             0xFF171B21u
#define CHARIS_UI_PANEL_RAISED      0xFF1F252Du
#define CHARIS_UI_SURFACE           0xFF29313Bu
#define CHARIS_UI_SURFACE_RAISED    0xFF29313Bu
#define CHARIS_UI_BORDER            0xFF2E3742u
#define CHARIS_UI_BORDER_STRONG     0xFF435161u
#define CHARIS_UI_TEXT              0xFFE8ECF1u
#define CHARIS_UI_MUTED             0xFFA3ADBAu
#define CHARIS_UI_ACCENT            0xFF5E9BD6u
#define CHARIS_UI_ACCENT_SURFACE    0xFF243B50u
#define CHARIS_UI_WARNING           0xFFD9A441u
#define CHARIS_UI_SUCCESS           0xFF5FB37Cu
#define CHARIS_UI_DANGER            0xFFE0625Au

/* Light theme tokens are reserved now so every surface can switch together
 * when theme selection is wired into the settings service. */
#define CHARIS_UI_LIGHT_BACKGROUND  0xFFE9EDF1u
#define CHARIS_UI_LIGHT_PANEL       0xFFF6F8FAu
#define CHARIS_UI_LIGHT_SURFACE     0xFFFFFFFFu
#define CHARIS_UI_LIGHT_RAISED      0xFFE3E8EDu
#define CHARIS_UI_LIGHT_BORDER      0xFFD2D8DFu
#define CHARIS_UI_LIGHT_TEXT        0xFF14181Du
#define CHARIS_UI_LIGHT_MUTED       0xFF4A5563u
#define CHARIS_UI_LIGHT_ACCENT      0xFF2F6DA8u
