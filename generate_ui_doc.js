const {
  Document, Packer, Paragraph, TextRun, HeadingLevel,
  AlignmentType, BorderStyle, ShadingType, LevelFormat, PageBreak, WidthType,
  Table, TableRow, TableCell
} = require('docx');
const fs = require('fs');

// ─── DESIGN TOKENS ───────────────────────────────────────────────────────────
const C = {
  bg:'111120', panel:'1A1A2E', panel2:'22223A',
  amber:'F5A623', amberL:'FFD080', amberD:'CC8500',
  blue:'3A86FF', blueL:'7AB8FF', teal:'00B4D8', tealL:'60D8F8',
  purple:'9B5DE5', purpleL:'C49AFF',
  green:'27AE60', greenL:'5EE892',
  red:'E03C3C', orange:'F77F00',
  white:'F0F0F8', gray1:'252538', gray2:'32324A',
  gray3:'555570', gray4:'8888AA', gray5:'CCCCDD',
  border:'3A3A55',
};
const F = { head:'Arial', body:'Consolas', code:'Courier New', title:'Arial' };

// ─── HELPERS ─────────────────────────────────────────────────────────────────
const sp  = (b,a) => ({ spacing:{ before:b||0, after:a||0 } });
const ind = (l,h) => ({ indent:{ left:l||0, hanging:h||0 } });
const TR  = (text,opts={}) => new TextRun({ text, font:F.body, size:22, color:C.white, ...opts });
const TRc = (t,col,o={})  => new TextRun({ text:t, font:F.body, size:22, color:col, ...o });
const TRm = (t,o={})      => new TextRun({ text:t, font:F.code, size:19, color:C.amberL||'FFD080', ...o });
const TRh = (t,o={})      => new TextRun({ text:t, font:F.head, size:22, color:C.white, ...o });

const PG = () => new Paragraph({ children:[new PageBreak()] });

const H1 = (t,c=C.amber) => new Paragraph({
  heading:HeadingLevel.HEADING_1, ...sp(500,140),
  shading:{ fill:C.panel, type:ShadingType.CLEAR },
  border:{ bottom:{ style:BorderStyle.SINGLE, size:10, color:c } },
  children:[new TextRun({ text:' '+t, font:F.head, size:60, bold:true, color:c })],
});
const H2 = (t,c=C.amber) => new Paragraph({
  heading:HeadingLevel.HEADING_2, ...sp(380,100),
  border:{ bottom:{ style:BorderStyle.SINGLE, size:4, color:c } },
  children:[new TextRun({ text:t, font:F.head, size:36, bold:true, color:c })],
});
const H3 = (t,c=C.white) => new Paragraph({
  heading:HeadingLevel.HEADING_3, ...sp(280,80),
  children:[new TextRun({ text:t, font:F.head, size:28, bold:true, color:c })],
});
const H4 = (t,c=C.gray4) => new Paragraph({
  ...sp(200,60),
  children:[new TextRun({ text:t, font:F.head, size:22, bold:true, color:c, allCaps:true })],
});

const Body = (t,b=80,a=80) => new Paragraph({
  ...sp(b,a),
  children:[new TextRun({ text:t, font:F.body, size:22, color:C.gray5 })],
});
const BodyRuns = (runs,b=80,a=80) => new Paragraph({
  ...sp(b,a),
  children:runs.map(r=>typeof r==='string'
    ?new TextRun({ text:r, font:F.body, size:22, color:C.gray5 })
    :new TextRun({ font:F.body, size:22, color:C.gray5, ...r })),
});
const Bul = (runs,lv=0) => new Paragraph({
  ...sp(50,50), ...ind(360+lv*320,280),
  numbering:{ reference:'bul', level:lv },
  children:(Array.isArray(runs)?runs:[runs]).map(r=>
    typeof r==='string'
      ?new TextRun({ text:r, font:F.body, size:22, color:C.gray5 })
      :new TextRun({ font:F.body, size:22, color:C.gray5, ...r })),
});
const Code = (lines,label='') => {
  const out=[];
  if(label) out.push(new Paragraph({
    ...sp(120,0), shading:{ fill:'0A0A18', type:ShadingType.CLEAR },
    children:[new TextRun({ text:'  // '+label, font:F.code, size:17, color:C.gray3, italics:true })],
  }));
  lines.forEach(ln=>out.push(new Paragraph({
    ...sp(0,0), ...ind(200),
    shading:{ fill:'070712', type:ShadingType.CLEAR },
    children:[new TextRun({ text:ln||' ', font:F.code, size:19, color:C.amberL||'FFD080' })],
  })));
  out.push(new Paragraph({ ...sp(0,100), shading:{ fill:'070712', type:ShadingType.CLEAR }, children:[TR('')] }));
  return out;
};
const Box = (lines,color=C.amber,fill=C.panel) => lines.map(ln=>new Paragraph({
  ...sp(0,0), ...ind(180),
  border:{ left:{ style:BorderStyle.SINGLE, size:8, color } },
  shading:{ fill, type:ShadingType.CLEAR },
  children:[new TextRun({ text:' '+ln, font:F.code, size:20, color:C.gray5 })],
}));
const Callout = (t,c=C.amber,fill=C.panel) => new Paragraph({
  ...sp(120,120), ...ind(160),
  border:{
    left:{ style:BorderStyle.SINGLE, size:10, color:c },
    top:{ style:BorderStyle.SINGLE, size:1, color:C.gray2 },
    bottom:{ style:BorderStyle.SINGLE, size:1, color:C.gray2 },
  },
  shading:{ fill, type:ShadingType.CLEAR },
  children:[new TextRun({ text:'  '+t, font:F.body, size:21, color:C.gray5 })],
});
const Sep = (c=C.border) => new Paragraph({
  ...sp(100,100), border:{ bottom:{ style:BorderStyle.SINGLE, size:2, color:c } },
  children:[TR('')],
});
const LBL = (t,c) => new TextRun({ text:` ${t} `, font:F.head, size:19, bold:true, color:c });

// ─── PHASE ROW ───────────────────────────────────────────────────────────────
const PhRow = (num,title,status,color,desc) => [
  new Paragraph({
    ...sp(300,60),
    border:{ left:{ style:BorderStyle.SINGLE, size:14, color } },
    children:[
      new TextRun({ text:` PHASE ${num} `, font:F.code, size:21, bold:true, color }),
      new TextRun({ text:` ${title}`, font:F.head, size:28, bold:true, color:C.white }),
      new TextRun({ text:`  ${status}`, font:F.head, size:17, bold:true, color:
        status==='DONE'?C.green:status==='NEXT'?C.blue:status==='IN PROGRESS'?C.amber:C.gray3 }),
    ],
  }),
  new Paragraph({ ...sp(40,80), ...ind(240),
    children:[new TextRun({ text:desc, font:F.body, size:21, color:C.gray4, italics:true })],
  }),
];

// ─── LAYER BOX ───────────────────────────────────────────────────────────────
const Layer = (name,detail,color) => new Paragraph({
  ...sp(0,0),
  shading:{ fill:C.panel2, type:ShadingType.CLEAR },
  border:{
    left:{ style:BorderStyle.SINGLE, size:10, color },
    top:{ style:BorderStyle.SINGLE, size:1, color:C.gray2 },
    bottom:{ style:BorderStyle.SINGLE, size:1, color:C.gray2 },
  },
  children:[
    new TextRun({ text:`  ${name}  `, font:F.head, size:22, bold:true, color }),
    new TextRun({ text:`— ${detail}`, font:F.body, size:21, color:C.gray4 }),
  ],
});

// ─── CONTENT ─────────────────────────────────────────────────────────────────
const children = [];

// ══════════════════════════════════════════════════════
// COVER
// ══════════════════════════════════════════════════════
children.push(new Paragraph({ ...sp(600,40), alignment:AlignmentType.CENTER,
  children:[new TextRun({ text:'CharisOS', font:F.title, size:120, bold:true, color:C.amber })],
}));
children.push(new Paragraph({ ...sp(0,40), alignment:AlignmentType.CENTER,
  children:[new TextRun({ text:'UI INFRASTRUCTURE', font:F.title, size:52, bold:true, color:C.white, allCaps:true, characterSpacing:300 })],
}));
children.push(new Paragraph({ ...sp(0,40), alignment:AlignmentType.CENTER,
  children:[new TextRun({ text:'GRAND PLAN', font:F.title, size:36, bold:true, color:C.amber, allCaps:true, characterSpacing:500 })],
}));
children.push(new Paragraph({ ...sp(60,40), alignment:AlignmentType.CENTER,
  border:{ bottom:{ style:BorderStyle.SINGLE, size:4, color:C.amber } },
  children:[],
}));
children.push(new Paragraph({ ...sp(60,40), alignment:AlignmentType.CENTER,
  children:[new TextRun({ text:'Adaptive Realistic Glass  ·  Spring Physics  ·  Layered Compositing', font:F.body, size:24, color:C.gray3, italics:true })],
}));
children.push(new Paragraph({ ...sp(20,600), alignment:AlignmentType.CENTER,
  children:[new TextRun({ text:'"Minimal. Futuristic. Alive. Stable."', font:F.body, size:28, color:C.gray4, italics:true })],
}));
children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 0 — PHILOSOPHY
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 0 — THE PHILOSOPHY', C.amber));
children.push(Body(
  'CharisOS is not imitating a web browser. It is not GTK on bare metal, it is not a theme on top of Linux, and it is not Electron pretending to be an OS. It is a bare-metal x86_64 kernel written in C and Assembly that builds every pixel on screen from physical memory. That changes everything about how the UI must be designed, architecturally and artistically.',
  80,120));
children.push(Body(
  'The four words are the law. "Minimal" means every element earns its place — no padding, no filler animations, no decorative chrome that slows down a real task. "Futuristic" means the visual language comes from the future, not from Windows 95 or Mac OS 9, not from flat-design-trend-of-2015. "Alive" means the desktop is not a static grid of boxes; it breathes, reacts, responds to motion and context. "Stable" means that when the GPU driver crashes, the user is still working.',
  80,120));

children.push(H2('Why Bare Metal Changes Everything'));
children.push(Body(
  'Every modern desktop UI (macOS, Windows, GNOME, KDE) runs on top of decades of abstraction: libc, libstdc++, OpenGL drivers, X11 or Wayland, GTK/Qt, D-Bus, systemd. All of that is gone. CharisOS starts from physical RAM and a framebuffer. The constraint is also the opportunity: because the UI stack is built from the first pixel, every design decision is intentional. Nothing is inherited. Nothing is accidental.'));
children.push(Body(
  'This also means the performance envelope is much tighter. A Gaussian blur that takes a millisecond in Chrome takes ten milliseconds on bare metal software rendering. Every algorithm must be justified. Every effect must have a fallback path. Every layer must be able to disappear without breaking anything above it.'));

children.push(H2('The Design Language: Adaptive Realistic Glass'));
children.push(Callout(
  'Do NOT call it Glassmorphism. Glassmorphism is a static CSS trick: fixed blur, fixed opacity, no awareness of context. Adaptive Realistic Glass is a live system that recalculates its own appearance based on the world behind it, the power of the GPU, and the accessibility needs of the user.',
  C.amber));
children.push(Body(
  'Old glassmorphism is a fixed set of values: blur:15px, opacity:0.3, background:rgba(255,255,255,0.1). It looks the same whether the wallpaper is pure white or pure black. On white: unreadable. On black: fine. It is a guess that sometimes works.'));
children.push(Body(
  'Adaptive Realistic Glass is a live computation. Before rendering any window, the compositor samples the wallpaper pixels underneath that window, calculates the average luminance and the local contrast variance, and chooses the blur radius, tint strength, opacity, and reflection intensity from that data. On a white wallpaper, the glass tints heavily and blurs less. On a dark wallpaper, the glass goes nearly invisible with strong reflections. On a busy wallpaper (high variance), the blur increases to hide the noise underneath.'));

children.push(H3('The Five Materials'));
children.push(Body('Every surface in CharisOS is made of one of five materials. The material determines how the compositor renders that surface and how it responds to luminance, GPU capability, and power mode.'));

const matData = [
  ['Frosted Glass', C.blue, 'Default for all standard windows. Blur radius 10-20px (GPU-dependent). Subtle noise texture to simulate ground glass. Moderate tint (~15%). Soft multi-layer shadow. Used in: app windows, panels.'],
  ['Crystal Glass', C.teal, 'For dialogs, authentication prompts, and system notifications that demand attention. Higher opacity (60-80%). Sharper edges (1px border highlight). Smaller blur radius (6-10px). More visible — conveys importance without screaming.'],
  ['Liquid Glass', C.purpleL||'C49AFF', 'For the Dock, Taskbar, and floating Widgets. Subtle barrel-distortion effect (looks like light bending through water). Lowest tint. Highest transparency. Smooth edge feathering. This material feels like it belongs on the surface of the desktop.'],
  ['Solid Surface', C.gray4, 'Battery saver mode, low GPU, remote desktop, safe mode, accessibility high-contrast mode. Flat color (no blur, no transparency). Still keeps rounded corners and correct spacing. The design language does not collapse — only the effects disappear.'],
  ['Acrylic Mist', C.greenL||'5EE892', 'For context menus, tooltips, dropdowns. Must render and dismiss in under 8ms. Very fast blur (4-6px, pre-cached). High tint to ensure readability regardless of background. No animations on dismiss — just fades in 80ms, fades out 60ms.'],
];
matData.forEach(([name,color,desc])=>{
  children.push(new Paragraph({
    ...sp(60,40),
    border:{ left:{ style:BorderStyle.SINGLE, size:10, color } },
    shading:{ fill:C.panel, type:ShadingType.CLEAR },
    children:[new TextRun({ text:`  ${name}  `, font:F.head, size:22, bold:true, color }),],
  }));
  children.push(new Paragraph({ ...sp(20,80), ...ind(240),
    children:[new TextRun({ text:desc, font:F.body, size:21, color:C.gray5 })],
  }));
});

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 1 — COMPLETE UI STACK
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 1 — THE COMPLETE UI STACK', C.teal));
children.push(Body(
  'The CharisOS UI has seven layers. Each layer talks only to the layer immediately below it. Applications never touch the framebuffer. The kernel never knows about windows. The compositor never knows about widgets. This separation is what allows each layer to have its own fallback, its own performance tier, and its own failure recovery.',
  80,120));

[
  [' Layer 7 ', ' Applications (userland ELF binaries)', C.purple, 'Submit surfaces and receive input events. They never draw pixels directly.'],
  [' Layer 6 ', ' CharisUI Widget Framework', C.blue, 'Manages layout, widget state, animations, theme. Composes surfaces from widgets.'],
  [' Layer 5 ', ' Surface Protocol', C.teal, 'Applications submit surface buffers + metadata to the compositor via syscall.'],
  [' Layer 4 ', ' CharisOS Compositor', C.amber, 'Blends surfaces, applies materials (blur, transparency, shadows, lighting).'],
  [' Layer 3 ', ' Renderer Backend (Vulkan | OpenGL | Software)', C.green, 'Executes draw calls: fill, blit, blur, alpha-blend. Swappable at runtime.'],
  [' Layer 2 ', ' GPU Driver / Framebuffer Driver', C.orange, 'Communicates with hardware. For QEMU: VirtIO GPU. For real hardware: eventually.'],
  [' Layer 1 ', ' Kernel (PMM, VMM, IRQ, PCI, DMA)', C.gray4, 'Allocates memory for buffers, routes GPU interrupts, DMA transfers.'],
].forEach(([num,name,color,desc])=>{
  children.push(new Paragraph({
    ...sp(2,2),
    shading:{ fill:C.panel2, type:ShadingType.CLEAR },
    border:{ left:{ style:BorderStyle.SINGLE, size:12, color } },
    children:[
      new TextRun({ text:num, font:F.code, size:20, bold:true, color }),
      new TextRun({ text:name, font:F.head, size:22, bold:true, color:C.white }),
    ],
  }));
  children.push(new Paragraph({ ...sp(0,4), ...ind(240),
    shading:{ fill:C.panel, type:ShadingType.CLEAR },
    children:[new TextRun({ text:'  '+desc, font:F.body, size:20, color:C.gray4 })],
  }));
});

children.push(H2('The Single Most Important Rule', C.red));
children.push(Callout(
  'Applications NEVER draw pixels directly to the framebuffer. They allocate a surface buffer (a chunk of physical memory mapped into both their address space and the compositor\'s), draw into it, then notify the compositor. The compositor owns the screen. This is non-negotiable. Any shortcut here breaks the entire security model, the blur system, the damage tracking, and the crash recovery.',
  C.red, C.panel));
children.push(Body(
  'This is identical to how Wayland works conceptually, but implemented entirely in C without any external dependencies. The surface submission is a single syscall: sys_present_surface(surface_id, damage_rect). The compositor processes it on the next frame tick.'));

children.push(H2('The GPU Capability Tiers'));
children.push(Body('At boot, CharisOS probes the GPU via PCI enumeration and assigns a capability tier. This tier controls which renderer backend is loaded and sets the maximum quality level for the material system.'));
children.push(Bul([{text:'Tier 3 — Full GPU (Vulkan/OpenGL):', bold:true, color:C.green}, ' Full blur, reflections, lighting engine, 120Hz compositor, hardware-accelerated 2D. Target: any discrete or integrated GPU from 2012+.']));
children.push(Bul([{text:'Tier 2 — Partial GPU (VirtIO / basic):', bold:true, color:C.amber}, ' Blur + transparency, no real-time reflections, 60Hz. Target: QEMU VirtIO GPU, older Intel GMA.']));
children.push(Bul([{text:'Tier 1 — Software (CPU only):', bold:true, color:C.orange}, ' Tint-only transparency (no blur), flat shadow, 30Hz. Target: no GPU detected, emergency boot.']));
children.push(Bul([{text:'Tier 0 — Emergency:', bold:true, color:C.red}, ' Solid colors, correct layout, no effects. Used when GPU crashes and software renderer also fails. Still shows windows, text, and cursor.']));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 2 — THE COMPOSITOR
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 2 — THE COMPOSITOR', C.amber));
children.push(Body(
  'The compositor is the most important piece of CharisOS UI infrastructure. Everything visual passes through it. It runs in the kernel at a fixed tick rate (60Hz default, 120Hz if GPU supports it) driven by a timer interrupt or GPU VSync signal. Understanding how it works end-to-end is essential before building any of the layers above it.',
  80,120));

children.push(H2('The Frame Loop'));
children.push(Body('Every frame, the compositor runs this sequence exactly once. The total budget is 16.6ms at 60Hz, 8.3ms at 120Hz. Every operation must fit inside this budget or the frame is dropped.'));
children.push(...Code([
  '// compositor_tick() -- called by timer IRQ or GPU VSync',
  'void compositor_tick(void) {',
  '    if (damage_list_empty()) return;  // Nothing changed: skip frame',
  '',
  '    // Step 1: Collect all pending surface updates from applications',
  '    collect_surface_updates();        // Process sys_present_surface() queue',
  '',
  '    // Step 2: Sort surfaces by z-order (painter\'s algorithm)',
  '    sort_surfaces_by_z();             // Insertion sort, O(n) for small n',
  '',
  '    // Step 3: For each dirty rectangle, re-composite affected surfaces',
  '    for each rect in damage_list:',
  '        composite_rect(rect);         // See below',
  '',
  '    // Step 4: Draw hardware cursor on top (no compositing, just blit)',
  '    draw_cursor(mouse_x, mouse_y);',
  '',
  '    // Step 5: Flip buffers',
  '    renderer_present();               // GPU: swap chain. Software: memcpy to fb.',
  '',
  '    // Step 6: Clear damage list',
  '    damage_list_clear();',
  '}',
],'compositor frame loop'));

children.push(H2('Damage Tracking'));
children.push(Body(
  'Damage tracking is the technique of only redrawing the parts of the screen that have actually changed. Without damage tracking, the compositor must redraw the entire 1920x1080 framebuffer every frame — 2MB of data at 60Hz = 120MB/s of wasted memory bandwidth. With damage tracking, only the changed rectangles are recomposited.'));
children.push(Body(
  'When an application updates its surface, it calls sys_present_surface() with a damage_rect parameter — the rectangle within its surface that changed. The compositor adds this rectangle (translated to screen coordinates) to the global damage list. When a window moves, the compositor adds both the old position and the new position to the damage list.'));
children.push(...Code([
  'typedef struct { int x, y, w, h; } rect_t;',
  '',
  'typedef struct {',
  '    rect_t rects[MAX_DAMAGE_RECTS]; // MAX_DAMAGE_RECTS = 64',
  '    int    count;',
  '    bool   full_invalidate;         // True: entire screen is dirty',
  '} damage_list_t;',
  '',
  '// Add a dirty rectangle to the damage list.',
  '// If the list is full, set full_invalidate = true.',
  'void damage_add(int x, int y, int w, int h) {',
  '    if (damage.full_invalidate) return; // Already full repaint',
  '    if (damage.count >= MAX_DAMAGE_RECTS) {',
  '        damage.full_invalidate = true;  // Too many rects: just repaint all',
  '        return;',
  '    }',
  '    // Clip to screen bounds',
  '    if (x < 0) { w += x; x = 0; }',
  '    if (y < 0) { h += y; y = 0; }',
  '    if (x + w > screen_w) w = screen_w - x;',
  '    if (y + h > screen_h) h = screen_h - y;',
  '    if (w <= 0 || h <= 0) return;',
  '    damage.rects[damage.count++] = (rect_t){x, y, w, h};',
  '}',
  '',
  '// When a window moves:',
  '// 1. Add old position to damage list (to repaint what was behind it)',
  '// 2. Add new position to damage list (to paint window in new place)',
  'void window_move_damage(window_t *w, int new_x, int new_y) {',
  '    damage_add(w->x, w->y, w->width, w->height); // Old position',
  '    w->x = new_x; w->y = new_y;',
  '    damage_add(w->x, w->y, w->width, w->height); // New position',
  '}',
],'damage tracking data structures'));

children.push(H2('The Surface Object'));
children.push(Body(
  'Every visual element on screen — windows, widgets, menus, the cursor, the desktop wallpaper — is a surface. A surface is a rectangular pixel buffer with metadata that tells the compositor how to render it.'));
children.push(...Code([
  'typedef struct surface {',
  '    uint32_t       id;',
  '    uint32_t      *pixels;        // Client-area pixel buffer (ARGB32)',
  '    int            width, height; // Pixel dimensions',
  '    int            screen_x;      // Position on screen',
  '    int            screen_y;',
  '',
  '    // Material system',
  '    material_type_t material;     // FROSTED, CRYSTAL, LIQUID, SOLID, ACRYLIC',
  '    float           opacity;      // 0.0 = invisible, 1.0 = opaque',
  '    float           blur_radius;  // Pixels of blur behind this surface',
  '    color_t         tint;         // Color tint applied to blurred background',
  '    float           tint_strength;// 0.0 = no tint, 1.0 = solid tint color',
  '',
  '    // Lighting',
  '    float           ambient_l;    // Ambient light intensity (0.0-1.0)',
  '    float           specular_l;   // Specular highlight intensity',
  '    color_t         light_color;  // Color of the light (sampled from wallpaper)',
  '',
  '    // Shadow',
  '    shadow_t        shadow;       // offset_x, offset_y, blur, spread, color',
  '',
  '    // Z-ordering and input',
  '    int             z_order;',
  '    bool            accepts_input;',
  '    rect_t          input_region; // May be smaller than visible area',
  '',
  '    // Animation state',
  '    anim_state_t    anim;         // Current spring animation values',
  '',
  '    // Blur cache',
  '    uint32_t       *blur_cache;   // Cached blurred wallpaper behind this surface',
  '    bool            blur_dirty;   // true = recalculate blur cache',
  '',
  '    struct surface *next;',
  '} surface_t;',
],'surface_t structure'));

children.push(H2('The Compositing Algorithm'));
children.push(Body('For each dirty rectangle, the compositor repaints that area by drawing all surfaces that overlap it, from bottom to top. The algorithm for rendering a single surface with Frosted Glass material is:'));
children.push(Bul([{text:'Step 1 — Wallpaper region:', bold:true, color:C.amber}, ' Copy the wallpaper pixels in the surface\'s bounds into a scratch buffer.']));
children.push(Bul([{text:'Step 2 — Gaussian blur:', bold:true, color:C.amber}, ' Apply a two-pass separable Gaussian blur to the scratch buffer with the surface\'s blur_radius. This is the glass effect. Cache this result — only recompute when the surface moves or the wallpaper changes (blur_dirty flag).']));
children.push(Bul([{text:'Step 3 — Luminance sample:', bold:true, color:C.amber}, ' Calculate the average luminance of the blurred scratch buffer. Adjust tint_strength: high luminance = more tint, low luminance = less tint.']));
children.push(Bul([{text:'Step 4 — Tint:', bold:true, color:C.amber}, ' Alpha-blend the tint color onto the scratch buffer at tint_strength. This creates the frosted effect.']));
children.push(Bul([{text:'Step 5 — Client pixels:', bold:true, color:C.amber}, ' Alpha-blend the surface\'s actual pixel content (drawn by the application) onto the scratch buffer.']));
children.push(Bul([{text:'Step 6 — Shadow:', bold:true, color:C.amber}, ' Draw the shadow below the surface. Shadow is pre-blurred into a cached shadow texture that is only regenerated when window size changes.']));
children.push(Bul([{text:'Step 7 — Lighting:', bold:true, color:C.amber}, ' Add the specular highlight (top edge, corner shine) sampled from the wallpaper\'s warm/cool light map.']));
children.push(Bul([{text:'Step 8 — Write to back buffer:', bold:true, color:C.amber}, ' Copy the fully composited pixels into the back buffer at the surface\'s screen position.']));
children.push(Bul([{text:'Step 9 (frame end) — Flip:', bold:true, color:C.amber}, ' After all dirty rects are processed, the back buffer is presented to the screen in a single atomic operation.']));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 3 — ADAPTIVE REALISTIC GLASS
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 3 — ADAPTIVE REALISTIC GLASS', C.blue));
children.push(Body(
  'This section explains the complete algorithm that makes a window surface "read" its environment and adjust its visual properties to match. This is the system that separates CharisOS glass from a CSS trick.',
  80,120));

children.push(H2('Step 1 — Luminance Detection'));
children.push(Body('Every time a window\'s position changes, or the wallpaper changes, the compositor samples the wallpaper pixels in the rectangle directly behind the window. From those pixels it calculates:'));
children.push(...Code([
  '// Luminance calculation (Rec. 709 standard)',
  '// L = 0.2126*R + 0.7152*G + 0.0722*B  (perceptual luminance)',
  '// Simplified integer version for speed:',
  '// L = (77*R + 150*G + 29*B) >> 8',
  '',
  'typedef struct {',
  '    float avg_luminance;   // 0.0 (black) to 1.0 (white)',
  '    float variance;        // How "busy" the area is (0=uniform, 1=chaotic)',
  '    float warm_cool;       // -1.0 (cool/blue) to +1.0 (warm/orange)',
  '    color_t dominant;      // Most common color in the sampled region',
  '} wallpaper_sample_t;',
  '',
  'wallpaper_sample_t sample_wallpaper_behind(surface_t *s) {',
  '    wallpaper_sample_t result = {0};',
  '    uint32_t *wp = wallpaper_pixels;',
  '    int x0 = s->screen_x, y0 = s->screen_y;',
  '    int x1 = x0 + s->width, y1 = y0 + s->height;',
  '    int count = 0;',
  '    float sum_L = 0, sum_sq = 0;',
  '    // Sample every 4th pixel for performance',
  '    for (int y = y0; y < y1; y += 4) {',
  '        for (int x = x0; x < x1; x += 4) {',
  '            uint32_t px = wp[y * screen_w + x];',
  '            int r=(px>>16)&0xFF, g=(px>>8)&0xFF, b=px&0xFF;',
  '            float L = (77*r + 150*g + 29*b) / 65280.0f;',
  '            sum_L  += L;',
  '            sum_sq += L * L;',
  '            count++;',
  '        }',
  '    }',
  '    result.avg_luminance = sum_L / count;',
  '    float mean_sq = sum_sq / count;',
  '    result.variance = sqrtf(mean_sq - result.avg_luminance*result.avg_luminance);',
  '    return result;',
  '}',
],'luminance sampling'));

children.push(H2('Step 2 — Material Parameter Calculation'));
children.push(Body('Given the wallpaper sample and the current system state (GPU tier, power mode, accessibility settings), the compositor calculates the exact parameters for the material. These parameters are then used every frame to render the window.'));
children.push(...Code([
  'void recalculate_material(surface_t *s) {',
  '    wallpaper_sample_t wp = sample_wallpaper_behind(s);',
  '',
  '    // Base values depend on material type',
  '    float base_blur    = material_base_blur[s->material];   // 15, 8, 20, 0, 5',
  '    float base_opacity = material_base_opacity[s->material];// 0.15, 0.35, 0.10, 1.0, 0.45',
  '',
  '    // ── Luminance adjustments ──────────────────────────────────',
  '    // High luminance (bright wallpaper): reduce transparency, add tint',
  '    float lum_factor = wp.avg_luminance; // 0.0 to 1.0',
  '    s->blur_radius   = base_blur - lum_factor * 8.0f;      // Bright: less blur needed',
  '    s->tint_strength = base_opacity + lum_factor * 0.25f;  // Bright: more tint',
  '',
  '    // High variance (busy wallpaper): more blur to hide noise',
  '    s->blur_radius  += wp.variance * 12.0f;',
  '',
  '    // ── Power mode adjustments ────────────────────────────────',
  '    if (power_mode == POWER_SAVER) {',
  '        s->blur_radius   = 0;',
  '        s->tint_strength = 0.9f;',
  '        s->material      = MATERIAL_SOLID;',
  '        return;',
  '    }',
  '',
  '    // ── GPU tier adjustments ──────────────────────────────────',
  '    if (gpu_tier < GPU_TIER_2) {',
  '        s->blur_radius  = 0;         // No blur on software renderer',
  '        s->tint_strength = 0.6f;',
  '    } else if (gpu_tier == GPU_TIER_2) {',
  '        s->blur_radius *= 0.6f;      // Reduced blur on medium GPU',
  '    }',
  '',
  '    // ── Clamp final values ────────────────────────────────────',
  '    if (s->blur_radius < 0) s->blur_radius = 0;',
  '    if (s->blur_radius > 25) s->blur_radius = 25;',
  '    if (s->tint_strength > 0.95f) s->tint_strength = 0.95f;',
  '',
  '    // ── Light color from wallpaper ────────────────────────────',
  '    // Warm wallpaper: warm highlights. Cool wallpaper: cool reflections.',
  '    s->light_color = wp.dominant;',
  '    s->ambient_l   = 0.15f + wp.avg_luminance * 0.10f;',
  '    s->specular_l  = (1.0f - wp.avg_luminance) * 0.20f;',
  '}',
],'material parameter calculation'));

children.push(H2('The Blur System — Five Layers'));
children.push(Body('There are five independent blur layers in CharisOS, each with its own quality level, update frequency, and GPU budget. They are NOT the same blur applied at different strengths — they are separate processes with separate caches.'));
[
  ['Wallpaper Blur', C.blue, 'Updated ONLY when the wallpaper changes. This is the most expensive blur and is run asynchronously during idle time. The result is cached as a pre-blurred wallpaper texture. ALL window blur operations start from this cached texture, not the original wallpaper.'],
  ['Window Background Blur', C.teal, 'Per-window. Uses the cached pre-blurred wallpaper as the source (much cheaper than blurring the original). Updated when the window moves. Radius: 8-20px depending on material and luminance. This is the frosted glass effect.'],
  ['Menu/Overlay Blur', C.amber, 'For context menus, dropdowns, tooltips. Very small radius (4-6px). Uses a pre-cached snapshot of the area taken when the menu opens. NOT updated while the menu is visible — menus appear over a static blur snapshot. Fast.'],
  ['Widget Blur (Liquid Glass)', C.purple, 'For the Dock and Taskbar. Uses a low-resolution (1/4 scale) blur of the entire desktop below the widget. Updated every 4 frames, not every frame. The lower resolution is imperceptible at full screen.'],
  ['Motion Blur (Future)', C.gray4, 'Window drag trails. Very subtle. Only enabled on Tier 3 GPU. Single 2-frame accumulation buffer. If FPS drops below 45Hz, automatically disabled.'],
].forEach(([name,color,desc])=>{
  children.push(new Paragraph({
    ...sp(80,20),
    border:{ left:{ style:BorderStyle.SINGLE, size:8, color } },
    children:[new TextRun({ text:`  ${name}`, font:F.head, size:23, bold:true, color })],
  }));
  children.push(new Paragraph({ ...sp(0,80), ...ind(240),
    children:[new TextRun({ text:desc, font:F.body, size:21, color:C.gray5 })],
  }));
});

children.push(H2('Software Gaussian Blur — The Algorithm'));
children.push(Body(
  'For GPU Tier 1 (software only), blur is implemented in C as a two-pass separable Gaussian filter. One horizontal pass plus one vertical pass equals a full 2D Gaussian. Separability reduces the complexity from O(r²) per pixel to O(r), making it viable on CPU.'));
children.push(...Code([
  '// Precomputed Gaussian weights for blur radius 0-20',
  '// gaussian(x, sigma) = exp(-x*x / (2*sigma*sigma)) / (sigma * sqrt(2*pi))',
  '// sigma = radius / 3.0',
  '',
  'static void gaussian_weights(float *out, int radius) {',
  '    float sigma = radius / 3.0f;',
  '    float sum   = 0;',
  '    int   n     = 2 * radius + 1;',
  '    for (int i = 0; i < n; i++) {',
  '        int   x = i - radius;',
  '        out[i]  = expf(-(float)(x*x) / (2*sigma*sigma));',
  '        sum    += out[i];',
  '    }',
  '    for (int i = 0; i < n; i++) out[i] /= sum;  // Normalize',
  '}',
  '',
  '// Two-pass separable blur. dst and tmp must be pre-allocated.',
  '// Work at half resolution: call with half-size buffers, then scale up.',
  'void blur_image(uint32_t *src, uint32_t *tmp, uint32_t *dst,',
  '                int w, int h, int radius) {',
  '    float weights[64];',
  '    gaussian_weights(weights, radius);',
  '    int n = 2 * radius + 1;',
  '',
  '    // Pass 1: horizontal blur src -> tmp',
  '    for (int y = 0; y < h; y++) {',
  '        for (int x = 0; x < w; x++) {',
  '            float r=0,g=0,b=0,a=0;',
  '            for (int k = 0; k < n; k++) {',
  '                int sx = x + k - radius;',
  '                if (sx < 0) sx = 0;',
  '                if (sx >= w) sx = w-1;',
  '                uint32_t px = src[y*w + sx];',
  '                float    wt = weights[k];',
  '                a += wt * ((px>>24)&0xFF);',
  '                r += wt * ((px>>16)&0xFF);',
  '                g += wt * ((px>> 8)&0xFF);',
  '                b += wt * ( px     &0xFF);',
  '            }',
  '            tmp[y*w+x] = ((uint32_t)a<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b;',
  '        }',
  '    }',
  '',
  '    // Pass 2: vertical blur tmp -> dst',
  '    for (int x = 0; x < w; x++) {',
  '        for (int y = 0; y < h; y++) {',
  '            float r=0,g=0,b=0,a=0;',
  '            for (int k = 0; k < n; k++) {',
  '                int sy = y + k - radius;',
  '                if (sy < 0) sy = 0;',
  '                if (sy >= h) sy = h-1;',
  '                uint32_t px = tmp[sy*w + x];',
  '                float    wt = weights[k];',
  '                a += wt * ((px>>24)&0xFF);',
  '                r += wt * ((px>>16)&0xFF);',
  '                g += wt * ((px>> 8)&0xFF);',
  '                b += wt * ( px     &0xFF);',
  '            }',
  '            dst[y*w+x] = ((uint32_t)a<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b;',
  '        }',
  '    }',
  '}',
  '',
  '// PERFORMANCE NOTE:',
  '// At 1920x1080, radius=10, this runs ~500ms on a single CPU core.',
  '// Strategy: run at 1/4 resolution (480x270), then bilinear-upscale.',
  '// That is 16x faster. Blur looks identical at screen viewing distance.',
  '// Cache the result. Only rerun when wallpaper or window position changes.',
],'two-pass Gaussian blur implementation'));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 4 — ANIMATION ENGINE
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 4 — SPRING PHYSICS ANIMATION ENGINE', C.purple));
children.push(Body(
  'Animations in CharisOS never use linear interpolation (lerp). Linear animations look robotic: they start instantly at full speed and stop abruptly. Spring physics produces animations that feel alive: they have inertia, they overshoot slightly (or not, depending on damping), and they settle naturally. More importantly, they handle interruptions gracefully — if you click a window while it is animating open, the animation does not snap or restart; it responds from its current velocity.',
  80,120));

children.push(H2('The Physics Model'));
children.push(Body('A spring-damper system models the animation. The value being animated (opacity, x-position, scale, blur_radius) is treated as a mass attached to a spring, with a damping force that prevents infinite oscillation.'));
children.push(...Code([
  '// Spring-damper integration (semi-implicit Euler method)',
  '// This runs for EVERY animated property on EVERY frame.',
  '',
  'typedef struct {',
  '    float value;          // Current value',
  '    float velocity;       // Current velocity',
  '    float target;         // Target value we spring toward',
  '    float stiffness;      // Spring constant k (higher = faster)',
  '    float damping;        // Damping coefficient d',
  '    bool  active;         // false = settled, skip update',
  '} spring_t;',
  '',
  '// dt = time since last frame in seconds (e.g. 0.01667 for 60Hz)',
  'bool spring_update(spring_t *s, float dt) {',
  '    if (!s->active) return false;',
  '',
  '    float displacement = s->value - s->target;',
  '    float spring_force = -s->stiffness * displacement;',
  '    float damper_force = -s->damping   * s->velocity;',
  '    float acceleration  = spring_force + damper_force;',
  '',
  '    s->velocity += acceleration * dt;',
  '    s->value    += s->velocity  * dt;',
  '',
  '    // Check if the spring has settled (both velocity and displacement are tiny)',
  '    if (fabsf(s->velocity)              < 0.01f &&',
  '        fabsf(s->value - s->target) < 0.01f) {',
  '        s->value    = s->target;  // Snap to exact target',
  '        s->velocity = 0;',
  '        s->active   = false;      // No more updates needed',
  '        return true;              // Settled',
  '    }',
  '    return false;',
  '}',
  '',
  '// Presets for different animation types:',
  '// Snappy (window open/close, button press):',
  '//   stiffness=500, damping=35  -- fast, no overshoot',
  '// Bouncy (notification slide-in):',
  '//   stiffness=300, damping=18  -- overshoots slightly, settles',
  '// Smooth (drag, resize):',
  '//   stiffness=200, damping=28  -- follows cursor without lag',
  '// Gentle (wallpaper breathing):',
  '//   stiffness=20,  damping=10  -- very slow, continuous motion',
],'spring physics model'));

children.push(H2('Animated Properties Per Window'));
children.push(Body('Each window maintains a set of spring animators, one per animated property. They update independently on every frame. When a window opens, all springs activate simultaneously, creating a coordinated multi-property animation.'));
children.push(...Code([
  'typedef struct {',
  '    spring_t opacity;       // 0.0 → 1.0 on open',
  '    spring_t scale_x;       // 0.95 → 1.0 on open',
  '    spring_t scale_y;       // 0.95 → 1.0 on open',
  '    spring_t blur_in;       // 20px → 0px on open (incoming blur)',
  '    spring_t shadow_spread; // 0 → 12px on open',
  '    spring_t x;             // Screen X position (for drag animation)',
  '    spring_t y;             // Screen Y position (for drag animation)',
  '    spring_t corner_radius; // 8 → target_radius on open',
  '} window_animators_t;',
  '',
  '// When a window opens:',
  'void window_animate_open(window_t *w) {',
  '    spring_set(&w->anim.opacity,       0.0f, 1.0f,  500, 35);',
  '    spring_set(&w->anim.scale_x,       0.95f,1.0f,  500, 35);',
  '    spring_set(&w->anim.scale_y,       0.95f,1.0f,  500, 35);',
  '    spring_set(&w->anim.blur_in,       20.0f,0.0f,  300, 28);',
  '    spring_set(&w->anim.shadow_spread, 0.0f, 12.0f, 400, 30);',
  '    spring_set(&w->anim.corner_radius, 4.0f, 12.0f, 400, 30);',
  '}',
  '',
  '// When a window closes:',
  'void window_animate_close(window_t *w) {',
  '    spring_set(&w->anim.opacity,       w->anim.opacity.value, 0.0f, 400, 30);',
  '    spring_set(&w->anim.scale_x,       1.0f, 0.93f, 400, 30);',
  '    spring_set(&w->anim.scale_y,       1.0f, 0.93f, 400, 30);',
  '    spring_set(&w->anim.shadow_spread, 12.0f,0.0f,  400, 30);',
  '    // Destroy window when opacity spring settles at 0',
  '}',
],'per-window animation system'));

children.push(H2('The Animation Timeline Manager'));
children.push(Body(
  'Springs handle ongoing properties, but some animations are one-shot events: a notification appears and dismisses after 4 seconds, an icon bounces once when clicked, a workspace slides and stops. These are handled by the Timeline Manager.'));
children.push(...Code([
  'typedef void (*timeline_cb_t)(void *ctx);',
  '',
  'typedef struct timeline {',
  '    float          duration;    // Seconds',
  '    float          elapsed;     // Seconds elapsed',
  '    timeline_cb_t  on_tick;     // Called each frame with (elapsed/duration)',
  '    timeline_cb_t  on_complete; // Called when elapsed >= duration',
  '    void          *ctx;         // User context pointer',
  '    bool           active;',
  '    struct timeline *next;',
  '} timeline_t;',
  '',
  '// Example: notification auto-dismiss after 4 seconds',
  'void show_notification(const char *text) {',
  '    notification_t *n = create_notification(text);',
  '    window_animate_open(n->window);',
  '',
  '    // After 3.5 seconds: begin fade-out',
  '    timeline_after(3.5f, notification_begin_fadeout, n);',
  '}',
  '',
  '// Example: dock icon bounce on app launch',
  'void dock_icon_bounce(dock_item_t *item) {',
  '    timeline_t *t = timeline_create(0.5f);',
  '    t->on_tick = (cb) => {',
  '        float p = t->elapsed / t->duration;    // 0.0 to 1.0',
  '        float offset = sinf(p * 3.14159f) * 12.0f; // Half sine: rise and fall',
  '        item->y_offset = -offset;',
  '    };',
  '}',
],'animation timeline manager'));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 5 — LIGHTING ENGINE
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 5 — LIGHTING ENGINE', C.teal));
children.push(Body(
  'The lighting engine makes windows feel like physical objects placed on a surface in a real space, lit by the same ambient light as the background behind them. It does this with three components per window: ambient light (the overall fill), a specular reflection (the bright edge highlight), and a rim light (a thin glow along the focused window\'s edge).',
  80,120));

children.push(H2('Light Source: The Wallpaper'));
children.push(Body(
  'The wallpaper is the only light source in CharisOS. Every window derives its lighting color from the wallpaper pixels behind and around it. A sunset wallpaper produces warm orange highlights. A deep space wallpaper produces cool blue-violet reflections. This is what makes the system feel coherent rather than arbitrary.'));
children.push(...Code([
  '// Sample a region of the wallpaper and extract its light properties.',
  '// Called once per window when wallpaper or position changes.',
  'light_t extract_wallpaper_light(int x, int y, int w, int h) {',
  '    // Sample a 16x16 grid of pixels for speed',
  '    int step_x = w / 16;',
  '    int step_y = h / 16;',
  '    float sum_r=0, sum_g=0, sum_b=0;',
  '    int   count = 0;',
  '',
  '    for (int py = y; py < y+h; py += step_y) {',
  '        for (int px = x; px < x+w; px += step_x) {',
  '            uint32_t pixel = wallpaper[py * screen_w + px];',
  '            sum_r += (pixel >> 16) & 0xFF;',
  '            sum_g += (pixel >>  8) & 0xFF;',
  '            sum_b +=  pixel        & 0xFF;',
  '            count++;',
  '        }',
  '    }',
  '',
  '    light_t light;',
  '    light.r = sum_r / count / 255.0f;',
  '    light.g = sum_g / count / 255.0f;',
  '    light.b = sum_b / count / 255.0f;',
  '    light.L = 0.2126f*light.r + 0.7152f*light.g + 0.0722f*light.b;',
  '',
  '    // Warm/cool classification',
  '    light.warm_cool = (light.r - light.b) * 2.0f; // -1 cool, +1 warm',
  '    return light;',
  '}',
],'wallpaper light extraction'));

children.push(H2('The Four Lighting Components'));
children.push(Bul([{text:'Ambient Fill:', bold:true, color:C.teal}, ' A very subtle tint of the wallpaper\'s dominant color applied uniformly across the entire window surface. Intensity: 3-8%. Almost invisible. Makes the window "belong" to its environment.']));
children.push(Bul([{text:'Top Edge Highlight:', bold:true, color:C.teal}, ' A 1-3px bright gradient at the top edge of the window, tinted with the wallpaper\'s warm/cool light. This simulates light hitting the top edge of a glass panel from above. Most visible on focused windows.']));
children.push(Bul([{text:'Corner Shine:', bold:true, color:C.teal}, ' A very small bright spot at the top-left or top-right corner (depending on estimated light direction from wallpaper). More visible when the wallpaper has a clear directional light source (like a sun).']));
children.push(Bul([{text:'Rim Light (focused only):', bold:true, color:C.teal}, ' A very thin (1px), semi-transparent colored line along ALL four edges of the focused window. Color: 15% opacity white-tinted-with-light-color. Makes the focused window "glow" subtly without being distracting. Unfocused windows: no rim light.']));

children.push(H2('Mouse-Reactive Specular Reflection'));
children.push(Body(
  'When the mouse moves over a window, the window\'s top highlight shifts very slightly in the direction of the mouse position relative to the window center. The shift is almost invisible — a maximum of 3px translation and 2% brightness change. The effect makes the glass feel like it has a physical surface that responds to viewing angle. This is the "barely there" effect that users feel but cannot explain.'));
children.push(...Code([
  '// Called every time the mouse moves',
  'void lighting_update_specular(window_t *w, int mouse_x, int mouse_y) {',
  '    // Normalized mouse position within window: -1.0 to +1.0',
  '    float nx = ((mouse_x - w->x) / (float)w->width)  * 2.0f - 1.0f;',
  '    float ny = ((mouse_y - w->y) / (float)w->height) * 2.0f - 1.0f;',
  '',
  '    // Clamp to window region (0 if mouse is outside)',
  '    if (nx < -1 || nx > 1 || ny < -1 || ny > 1) {',
  '        w->specular_offset_x = 0; w->specular_offset_y = 0;',
  '        return;',
  '    }',
  '',
  '    // Very subtle: max 3px offset',
  '    w->specular_offset_x = (int)(nx * 3.0f);',
  '    w->specular_offset_y = (int)(ny * 3.0f);',
  '    w->dirty = true;  // Flag for recomposite',
  '}',
],'mouse-reactive specular'));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 6 — TEXT RENDERING
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 6 — TEXT RENDERING PIPELINE', C.green));
children.push(Body(
  'CharisOS currently uses PSF2 bitmap fonts (Phase 5). That is correct for the early kernel — they are simple, fast, and have zero dependencies. But a real desktop OS needs vector fonts with optical sizing, kerning, ligatures, and hinting. This section describes the complete migration path from PSF2 to a full professional text stack, and exactly when each step is implemented.',
  80,120));

children.push(H2('Phase-by-Phase Text Stack Evolution'));

children.push(H3('Phase 5 (Done) — PSF2 Bitmap Fonts'));
children.push(Body('The kernel console uses PSF2 bitmap fonts embedded directly in the kernel binary. Each glyph is a fixed-size bitmap (typically 8x16 or 16x32 pixels). No shaping, no kerning, no ligatures. Monospace only. This is correct for the kernel console and will remain the emergency fallback forever.'));

children.push(H3('Phase 9 — FreeType Integration'));
children.push(Body(
  'FreeType is a C library that rasterizes TrueType and OpenType vector font files. It has no dependencies except libc (which CharisOS implements). Integration steps: port FreeType to compile with x86_64-elf-gcc (-ffreestanding), provide a kmalloc/kfree allocator wrapper, load font files from the VFS, and render glyphs to alpha-channel bitmaps. The result: any TTF/OTF font works, at any size, with sub-pixel hinting.'));

children.push(H3('Phase 10 — Glyph Atlas'));
children.push(Body(
  'Rasterizing every glyph on every draw call is too slow. Instead: the first time a glyph is needed at a given size, FreeType rasterizes it and stores it in a glyph atlas — a large texture (typically 2048x2048 pixels) that acts as a cache. Subsequent draws just blit from the atlas. The atlas is partitioned by font family + size + weight. LRU eviction when full.'));

children.push(H3('Phase 11 — HarfBuzz + Complex Shaping'));
children.push(Body(
  'HarfBuzz is a text shaping engine. It handles: ligatures (fi, ffi, fl become single glyphs), kerning (spacing adjustments between specific letter pairs), right-to-left text (Arabic, Hebrew), and script-specific rules (Devanagari, Thai). Without HarfBuzz, text looks technically correct but feels wrong in ways that are hard to articulate. HarfBuzz also has no dependencies except a standard C library.'));

children.push(H3('Phase 14 — Subpixel Rendering + Variable Fonts'));
children.push(Body(
  'Subpixel rendering (also called LCD hinting) uses the fact that each screen pixel contains three sub-pixels (R, G, B) to render text at 3x horizontal resolution. This makes text at small sizes much sharper on LCD monitors. Variable fonts (OpenType Format variable fonts) allow a single font file to contain a continuous design space across weight, width, and optical size — the OS automatically chooses the correct weight for the current DPI and font size.'));

children.push(H2('Typography System Rules'));
children.push(Body('The CharisUI typography system uses a fixed scale based on powers of 1.25 (a "major third" scale). Every font size in the OS is one of these values:'));
children.push(...Code([
  '// Typography scale (in CSS-equivalent px, actual px depends on DPI)',
  '// Scale factor: 1.25 (major third)',
  '',
  'FONT_XS   = 10px  // System labels, timestamps',
  'FONT_SM   = 12px  // Secondary text, captions, status bar',
  'FONT_BASE = 14px  // Default body text',
  'FONT_MD   = 16px  // Window titles, list items',
  'FONT_LG   = 20px  // Dialog headings, section headers',
  'FONT_XL   = 24px  // App names, large UI elements',
  'FONT_2XL  = 32px  // Lock screen clock (hour:minute)',
  'FONT_3XL  = 64px  // Full-screen clock, splash text',
  '',
  '// Weight system',
  'WEIGHT_LIGHT   = 300',
  'WEIGHT_REGULAR = 400  // Default',
  'WEIGHT_MEDIUM  = 500  // Buttons, labels',
  'WEIGHT_SEMIBOLD= 600  // Window titles',
  'WEIGHT_BOLD    = 700  // Alerts, headings',
  '',
  '// DPI-adaptive weight rule:',
  '// Under 120 DPI: use MEDIUM weight for body (thicker = more readable)',
  '// 120-220 DPI:   use REGULAR weight',
  '// Over 220 DPI:  use LIGHT weight (thinner = better on high-res display)',
],'typography scale'));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 7 — CharisUI WIDGET FRAMEWORK
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 7 — CHARISMUS WIDGET FRAMEWORK', C.amber));
children.push(Body(
  'CharisUI is the application-facing UI framework. It is the layer that turns pixels into buttons, windows, dropdowns, text fields, sliders, and scrollable lists. Every application in CharisOS uses CharisUI — there is no alternative. This guarantees visual consistency across every app because every component comes from the same material definitions, the same spacing scale, the same animation curves, and the same accessibility rules.',
  80,120));

children.push(H2('The Widget Hierarchy'));
children.push(...Code([
  'Widget                          -- Base: position, size, margins, state machine',
  '├── Container                  -- Has layout: flex/grid/absolute/stack',
  '│   ├── Window                 -- Titlebar, content area, decorations, resize',
  '│   ├── Panel                  -- Background, padding, border, material',
  '│   ├── ScrollView             -- Clipped overflow, scroll bar, momentum physics',
  '│   └── Stack                  -- Layered children (for overlay patterns)',
  '├── Display',
  '│   ├── Label                  -- Text, typography, truncation, selection',
  '│   ├── Icon                   -- Vector icon with outline/filled/two-tone modes',
  '│   ├── Image                  -- Pixel buffer, aspect ratio, format conversion',
  '│   ├── ProgressBar            -- Determinate and indeterminate with animation',
  '│   └── Separator              -- Horizontal/vertical rule with spacing',
  '├── Input',
  '│   ├── Button                 -- 5 states: normal, hover, pressed, focused, disabled',
  '│   ├── TextField              -- Text input, cursor, selection, placeholder',
  '│   ├── Slider                 -- Value, range, step, thumb animation',
  '│   ├── Toggle                 -- Boolean switch, thumb spring animation',
  '│   ├── Dropdown               -- Filtered list, keyboard search, animations',
  '│   ├── Checkbox               -- Three states: unchecked, checked, indeterminate',
  '│   └── RadioButton            -- Group-aware, exclusive selection',
  '├── Navigation',
  '│   ├── TabBar                 -- Tabs with animated active indicator',
  '│   ├── Breadcrumb             -- Path navigation, truncation on overflow',
  '│   ├── Sidebar                -- Collapsible tree navigation',
  '│   └── Toolbar                -- Icon buttons, spacers, overflow menu',
  '└── Overlay',
  '    ├── Dialog                 -- Modal, with animated backdrop + Crystal Glass',
  '    ├── Notification           -- Slide from top-right, auto-dismiss, interactive',
  '    ├── Tooltip                -- Hover delay (400ms), arrow, Acrylic Mist',
  '    └── ContextMenu            -- Right-click, keyboard navigation, sub-menus',
],'CharisUI widget hierarchy'));

children.push(H2('The Widget State Machine'));
children.push(Body('Every interactive widget has a state machine with five states. Transitions between states trigger animations. The state machine is centralized in the Widget base class.'));
children.push(...Code([
  '// Widget states',
  'WIDGET_NORMAL    -- Default: no special styling',
  'WIDGET_HOVER     -- Mouse is over the widget',
  'WIDGET_PRESSED   -- Mouse button down while hovering',
  'WIDGET_FOCUSED   -- Keyboard focus (Tab navigation)',
  'WIDGET_DISABLED  -- Not interactive',
  '',
  '// State transitions trigger visual changes via springs:',
  '// NORMAL  → HOVER:   background lightens by 8%, cursor changes',
  '// HOVER   → PRESSED: background darkens by 12%, scale spring to 0.97',
  '// PRESSED → HOVER:   scale spring returns to 1.0, slight ripple',
  '// ANY     → FOCUSED: border-color animates to focus-ring-color (Tier 3)',
  '// ANY     → DISABLED:opacity spring to 0.38 (same as Material Design rule)',
  '',
  '// Loading state (optional per-widget):',
  'WIDGET_LOADING -- Spinner overlay, disabled input, content dimmed to 50%',
],'widget state machine'));

children.push(H2('The Layout Engine'));
children.push(Body(
  'CharisUI uses a single-pass layout engine similar to CSS Flexbox but implemented entirely in C. Containers specify a direction (horizontal or vertical), alignment, justify rules, gap, and padding. Children specify min/max sizes, flex-grow, and flex-shrink factors. The engine runs top-down: each container calculates its own size, then distributes space to children.'));
children.push(...Code([
  'typedef struct {',
  '    flex_direction_t direction;  // ROW or COLUMN',
  '    justify_t        justify;    // START, CENTER, END, SPACE_BETWEEN, SPACE_AROUND',
  '    align_t          align;      // STRETCH, START, CENTER, END',
  '    int              gap;        // Pixels between children',
  '    int              pad_top, pad_right, pad_bottom, pad_left;',
  '} layout_flex_t;',
  '',
  'typedef struct {',
  '    int   min_w,  min_h;',
  '    int   max_w,  max_h;   // -1 = no limit',
  '    float flex_grow;       // 0 = fixed size, 1+ = expands',
  '    float flex_shrink;     // 0 = never shrink, 1 = normal',
  '    align_t self_align;    // Override parent align for this child',
  '} widget_size_t;',
  '',
  '// Layout runs in two phases:',
  '// Phase 1 (measure): children report their desired sizes',
  '// Phase 2 (arrange): container distributes available space',
  '',
  'void layout_flex_arrange(container_t *c, int avail_w, int avail_h) {',
  '    // Step 1: Give all children their minimum size',
  '    // Step 2: Calculate total flex factor',
  '    // Step 3: Distribute remaining space proportionally to flex_grow',
  '    // Step 4: Set final x,y of each child',
  '}',
],'layout engine data structures'));

children.push(H2('The Theme Engine'));
children.push(Body(
  'The theme engine provides a single source of truth for all visual values: colors, spacing, corner radii, font sizes, shadow parameters, animation curves, and material assignments. All widgets pull values from the theme — no widget has hardcoded colors or sizes. This means a high-contrast accessibility theme, a warm "sepia" theme, or a future "neon" theme can be applied by swapping one data structure.'));
children.push(...Code([
  '// The CharisOS system theme (default dark)',
  'typedef struct {',
  '    // Colors',
  '    color_t accent;          // Primary interactive color (default: #3A86FF)',
  '    color_t accent_hover;    // Lighter accent on hover',
  '    color_t accent_pressed;  // Darker accent on press',
  '    color_t bg_primary;      // Main background (default: #111120)',
  '    color_t bg_secondary;    // Panel/card background',
  '    color_t bg_tertiary;     // Input field background',
  '    color_t text_primary;    // Main text (default: #F0F0F8)',
  '    color_t text_secondary;  // Labels, captions (default: #8888AA)',
  '    color_t text_disabled;   // Disabled state text',
  '    color_t border_default;  // Default border/divider color',
  '    color_t focus_ring;      // Keyboard focus indicator color',
  '    color_t destructive;     // Red: delete, warning, error',
  '    color_t success;         // Green: confirm, done',
  '',
  '    // Spacing scale (4-base)',
  '    int space[8];            // 2, 4, 8, 12, 16, 24, 32, 48',
  '',
  '    // Corner radius',
  '    int radius_sm;           // 6px  (buttons, inputs)',
  '    int radius_md;           // 12px (cards, panels)',
  '    int radius_lg;           // 18px (windows)',
  '    int radius_xl;           // 24px (dialogs, notifications)',
  '',
  '    // Typography',
  '    font_ref_t font_default;',
  '    font_ref_t font_mono;',
  '    int        base_font_size;',
  '',
  '    // Materials for each component type',
  '    material_type_t material_window;     // FROSTED',
  '    material_type_t material_dialog;     // CRYSTAL',
  '    material_type_t material_menu;       // ACRYLIC_MIST',
  '    material_type_t material_dock;       // LIQUID',
  '',
  '    // Animation presets',
  '    spring_preset_t spring_default;     // stiffness=400, damping=30',
  '    spring_preset_t spring_bounce;      // stiffness=300, damping=18',
  '    spring_preset_t spring_smooth;      // stiffness=200, damping=28',
  '',
  '    // Feature flags',
  '    bool enable_blur;',
  '    bool enable_animations;',
  '    bool enable_reflections;',
  '    bool enable_transparency;',
  '    bool reduce_motion;          // Accessibility',
  '    bool high_contrast;          // Accessibility',
  '} charis_theme_t;',
],'theme engine data structure'));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 8 — DESKTOP ENVIRONMENT
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 8 — DESKTOP ENVIRONMENT', C.blue));
children.push(Body(
  'The desktop environment is the collection of persistent UI elements that are always visible: the wallpaper, the taskbar, the system tray, the clock, the launcher, and the workspace manager. Each of these is an application built on CharisUI that has been granted the "desktop shell" capability by the kernel — meaning it can draw below all other windows and cannot be closed by normal means.',
  80,120));

children.push(H2('The Taskbar'));
children.push(Body(
  'The CharisOS taskbar floats at the bottom of the screen. It is not attached to the screen edge — there is a 12px gap below it. It uses the Liquid Glass material. It contains: a system launcher button (left), running application icons with dock badges (center), and a system tray with clock and quick settings (right).'));
children.push(Bul([{text:'Floating gap:', bold:true, color:C.blue}, ' 12px below the taskbar, the wallpaper is visible. The taskbar appears to float on the desktop, not sit on an edge.']));
children.push(Bul([{text:'App icons:', bold:true, color:C.blue}, ' On hover, icons rise 6px (spring: stiffness=300, damping=20). On click, icons compress 4px then release (spring: stiffness=600, damping=35). Running apps show a 3px dot below their icon.']));
children.push(Bul([{text:'Active app indicator:', bold:true, color:C.blue}, ' The currently focused app\'s icon has a slightly brighter dot and a very subtle backlit glow behind its icon.']));
children.push(Bul([{text:'Auto-hide (optional):', bold:true, color:C.blue}, ' When enabled, the taskbar slides off-screen (spring animation, 200ms) when a fullscreen window is active. Slides back in when the mouse moves to the bottom edge.']));

children.push(H2('The Launcher'));
children.push(Body(
  'The launcher is not a traditional start menu. It is a focused, intelligent surface that appears in the center of the screen, Crystal Glass material, blurring everything behind it. It has three zones: a search field at the top (AI-powered), a pinned apps grid in the middle, and recent files at the bottom.'));
children.push(Bul([{text:'Appearance animation:', bold:true, color:C.amber}, ' Scale from 0.92 to 1.0, opacity 0 to 1, blur 20px to 0px, all with the snappy spring preset. Appears in 120ms.']));
children.push(Bul([{text:'Search:', bold:true, color:C.amber}, ' Instant-search across installed apps, recent files, settings, and eventually natural language commands. Results update within the same frame the user types.']));
children.push(Bul([{text:'Pinned apps:', bold:true, color:C.amber}, ' A 6-column grid of large (48x48px) vector icons. Drag-to-rearrange with physics: dragged icon follows cursor, other icons spring-animate out of the way.']));
children.push(Bul([{text:'Dismiss:', bold:true, color:C.amber}, ' Click outside, press Escape, or press the launcher key again. Reverse of open animation: scale to 0.92, opacity to 0, 80ms.']));

children.push(H2('Workspaces'));
children.push(Body(
  'CharisOS supports multiple virtual desktops (workspaces). Each workspace has its own wallpaper, its own window layout, and its own icon arrangement. Switching between workspaces triggers a "camera slide" animation: the current workspace slides out to the left, the new workspace slides in from the right (or reverse). The animation is a spring with stiffness=400, damping=40 — fast, no overshoot.'));
children.push(Body(
  'The workspace overview (mission control equivalent) shows all workspaces as scaled-down live thumbnails. Hover over a thumbnail to see a full-resolution preview of that workspace\'s content. Drag windows between workspace thumbnails to move them.'));

children.push(H2('The Wallpaper Engine'));
children.push(Body(
  'The wallpaper is not static. It uses a very slow animation — the "breathing" effect. The wallpaper moves by 0.5% of its resolution (about 10 pixels at 1920x1080) in a slow sinusoidal pattern with a period of 20-30 seconds. The effect is barely visible but makes the desktop feel like a living environment rather than a static screenshot.'));
children.push(...Code([
  '// Wallpaper breathing animation',
  '// The wallpaper is loaded at 102% of screen resolution to allow movement',
  '',
  'static float breathe_x = 0.0f;  // Current X offset (pixels)',
  'static float breathe_y = 0.0f;  // Current Y offset (pixels)',
  'static float breathe_t = 0.0f;  // Time accumulator (seconds)',
  '',
  'void wallpaper_breathe_update(float dt) {',
  '    breathe_t += dt;',
  '    // Two slow sine waves at different frequencies for organic feel',
  '    float max_offset = screen_w * 0.005f;  // 0.5% of screen width',
  '    breathe_x = sinf(breathe_t * 0.08f) * max_offset',
  '              + sinf(breathe_t * 0.053f) * max_offset * 0.4f;',
  '    breathe_y = cosf(breathe_t * 0.063f) * max_offset * 0.6f;',
  '',
  '    // Invalidate the wallpaper region (triggers blur cache update for all windows)',
  '    damage_add(0, 0, screen_w, screen_h);',
  '}',
],'wallpaper breathing animation'));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 9 — THE CURSOR
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 9 — CURSOR SYSTEM', C.orange));
children.push(Body(
  'The cursor in CharisOS is not a static bitmap. It is a small physics simulation. It has inertia (slight lag behind the actual mouse position), a hover glow that activates when over interactive elements, a click-compression animation, and magnetic snapping that pulls it toward button centers when nearby.',
  80,120));

children.push(...Code([
  'typedef struct {',
  '    // Real mouse position (from PS/2 driver)',
  '    int      real_x, real_y;',
  '',
  '    // Animated display position (springs toward real position)',
  '    spring_t display_x;    // stiffness=800, damping=40 (very snappy, barely lags)',
  '    spring_t display_y;',
  '',
  '    // Glow intensity (activates near interactive elements)',
  '    spring_t glow;         // stiffness=200, damping=25',
  '    color_t  glow_color;   // Accent color of hovered element',
  '',
  '    // Click animation (cursor compresses on press)',
  '    spring_t press_scale;  // 1.0 → 0.8 on press, back to 1.0 on release',
  '',
  '    // Magnetic snap (when within 12px of button center)',
  '    spring_t snap_x;       // Pulls cursor display toward button center',
  '    spring_t snap_y;',
  '    bool     snapping;',
  '',
  '    cursor_shape_t shape;  // ARROW, POINTER, TEXT, CROSSHAIR, WAIT, RESIZE_*',
  '} cursor_state_t;',
  '',
  '// Cursor shapes transition between each other with a 60ms cross-fade.',
  '// The cursor bitmap is never redrawn from scratch -- a set of',
  '// pre-rendered cursor shapes is loaded at boot and blended.',
  '',
  '// Magnetic snap logic:',
  '// For each visible button near the cursor (within 24px of its center):',
  '//   dist = distance(real_x, real_y, button_center_x, button_center_y)',
  '//   if dist < 12px: activate snap, set snap target to button center',
  '//   strength = max(0, 1.0 - dist/12.0) * 0.35  (max 35% pull)',
  '// snapped_x = real_x + (button_center_x - real_x) * strength',
  '// snapped_y = real_y + (button_center_y - real_y) * strength',
],'cursor system'));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 10 — PERFORMANCE SYSTEM
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 10 — PERFORMANCE & FAILURE SYSTEM', C.green));
children.push(Body(
  'The performance system ensures the UI never freezes. It monitors GPU usage and frame times, automatically sheds visual effects when the system is under load, and recovers them when load decreases. The fallback chain also handles complete GPU driver failure.',
  80,120));

children.push(H2('The Quality Tier Ladder'));
children.push(Body('Every effect in CharisOS is assigned to a quality tier. When GPU usage exceeds a threshold, the compositor walks down the ladder, shedding effects one tier at a time until frame time is within budget.'));
children.push(...Code([
  'Quality Tier 5 (Max): 120Hz target',
  '  All effects: full blur, reflections, lighting, specular, mouse-reactive glow,',
  '  cursor physics, wallpaper breathing, window shadows (all 3 layers)',
  '',
  'Quality Tier 4 (High): 60Hz target',
  '  Remove: 120Hz (drops to 60Hz), mouse-reactive specular',
  '  Keep: full blur, reflections, lighting, cursor physics, wallpaper breathing',
  '',
  'Quality Tier 3 (Medium): 60Hz target',
  '  Remove: reflections, wallpaper breathing',
  '  Keep: full blur, ambient lighting only, cursor physics',
  '',
  'Quality Tier 2 (Low): 30Hz target',
  '  Remove: per-window blur (replace with flat tint), cursor physics',
  '  Keep: transparency (tint-only, no blur), basic shadows',
  '',
  'Quality Tier 1 (Minimal): 30Hz target',
  '  Remove: all transparency effects (switch to SOLID SURFACE material)',
  '  Keep: correct layout, correct colors, correct animations (simplified)',
  '',
  'Quality Tier 0 (Emergency): Software renderer, any frame rate',
  '  All effects removed. Solid color windows. Still shows titles, text, cursor.',
  '  This tier activates when GPU driver crashes or fails to respond.',
],'quality tier ladder'));

children.push(H2('The Compositor Crash Recovery'));
children.push(Body(
  'The compositor runs as a privileged kernel process. If it crashes (unhandled exception in the compositor loop), the kernel does NOT bring down all applications. Applications are separate tasks with separate address spaces (Phase 8). The kernel:'));
children.push(Bul([{text:'Step 1:', bold:true, color:C.green}, ' Detects compositor crash via watchdog timer (if the compositor misses 5 consecutive frame ticks, it is considered hung).']));
children.push(Bul([{text:'Step 2:', bold:true, color:C.green}, ' Switches to the Emergency UI immediately — the kernel has a minimal built-in "window list" that can draw window titles, basic decorations, and the cursor without any compositor infrastructure. The screen goes gray but applications keep running.']));
children.push(Bul([{text:'Step 3:', bold:true, color:C.green}, ' Spawns a new compositor process. The compositor reads the current window list and surface buffers (which are owned by applications, not the compositor) and reconstructs the scene. This takes 100-500ms.']));
children.push(Bul([{text:'Step 4:', bold:true, color:C.green}, ' Compositor reconnects to all surfaces and resumes normal rendering. Applications never notice — they kept drawing into their surface buffers the entire time.']));
children.push(Callout(
  'This is identical in concept to how Wayland compositors work (the compositor can crash and restart without killing applications), but implemented entirely in the CharisOS kernel without any external dependencies.',
  C.green));

children.push(H2('GPU Driver Failure Fallback'));
children.push(Body('The renderer backend is a swappable module. The compositor does not call GPU functions directly — it calls through a renderer_ops_t function table (like a vtable). When the GPU driver reports an error or stops responding, the compositor swaps the renderer_ops_t pointer from the GPU renderer to the software renderer without any restart.'));
children.push(...Code([
  'typedef struct {',
  '    void (*present)(void);                           // Flip buffers to screen',
  '    void (*fill_rect)(int x, int y, int w, int h, color_t c);',
  '    void (*blit)(int dx, int dy, surface_t *src, rect_t *clip);',
  '    void (*blur)(surface_t *src, int radius);',
  '    void (*alpha_blend)(surface_t *dst, surface_t *src, float alpha);',
  '    int  (*get_frametime_ms)(void);',
  '} renderer_ops_t;',
  '',
  '// At boot: renderer is set to gpu_ops or software_ops based on GPU detection',
  'renderer_ops_t *renderer = &software_ops; // Safe default',
  '',
  '// During GPU init:',
  'if (gpu_init() == GPU_OK) renderer = &gpu_ops;',
  '',
  '// On GPU error:',
  'void gpu_error_callback(void) {',
  '    renderer = &software_ops;  // Instant atomic swap',
  '    quality_tier = 0;',
  '    damage_add(0, 0, screen_w, screen_h); // Full repaint in software',
  '}',
],'renderer backend abstraction'));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 11 — ACCESSIBILITY
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 11 — ACCESSIBILITY', C.purple));
children.push(Body(
  'Accessibility is not an afterthought. It is designed into the theme engine, the animation system, and the material system from the start. Every accessibility feature is a flag in the theme that overrides visual defaults — no feature requires a separate code path in widgets.',
  80,120));

children.push(Bul([{text:'High Contrast Mode:', bold:true, color:C.purple}, ' Switches to a high-contrast theme with no transparency (all materials → SOLID SURFACE), text rendered in pure white on pure black or vice versa, and focus rings increased from 2px to 4px. No special widget code — entirely a theme swap.']));
children.push(Bul([{text:'Reduce Motion:', bold:true, color:C.purple}, ' When enabled, all spring animations are replaced with instant property changes (spring.stiffness=999999, dt=1). Wallpaper breathing stops. Window open/close animations snap instantly. The system still responds to every interaction — the feedback is immediate, just not animated.']));
children.push(Bul([{text:'Large Text Mode:', bold:true, color:C.purple}, ' Multiplies all font sizes by 1.5x. The layout engine reflows all widgets to accommodate larger text. Minimum tap target size increases from 28px to 44px.']));
children.push(Bul([{text:'Large Cursor Mode:', bold:true, color:C.purple}, ' Cursor rendered at 2x or 3x scale. Magnetic snap range increases from 12px to 24px. Cursor glow intensity doubles.']));
children.push(Bul([{text:'Color Filter Mode:', bold:true, color:C.purple}, ' A full-screen color matrix is applied to the final composited frame at the renderer level. Presets: Deuteranopia (green-blind), Protanopia (red-blind), Tritanopia (blue-blind), Grayscale. The matrix operation is cheap — one matrix multiply per pixel.']));
children.push(Bul([{text:'Keyboard Navigation:', bold:true, color:C.purple}, ' Every interactive widget is in the Tab order. Focus ring (2px, accent color) shows which widget has focus. Arrow keys navigate within complex widgets (menus, lists, grids). Enter/Space activate. Escape closes overlays. This works with zero mouse input.']));

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 12 — PHASE ROADMAP
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 12 — UI PHASE IMPLEMENTATION ROADMAP', C.amber));
children.push(Body('This is the exact order in which the UI systems are built, tied to the existing CharisOS phase numbers. Every phase listed here is a functional milestone: something visible and testable by the end of the phase.', 80, 120));

const phases = [
  ['6.0','Basic Double-Buffer Compositor','NEXT',C.blue,'Damage tracking. Sorted z-order. Basic flat-color window composition. No blur, no transparency. Double back-buffer with memcpy flip. This is the foundational infrastructure everything else builds on.'],
  ['6.1','Spring Animation Engine','NEXT',C.blue,'spring_t, spring_update(), window_animators_t. Window open/close spring animations (opacity + scale). No GPU needed — pure math in the compositor tick.'],
  ['6.2','Software Alpha Blending','NEXT',C.blue,'Per-pixel ARGB alpha compositing in the software renderer. Windows can have fractional opacity. Needed before any glass material can work.'],
  ['6.3','First Glass Material (Software)','NEXT',C.blue,'Tint-only glass: no blur, but solid-color tint with correct opacity over the wallpaper snapshot. Looks like Solid Surface material but adapts tint strength to wallpaper luminance. Visible result: windows look like frosted glass even in software mode.'],
  ['6.4','Wallpaper Breathing + Desktop','IN PROGRESS',C.amber,'Wallpaper engine with breathing animation. Desktop shell process. Taskbar with Liquid Glass material (tint-only first). Clock and system tray.'],
  ['7.0','CharisUI Widgets v1','NEXT',C.blue,'Label, Button, Panel, TextField. Spring-animated hover/press states. The five-state widget state machine. Spacing scale. Theme engine. No layout engine yet — absolute positioning only.'],
  ['7.1','Layout Engine','NEXT',C.blue,'Flex layout (Row + Column only). Children with min/max sizes and flex-grow. Reflow on resize. This makes complex multi-widget UIs manageable.'],
  ['8.0','Per-Process VMM (kernel phase)','NEXT',C.blue,'Required for security: user processes can no longer reach compositor memory.'],
  ['8.1','FreeType Integration','NEXT',C.blue,'Compile FreeType as a freestanding library. TTF/OTF font loading from VFS. Glyph rasterization. Glyph atlas. Replace PSF2 in CharisUI (PSF2 remains in kernel console).'],
  ['9.0','Software Gaussian Blur','FUTURE',C.gray3,'Half-resolution Gaussian blur. Pre-blurred wallpaper cache. Frosted Glass material (real blur, not tint-only). This is the first time windows look like real glass.'],
  ['9.1','Lighting Engine','FUTURE',C.gray3,'Wallpaper light extraction. Top-edge highlight. Corner shine. Ambient fill. Mouse-reactive specular (subtle). Windows now respond to their environment.'],
  ['10.0','PCI GPU Detection','FUTURE',C.gray3,'PCI enumeration finds GPU. VirtIO GPU driver for QEMU. Renderer backend swaps from software to GPU ops. Double-buffering via GPU buffer swap instead of memcpy.'],
  ['10.1','Hardware-Accelerated 2D','FUTURE',C.gray3,'Blitting, fill_rect, alpha_blend all go through GPU. Compositor runs at 60Hz without CPU blur cost. Enables Tier 2 quality (reduced blur) even on QEMU VirtIO.'],
  ['11.0','HarfBuzz + Full Text Stack','FUTURE',C.gray3,'HarfBuzz text shaping. Ligatures, kerning, complex scripts. Variable font support with DPI-adaptive weight.'],
  ['12.0','Complete Adaptive Realistic Glass','FUTURE',C.gray3,'Full luminance sampling on window move. Per-material parameter calculation. All five materials fully implemented. GPU-accelerated blur with quality tier system. Dynamic quality ladder (Tier 5 through Tier 0) with automatic adjustment.'],
  ['13.0','Launcher + Workspace Manager','FUTURE',C.gray3,'Launcher overlay with app search and pinned apps grid. Workspace switching with spring-animated camera slide. Workspace overview with live thumbnails.'],
  ['14.0','Full CharisUI v2','FUTURE',C.gray3,'Complete widget set (all widgets in the hierarchy). Accessibility features. High contrast, reduce motion, keyboard navigation. Drag-and-drop with physics. CharisUI used by all system apps.'],
  ['14.1','System Applications','FUTURE',C.gray3,'Settings app (searchable, 11 sections). File Manager (tabbed, split view, smooth transitions). Terminal (tabbed, split pane, GPU-accelerated text).'],
];
phases.forEach(([num,title,status,color,desc])=>{
  children.push(...PhRow(num,title,status,color,desc));
});

children.push(PG());

// ══════════════════════════════════════════════════════
// SECTION 13 — CONSISTENCY RULES
// ══════════════════════════════════════════════════════
children.push(H1('SECTION 13 — THE 10 INVIOLABLE CONSISTENCY RULES', C.red));
children.push(Body('These rules apply to every piece of UI in CharisOS forever. No exception. They exist to ensure the OS feels like one coherent product rather than separate apps stitched together.', 80, 120));

const rules = [
  ['Rule 1','One Spacing Scale',C.amber,'All spacing, padding, gap, and margin values must come from the 8-value spacing scale: 2, 4, 8, 12, 16, 24, 32, 48px. No other values. "20px margin" is not allowed. "24px margin" is.'],
  ['Rule 2','One Corner Radius System',C.amber,'Only four corner radii: 6px (inputs, buttons), 12px (cards, panels), 18px (windows), 24px (dialogs, notifications). No arbitrary border-radius. Every rounded corner in the OS uses one of these four values.'],
  ['Rule 3','One Animation Curve',C.amber,'All animations use spring physics. No linear, no ease-in, no ease-out-cubic. If an effect cannot be expressed as a spring property, it should not animate. The three spring presets (snappy, bouncy, smooth) cover all cases.'],
  ['Rule 4','One Typography Scale',C.amber,'All text is one of the eight font sizes. No "13px" or "15px" font sizes. Font weight is DPI-adaptive. No hardcoded weights in widget code.'],
  ['Rule 5','One Icon Language',C.blue,'All icons come from the CharisOS icon system. No PNG icons. No icons from different libraries mixed together. Every icon supports four modes (outline, filled, two-tone, adaptive) and a color tint.'],
  ['Rule 6','One Color Palette',C.blue,'All colors come from the theme engine. No hardcoded hex values in widget code. Every widget derives its colors from the theme — this is what makes theme switching work.'],
  ['Rule 7','Five Interactive States',C.blue,'Every interactive widget must implement all five states: normal, hover, pressed, focused, disabled. Missing a state is a bug. The loading state is optional but must follow the same specification when used.'],
  ['Rule 8','One Material per Component Type',C.green,'Windows: Frosted Glass. Dialogs: Crystal Glass. Dock/Taskbar: Liquid Glass. Context menus: Acrylic Mist. Low-power/emergency: Solid Surface. No component invents its own material.'],
  ['Rule 9','Applications Never Touch the Framebuffer',C.green,'No application, utility, daemon, or background service may write directly to the framebuffer or compositor buffer. Everything goes through the surface protocol. There are no exceptions.'],
  ['Rule 10','Accessibility is Never Optional',C.purple,'Every component that renders text must support font size scaling. Every interactive component must be keyboard-navigable. Every animation must respect reduce_motion. These are not features — they are requirements. An inaccessible component is an incomplete component.'],
];
rules.forEach(([num,title,color,desc])=>{
  children.push(new Paragraph({
    ...sp(120,40),
    border:{ left:{ style:BorderStyle.SINGLE, size:12, color } },
    children:[
      new TextRun({ text:`  ${num}  `, font:F.code, size:20, bold:true, color }),
      new TextRun({ text:title, font:F.head, size:24, bold:true, color:C.white }),
    ],
  }));
  children.push(new Paragraph({ ...sp(20,80), ...ind(240),
    children:[new TextRun({ text:desc, font:F.body, size:21, color:C.gray5 })],
  }));
});

// ── FINAL STATEMENT ──────────────────────────────────
children.push(PG());
children.push(new Paragraph({ ...sp(200,60), alignment:AlignmentType.CENTER,
  children:[new TextRun({ text:'"CharisOS should behave like a real-time graphics engine that happens to run applications."', font:F.body, size:24, italics:true, color:C.gray3 })],
}));
children.push(Sep(C.amber));
children.push(new Paragraph({ ...sp(60,40), alignment:AlignmentType.CENTER,
  children:[new TextRun({ text:'CharisOS  ·  Nairobi, Kenya  ·  github.com/Liladizilla/charis__OS', font:F.code, size:20, color:C.gray4 })],
}));

// ─── BUILD DOC ───────────────────────────────────────────────────────────────
const doc = new Document({
  numbering: {
    config:[{
      reference:'bul',
      levels:[
        { level:0, format:LevelFormat.BULLET, text:'\u25B8',
          style:{ paragraph:{ indent:{ left:440, hanging:300 } } } },
        { level:1, format:LevelFormat.BULLET, text:'\u2013',
          style:{ paragraph:{ indent:{ left:760, hanging:300 } } } },
      ],
    }],
  },
  styles: {
    default:{ document:{ run:{ font:F.body, size:22, color:C.white } } },
    paragraphStyles:[
      { id:'Heading1', name:'Heading 1', basedOn:'Normal', next:'Normal', quickFormat:true,
        run:{ size:60, bold:true, font:F.head },
        paragraph:{ spacing:{ before:500, after:140 }, outlineLevel:0 } },
      { id:'Heading2', name:'Heading 2', basedOn:'Normal', next:'Normal', quickFormat:true,
        run:{ size:36, bold:true, font:F.head },
        paragraph:{ spacing:{ before:380, after:100 }, outlineLevel:1 } },
      { id:'Heading3', name:'Heading 3', basedOn:'Normal', next:'Normal', quickFormat:true,
        run:{ size:28, bold:true, font:F.head },
        paragraph:{ spacing:{ before:280, after:80 }, outlineLevel:2 } },
    ],
  },
  sections:[{
    properties:{
      page:{
        size:{ width:12240, height:15840 },
        margin:{ top:1080, right:1080, bottom:1080, left:1080 },
      },
    },
    children,
  }],
});

Packer.toBuffer(doc).then(buf => {
  fs.writeFileSync('/mnt/user-data/outputs/CharisOS_UI_Infrastructure.docx', buf);
  console.log('Done');
});