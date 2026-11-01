import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { PROP } from "../.tools/pocketjs/contracts/spec/spec.ts";
import type { AotExpr, AotNode, AotProgram } from "../.tools/pocketjs/microts/compiler/aot-ir.ts";

type Element = Extract<AotNode, { kind: "element" }>;
type Projection = { id: number; condition?: string; text?: string; tag?: string };

function binding(name: string): string {
  if (/^line[0-5]$/.test(name)) return `view.lines[${name.slice(-1)}]`;
  if (/^opt[0-3]$/.test(name)) return `view.options[${name.slice(-1)}]`;
  if (["title", "hero", "hint", "keys", "success", "download", "provisioning", "screen"].includes(name)) return `view.${name}`;
  if (name === "questionSize") return "view.question_size";
  if (name === "optionSize") return "view.option_size";
  throw new Error(`unsupported projection prop ${name}`);
}

function condition(expr: AotExpr): string {
  if (expr.kind === "binding" && expr.scope === "prop" && ["success", "download", "provisioning"].includes(expr.name)) return binding(expr.name);
  if (expr.kind === "binary" && expr.operator === "===" && expr.left.kind === "binding" && expr.left.scope === "prop"
    && ["screen", "questionSize", "optionSize"].includes(expr.left.name) && expr.right.kind === "literal" && Number.isInteger(expr.right.value)) {
    return `${binding(expr.left.name)} == ${expr.right.value}`;
  }
  throw new Error(`unsupported projection condition ${JSON.stringify(expr)}`);
}

function textValue(node: Element): { source: string; count: string; label: string } {
  if (node.children.length || node.text?.parts.length !== 1) throw new Error("unsupported projection text");
  const part = node.text.parts[0];
  const literal = typeof part === "string" ? part : part.kind === "literal" && typeof part.value === "string" ? part.value : undefined;
  if (literal !== undefined) return { source: JSON.stringify(literal), count: literal.length ? "1" : "0", label: literal };
  if (typeof part !== "string" && part.kind === "binding" && part.scope === "prop") {
    const field = binding(part.name);
    return { source: `${field}.as_str()`, count: `usize::from(!${field}.is_empty())`, label: part.name };
  }
  throw new Error("unsupported projection text binding");
}

export function writeProjection(out: string, ir: AotProgram): void {
  const projected: Projection[] = [];
  function count(nodes: AotNode[]): string {
    return nodes.map(node => {
      if (node.kind === "if") {
        function branch(index: number): string {
          const item = node.branches[index];
          if (!item) return "0";
          if (item.condition == null) {
            if (index !== node.branches.length - 1) throw new Error("non-final projection else");
            return count(item.children);
          }
          const test = condition(item.condition);
          projected.push({ id: node.id, condition: test });
          return `(if ${test} { ${count(item.children)} } else { ${branch(index + 1)} })`;
        }
        return branch(0);
      }
      if (node.kind !== "element" || !["View", "Text"].includes(node.tag)) throw new Error("unsupported projected element");
      if (node.tag === "Text") {
        const text = textValue(node);
        projected.push({ id: node.id, text: text.label });
        return text.count;
      }
      projected.push({ id: node.id, tag: node.tag });
      return `(1 + ${count(node.children)})`;
    }).join(" + ") || "0";
  }
  function slotOf(style: number): number {
    const font = ir.styles.records[style]?.base?.find(prop => prop.prop === PROP.fontSlot);
    if (!font) throw new Error(`style ${style} without font slot`);
    return font.value;
  }
  function styled(expr: AotExpr, text: string): string {
    if (expr.kind === "conditional") {
      const test = expr.condition;
      // fit 的字号槽必须与模板真正选中的槽一致。
      if (test.kind === "binary" && test.left.kind === "binding" && ["questionSize", "optionSize"].includes(test.left.name)
        && test.right.kind === "literal" && (expr.consequent.kind !== "literal" || typeof expr.consequent.value !== "number"
          || slotOf(expr.consequent.value) !== test.right.value)) {
        throw new Error(`${test.left.name} === ${test.right.value} must select font slot ${test.right.value}`);
      }
      return `if ${condition(test)} { ${styled(expr.consequent, text)} } else { ${styled(expr.alternate, text)} }`;
    }
    if (expr.kind === "literal" && typeof expr.value === "number") return `if slot == ${slotOf(expr.value)} { f(${text}); }`;
    throw new Error("unsupported dynamic style");
  }
  function fontText(nodes: AotNode[]): string {
    return nodes.map(node => {
      if (node.kind === "if") {
        const branch = (index: number): string => {
          const item = node.branches[index];
          if (!item) return "";
          if (item.condition == null) return fontText(item.children);
          return `if ${condition(item.condition)} { ${fontText(item.children)} } else { ${branch(index + 1)} }`;
        };
        return branch(0);
      }
      if (node.kind !== "element") throw new Error("unsupported projected element");
      if (node.tag !== "Text") return fontText(node.children);
      const text = textValue(node).source;
      if (node.style >= 0) return `if slot == ${slotOf(node.style)} { f(${text}); }`;
      if (!node.dynamicStyle) throw new Error("text without font style");
      return styled(node.dynamicStyle.expression, text);
    }).join(" ");
  }
  const nodes = ir.components[0].nodes;
  const expression = `1 + ${count(nodes)}`;
  if (/set_layout_(static|formula)|set_region_root/.test(readFileSync(join(out, "app.rs"), "utf8"))) throw new Error("unsupported layout projection mode");
  writeFileSync(join(out, "projection.rs"), `#[allow(unused_parens)]\nfn projected_nodes(view:&View)->usize { ${expression} }\n`);
  writeFileSync(join(out, "font_text.rs"), `#[allow(unused_variables)]\nfn font_text<'a>(view: &'a crate::View, slot: u8, f: &mut dyn FnMut(&'a str)) { ${fontText(nodes)} }\n`);
  writeFileSync(join(out, "projection.json"), JSON.stringify({ coreRoot: 1, slotmapSentinelAddedByCaller: 0, nodes: projected, expression }, null, 2) + "\n");
}
