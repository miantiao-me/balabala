import { cpSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import type { Card } from "./native-content.ts";

export interface CourseUnit {
  subject: string;
  unit: number;
  grade: number;
  title: string;
  cards: Card[];
}
const subjects = ["hanzi", "pinyin", "english", "math"];
export function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function stableKey(subject: string, id: string): number {
  let key = 0x811c9dc5;
  for (const b of new TextEncoder().encode(`${subject}:${id}`)) key = Math.imul(key ^ b, 0x01000193) >>> 0;
  return key;
}

/** 目录借用Flash；SD只保存正文。每卡CRC使启动验证后的SD变动也能被发现。 */
export function writeCoursePack(root: string, out: string, units: CourseUnit[]): void {
  const directory = join(root, ".build/sdcard/balabala/curriculum");
  mkdirSync(directory, { recursive: true });
  const attribution = join(directory, "attribution");
  mkdirSync(join(attribution, "licenses"), { recursive: true });
  cpSync(join(root, "content/curriculum"), join(attribution, "content/curriculum"), { recursive: true });
  for (const name of ["CC-BY-SA-4.0.txt", "Unicode-License-V3.txt", "curriculum.md"]) {
    writeFileSync(join(attribution, "licenses", name), readFileSync(join(root, "licenses", name)));
  }
  writeFileSync(join(directory, "NOTICE.md"), `# 课程数据归属\n\n英语词汇依据 Browne, C., Culligan, B., & Phillips, J. (2023), The New General Service List (Version 1.2), https://www.newgeneralservicelist.com 。英语课程数据及派生词表按 CC BY-SA 4.0 分发，已作选词、中文释义与双向练习编排。\n\n汉字读音依据 Unicode Unihan 16.0.0，Copyright © 2024 Unicode®, Inc.，按 Unicode License V3 使用；具体词境读音另有项目覆盖。\n\n完整归属与许可位于 attribution/licenses/，人工源、固定版本和下载摘要位于 attribution/content/curriculum/。这些文件须随课程一同分发。主题词、拼音规则及数学题为项目编排或运算生成；课程未逐题人工审校。\n`);
  const rows: { id: string; subject: number; unit: number; offset: number; bytes: number; crc: number; key: number }[] = [];
  const lessons: string[] = [];
  const chunks: Uint8Array[] = [];
  const ids = new Set<string>();
  const keys = new Set<number>();
  let offset = 16;
  for (const subject of subjects) {
    const lessonsForSubject = units.filter(u => u.subject === subject).sort((a, b) => a.unit - b.unit);
    for (const [ordinal, lesson] of lessonsForSubject.entries()) {
      if (lesson.unit !== ordinal || ![1, 2, 3].includes(lesson.grade) || !lesson.cards.length || lesson.cards.length > 40) throw new Error(`invalid course unit ${subject}/${lesson.unit}`);
      const first = rows.length;
      for (const card of lesson.cards) {
        const identity = `${subject}:${card.id}`;
        const key = stableKey(subject, card.id);
        if (!/^[a-z0-9-]{1,32}$/.test(card.id) || ids.has(identity) || keys.has(key)) throw new Error(`duplicate/invalid course identity ${identity}`);
        ids.add(identity); keys.add(key);
        const data = new TextEncoder().encode(JSON.stringify(card));
        if (data.length > 2048) throw new Error(`course card exceeds 2048 bytes: ${identity}`);
        rows.push({ id: card.id, subject: subjects.indexOf(subject), unit: ordinal, offset, bytes: data.length, crc: crc32(data), key });
        chunks.push(data); offset += data.length;
      }
      lessons.push(`CourseUnit{subject:${subjects.indexOf(subject)},unit:${ordinal},grade:${lesson.grade},title:${JSON.stringify(lesson.title)},first:${first},count:${rows.length - first}}`);
    }
  }
  if (rows.length > 4096) throw new Error(`course exceeds 4096 cards: ${rows.length}`);
  const payload = new Uint8Array(offset - 16);
  let cursor = 0;
  for (const chunk of chunks) { payload.set(chunk, cursor); cursor += chunk.length; }
  const crc = crc32(payload);
  const pack = new Uint8Array(offset);
  pack.set(new TextEncoder().encode("BALC"));
  const view = new DataView(pack.buffer);
  view.setUint32(4, 1, true); view.setUint32(8, payload.length, true); view.setUint32(12, crc, true);
  pack.set(payload, 16);
  writeFileSync(join(directory, "course.bin"), pack);
  const order = rows.map((r, index) => ({ key: r.key, index })).sort((a, b) => a.key - b.key).map(r => r.index);
  writeFileSync(join(out, "course.rs"), `pub const COURSE_ID:u32=0x5052494d;\npub const COURSE_VERSION:u32=1;\npub const COURSE_BYTES:u32=${pack.length};\npub const COURSE_CRC:u32=${crc};\npub static COURSE_CARDS:&[CourseCard]=&[${rows.map(r => `CourseCard{id:${JSON.stringify(r.id)},subject:${r.subject},unit:${r.unit},offset:${r.offset},bytes:${r.bytes},crc:${r.crc},key:${r.key}}`).join(",")}];\npub static COURSE_KEYS:&[u16]=&[${order.join(",")}];\npub static COURSE_UNITS:&[CourseUnit]=&[${lessons.join(",")}];\n`);
  writeFileSync(join(directory, "manifest.json"), JSON.stringify({ format: 1, course: "balabala-primary", version: 1, bytes: pack.length, crc32: crc.toString(16).padStart(8, "0"), cards: rows.length, subjects: Object.fromEntries(subjects.map(s => [s, { cards: rows.filter(r => r.subject === subjects.indexOf(s)).length, units: units.filter(u => u.subject === s).length }])) }, null, 2) + "\n");
}
