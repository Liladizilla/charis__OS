#include <kernel/raster.h>
#include <kernel/memory.h>
#include <kernel/graphics.h>

static raster_vertex_t vertices[RASTER_MAX_VERTICES];
static raster_triangle_t triangles[RASTER_MAX_TRIANGLES];
static int vertex_count = 0;
static int triangle_count = 0;

/* Fixed-point multiplication */
static fx16_t fx16_mul(fx16_t a, fx16_t b) {
    return (fx16_t)(((s64)a * b) >> 16);
}

/* Fixed-point division */
static fx16_t fx16_div(fx16_t a, fx16_t b) {
    return (fx16_t)(((s64)a << 16) / b);
}

/* Barycentric coordinate calculation using fixed-point */
static bool raster_barycentric(int x, int y, int x0, int y0, int x1, int y1, int x2, int y2, fx16_t* out_u, fx16_t* out_v, fx16_t* out_w) {
    int denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (denom == 0) return false;
    
    int num_u = (y1 - y2) * (x - x2) + (x2 - x1) * (y - y2);
    int num_v = (y2 - y0) * (x - x2) + (x0 - x2) * (y - y2);
    
    fx16_t u = fx16_div(FX16_FROM_INT(num_u), FX16_FROM_INT(denom));
    fx16_t v = fx16_div(FX16_FROM_INT(num_v), FX16_FROM_INT(denom));
    fx16_t w = FX16_ONE - u - v;
    
    if (u < 0 || v < 0 || w < 0) return false;
    
    *out_u = u;
    *out_v = v;
    *out_w = w;
    return true;
}

void raster_init(raster_state_t* state, u32 width, u32 height, u32* framebuffer) {
    state->width = width;
    state->height = height;
    state->framebuffer = framebuffer;
    state->zbuffer = NULL;
    state->texture = NULL;
    state->tex_width = 0;
    state->tex_height = 0;
    
    /* Allocate depth buffer only if framebuffer is small enough */
    usize fb_bytes = width * height * sizeof(fx16_t);
    if (fb_bytes <= 480 * 640 * sizeof(fx16_t) && width > 0 && height > 0) {
        state->zbuffer = (fx16_t*)kmalloc(fb_bytes);
        if (state->zbuffer) {
            for (u32 i = 0; i < width * height; i++) {
                state->zbuffer[i] = FX16_ONE; /* Far plane in NDC */
            }
        }
    }
}

void raster_set_viewport(raster_state_t* state, u32 width, u32 height) {
    state->width = width;
    state->height = height;
}

void raster_set_mvp(raster_state_t* state, const raster_matrix_t* mvp) {
    state->mvp = *mvp;
}

void raster_load_vertices(raster_vertex_t* verts, int count) {
    vertex_count = count < RASTER_MAX_VERTICES ? count : RASTER_MAX_VERTICES;
    for (int i = 0; i < vertex_count; i++) {
        vertices[i] = verts[i];
    }
}

void raster_load_triangles(raster_triangle_t* tris, int count) {
    triangle_count = count < RASTER_MAX_TRIANGLES ? count : RASTER_MAX_TRIANGLES;
    for (int i = 0; i < triangle_count; i++) {
        triangles[i] = tris[i];
    }
}

/* Transform vertex by MVP matrix with perspective division */
static void raster_transform_vertex(raster_state_t* state, const raster_vertex_t* in, int32_t* out_x, int32_t* out_y, fx16_t* out_z) {
    const fx16_t* m = state->mvp.m;
    
    fx16_t clip_x = fx16_mul(in->x, m[0]) + fx16_mul(in->y, m[4]) + fx16_mul(in->z, m[8]) + fx16_mul(in->w, m[12]);
    fx16_t clip_y = fx16_mul(in->x, m[1]) + fx16_mul(in->y, m[5]) + fx16_mul(in->z, m[9]) + fx16_mul(in->w, m[13]);
    fx16_t clip_z = fx16_mul(in->x, m[2]) + fx16_mul(in->y, m[6]) + fx16_mul(in->z, m[10]) + fx16_mul(in->w, m[14]);
    fx16_t clip_w = fx16_mul(in->x, m[3]) + fx16_mul(in->y, m[7]) + fx16_mul(in->z, m[11]) + fx16_mul(in->w, m[15]);
    
    /* Perspective division */
    fx16_t ndc_x = fx16_div(clip_x, clip_w);
    fx16_t ndc_y = fx16_div(clip_y, clip_w);
    fx16_t ndc_z = fx16_div(clip_z, clip_w);
    
    /* Viewport transform */
    *out_x = (FX16_TO_INT(ndc_x) + 1) * ((int32_t)state->width / 2);
    *out_y = (1 - FX16_TO_INT(ndc_y)) * ((int32_t)state->height / 2);
    *out_z = ndc_z;
}

void raster_draw_mesh(raster_state_t* state) {
    if (!state->framebuffer) return;
    
    for (int t = 0; t < triangle_count; t++) {
        raster_triangle_t* tri = &triangles[t];
        const raster_vertex_t* v0 = &vertices[tri->v1];
        const raster_vertex_t* v1 = &vertices[tri->v2];
        const raster_vertex_t* v2 = &vertices[tri->v3];
        
        int32_t x0, y0, x1, y1, x2, y2;
        fx16_t z0, z1, z2;
        
        raster_transform_vertex(state, v0, &x0, &y0, &z0);
        raster_transform_vertex(state, v1, &x1, &y1, &z1);
        raster_transform_vertex(state, v2, &x2, &y2, &z2);
        
        /* Compute bounding box */
        int min_x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        int max_x = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        int min_y = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
        int max_y = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
        
        /* Clamp to screen */
        if (min_x < 0) min_x = 0;
        if (max_x >= (int)state->width) max_x = state->width - 1;
        if (min_y < 0) min_y = 0;
        if (max_y >= (int)state->height) max_y = state->height - 1;
        
        /* Rasterize */
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                fx16_t u, v, w;
                if (!raster_barycentric(x, y, x0, y0, x1, y1, x2, y2, &u, &v, &w)) continue;
                
                int idx = y * state->width + x;
                
                /* Depth test (skip if no zbuffer) */
                if (state->zbuffer) {
                    fx16_t z = fx16_mul(u, z0) + fx16_mul(v, z1) + fx16_mul(w, z2);
                    /* NDC z: -1 is near, +1 is far. Compare with stored value (far = +1) */
                    if ((s32)z >= (s32)state->zbuffer[idx]) continue;
                    state->zbuffer[idx] = z;
                }
                
                state->framebuffer[idx] = v0->color;
            }
        }
    }
}

void raster_draw_rect(int x, int y, int w, int h, u32 color) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            if (px >= 0 && py >= 0 && (u32)px < g_framebuffer.width && (u32)py < g_framebuffer.height) {
                g_framebuffer.pixels[py * g_framebuffer.width + px] = color;
            }
        }
    }
}

void raster_draw_tri(int x0, int y0, int x1, int y1, int x2, int y2, u32 color) {
    int min_x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int max_x = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int min_y = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int max_y = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    
    if (min_x < 0) min_x = 0;
    if (max_x >= (int)g_framebuffer.width) max_x = g_framebuffer.width - 1;
    if (min_y < 0) min_y = 0;
    if (max_y >= (int)g_framebuffer.height) max_y = g_framebuffer.height - 1;
    
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            fx16_t u, v, w;
            if (!raster_barycentric(x, y, x0, y0, x1, y1, x2, y2, &u, &v, &w)) continue;
            
            g_framebuffer.pixels[y * g_framebuffer.width + x] = color;
        }
    }
}

/* Orthographic projection matrix */
void raster_ortho(fx16_t left, fx16_t right, fx16_t bottom, fx16_t top, fx16_t near, fx16_t far, raster_matrix_t* out) {
    fx16_t* m = out->m;
    fx16_t width = right - left;
    fx16_t height = top - bottom;
    fx16_t depth = far - near;
    
    /* Initialize all to 0 */
    for (int i = 0; i < 16; i++) m[i] = 0;
    
    /* Orthographic projection: simple rational scaling */
    m[0] = (FX16_ONE * 2) / width;
    m[5] = (FX16_ONE * 2) / height;
    m[10] = (-FX16_ONE * 2) / depth;
    m[15] = FX16_ONE;
    m[12] = -(right + left) / width;
    m[13] = -(top + bottom) / height;
    m[14] = -(far + near) / depth;
}