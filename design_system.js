// ─── SHARED DESIGN SYSTEM ────────────────────────────────────────────────────
const {
  Document, Packer, Paragraph, TextRun, HeadingLevel,
  AlignmentType, BorderStyle, WidthType, ShadingType,
  LevelFormat, PageBreak
} = require('docx');

const C = {
  black:'0D0D0D', bg:'111122', panel:'1A1A30',
  amber:'F5A623', amberD:'CC8500', amberL:'FFD080',
  red:'E03C3C',   green:'27AE60', blue:'3A86FF',
  purple:'9B5DE5',teal:'00B4D8',  orange:'F77F00',
  white:'F0F0F8', gray1:'252538', gray2:'32324A',
  gray3:'555570', gray4:'8888AA', gray5:'CCCCDD',
  border:'3A3A55',
  crit:'E03C3C', serious:'F5A623', moderate:'3A86FF',
  minor:'27AE60',
};
const F = { head:'Arial', body:'Consolas', code:'Courier New' };
const sp = (b,a) => ({ spacing:{ before:b||0, after:a||0 } });
const ind = (l,h) => ({ indent:{ left:l||0, hanging:h||0 } });

// ─── PRIMITIVE BUILDERS ──────────────────────────────────────────────────────
const TR = (text, opts={}) =>
  new TextRun({ text, font:F.body, size:22, color:C.white, ...opts });

const TRc = (text, color, opts={}) =>
  new TextRun({ text, font:F.body, size:22, color, ...opts });

const TRm = (text, opts={}) =>
  new TextRun({ text, font:F.code, size:19, color:C.amberL||'FFD080', ...opts });

const P = (children, spaceBefore=80, spaceAfter=80, extra={}) =>
  new Paragraph({ ...sp(spaceBefore,spaceAfter), children: Array.isArray(children)?children:[children], ...extra });

// ─── SECTION HEADERS ─────────────────────────────────────────────────────────
const H1 = (text, color=C.amber) => new Paragraph({
  heading: HeadingLevel.HEADING_1,
  ...sp(480,140),
  border:{ bottom:{ style:BorderStyle.SINGLE, size:8, color } },
  shading:{ fill:C.panel, type:ShadingType.CLEAR },
  children:[new TextRun({ text:' '+text, font:F.head, size:56, bold:true, color })],
});
const H2 = (text, color=C.amber) => new Paragraph({
  heading: HeadingLevel.HEADING_2,
  ...sp(380,100),
  border:{ bottom:{ style:BorderStyle.SINGLE, size:3, color } },
  children:[new TextRun({ text, font:F.head, size:34, bold:true, color })],
});
const H3 = (text, color=C.white) => new Paragraph({
  heading: HeadingLevel.HEADING_3,
  ...sp(260,80),
  children:[new TextRun({ text, font:F.head, size:26, bold:true, color })],
});
const H4 = (text, color=C.gray4) => new Paragraph({
  ...sp(200,60),
  children:[new TextRun({ text, font:F.head, size:22, bold:true, color, allCaps:true })],
});

// ─── BODY TEXT ───────────────────────────────────────────────────────────────
const Body = (text, before=80, after=80) => new Paragraph({
  ...sp(before,after),
  children:[new TextRun({ text, font:F.body, size:22, color:C.gray5 })],
});
const BodyRuns = (runs, before=80, after=80) => new Paragraph({
  ...sp(before,after),
  children: runs.map(r => typeof r==='string'
    ? new TextRun({ text:r, font:F.body, size:22, color:C.gray5 })
    : new TextRun({ font:F.body, size:22, color:C.gray5, ...r })),
});

// ─── BULLETS ─────────────────────────────────────────────────────────────────
const Bul = (runs, level=0) => new Paragraph({
  ...sp(50,50),
  ...ind(360+level*320, 280),
  numbering:{ reference:'bul', level },
  children: (Array.isArray(runs)?runs:[runs]).map(r =>
    typeof r==='string'
      ? new TextRun({ text:r, font:F.body, size:22, color:C.gray5 })
      : new TextRun({ font:F.body, size:22, color:C.gray5, ...r })),
});

// ─── CODE BLOCKS ─────────────────────────────────────────────────────────────
const CodeBlock = (lines, label='') => {
  const out = [];
  if(label) out.push(new Paragraph({
    ...sp(120,0),
    shading:{ fill:'0D0D1E', type:ShadingType.CLEAR },
    children:[new TextRun({ text:'  // '+label, font:F.code, size:17, color:C.gray3, italics:true })],
  }));
  for(const ln of lines) out.push(new Paragraph({
    ...sp(0,0), ...ind(200),
    shading:{ fill:'080818', type:ShadingType.CLEAR },
    children:[new TextRun({ text: ln||' ', font:F.code, size:19, color:'FFD080' })],
  }));
  out.push(new Paragraph({ ...sp(0,100), shading:{ fill:'080818', type:ShadingType.CLEAR }, children:[TR('')] }));
  return out;
};

const Separator = (color=C.border) => new Paragraph({
  ...sp(100,100),
  border:{ bottom:{ style:BorderStyle.SINGLE, size:2, color } },
  children:[TR('')],
});

const Callout = (text, color=C.amber) => new Paragraph({
  ...sp(120,120), ...ind(160),
  border:{ left:{ style:BorderStyle.SINGLE, size:10, color }, top:{ style:BorderStyle.SINGLE, size:1, color:C.gray2 }, bottom:{ style:BorderStyle.SINGLE, size:1, color:C.gray2 } },
  shading:{ fill:C.panel, type:ShadingType.CLEAR },
  children:[new TextRun({ text:'  '+text, font:F.body, size:21, color:C.gray5 })],
});

const SPC = (n=1) => Array(n).fill(0).map(()=>
  new Paragraph({ ...sp(0,0), children:[TR('')] }));

// ─── BUG BLOCK ───────────────────────────────────────────────────────────────
const BugBlock = (id, title, sev, sevColor, problem, impact, beforeLines, afterLines, note='') => {
  const items = [];
  items.push(new Paragraph({
    ...sp(220,60),
    border:{ left:{ style:BorderStyle.SINGLE, size:14, color:sevColor } },
    children:[
      new TextRun({ text:'  BUG-'+String(id).padStart(2,'0')+'  ', font:F.code, size:22, bold:true, color:sevColor }),
      new TextRun({ text:title, font:F.head, size:26, bold:true, color:C.white }),
      new TextRun({ text:'  ['+sev+']  ', font:F.head, size:19, bold:true, color:sevColor }),
    ],
  }));
  items.push(new Paragraph({ ...sp(40,40), ...ind(280), children:[
    new TextRun({ text:'PROBLEM:  ', font:F.head, size:20, bold:true, color:C.gray3 }),
    new TextRun({ text:problem, font:F.body, size:21, color:C.gray5 }),
  ]}));
  items.push(new Paragraph({ ...sp(40,80), ...ind(280), children:[
    new TextRun({ text:'IMPACT:   ', font:F.head, size:20, bold:true, color:C.red }),
    new TextRun({ text:impact, font:F.body, size:21, color:C.gray5 }),
  ]}));
  if(beforeLines && beforeLines.length) {
    items.push(H4('  BROKEN CODE (before)', C.red));
    items.push(...CodeBlock(beforeLines));
  }
  if(afterLines && afterLines.length) {
    items.push(H4('  FIXED CODE (after)', C.green));
    items.push(...CodeBlock(afterLines));
  }
  if(note) items.push(new Paragraph({ ...sp(60,120), ...ind(280), children:[
    new TextRun({ text:'\u26A0  ', font:F.code, size:20, color:C.amber }),
    new TextRun({ text:note, font:F.body, size:20, italics:true, color:C.gray4 }),
  ]}));
  return items;
};

// ─── PHASE HEADER ────────────────────────────────────────────────────────────
const PhaseHeader = (num, title, status, color, desc) => {
  const sColors = { COMPLETE:C.green, 'IN PROGRESS':C.amber, NEXT:C.blue, FUTURE:C.gray3 };
  const sc = sColors[status]||C.gray3;
  return [
    new Paragraph({
      ...sp(360,80),
      border:{ left:{ style:BorderStyle.SINGLE, size:16, color }, bottom:{ style:BorderStyle.SINGLE, size:1, color:C.gray2 } },
      children:[
        new TextRun({ text:'  PHASE '+num+'  ', font:F.code, size:22, bold:true, color }),
        new TextRun({ text:title, font:F.head, size:32, bold:true, color:C.white }),
        new TextRun({ text:'   '+status+'  ', font:F.head, size:18, bold:true, color:sc }),
      ],
    }),
    new Paragraph({ ...sp(60,100), ...ind(240), children:[
      new TextRun({ text:desc, font:F.body, size:22, color:C.gray4, italics:true }),
    ]}),
  ];
};

// ─── FILE HEADER ─────────────────────────────────────────────────────────────
const FileHeader = (path) => new Paragraph({
  ...sp(160,0),
  border:{ left:{ style:BorderStyle.SINGLE, size:6, color:C.teal } },
  shading:{ fill:'0D1A22', type:ShadingType.CLEAR },
  children:[
    new TextRun({ text:'  FILE: ', font:F.head, size:19, bold:true, color:C.teal }),
    new TextRun({ text:path, font:F.code, size:19, bold:true, color:C.amberL||'FFD080' }),
  ],
});

module.exports = {
  C, F, sp, ind, TR, TRc, TRm, P, H1, H2, H3, H4,
  Body, BodyRuns, Bul, CodeBlock, Separator, Callout, SPC,
  BugBlock, PhaseHeader, FileHeader,
  Document, Packer, Paragraph, TextRun, HeadingLevel,
  AlignmentType, BorderStyle, WidthType, ShadingType,
  LevelFormat, PageBreak
};