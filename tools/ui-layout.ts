// 新模板与 Rust 叠加层共用的几何。App.vue 在 <template> 中以 `$名称` 引用，构建时替换后编译，
// 同时生成 Rust 常量，避免文字坐标与框线坐标两处手写漂移。
type Rect = [number, number, number, number];

/** 按原版 X4 实拍透视校正估算的四键中心；正视核验后只改这里。 */
const keyX = [90, 185, 295, 390];
const column = (x: number): Rect => [x - 44, 520, 88, 264];
const tiles: Rect[] = [[24, 256, 204, 208], [252, 256, 204, 208], [24, 488, 204, 208], [252, 488, 204, 208]];
const deckCards = [96, 300, 504].map((y): Rect => [24, y, 432, 184]);
const settingsRows = [0, 1, 2, 3, 4].map((i): Rect => [24, 112 + 80 * i, 432, 64]);
const statsRows = [112, 248, 384, 520];

export const uiLayout = {
  screen: { legacy: 0, home: 1, quiz: 2, read: 3, deck: 4, settings: 5, stats: 6, message: 7, sleep: 8 },
  keyX,
  stroke: 3,
  radius: 16,
  // 正面键帽：只画当前页有作用的键。
  caps: keyX.map((x): Rect => [x - 40, 736, 80, 48]),
  head: {
    chip: [24, 24, 48, 48] as Rect,
    title: [88, 28, 368, 36] as Rect,
    // 存储提示右对齐；有提示时不画进度点。
    status: [216, 28, 240, 36] as Rect,
    // 进度点右端与竖直中心。
    dots: [456, 48],
  },
  home: {
    card: [24, 72, 432, 160] as Rect,
    play: [56, 112, 80, 80] as Rect,
    hero: [168, 90, 160, 70] as Rect,
    chip: [300, 104, 40, 40] as Rect,
    // 卡内进度点左端与竖直中心。
    dots: [172, 192],
    tiles,
    // 科目大字与标签相对块顶部的位置。
    tileGlyphs: tiles.map(t => t[1] + 48),
    tileLabels: tiles.map(t => t[1] + 152),
  },
  deck: {
    cards: deckCards,
    icons: deckCards.map(([x, y]): Rect => [x + 24, y + 52, 80, 80]),
    names: deckCards.map(([, y]): Rect => [152, y + 38, 280, 70]),
    notes: deckCards.map(([, y]): Rect => [152, y + 114, 280, 36]),
  },
  read: {
    card: [24, 96, 432, 624] as Rect,
    // 认一认正面：特大字或 48px Bold 在卡片内居中。
    front: [36, 108, 408, 600] as Rect,
    // 背面：上方小号正面、分隔线、背面文字。
    backFront: [36, 116, 408, 72] as Rect,
    divider: [72, 204, 336, 2] as Rect,
    back: [36, 224, 408, 480] as Rect,
    leading: [64, 36],
    // 背面文字用 48px Bold 时最多 3 行，否则退回 24px。
    boldBackLines: 3,
  },
  quiz: {
    question: [24, 96, 432, 400] as Rect,
    columns: keyX.map(column),
    // 列内选项区：去掉描边后留白，底部到分隔线为止。
    options: keyX.map((x): Rect => [x - 40, 526, 80, 186]),
    dividers: keyX.map((x): Rect => [x - 30, 716, 60, 2]),
    letterTop: 734,
    // 作答后列底改画导航键帽，正确列分隔线以上反白。
    slots: keyX.map((x): Rect => [x - 40, 718, 80, 64]),
    answers: keyX.map((x): Rect => [x - 44, 520, 88, 198]),
    // 作答反馈占用题目区：插图、标题、解释。
    art: [24, 96, 432, 208] as Rect,
    feedbackTitle: [24, 304, 432, 70] as Rect,
    feedbackBody: [24, 384, 432, 128] as Rect,
    // 行距按字号从大到小：题目 48 Bold / 24；选项 48 Bold / 32 / 24。可排行数 = 区域高度 / 行距。
    questionLeading: [64, 36],
    optionLeading: [64, 46, 30],
  },
  settings: {
    title: [24, 24, 432, 70] as Rect,
    rows: settingsRows,
    labels: settingsRows.map(([, y]): Rect => [48, y + 14, 352, 36]),
    value: [232, 126, 200, 36] as Rect,
    chevrons: settingsRows.map(([, y]): Rect => [412, y + 22, 12, 20]),
    note: [24, 528, 432, 144] as Rect,
  },
  stats: {
    title: [24, 24, 432, 70] as Rect,
    chips: statsRows.map((y): Rect => [24, y, 56, 40]),
    names: statsRows.map((y): Rect => [92, y + 2, 104, 36]),
    counts: statsRows.map((y): Rect => [196, y + 2, 260, 36]),
    bars: statsRows.map((y): Rect => [24, y + 56, 432, 28]),
    legend: [24, 164, 304].map((x): Rect => [x, 672, 20, 20]),
    legendLabels: [24, 164, 304].map((x): Rect => [x + 28, 664, 104, 36]),
  },
  sleep: {
    // 月亮墨迹在画布中线下方 8px，向上补偿后与屏幕中线对齐。
    art: [0, 282, 480, 220] as Rect,
    title: [24, 536, 432, 70] as Rect,
    body: [24, 616, 432, 72] as Rect,
  },
  message: {
    art: [0, 64, 480, 224] as Rect,
    title: [24, 304, 432, 70] as Rect,
    body: [24, 384, 432, 144] as Rect,
    bodyLeading: 36,
    buttons: [[48, 560, 384, 72], [48, 648, 384, 72]] as Rect[],
  },
};

function lookup(path: string): number {
  let value: any = uiLayout;
  for (const key of path.split(".")) {
    const rect = Array.isArray(value) && value.length === 4 && value.every(v => typeof v === "number");
    value = rect && "xywh".includes(key) && key.length === 1 ? value["xywh".indexOf(key)] : value?.[key];
  }
  if (typeof value !== "number") throw new Error(`ui-layout: $${path} is not a number`);
  return value;
}

/** 只替换 <template> 段，`$a.b.x` 取矩形分量；未知名称直接失败。 */
export function applyUiLayout(vue: string): string {
  const start = vue.indexOf("<template>");
  if (start < 0) throw new Error("ui-layout: App.vue without <template>");
  return vue.slice(0, start) + vue.slice(start).replace(/\$([A-Za-z][\w.]*[A-Za-z0-9])/g, (_, path) => String(lookup(path)));
}

function rust(value: any): { type: string; value: string } {
  if (typeof value === "number") return { type: "i32", value: String(value) };
  const items = value.map(rust);
  return { type: `[${items[0].type}; ${items.length}]`, value: `[${items.map((i: any) => i.value).join(", ")}]` };
}
export function uiLayoutRust(): string {
  const lines: string[] = [];
  const emit = (prefix: string, value: any) => {
    if (typeof value === "object" && !Array.isArray(value)) {
      for (const [key, inner] of Object.entries(value)) emit(prefix ? `${prefix}_${key}` : key, inner);
      return;
    }
    const name = prefix.replace(/([a-z0-9])([A-Z])/g, "$1_$2").toUpperCase();
    const r = prefix.startsWith("screen_") ? { type: "u8", value: String(value) } : rust(value);
    lines.push(`pub const ${name}: ${r.type} = ${r.value};`);
  };
  emit("", uiLayout);
  return lines.join("\n") + "\n";
}
