import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

interface Rect { x: number; y: number; w: number; h: number }
export interface ProvisioningLayout {
  width: number;
  height: number;
  fontCellHeight: number;
  text: { x: number; y: number; text: string; bold?: boolean }[];
  states: (Rect & { status: number; text: string })[];
  identity: (Rect & { prop: string; bold?: boolean; digits?: boolean })[];
  hint: { x: number; y: number; prop: string };
  pop: Rect & { pitch: number };
}

export function provisioningTemplate(layout: ProvisioningLayout): string {
  const text = (x: number, y: number, value: string, bold = false) =>
    `      <Text class="absolute left-[${x}] top-[${y}] ${bold ? "text-5xl font-bold" : "text-2xl font-bold"} text-black">${value}</Text>`;
  const nodes = ['    <View v-else-if="props.provisioning" class="absolute left-[0] top-[0] w-[480] h-[800]">'];
  for (const t of layout.text) nodes.push(text(t.x, t.y, t.text, t.bold));
  // 六位密码由叠加层直接读 Flash 字形，固定状态块在活跃期零分配重绘。
  for (const i of layout.identity) if (!i.digits) nodes.push(text(i.x, i.y, `{{ props.${i.prop} }}`, i.bold));
  nodes.push(text(layout.hint.x, layout.hint.y, `{{ props.${layout.hint.prop} }}`), '    </View>');
  return nodes.join("\n");
}
export function provisioningContract(root: string, out: string) {
  const layout: ProvisioningLayout = JSON.parse(readFileSync(join(root, "native/provisioning-layout.json"), "utf8"));
  const template = provisioningTemplate(layout);
  if (!readFileSync(join(root, "native/App.vue"), "utf8").includes(template)) throw new Error("provisioning layout differs from canonical contract");
  const p = layout.pop;
  const states = layout.states.map(s => `(${s.status},[${s.x},${s.y},${s.w},${s.h}],${JSON.stringify(s.text)})`);
  // 每个状态块左上角留出 4px 探针，验证最终提交帧的反白状态。
  const markers = layout.states.map(s => `(${s.status},[${s.x + 5},${s.y + 5},4,4])`);
  const updates = layout.states.map(s => `[${s.x},${s.y},${s.w},${s.h}]`);
  const identities = [layout.identity[0], ...Array.from({ length: 6 }, (_, i) => ({
    x: p.x + i * p.pitch + 3, y: p.y + 3, w: p.w - 6, h: p.h - 6,
  }))].map(i => `[${i.x},${i.y},${i.w},${i.h}]`);
  const texts = [...layout.text, ...layout.states].map(t => JSON.stringify(t.text)).join(",");
  for (const r of [...layout.identity, ...layout.states]) {
    if (r.x < 0 || r.y < 0 || r.x + r.w > layout.width || r.y + r.h > layout.height) throw new Error("provisioning rectangle outside screen");
  }
  writeFileSync(join(out, "provisioning.rs"), `pub const PANEL_TEXT: &[&str]=&[${texts}];
pub const PANEL_MARKERS: &[(u32,[usize;4])]=&[${markers}];
pub const PANEL_UPDATES: &[[usize;4]]=&[${updates}];
pub const PANEL_STATES: &[(u32,[i32;4],&str)]=&[${states}];
pub const PANEL_POP: [i32;5]=[${p.x},${p.y},${p.pitch},${p.w},${p.h}];
pub const PANEL_IDENTITIES: &[[usize;4]]=&[${identities}];
`);
  return layout;
}
