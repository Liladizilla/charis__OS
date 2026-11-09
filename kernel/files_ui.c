/* files_ui.c - Native read-only FAT32 root browser.
 * The view is deliberately honest about current filesystem capabilities.
 */
#include <kernel/files_ui.h>
#include <kernel/fs.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/ui_theme.h>
#include <kernel/serial.h>

#define FILES_CAPACITY 24u
static bool files_visible;
static fs_dirent_t files_entries[FILES_CAPACITY];
static int files_count, files_selected=-1;
static bool files_available;

static u32 px(u32 v) { return gfx_ui_scale(v); }
static u32 text_h(void) { return (u32)font_cell_h*gfx_text_get_scale(); }
static u32 wx(void) { return px(36); }
static u32 wy(void) { return px(28); }
static u32 ww(void) {
    u32 m=px(72);
    return g_framebuffer.width>m ? g_framebuffer.width-m : g_framebuffer.width;
}
static u32 wh(void) {
    u32 m=px(100);
    return g_framebuffer.height>m ? g_framebuffer.height-m : g_framebuffer.height;
}
static u32 title_h(void) { return px(42); }
static u32 toolbar_y(void) { return wy()+title_h()+px(10); }
static u32 content_y(void) { return toolbar_y()+px(52); }
static u32 list_y(void) {
    return content_y()+px(16)+text_h()*3u+px(30);
}
static u32 row_h(void) {
    u32 minimum=px(42), readable=text_h()+px(18);
    return readable>minimum ? readable : minimum;
}
static u32 sidebar_w(void) {
    u32 label=0;
    gfx_text_size("FAT32 volume",&label,NULL);
    u32 needed=label+px(52), minimum=px(150);
    return needed>minimum ? needed : minimum;
}

static void files_refresh(void) {
    files_count=fs_list_root(files_entries,FILES_CAPACITY);
    files_available=files_count>=0;
    if(files_count<0) files_count=0;
    if(files_count>(int)FILES_CAPACITY) files_count=FILES_CAPACITY;
    if(files_selected>=files_count) files_selected=files_count-1;
}
void files_open(void) {
    files_refresh(); files_visible=true;
    serial_puts("FILES_OPEN");
    serial_printf("FILES_ROOT_COUNT=%d",files_count);
}
void files_close(void) {
    files_visible=false; files_selected=-1; serial_puts("FILES_CLOSE");
}
bool files_is_open(void) { return files_visible; }

bool files_handle_key(u8 scancode,bool pressed) {
    if(!files_visible || !pressed) return false;
    u8 code=scancode&0x7Fu;
    if(code==0x01u) { files_close(); return true; }
    if(code==0x3Fu) {
        files_refresh(); serial_puts("FILES_REFRESH");
        serial_printf("FILES_ROOT_COUNT=%d",files_count); return true;
    }
    if(code==0x48u && files_count>0) {
        if(files_selected>0) files_selected--;
        else if(files_selected<0) files_selected=0;
    } else if(code==0x50u && files_count>0) {
        if(files_selected+1<files_count) files_selected++;
    }
    return true;
}

bool files_handle_mouse(int mx,int my) {
    if(!files_visible) return false;
    u32 x=wx(),y=wy(),w=ww(),h=wh();
    if(mx<(int)x || mx>=(int)(x+w) || my<(int)y || my>=(int)(y+h)) {
        files_close(); return true;
    }
    if(my<(int)(y+title_h()) && mx>=(int)(x+w-px(48))) {
        files_close(); return true;
    }
    u32 sy=list_y(), rh=row_h();
    if(my>=(int)sy && mx>=(int)(x+sidebar_w()+px(12))) {
        u32 row=(u32)(my-(int)sy)/rh;
        if(row<(u32)files_count && row<FILES_CAPACITY)
            files_selected=(int)row;
    }
    return true;
}

static void draw_size(u32 x,u32 y,u32 value) {
    char out[11],rev[10]; u32 n=0,p=0;
    do { rev[n++]=(char)('0'+value%10u); value/=10u; }
    while(value && n<sizeof(rev));
    while(n) out[p++]=rev[--n];
    out[p]=0; gfx_draw_string(x,y,out,CHARIS_UI_TEXT,FB_TRANSPARENT);
}
static void card(u32 x,u32 y,u32 w,u32 h) {
    fb_fill_rect(x,y,w,h,CHARIS_UI_PANEL);
    fb_draw_rect(x,y,w,h,CHARIS_UI_BORDER);
}

void files_render(void) {
    if(!files_visible || !g_framebuffer.initialized) return;
    u32 x=wx(),y=wy(),w=ww(),h=wh(),th=text_h();
    u32 side=sidebar_w(), foot=y+h-px(34);
    u32 main_x=x+side+px(16), main_w=w-side-px(32);
    u32 inspector_w=px(220), list_w=main_w-inspector_w-px(14);
    u32 inspector_x=main_x+list_w+px(14);

    fb_fill_rect(x+px(5),y+px(6),w,h,CHARIS_UI_SHADOW);
    fb_fill_rect(x,y,w,h,CHARIS_UI_PANEL);
    fb_draw_rect(x,y,w,h,CHARIS_UI_BORDER_STRONG);
    fb_fill_rect(x,y,w,title_h(),CHARIS_UI_PANEL_RAISED);
    fb_fill_rect(x,y+title_h(),w,px(1),CHARIS_UI_BORDER);
    gfx_draw_string(x+px(18),y+(title_h()-th)/2u,
                    "Files",CHARIS_UI_TEXT,FB_TRANSPARENT);
    gfx_draw_string(x+px(78),y+(title_h()-th)/2u,
                    "FILE MANAGER",CHARIS_UI_MUTED,FB_TRANSPARENT);
    fb_fill_rect(x+w-px(40),y+px(10),px(26),px(24),CHARIS_UI_SURFACE);
    fb_draw_rect(x+w-px(40),y+px(10),px(26),px(24),CHARIS_UI_BORDER);
    gfx_draw_string(x+w-px(32),y+px(15),"x",CHARIS_UI_TEXT,FB_TRANSPARENT);

    /* Location and action toolbar. */
    u32 ty=toolbar_y();
    fb_fill_rect(x+px(12),ty,w-px(24),px(38),CHARIS_UI_SURFACE);
    fb_draw_rect(x+px(12),ty,w-px(24),px(38),CHARIS_UI_BORDER);
    gfx_draw_string(x+px(26),ty+(px(38)-th)/2u,
                    "VOLUMES   /   ROOT",CHARIS_UI_ACCENT,FB_TRANSPARENT);
    gfx_draw_string(x+w-px(116),ty+(px(38)-th)/2u,
                    "F5  REFRESH",CHARIS_UI_MUTED,FB_TRANSPARENT);

    /* Places sidebar. */
    u32 cy=content_y();
    card(x+px(12),cy,side-px(4),foot-cy-px(12));
    gfx_draw_string(x+px(26),cy+px(18),"LOCATIONS",
                    CHARIS_UI_MUTED,FB_TRANSPARENT);
    fb_fill_rect(x+px(18),cy+px(48),side-px(16),px(36),
                 CHARIS_UI_ACCENT_SURFACE);
    fb_fill_rect(x+px(18),cy+px(48),px(3),px(36),CHARIS_UI_ACCENT);
    gfx_draw_string(x+px(32),cy+px(60),"Root volume",
                    CHARIS_UI_TEXT,FB_TRANSPARENT);
    gfx_draw_string(x+px(26),cy+px(112),"DEVICES",
                    CHARIS_UI_MUTED,FB_TRANSPARENT);
    gfx_draw_string(x+px(32),cy+px(144),
                    files_available ? "FAT32 volume" : "No volume",
                    files_available ? CHARIS_UI_SUCCESS : CHARIS_UI_WARNING,
                    FB_TRANSPARENT);
    fb_fill_rect(x+px(22),foot-px(58),side-px(24),px(1),CHARIS_UI_BORDER);
    gfx_draw_string(x+px(26),foot-px(42),"LOCAL STORAGE",
                    CHARIS_UI_MUTED,FB_TRANSPARENT);

    /* Main directory list. */
    card(main_x,cy,list_w,foot-cy-px(12));
    gfx_draw_string(main_x+px(18),cy+px(16),"Root directory",
                    CHARIS_UI_TEXT,FB_TRANSPARENT);
    gfx_draw_string(main_x+px(18),cy+px(16)+th+px(8),"FAT32  /  8.3 NAMES",
                    CHARIS_UI_MUTED,FB_TRANSPARENT);
    u32 header_y=list_y()-th-px(12);
    fb_fill_rect(main_x+px(12),header_y+th+px(6),list_w-px(24),px(1),
                 CHARIS_UI_BORDER);
    gfx_draw_string(main_x+px(20),header_y,"NAME",
                    CHARIS_UI_MUTED,FB_TRANSPARENT);
    gfx_draw_string(main_x+list_w-px(92),header_y,"SIZE",
                    CHARIS_UI_MUTED,FB_TRANSPARENT);

    if(!files_available) {
        gfx_draw_string(main_x+px(20),list_y()+px(28),
                        "No readable FAT32 volume is mounted.",
                        CHARIS_UI_WARNING,FB_TRANSPARENT);
        gfx_draw_string(main_x+px(20),list_y()+px(56),
                        "Connect a supported volume and refresh.",
                        CHARIS_UI_MUTED,FB_TRANSPARENT);
    } else if(!files_count) {
        gfx_draw_string(main_x+px(20),list_y()+px(28),
                        "This directory is empty.",
                        CHARIS_UI_MUTED,FB_TRANSPARENT);
    }

    for(int i=0;i<files_count && i<(int)FILES_CAPACITY;i++) {
        u32 ry=list_y()+(u32)i*row_h();
        if(ry+row_h()>foot-px(18)) break;
        if(i==files_selected)
            fb_fill_rect(main_x+px(8),ry,list_w-px(16),row_h(),
                         CHARIS_UI_ACCENT_SURFACE);
        u32 ix=main_x+px(18),iy=ry+px(10);
        fb_fill_rect(ix,iy,px(18),px(22),
                     files_entries[i].directory ? 0xFF4A3B24u : CHARIS_UI_SURFACE);
        fb_draw_rect(ix,iy,px(18),px(22),CHARIS_UI_BORDER_STRONG);
        fb_fill_rect(ix+px(4),iy+px(7),px(10),px(1),CHARIS_UI_MUTED);
        fb_fill_rect(ix+px(4),iy+px(12),px(8),px(1),CHARIS_UI_MUTED);
        gfx_draw_string(main_x+px(48),ry+(row_h()-th)/2u,
                        files_entries[i].name,CHARIS_UI_TEXT,FB_TRANSPARENT);
        if(files_entries[i].directory)
            gfx_draw_string(main_x+list_w-px(92),ry+(row_h()-th)/2u,
                            "Folder",CHARIS_UI_MUTED,FB_TRANSPARENT);
        else
            draw_size(main_x+list_w-px(92),ry+(row_h()-th)/2u,
                      files_entries[i].size);
    }

    /* Inspector gives selection context without pretending to open files. */
    card(inspector_x,cy,inspector_w,foot-cy-px(12));
    gfx_draw_string(inspector_x+px(16),cy+px(18),"DETAILS",
                    CHARIS_UI_MUTED,FB_TRANSPARENT);
    if(files_selected>=0 && files_selected<files_count) {
        gfx_draw_string(inspector_x+px(16),cy+px(58),
                        files_entries[files_selected].name,
                        CHARIS_UI_TEXT,FB_TRANSPARENT);
        gfx_draw_string(inspector_x+px(16),cy+px(92),
                        files_entries[files_selected].directory ? "Folder" : "File",
                        CHARIS_UI_ACCENT,FB_TRANSPARENT);
        gfx_draw_string(inspector_x+px(16),cy+px(124),"Size",
                        CHARIS_UI_MUTED,FB_TRANSPARENT);
        draw_size(inspector_x+px(16),cy+px(148),
                  files_entries[files_selected].size);
    } else {
        gfx_draw_string(inspector_x+px(16),cy+px(58),
                        "Select an item",CHARIS_UI_TEXT,FB_TRANSPARENT);
        gfx_draw_string(inspector_x+px(16),cy+px(88),
                        "to view its details.",CHARIS_UI_MUTED,FB_TRANSPARENT);
    }
    fb_fill_rect(inspector_x+px(14),cy+px(184),inspector_w-px(28),px(1),
                 CHARIS_UI_BORDER);
    gfx_draw_string(inspector_x+px(16),cy+px(204),"ACCESS",
                    CHARIS_UI_MUTED,FB_TRANSPARENT);
    gfx_draw_string(inspector_x+px(16),cy+px(230),"Read only",
                    CHARIS_UI_WARNING,FB_TRANSPARENT);

    /* Footer. */
    fb_fill_rect(x+px(12),foot-px(8),w-px(24),px(1),CHARIS_UI_BORDER);
    gfx_draw_string(x+px(18),foot+px(4),
                    files_available ? "FAT32 volume  |  Read-only" : "Storage unavailable",
                    CHARIS_UI_MUTED,FB_TRANSPARENT);
    gfx_draw_string(x+w-px(90),foot+px(4),"ESC  CLOSE",
                    CHARIS_UI_MUTED,FB_TRANSPARENT);
}
