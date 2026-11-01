// 构建期把原创 SVG 线稿与特大字轮廓栅格化为 1bpp 位图，运行时由叠加层直接从 Flash 画进帧缓冲。
// 特大字按墨迹外框裁剪存储，避免为整格空白像素占用Flash。
import { readdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

type Bitmap = { w: number; h: number; x: number; y: number; bits: Uint8Array };
type Font = { unitsPerEm: number; charToGlyph(ch: string): any; charToGlyphIndex(ch: string): number };

/** 特大字两档；112 使 Bold 下最宽的内置词（banana）也放得进 432px，再放不下时由 fit 退回 48px Bold。 */
export const GIANT_PX = [176, 112];

export async function buildSprites(root: string, out: string, fonts: Font[], giantChars: number[]) {
  // 固定版本与 integrity 由 tools/svg/bun.lock 校验，tools/build.sh 安装到忽略目录。
  const { Resvg } = await import(join(root, ".tools/svg/node_modules/@resvg/resvg-js/index.js"));
  // 抗锯齿渲染后取 50% 阈值，比直接 crispEdges 的斜线和曲线更平滑。
  function raster(svg: string): { w: number; h: number; ink: (x: number, y: number) => boolean } {
    const image = new Resvg(svg, { background: "white", font: { loadSystemFonts: false } }).render();
    const px = image.pixels as Uint8Array;
    return { w: image.width, h: image.height, ink: (x, y) => px[(y * image.width + x) * 4] < 128 };
  }
  function pack(r: ReturnType<typeof raster>, [x0, y0, w, h]: number[]): Uint8Array {
    const stride = Math.ceil(w / 8);
    const bits = new Uint8Array(stride * h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      if (r.ink(x0 + x, y0 + y)) bits[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
    }
    return bits;
  }
  function inkBox(r: ReturnType<typeof raster>): number[] | null {
    let [x0, y0, x1, y1] = [r.w, r.h, -1, -1];
    for (let y = 0; y < r.h; y++) for (let x = 0; x < r.w; x++) {
      if (r.ink(x, y)) { x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y); }
    }
    return x1 < 0 ? null : [x0, y0, x1 - x0 + 1, y1 - y0 + 1];
  }

  const art: [string, Bitmap][] = [];
  for (const file of readdirSync(join(root, "native/art")).filter(f => f.endsWith(".svg")).sort()) {
    const r = raster(readFileSync(join(root, "native/art", file), "utf8"));
    // 线稿保留画布尺寸，绘制时按画布对齐；不裁剪留白。
    art.push([file.slice(0, -4), { w: r.w, h: r.h, x: 0, y: 0, bits: pack(r, [0, 0, r.w, r.h]) }]);
  }

  const giant = GIANT_PX.map(px => {
    const pad = Math.ceil(px / 4);
    const baseline = pad + px;
    return giantChars.flatMap(cp => {
      const ch = String.fromCodePoint(cp);
      const font = fonts.find(f => f.charToGlyphIndex(ch) > 0);
      if (!font) throw new Error(`giant font missing U+${cp.toString(16)}`);
      const glyph = font.charToGlyph(ch);
      const scale = px / font.unitsPerEm;
      const advance = Math.round(glyph.advanceWidth * scale);
      const d = glyph.getPath(pad, baseline, px).toPathData(2);
      const r = raster(`<svg xmlns="http://www.w3.org/2000/svg" width="${advance + 2 * pad}" height="${px * 2 + pad}"><path d="${d}" fill="#000"/></svg>`);
      const box = inkBox(r);
      // 空格等无墨迹字符只留步进。
      const bitmap: Bitmap = box
        ? { w: box[2], h: box[3], x: box[0] - pad, y: box[1] - baseline, bits: pack(r, box) }
        : { w: 0, h: 0, x: 0, y: 0, bits: new Uint8Array() };
      return [[cp, advance, bitmap] as const];
    });
  });

  const chunks: Uint8Array[] = [];
  let offset = 0;
  const sprite = (b: Bitmap, advance = b.w) => {
    const at = offset;
    chunks.push(b.bits);
    offset += b.bits.length;
    return `Sprite { w: ${b.w}, h: ${b.h}, x: ${b.x}, y: ${b.y}, advance: ${advance}, at: ${at} }`;
  };
  const lines = art.map(([name, b]) => `pub const ART_${name.toUpperCase().replace(/[^A-Z0-9]/g, "_")}: Sprite = ${sprite(b)};`);
  lines.push(`pub const GIANT_PX: [i32; ${GIANT_PX.length}] = [${GIANT_PX.join(", ")}];`);
  lines.push(`pub const GIANT: [&[(char, Sprite)]; ${GIANT_PX.length}] = [${giant.map(tier =>
    `&[${tier.map(([cp, advance, b]) => `('\\u{${cp.toString(16)}}', ${sprite(b, advance)})`).join(", ")}]`).join(", ")}];`);
  const bin = new Uint8Array(offset);
  let at = 0;
  for (const c of chunks) { bin.set(c, at); at += c.length; }
  writeFileSync(join(out, "sprites.bin"), bin);
  lines.push(`pub const SPRITE_BITS: &[u8] = include_bytes!(concat!(env!("BALABALA_GENERATED"), "/sprites.bin"));`);
  writeFileSync(join(out, "sprites.rs"), lines.join("\n") + "\n");
  return { art: art.length, giant: giant.map(t => t.length), bytes: offset };
}
