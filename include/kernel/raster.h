/* raster.h - Software 3D rasterizer for CharisOS */
#pragma once
#include <kernel/types.h>

#define RASTER_MAX_VERTICES  1024
#define RASTER_MAX_TRIANGLES 2048

/* 4x4 matrix for vertex transformations */
typedef struct {
    float m[16];
} raster_matrix_t;

/* Vertex with position, texcoord, color */
typedef struct {
    float x, y, z, w;
    float u, v;
    u32 color;
} raster_vertex_t;

/* Triangle from vertex indices */
typedef struct {
    u32 v1, v2, v3;
} raster_triangle_t;

/* Render state */
typedef struct {
    raster_matrix_t mvp;     /* Model-view-projection matrix */
    u32 width, height;     /* Viewport dimensions */
    int32_t* zbuffer;      /* Depth buffer */
    u32* framebuffer;      /* Backbuffer */
    u32* texture;          /* Active texture (optional) */
    u32 tex_width, tex_height;
} raster_state_t;

/* Initialize rasterizer state */
void raster_init(raster_state_t* state, u32 width, u32 height, u32* framebuffer);

/* Set viewport and matrices */
void raster_set_viewport(raster_state_t* state, u32 width, u32 height);
void raster_set_mvp(raster_state_t* state, const raster_matrix_t* mvp);

/* Submit geometry */
void raster_load_vertices(raster_vertex_t* vertices, int count);
void raster_load_triangles(raster_triangle_t* triangles, int count);

/* Render a mesh */
void raster_draw_mesh(raster_state_t* state);

/* Draw 2D primitives (for UI) */
void raster_draw_rect(int x, int y, int w, int h, u32 color);
void raster_draw_tri(int x0, int y0, int x1, int y1, int x2, int y2, u32 color);

/* Utility: create orthographic projection matrix */
void raster_ortho(float left, float right, float bottom, float top, float near, float far, raster_matrix_t* out);