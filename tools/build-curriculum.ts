import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";
import type { Card } from "./native-content";

import type { CourseUnit } from "./course-pack.ts";
export type { CourseUnit } from "./course-pack.ts";
interface Reading { common?: string; all?: string[] }
interface Sources { whitelist: string[]; readings: Record<string, Reading>; gb2312Han: string; headwordCount: number; supplementRows: number; sources: unknown[] }
const json = (path: string) => JSON.parse(readFileSync(path, "utf8"));
const tokens = (s: string) => s.toLowerCase().match(/[a-z]+(?:['’][a-z]+)*/g) ?? [];
const texts = (c: Card) => [c.front, c.back, c.question, ...c.options, c.explanation];
const fail = (message: string): never => { throw new Error(message); };
function choice(id: string, front: string, question: string, answer: string, candidates: string[], back: string, explanation = back): Card {
  const wrong = [...new Set(candidates)].filter(v => v !== answer).slice(0, 3);
  if (wrong.length !== 3) fail(`${id}: 干扰项不足`);
  const correct = [...id].reduce((sum, c) => sum + c.charCodeAt(0), 0) % 4;
  wrong.splice(correct, 0, answer);
  return { id, front, back, question, options: wrong as Card["options"], correct, explanation };
}
function rows(root: string, name: string): { grade: number; title: string; entries: string[] }[] {
  return readFileSync(join(root, `content/curriculum/${name}.tsv`), "utf8").split(/\r?\n/).filter(l => l && !l.startsWith("#")).map(l => {
    const [grade, title, entries] = l.split("|");
    return { grade: Number(grade), title, entries: entries.trim().split(/\s+/) };
  });
}
// 重新分配每组的尾段，避免把非末单元拆成不足20卡。
function append(units: CourseUnit[], subject: string, grade: number, title: string, cards: Card[]) {
  const n = Math.ceil(cards.length / 32);
  let pos = 0;
  for (let i = 0; i < n; i++) {
    const size = Math.ceil((cards.length - pos) / (n - i));
    units.push({ subject, unit: units.filter(u => u.subject === subject).length, grade,
      title: n === 1 ? title : `${title}${i + 1}`, cards: cards.slice(pos, pos + size) });
    pos += size;
  }
}
function english(root: string, source: Sources, legacy: Card[], units: CourseUnit[], retired: string[]) {
  const whitelist = new Set(source.whitelist);
  const seen = new Set<string>();
  for (const row of rows(root, "english")) {
    const pairs = row.entries.map(e => { const [word, meaning] = e.split("="); return { word, meaning }; });
    for (const { word, meaning } of pairs) {
      if (!whitelist.has(word) || !meaning || seen.has(word)) fail(`英语种子重复、无释义或超NGSL：${word}`);
      seen.add(word);
    }
    const cards = pairs.flatMap(({ word, meaning }) => {
      const old = legacy.find(c => c.front === word && !retired.includes(c.id));
      const forward = old ? structuredClone(old) : choice(`en-${word}`, word, `哪个词表示“${meaning}”？`, word,
        pairs.map(p => p.word), `${word}：${meaning}。`);
      if (old) {
        const used = new Set(forward.options.filter(v => whitelist.has(v)));
        forward.options = forward.options.map(v => {
          if (whitelist.has(v)) return v;
          const replacement = pairs.map(p => p.word).find(w => !used.has(w));
          if (!replacement) fail(`${old.id}: 无法修复干扰项`);
          used.add(replacement); return replacement;
        }) as Card["options"];
      }
      const reverse = choice(`en-${word}-meaning`, word, `“${word}”在本课中是什么意思？`, meaning,
        pairs.map(p => p.meaning), `${word}：${meaning}。`);
      return [forward, reverse];
    });
    append(units, "english", row.grade, row.title, cards);
  }
  return seen.size;
}
function hanzi(root: string, source: Sources, legacy: Card[], units: CourseUnit[]) {
  const overrides = json(join(root, "content/curriculum/readings.json")) as Record<string, string>;
  const seen = new Set(legacy.map(c => c.front));
  const contexts: { char: string; word: string; reading: string; grade: number; theme: string }[] = [];
  for (const row of rows(root, "hanzi")) for (const word of row.entries) for (const char of word) {
    if (seen.has(char)) continue;
    seen.add(char);
    const reading = overrides[char] ?? source.readings[char]?.common;
    if (!reading) fail(`无读音：${char}`);
    contexts.push({ char, word, reading, grade: row.grade, theme: row.title });
  }
  const options = [...new Set(contexts.map(c => c.reading))];
  for (const grade of [1, 2, 3]) {
    const cards = grade === 1 ? structuredClone(legacy) : [];
    for (const [i, context] of contexts.entries()) {
      if (context.grade !== grade) continue;
      const { char, word, reading } = context;
      // 干扰项排除Unihan收录的该字全部读音，避免多音字造成多个合法选项。
      const valid = new Set([reading, ...(source.readings[char]?.all ?? [])]);
      const distractors = [...options.slice(i % options.length), ...options.slice(0, i % options.length)].filter(p => !valid.has(p));
      cards.push(choice(`hz-u${char.codePointAt(0)!.toString(16)}`, char, `“${word}”中的“${char}”怎么读？`, reading,
        distractors, `${char}：${reading}。组词：${word}。`, `在“${word}”中，“${char}”读${reading}。`));
    }
    append(units, "hanzi", grade, "主题识字", cards);
  }
  return { uniqueCharacters: seen.size, contexts };
}
function pinyin(root: string, legacy: Card[], units: CourseUnit[]) {
  const seed = json(join(root, "content/curriculum/pinyin.json"));
  const cards = structuredClone(legacy);
  for (const p of seed.initials as string[]) cards.push(choice(`py-initial-${p}`, p, `哪一个是声母${p}？`, p, ["b", "m", "n", "l"], `${p}是声母，请家长示范发音。`));
  for (const p of seed.finals as string[]) cards.push(choice(`py-final-${p.replace("ü", "v")}`, p, `哪一个是韵母${p}？`, p, seed.finals, `${p}是韵母，请家长示范发音。`));
  for (const p of seed.whole as string[]) cards.push(choice(`py-whole-${p}`, p, `哪一个是整体认读音节${p}？`, p, seed.whole, `${p}是整体认读音节，整体认读。`));
  const originalFronts = new Set(legacy.map(c => c.front));
  for (const row of seed.toneRows as string[]) {
    const forms = row.split(" ");
    for (let i = 0; i < 4; i++) if (!originalFronts.has(forms[i])) {
      const base = forms[i].normalize("NFD").replace(/[\u0300\u0301\u0304\u030c]/g, "").normalize("NFC");
      cards.push(choice(`py-tone-${cards.length}`, forms[i], `哪一个是${base}的第${i + 1}声？`, forms[i], forms, `${forms[i]}是第${i + 1}声，请家长示范。`));
    }
  }
  const practices = (seed.practice as string).split(" ").map(p => p.split("="));
  for (const [char, p] of practices) cards.push(choice(`py-word-u${char.codePointAt(0)!.toString(16)}`, p,
    `“${char}”的常用读音是哪一个？`, p, practices.map(p => p[1]), `${char}读${p}。请结合字义练习拼读。`));
  for (const [i, rule] of seed.rules.entries()) {
    const [front, question, options, correct, explanation] = rule;
    cards.push({ id: `py-rule-${i + 1}`, front, question, options, correct, back: explanation, explanation });
  }
  append(units, "pinyin", 1, "拼音基础与拼读", cards);
}
function mathCards(kind: string, count: number, legacy: Card[]): Card[] {
  const cards: Card[] = [];
  const oldExpressions = new Set(legacy.map(c => c.front.replace(/\s+/g, "")));
  const keys = new Set<string>();
  const add = (front: string, question: string, answer: string | number, explanation: string, wrong?: string[]) => {
    const key = question;
    if (cards.length >= count || keys.has(key) || oldExpressions.has(front.replace(/\s+/g, ""))) return;
    keys.add(key);
    const value = String(answer);
    const n = Number(answer);
    const candidates = wrong ?? [n + 1, n + 2, Math.max(0, n - 1), n + 3].map(String);
    cards.push(choice(`ma-${kind}-${cards.length + 1}`, front, question, value, candidates, explanation));
  };
  const operation = (a: number, b: number, op: string, result: number) => {
    const front = `${a}${op}${b}`;
    add(front, `${front}等于多少？`, result, `${front}=${result}。`);
  };
  if (kind === "small") for (let a = 0; a <= 10; a++) for (let b = 0; b <= 10; b++) {
    if (a + b <= 10) operation(a, b, "+", a + b);
    if (a >= b) operation(a, b, "-", a - b);
  }
  if (kind === "carry") {
    for (let a = 1; a <= 19; a++) for (let b = 1; b <= 9; b++) {
      if (a < 10 && a + b >= 10) operation(a, b, "+", a + b);
      if (a >= 10 && a < 20 && a % 10 < b) operation(a, b, "-", a - b);
    }
    for (let a = 1; a <= 9; a++) for (let b = 1; b <= 9; b++) if (a + b >= 10) {
      add(`${a}+?=${a + b}`, `${a}加几等于${a + b}？`, b, `${a + b}-${a}=${b}，所以要加${b}。`);
    }
  }
  if (kind === "hundred") for (let i = 0; i < 120; i++) {
    const a = 21 + (i * 17) % 60, b = 1 + (i * 13) % 19;
    operation(a, b, i % 2 === 0 ? "+" : "-", i % 2 === 0 ? a + b : a - b);
  }
  if (kind === "place") for (let n = 21; n <= 48; n++) add(String(n), `${n}中的${Math.floor(n / 10)}表示多少？`, Math.floor(n / 10) * 10,
    `${n}有${Math.floor(n / 10)}个十和${n % 10}个一。`);
  if (kind === "time-money") {
    for (let hour = 1; hour <= 12; hour++) {
      add(`${hour}:00`, `${hour}:00是几时？`, `${hour}时`, `分针指12，时针指${hour}，是${hour}时。`, [`${hour + 1}时`, `${hour}时半`, `${hour + 2}时`]);
      add(`${hour}:30`, `${hour}:30也可以怎样说？`, `${hour}时半`, `${hour}:30就是${hour}时半。`, [`${hour}时整`, `${hour + 1}时`, `${hour + 1}时半`]);
    }
    for (let n = 1; n <= 8; n++) {
      add(`${n}元`, `${n}元等于几角？`, n * 10, `1元=10角，${n}元=${n * 10}角。`);
      add(`${n}角`, `${n}角等于几分？`, n * 10, `1角=10分，${n}角=${n * 10}分。`);
    }
  }
  if (kind === "multiply" || kind === "divide") for (let a = 1; a <= 9; a++) for (let b = 1; b <= 9; b++) {
    if (kind === "multiply") operation(a, b, "×", a * b);
    else operation(a * b, a, "÷", b);
  }
  if (kind === "remainder") for (let divisor = 2; divisor <= 9; divisor++) for (let quotient = 1; quotient <= 9; quotient++) {
    const remainder = quotient % (divisor - 1) + 1, dividend = divisor * quotient + remainder;
    add(`${dividend}÷${divisor}`, `${dividend}÷${divisor}的余数是多少？`, remainder,
      `${dividend}=${divisor}×${quotient}+${remainder}，余数小于除数。`, ["0", String(divisor), String(divisor + 1)]);
  }
  if (kind === "mixed") for (let a = 2; a <= 9; a++) for (let b = 2; b <= 9; b++) {
    add(`${a}+${b}×2`, `${a}+${b}×2等于多少？`, a + b * 2, `先算${b}×2=${b * 2}，再加${a}。`);
    add(`(${a}+${b})×2`, `(${a}+${b})×2等于多少？`, (a + b) * 2, `先算括号内${a}+${b}=${a + b}，再乘2。`);
  }
  if (kind === "length") {
    for (let n = 1; n <= 36; n++) {
      add(`${n}米`, `${n}米等于多少厘米？`, n * 100, `1米=100厘米，${n}米=${n * 100}厘米。`);
      add(`${n}厘米`, `${n}厘米等于多少毫米？`, n * 10, `1厘米=10毫米，${n}厘米=${n * 10}毫米。`);
    }
    const geometry = [
      ["三角形", "三角形有几条边？", "3", "三角形有3条边。", ["2", "4", "5"]],
      ["正方形", "正方形有几个角？", "4", "正方形有4个角，都是直角。", ["2", "3", "5"]],
      ["四边等长", "四边等长、四角为直角的是？", "正方形", "正方形四条边等长，四个角都是直角。", ["三角形", "圆形", "五边形"]],
      ["圆形", "下面哪个图形没有顶点？", "圆形", "圆形没有顶点。", ["三角形", "正方形", "长方形"]],
      ["正方体", "正方体有几个面？", "6", "正方体有6个面。", ["4", "5", "8"]],
      ["正方体的面", "正方体的每个面是什么形状？", "正方形", "正方体的6个面都是相同的正方形。", ["圆形", "三角形", "五边形"]],
      ["锐角", "锐角与直角相比，哪个角大？", "直角", "锐角比直角小。", ["锐角", "一样大", "无法比较"]],
      ["钝角", "钝角与直角相比，哪个角大？", "钝角", "钝角比直角大，比平角小。", ["直角", "一样大", "无法比较"]]
    ] as const;
    for (const [front, question, answer, explanation, wrong] of geometry) add(front, question, answer, explanation, [...wrong]);
  }
  if (kind === "thousand") for (let i = 0; i < 50; i++) {
    const a = 125 + i * 103, b = 168 + i * 17;
    operation(a, b, "+", a + b); operation(a + b, b, "-", a);
  }
  if (kind === "large-multiply") for (let i = 0; i < 60; i++) {
    const a = 12 + (i * 37) % 400, b = 2 + i % 8;
    operation(a, b, "×", a * b); operation(a * b, b, "÷", a);
  }
  if (kind === "fraction") {
    for (let d = 2; d <= 10; d++) for (let n = 1; n < d; n++) {
      add(`${n}/${d}`, `平均分${d}份，取${n}份是几分之几？`, `${n}/${d}`,
        `分母${d}表示平均分的份数，分子${n}表示取的份数。`, [`${d}/${n}`, `${n + 1}/${d}`, `${n}/${d + 1}`]);
    }
    for (let d = 3; d <= 10; d++) for (let n = 1; n < d - 1; n++) {
      add(`${n}/${d}+1/${d}`, `${n}/${d}+1/${d}等于多少？`, `${n + 1}/${d}`,
        `同分母分数相加，分母${d}不变，分子${n}+1=${n + 1}。`, [`${n + 1}/${d + 1}`, `${n}/${d}`, `${n + 2}/${d}`]);
      add(`${n + 1}/${d}-1/${d}`, `${n + 1}/${d}-1/${d}等于多少？`, `${n}/${d}`,
        `同分母分数相减，分母${d}不变，分子${n + 1}-1=${n}。`, [`${n + 1}/${d}`, `${n}/${d + 1}`, `${n + 2}/${d}`]);
    }
  }
  if (kind === "perimeter") for (let n = 1; n <= 20; n++) {
    add(`边长${n}厘米`, `正方形边长${n}厘米，周长几厘米？`, n * 4, `正方形周长=边长×4，${n}×4=${n * 4}。`);
    add(`长${n + 2}宽${n}`, `长方形长${n + 2}厘米宽${n}厘米，周长是？`, (n * 2 + 2) * 2,
      `长方形周长=（长+宽）×2，(${n + 2}+${n})×2=${(n * 2 + 2) * 2}。`);
    add(`${n}千克`, `${n}千克等于几克？`, n * 1000, `1千克=1000克，${n}千克=${n * 1000}克。`);
  }
  if (kind === "calendar") {
    const months = [31,28,31,30,31,30,31,31,30,31,30,31];
    for (let m = 1; m <= 12; m++) add(`${m}月`, `平年的${m}月有多少天？`, months[m - 1], `平年的${m}月有${months[m - 1]}天。`, ["28", "29", "30", "31"]);
    for (let h = 1; h <= 11; h++) add(`下午${h}时`, `下午${h}时用24时计时法是几时？`, h + 12, `下午${h}时加12，是${h + 12}时。`);
    for (let h = 1; h <= 11; h++) add(`${h + 12}时`, `${h + 12}时是下午几时？`, h, `${h + 12}时减12，是下午${h}时。`);
    for (let h = 1; h <= 6; h++) add(`${h}小时`, `${h}小时等于几分钟？`, h * 60, `1小时=60分钟，${h}小时=${h * 60}分钟。`);
  }
  if (cards.length !== count) fail(`${kind}: 仅生成${cards.length}张，预期${count}`);
  return cards;
}

export function buildCurriculumUnits(root: string, out: string): CourseUnit[] {
  root = resolve(root); out = resolve(out);
  if (out !== join(root, ".build") && !out.startsWith(join(root, ".build") + "/")) fail("课程产物目录必须在项目.build内");
  // 每次构建重验原始文件，validated.json不是可绕过固定摘要的可信缓存。
  execFileSync("python3", [join(root, "tools/fetch-learning-data.py"), "--root", root, "--verify"], { encoding: "utf8" });
  const source = json(join(root, ".tools/learning-data/validated.json")) as Sources;
  const outline = json(join(root, "content/curriculum/outline.json"));
  const legacy = Object.fromEntries(["hanzi", "pinyin", "english", "math"].map(s => [s, json(join(root, `content/${s}.json`)).cards as Card[]]));
  const units: CourseUnit[] = [];
  const hanziEvidence = hanzi(root, source, legacy.hanzi, units);
  pinyin(root, legacy.pinyin, units);
  const uniqueEnglishWords = english(root, source, legacy.english, units, outline.retiredIds.english);
  append(units, "math", 1, "数认知与基础加减", structuredClone(legacy.math));
  for (const spec of outline.math) append(units, "math", spec.grade, spec.title, mathCards(spec.kind, spec.count, legacy.math));
  const allowedHan = new Set(source.gb2312Han), whitelist = new Set(source.whitelist);
  const englishTokens = new Set<string>();
  const ids = new Map<string, Set<string>>();
  const max = { id: 0, front: 0, question: 0, option: 0, back: 0, explanation: 0, cardBytes: 0 };
  for (const u of units) {
    const seen = ids.get(u.subject) ?? new Set<string>(); ids.set(u.subject, seen);
    if (!u.cards.length || u.cards.length > 40 || u.title.length > 16 || ![1,2,3].includes(u.grade)) fail(`单元边界错误：${u.subject}/${u.unit}`);
    const sameSubject = units.filter(v => v.subject === u.subject);
    if (sameSubject[u.unit] !== u || (u.cards.length < 20 && sameSubject.at(-1) !== u)) fail(`单元顺序/长度错误：${u.subject}/${u.unit}`);
    for (const c of u.cards) {
      if (!/^[a-z0-9-]{1,32}$/.test(c.id) || seen.has(c.id)) fail(`ID错误：${c.id}`);
      seen.add(c.id); max.id = Math.max(max.id, c.id.length);
      for (const field of ["front", "question", "back", "explanation"] as const) {
        const length = [...c[field]].length, limit = field === "front" ? 12 : field === "question" ? 32 : 64;
        max[field] = Math.max(max[field], length);
        if (!c[field].trim() || length > limit || ((field === "back" || field === "explanation") && Buffer.byteLength(c[field]) > 192)) fail(`${c.id}.${field}: 字数/字节越界`);
      }
      if (c.options.length !== 4 || new Set(c.options.map(s => s.trim().replace(/\s+/g, " "))).size !== 4 || !Number.isInteger(c.correct) || c.correct < 0 || c.correct > 3) fail(`${c.id}: 选项/答案错误`);
      for (const option of c.options) {
        max.option = Math.max(max.option, [...option].length);
        if (!option.trim() || [...option].length > 16) fail(`${c.id}: 选项字数越界`);
      }
      max.cardBytes = Math.max(max.cardBytes, Buffer.byteLength(JSON.stringify(c)));
      for (const text of texts(c)) for (const char of text) if (/\p{Script=Han}/u.test(char) && !allowedHan.has(char)) fail(`${c.id}: 非GB2312汉字${char}`);
      if (u.subject === "english") for (const text of texts(c)) for (const token of tokens(text)) {
        if (!whitelist.has(token)) fail(`${c.id}: 英语超白名单${token}`);
        englishTokens.add(token);
      }
    }
  }
  const allCards = units.flatMap(u => u.cards);
  if (allCards.length > 4096 || max.cardBytes > 2048) fail("课程总数/卡片序列化大小超过整合边界");
  const counts = Object.fromEntries([...ids].map(([s, set]) => [s, { cards: set.size, units: units.filter(u => u.subject === s).length,
    byGrade: Object.fromEntries([1,2,3].map(g => [g, units.filter(u => u.subject === s && u.grade === g).reduce((n, u) => n + u.cards.length, 0)])) }]));
  const preserved = Object.fromEntries(Object.entries(legacy).map(([s, cs]) => [s, cs.filter(c => ids.get(s)!.has(c.id)).map(c => c.id)]));
  const report = { schema: 1, counts, totalCards: allCards.length, totalUnits: units.length, targets: outline.targets,
    shortfall: Object.fromEntries(Object.entries(outline.targets).map(([s, n]) => [s, Math.max(0, Number(n) - ids.get(s)!.size)])),
    preservedIds: preserved, retiredIds: outline.retiredIds, retiredReason: outline.retirementReason,
    hanzi: hanziEvidence, english: { uniqueLearningWords: uniqueEnglishWords, usedTokens: [...englishTokens].sort(), whitelistSize: whitelist.size,
      headwordCount: source.headwordCount, supplementRows: source.supplementRows, policy: "仅官方词目及补充表首列原形；无词形推断、字母或专名豁免", violations: 0 },
    sources: source.sources, fieldMaxima: max, dependencies: outline.dependencies, gradePolicy: outline.gradePolicy,
    manualReview: "主题词/释义/规则为项目编排，尚未逐题人工审校；多音字词境、年龄适用性和实际设备排版待复核。没有复制教材题目。" };
  mkdirSync(out, { recursive: true });
  const curriculumPath = join(out, "curriculum.json");
  writeFileSync(curriculumPath, JSON.stringify(units, null, 2) + "\n");
  const gb2312 = JSON.parse(execFileSync("python3", [join(root, "tools/fetch-learning-data.py"), "--validate", curriculumPath], { encoding: "utf8" }));
  writeFileSync(join(out, "curriculum-report.json"), JSON.stringify({ ...report, gb2312 }, null, 2) + "\n");
  return units;
}
export async function buildCurriculum(root: string, out: string): Promise<CourseUnit[]> {
  return buildCurriculumUnits(root, out);
}
if (import.meta.main) {
  const args = process.argv.slice(2);
  if (args.length !== 2 || args[0] !== "--out") fail("用法：bun tools/build-curriculum.ts --out .build/curriculum");
  const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
  const units = await buildCurriculum(root, args[1]);
  console.log(JSON.stringify({ units: units.length, cards: units.reduce((n, u) => n + u.cards.length, 0), out: resolve(args[1]) }));
}
