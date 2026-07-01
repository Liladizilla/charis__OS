/* compositor.h - CharisOS compositor */
#pragma once
#include <kernel/types.h>
#include <kernel/fb.h>
#include <stdint.h>

/* Surface material types */
typedef enum {
    MATERIAL_FROSTED,    /* Default: blur + tint */
    MATERIAL_CRYSTAL,    /* Dialogs: higher opacity, sharper */
    MATERIAL_LIQUID,     /* Dock/taskbar: very transparent */
    MATERIAL_SOLID,      /* No effects, flat color */
    MATERIAL_ACRYLIC,    /* Menus: fast, small blur */
} material_type_t;

/* Surface structure */
typedef struct surface {
    u32 id;
    u32* pixels;          /* Client-area pixel buffer (ARGB32) */
    int width, height;    /* Pixel dimensions */
    int screen_x, screen_y; /* Position on screen */
    
    /* Material system */
    material_type_t material;
    float opacity;        /* 0.0 = invisible, 1.0 = opaque */
    float blur_radius;    /* Pixels of blur */
    u32 tint;             /* Color tint (ARGB) */
    float tint_strength;  /* 0.0 = no tint, 1.0 = solid tint */
    
    /* Z-order and input */
    int z_order;
    bool accepts_input;
    bool visible;
    bool dirty;           /* Needs re-composite */
    
    /* Animation state */
    float anim_opacity;   /* Current animated opacity */
    float anim_scale;     /* Current animated scale */
    
    struct surface* next;
} surface_t;

/* Damage tracking */
typedef struct {
    int x, y, w, h;
} rect_t;

typedef struct {
    rect_t rects[64];
    int count;
    bool full_invalidate;
} damage_list_t;

/* Compositor state */
extern surface_t* g_surface_list;
extern damage_list_t g_damage;
extern int g_screen_width;
extern int g_screen_height;

/* Initialize compositor */
void compositor_init(void);

/* Frame loop - called by timer interrupt */
void compositor_tick(void);

/* Surface management */
surface_t* compositor_create_surface(int width, int height, int x, int y);
void compositor_destroy_surface(surface_t* surf);
void compositor_present_surface(surface_t* surf, int x, int y, int w, int h);

/* Damage tracking */
void compositor_damage_add(int x, int y, int w, int h);
void compositor_damage_clear(void);

/* Rendering */
void compositor_render_frame(void);
void compositor_blit(surface_t* src, int dx, int dy, int w, int h, int sx, int sy);
void compositor_fill_rect(int x, int y, int w, int h, u32 color);

/* Software blur (for Tier 1/2) */
void compositor_blur_half(uint32_t* src, uint32_t* dst, int w, int h, int radius);

/* Spring animation */
typedef struct {
    float value;
    float velocity;
    float target;
    float stiffness;
    float damping;
    bool active;
} spring_t;

void spring_init(spring_t* s, float initial, float target, float stiffness, float damping);
bool spring_update(spring_t* s, float dt);

#endif