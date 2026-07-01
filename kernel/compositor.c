/* compositor.c - CharisOS compositor (Phase 6) */
#include <kernel/compositor.h>
#include <kernel/fb.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <string.h>

/* Compositor state */
surface_t* g_surface_list = NULL;
damage_list_t g_damage = {0};
int g_screen_width = 640;
int g_screen_height = 480;

/* Back buffer for double-buffering */
static u32* g_back_buffer = NULL;
static u32* g_wallpaper = NULL;

/* Animation timing */
static float g_time_accum = 0.0f;

/* Forward declarations */
static void composite_surface(surface_t* surf, int x, int y);
static void apply_material(surface_t* surf, int x, int y);
static void blur_horizontal(uint32_t* src, uint32_t* dst, int w, int h, int radius);
static void blur_vertical(uint32_t* src, uint32_t* dst, int w, int h, int radius);

/* Initialize compositor */
void compositor_init(void) {
    g_surface_list = NULL;
    g_damage.count = 0;
    g_damage.full_invalidate = true;
    
    g_screen_width = g_framebuffer.width;
    g_screen_height = g_framebuffer.height;
    
    /* Allocate back buffer */
    g_back_buffer = (u32*)kmalloc(g_screen_width * g_screen_height * 4);
    if (!g_back_buffer) {
        vga_puts("Compositor: Failed to allocate back buffer\n");
        return;
    }
    
    /* Allocate wallpaper buffer */
    g_wallpaper = (u32*)kmalloc(g_screen_width * g_screen_height * 4);
    if (!g_wallpaper) {
        vga_puts("Compositor: Failed to allocate wallpaper buffer\n");
        return;
    }
    
    /* Clear back buffer */
    memset(g_back_buffer, 0, g_screen_width * g_screen_height * 4);
    memset(g_wallpaper, 0, g_screen_width * g_screen_height * 4);
    
    vga_puts("Compositor: Initialized\n");
}

/* Frame loop - called by timer interrupt at 60Hz */
void compositor_tick(void) {
    if (g_damage.full_invalidate) {
        compositor_render_frame();
        compositor_damage_clear();
        return;
    }
    
    if (g_damage.count > 0) {
        compositor_render_frame();
        compositor_damage_clear();
    }
}

/* Create a new surface */
surface_t* compositor_create_surface(int width, int height, int x, int y) {
    surface_t* surf = (surface_t*)kmalloc(sizeof(surface_t));
    if (!surf) return NULL;
    
    surf->pixels = (u32*)kmalloc(width * height * 4);
    if (!surf->pixels) {
        kfree(surf);
        return NULL;
    }
    
    surf->id = (u32)((u64)surf & 0xFFFFFFFF);
    surf->width = width;
    surf->height = height;
    surf->screen_x = x;
    surf->screen_y = y;
    surf->material = MATERIAL_FROSTED;
    surf->opacity = 1.0f;
    surf->blur_radius = 0.0f;
    surf->tint = 0xFF1A1A2E;
    surf->tint_strength = 0.15f;
    surf->z_order = 0;
    surf->accepts_input = true;
    surf->visible = true;
    surf->dirty = true;
    surf->anim_opacity = 0.0f;
    surf->anim_scale = 0.95f;
    surf->next = NULL;
    
    /* Clear pixel buffer */
    memset(surf->pixels, 0, width * height * 4);
    
    /* Add to surface list */
    surf->next = g_surface_list;
    g_surface_list = surf;
    
    /* Mark screen dirty */
    compositor_damage_add(x, y, width, height);
    
    return surf;
}

/* Destroy a surface */
void compositor_destroy_surface(surface_t* surf) {
    if (!surf) return;
    
    /* Remove from list */
    surface_t** pp = &g_surface_list;
    while (*pp && *pp != surf) pp = &(*pp)->next;
    if (*pp) *pp = surf->next;
    
    /* Free resources */
    if (surf->pixels) kfree(surf->pixels);
    kfree(surf);
}

/* Present surface update */
void compositor_present_surface(surface_t* surf, int x, int y, int w, int h) {
    if (!surf || !surf->visible) return;
    
    surf->dirty = true;
    compositor_damage_add(surf->screen_x + x, surf->screen_y + y, w, h);
}

/* Add damage rectangle */
void compositor_damage_add(int x, int y, int w, int h) {
    if (g_damage.full_invalidate) return;
    
    if (g_damage.count >= 64) {
        g_damage.full_invalidate = true;
        return;
    }
    
    /* Clip to screen bounds */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_screen_width) w = g_screen_width - x;
    if (y + h > g_screen_height) h = g_screen_height - y;
    if (w <= 0 || h <= 0) return;
    
    g_damage.rects[g_damage.count++] = (rect_t){x, y, w, h};
}

/* Clear damage list */
void compositor_damage_clear(void) {
    g_damage.count = 0;
    g_damage.full_invalidate = false;
}

/* Render frame */
void compositor_render_frame(void) {
    /* Clear back buffer */
    memset(g_back_buffer, 0, g_screen_width * g_screen_height * 4);
    
    /* Sort surfaces by z-order (insertion sort for small n) */
    surface_t* sorted = NULL;
    surface_t* cur = g_surface_list;
    while (cur) {
        surface_t* next = cur->next;
        if (!sorted || cur->z_order < sorted->z_order) {
            cur->next = sorted;
            sorted = cur;
        } else {
            surface_t* s = sorted;
            while (s->next && s->next->z_order <= cur->z_order) s = s->next;
            cur->next = s->next;
            s->next = cur;
        }
        cur = next;
    }
    
    /* Composite each surface */
    for (cur = sorted; cur; cur = cur->next) {
        if (!cur->visible) continue;
        composite_surface(cur, cur->screen_x, cur->screen_y);
        cur->dirty = false;
    }
    
    /* Copy back buffer to framebuffer */
    memcpy(g_framebuffer.pixels, g_back_buffer, g_screen_width * g_screen_height * 4);
}

/* Composite a single surface */
static void composite_surface(surface_t* surf, int x, int y) {
    if (!surf->pixels || !surf->visible) return;
    
    int w = surf->width;
    int h = surf->height;
    
    /* Clip to screen bounds */
    int sx = 0, sy = 0;
    if (x < 0) { sx = -x; w += x; x = 0; }
    if (y < 0) { sy = -y; h += y; y = 0; }
    if (x + w > g_screen_width) w = g_screen_width - x;
    if (y + h > g_screen_height) h = g_screen_height - y;
    if (w <= 0 || h <= 0) return;
    
    /* Apply material effects */
    apply_material(surf, x, y);
    
    /* Blit surface to back buffer with alpha blending */
    for (int row = 0; row < h; row++) {
        u32* dst_row = &g_back_buffer[(y + row) * g_screen_width + x];
        u32* src_row = &surf->pixels[(sy + row) * surf->width + sx];
        
        for (int col = 0; col < w; col++) {
            u32 src_px = src_row[col];
            u32 dst_px = dst_row[col];
            
            /* Alpha blend */
            u32 src_a = (src_px >> 24) & 0xFF;
            if (src_a == 0) continue;
            if (src_a == 0xFF) {
                dst_row[col] = src_px;
            } else {
                u32 src_r = (src_px >> 16) & 0xFF;
                u32 src_g = (src_px >> 8) & 0xFF;
                u32 src_b = src_px & 0xFF;
                
                u32 dst_r = (dst_px >> 16) & 0xFF;
                u32 dst_g = (dst_px >> 8) & 0xFF;
                u32 dst_b = dst_px & 0xFF;
                
                float alpha = (float)src_a / 255.0f;
                u32 out_r = (u32)(src_r * alpha + dst_r * (1.0f - alpha));
                u32 out_g = (u32)(src_g * alpha + dst_g * (1.0f - alpha));
                u32 out_b = (u32)(src_b * alpha + dst_b * (1.0f - alpha));
                
                dst_row[col] = (0xFF << 24) | (out_r << 16) | (out_g << 8) | out_b;
            }
        }
    }
}

/* Apply material effects (blur, tint, etc.) */
static void apply_material(surface_t* surf, int x, int y) {
    /* For now, just apply tint overlay */
    if (surf->tint_strength <= 0.0f) return;
    
    int w = surf->width;
    int h = surf->height;
    
    /* Clip to screen bounds */
    if (x < 0 || y < 0 || x + w > g_screen_width || y + h > g_screen_height) return;
    
    u32 tint_r = (surf->tint >> 16) & 0xFF;
    u32 tint_g = (surf->tint >> 8) & 0xFF;
    u32 tint_b = surf->tint & 0xFF;
    float strength = surf->tint_strength;
    
    for (int row = 0; row < h; row++) {
        u32* dst_row = &g_back_buffer[(y + row) * g_screen_width + x];
        for (int col = 0; col < w; col++) {
            u32 px = dst_row[col];
            u32 r = (px >> 16) & 0xFF;
            u32 g = (px >> 8) & 0xFF;
            u32 b = px & 0xFF;
            
            r = (u32)(r * (1.0f - strength) + tint_r * strength);
            g = (u32)(g * (1.0f - strength) + tint_g * strength);
            b = (u32)(b * (1.0f - strength) + tint_b * strength);
            
            dst_row[col] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

/* Blit surface to back buffer */
void compositor_blit(surface_t* src, int dx, int dy, int w, int h, int sx, int sy) {
    if (!src || !src->pixels) return;
    
    for (int row = 0; row < h; row++) {
        u32* dst_row = &g_back_buffer[(dy + row) * g_screen_width + dx];
        u32* src_row = &src->pixels[(sy + row) * src->width + sx];
        memcpy(dst_row, src_row, w * 4);
    }
}

/* Fill rectangle */
void compositor_fill_rect(int x, int y, int w, int h, u32 color) {
    if (x < 0 || y < 0 || x + w > g_screen_width || y + h > g_screen_height) return;
    
    for (int row = 0; row < h; row++) {
        u32* dst_row = &g_back_buffer[(y + row) * g_screen_width + x];
        for (int col = 0; col < w; col++) {
            dst_row[col] = color;
        }
    }
}

/* Half-resolution blur (software fallback) */
void compositor_blur_half(uint32_t* src, uint32_t* dst, int w, int h, int radius) {
    /* Allocate temporary buffer at half resolution */
    int hw = w / 2;
    int hh = h / 2;
    uint32_t* tmp = (uint32_t*)kmalloc(hw * hh * 4);
    if (!tmp) return;
    
    /* Downscale to half resolution */
    for (int y = 0; y < hh; y++) {
        for (int x = 0; x < hw; x++) {
            int sx = x * 2;
            int sy = y * 2;
            tmp[y * hw + x] = src[sy * w + sx];
        }
    }
    
    /* Apply blur at half resolution (16x faster) */
    uint32_t* tmp2 = (uint32_t*)kmalloc(hw * hh * 4);
    if (tmp2) {
        blur_horizontal(tmp, tmp2, hw, hh, radius);
        blur_vertical(tmp2, tmp, hw, hh, radius);
        kfree(tmp2);
    }
    
    /* Upscale back to full resolution */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int hx = x / 2;
            int hy = y / 2;
            dst[y * w + x] = tmp[hy * hw + hx];
        }
    }
    
    kfree(tmp);
}

/* Horizontal blur pass */
static void blur_horizontal(uint32_t* src, uint32_t* dst, int w, int h, int radius) {
    /* Simple box blur for now (can be replaced with Gaussian) */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r = 0, g = 0, b = 0, count = 0;
            for (int k = -radius; k <= radius; k++) {
                int sx = x + k;
                if (sx < 0 || sx >= w) continue;
                uint32_t px = src[y * w + sx];
                r += (px >> 16) & 0xFF;
                g += (px >> 8) & 0xFF;
                b += px & 0xFF;
                count++;
            }
            r /= count; g /= count; b /= count;
            dst[y * w + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

/* Vertical blur pass */
static void blur_vertical(uint32_t* src, uint32_t* dst, int w, int h, int radius) {
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            int r = 0, g = 0, b = 0, count = 0;
            for (int k = -radius; k <= radius; k++) {
                int sy = y + k;
                if (sy < 0 || sy >= h) continue;
                uint32_t px = src[sy * w + x];
                r += (px >> 16) & 0xFF;
                g += (px >> 8) & 0xFF;
                b += px & 0xFF;
                count++;
            }
            r /= count; g /= count; b /= count;
            dst[y * w + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

/* Spring animation */
void spring_init(spring_t* s, float initial, float target, float stiffness, float damping) {
    s->value = initial;
    s->velocity = 0.0f;
    s->target = target;
    s->stiffness = stiffness;
    s->damping = damping;
    s->active = true;
}

bool spring_update(spring_t* s, float dt) {
    if (!s->active) return false;
    
    float displacement = s->value - s->target;
    float spring_force = -s->stiffness * displacement;
    float damper_force = -s->damping * s->velocity;
    float acceleration = spring_force + damper_force;
    
    s->velocity += acceleration * dt;
    s->value += s->velocity * dt;
    
    /* Check if settled */
    if (fabsf(s->velocity) < 0.01f && fabsf(s->value - s->target) < 0.01f) {
        s->value = s->target;
        s->velocity = 0.0f;
        s->active = false;
        return true;
    }
    
    return false;
}