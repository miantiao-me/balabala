import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

export interface Card {
  id: string;
  front: string;
  back: string;
  question: string;
  options: [string, string, string, string];
  correct: number;
  explanation: string;
}
export interface Deck {
  version: number;
  id: string;
  subject: string;
  title: string;
  cards: Card[];
}
export function readDecks(root: string): Deck[] {
  return ["hanzi", "pinyin", "english", "math"].map(subject => JSON.parse(readFileSync(join(root, `content/${subject}.json`), "utf8")) as Deck);
}
export function writeBuiltinContent(out: string, decks: Deck[]): void {
  const rust = (value: string) => JSON.stringify(value);
  const code = decks.map(d => `model::Deck{schema:1,min_format:1,version:${d.version},id:${rust(d.id)}.into(),subject:${rust(d.subject)}.into(),title:${rust(d.title)}.into(),cards:alloc::vec![${d.cards.map(c => `model::Card{id:${rust(c.id)}.into(),front:${rust(c.front)}.into(),back:${rust(c.back)}.into(),question:${rust(c.question)}.into(),options:[${c.options.map(o => rust(o) + ".into()").join(",")}],correct:${c.correct},explanation:${rust(c.explanation)}.into()}`).join(",")}]}`).join(",\n");
  writeFileSync(join(out, "content.rs"), `fn builtin_decks()->alloc::vec::Vec<model::Deck>{alloc::vec![${code}]}\n`);
}
