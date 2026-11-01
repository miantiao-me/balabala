import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { bakeSlot, DEFAULT_REGULAR } from "../.tools/pocketjs/framework/compiler/bake-font.ts";
import opentype from "../.tools/pocketjs/node_modules/opentype.js";
import { buildSprites } from "./sprites.ts";
import type { Deck } from "./native-content.ts";
import type { ProvisioningLayout } from "./provisioning-layout.ts";
import type { CourseUnit } from "./course-pack.ts";

export async function buildAssets(root: string, out: string, decks: Deck[], panelLayout: ProvisioningLayout, curriculum: CourseUnit[] = []) {
// 所有原生运行时文案都进入字形档，包含C ABI恢复失败等模型外错误。
function rustSources(dir:string):string[]{
 return readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?rustSources(join(dir,e.name)):e.name.endsWith(".rs")?[join(dir,e.name)]:[]);
}
const uiSources=rustSources(join(root,"native/src")).sort();
const uiText=uiSources.flatMap(path=>[...readFileSync(path,"utf8").matchAll(/"((?:\\.|[^"\\])*)"/g)].map(m=>m[1]
 .replace(/\\u\{([0-9a-fA-F_]+)\}/g,(_,hex)=>String.fromCodePoint(parseInt(hex.replaceAll("_",""),16)))
 .replace(/\\([nrt"\\])/g,(_,c)=>({n:"\n",r:"\r",t:"\t",'"':'"',"\\":"\\"}[c]!)))).join("");
writeFileSync(join(out,"ui-font-sources.json"),JSON.stringify([...uiSources.map(p=>p.slice(root.length+1)),"native/font-sources.json","native/provisioning-layout.json"],null,2)+"\n");
const normal=new Set<number>();const large=new Set<number>();
function chars(s:string,set:Set<number>){for(const c of s){if(c.codePointAt(0)!>=32)set.add(c.codePointAt(0)!);}}
// ASCII、全部声调预组字符与组合符号为v1导入白名单的一部分。
const pinyin="āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜüêĀÁǍÀĒÉĚÈĪÍǏÌŌÓǑÒŪÚǓÙǕǗǙǛÜ\u0304\u0301\u030c\u0300\u0308";
chars(Array.from({length:95},(_,i)=>String.fromCharCode(i+32)).join("")+pinyin,normal);
// 24px涵盖全部文案；48px使用明确UI字符源，保留原覆盖，避免小字号错误文案重复占Flash。
const fontSources=JSON.parse(readFileSync(join(root,"native/font-sources.json"),"utf8"));
chars(uiText+readFileSync(join(root,"native/App.vue"),"utf8")+fontSources.legacyNormalUiChars+fontSources.largeUiChars+[...panelLayout.text,...panelLayout.states].map((t)=>t.text).join(""),normal);chars(fontSources.largeUiChars,large);
for(const deck of decks){
 if(deck.cards.length!==32)throw new Error(`${deck.subject}: expected 32 authored cards`);
 chars(deck.title,normal);
 for(const c of deck.cards){for(const field of [c.front,c.back,c.question,...c.options,c.explanation])chars(field,normal);chars(c.front,large);}
}
for (const unit of curriculum) {
 chars(unit.title, normal);
 for (const card of unit.cards) {
  for (const field of [card.front,card.back,card.question,...card.options,card.explanation]) chars(field,normal);
  chars(card.front,large);
 }
}
// 新模板的 32px 与 48 Bold 覆盖全部可导入字符，fit 才能对任何导入内容按同一规则选字号；
// 旧模板 48 Regular 只保留原 hero 覆盖。字形库都在 Flash，页面只加载用到的子集。
// 5/14保留Noto旧模板与刷机最终提示；12/6/15和特大字使用同一份单字重字体。
const legacy=opentype.parse(await Bun.file(join(root,".tools/fonts/NotoSansCJKsc-Regular.otf")).arrayBuffer());
const uiFont=process.env.BALABALA_UI_FONT??"neoxihei";
if(!["neoxihei","ipaex"].includes(uiFont))throw new Error("BALABALA_UI_FONT must be neoxihei or ipaex");
const neo=opentype.parse(await Bun.file(join(root,".tools/fonts/LXGWNeoXiHei.ttf")).arrayBuffer());
const font=uiFont==="neoxihei"?neo:opentype.parse(await Bun.file(join(root,".tools/fonts/ipaexg.ttf")).arrayBuffer());
const fallback=opentype.parse(await Bun.file(DEFAULT_REGULAR).arrayBuffer());
const assets=[];
const slots=[[5,24,normal,legacy],[6,32,normal,font],[12,24,normal,font],[14,48,large,legacy],[15,48,normal,font]] as const;
for(const [slot,px,set,face] of slots){
 const chain=face===legacy?[fallback]:uiFont==="ipaex"?[neo]:[];
 for(const cp of set){if([face,...chain].every(f=>f.charToGlyphIndex(String.fromCodePoint(cp))<=0))throw new Error(`font missing U+${cp.toString(16)}`);}
 const atlas=bakeSlot(face,slot,px,slot===15,[...set].sort((a,b)=>a-b),1,chain,true);
 if(slot===12 && atlas.bytes[9]!==panelLayout.fontCellHeight)throw new Error("provisioning font cellHeight changed");
 writeFileSync(join(out,`font-${slot}.bin`),atlas.bytes);
 assets.push({slot,px,glyphs:atlas.glyphCount,bytes:atlas.bytes.length,cellWidth:atlas.bytes[8],cellHeight:atlas.bytes[9]});
}
writeFileSync(join(out,"fonts.rs"),`pub const FONT_SLOTS: [u8; ${slots.length}] = [${slots.map(s=>s[0]).join(", ")}];\npub const FONTS: [&'static [u8]; ${slots.length}] = [${slots.map(s=>`include_bytes!(concat!(env!("BALABALA_GENERATED"),"/font-${s[0]}.bin"))`).join(",")}];\n`);
// 特大字只覆盖内置卡组 front；导入卡组遇到范围外字符时 fit 退回 48px Bold。
const giant=new Set<number>();
for(const deck of decks)for(const c of deck.cards)chars(c.front,giant);
const sprites=await buildSprites(root,out,[font,neo],[...giant].sort((a,b)=>a-b));
return { uiFont, fonts: assets, sprites };
}
