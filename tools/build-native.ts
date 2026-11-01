import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { buildAot } from "../.tools/pocketjs/microts/compiler/aot-build.ts";
import type { AotProgram } from "../.tools/pocketjs/microts/compiler/aot-ir.ts";
import { provisioningContract } from "./provisioning-layout.ts";
import { applyUiLayout, uiLayoutRust } from "./ui-layout.ts";
import { readDecks, writeBuiltinContent } from "./native-content.ts";
import { buildAssets } from "./native-fonts.ts";
import { writeProjection } from "./native-projection.ts";
import { buildCurriculum } from "./curriculum.ts";
import { writeCoursePack } from "./course-pack.ts";

const root = resolve(import.meta.dir, "..");
const out = join(root, ".build/generated");
mkdirSync(join(out, "vue"), { recursive: true });
const panelLayout = provisioningContract(root, out);
// 坐标替换保留行号，AOT 错误可直接对照源模板。
writeFileSync(join(out, "vue/App.vue"), applyUiLayout(readFileSync(join(root, "native/App.vue"), "utf8")));
writeFileSync(join(out, "layout.rs"), uiLayoutRust());
await buildAot(join(out, "vue/App.vue"), { strict: true, format: false, outDir: out, ir: join(out, "aot.json") });
const ir: AotProgram = JSON.parse(readFileSync(join(out, "aot.json"), "utf8"));
writeProjection(out, ir);
const decks = readDecks(root);
writeBuiltinContent(out, decks);
const curriculum = buildCurriculum(root);
writeCoursePack(root, out, curriculum);
const assets = await buildAssets(root, out, decks, panelLayout, curriculum);
writeFileSync(join(out, "assets.json"), JSON.stringify(assets, null, 2) + "\n");
console.log(`strict MicroTS AOT; UI font ${assets.uiFont}; Flash glyph archive ${assets.fonts.reduce((n, a) => n + a.bytes, 0)} bytes; sprites ${assets.sprites.bytes} bytes (${assets.sprites.art} art, giant ${assets.sprites.giant.join("/")}); runtime retains page cmap and borrows Flash coverage (${assets.fonts.map(a => `${a.px}px`).join("/")})`);
