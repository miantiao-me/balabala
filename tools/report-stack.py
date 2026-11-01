#!/usr/bin/env python3
"""从最终RISC-V objdump提取固定入口帧和可解析直调用链；不证明运行时上界。"""
import re
import sys

functions = {}
current = None
for line in open(sys.argv[1]):
    header = re.match(r"^([0-9a-f]+) <(.+)>:$", line)
    if header:
        current = int(header[1], 16)
        functions[current] = {"name": header[2], "lines": []}
    elif current is not None:
        instruction = re.match(r"^\s*([0-9a-f]+):\s+[0-9a-f]+\s+(.+)$", line)
        if instruction:
            functions[current]["lines"].append((int(instruction[1], 16), instruction[2]))

for addr, f in functions.items():
    constants, frame, calls, unresolved = {}, 0, set(), []
    for i, (pc, op) in enumerate(f["lines"]):
        # 固定序言仅查看前32条；后续sp恢复/临时调整另由人工检查。
        if i < 32:
            match = re.match(r"(li|lui)\s+(\w+),(-?0x[0-9a-f]+|-?\d+)", op)
            if match:
                constants[match[2]] = int(match[3], 0) * (4096 if match[1] == "lui" else 1)
            match = re.match(r"addi\s+(\w+),(\w+),(-?\d+)", op)
            if match:
                dest, src, val = match[1], match[2], int(match[3])
                if dest == src == "sp" and val < 0:
                    frame -= val
                elif src in constants:
                    constants[dest] = constants[src] + val
            match = re.match(r"sub\s+sp,sp,(\w+)", op)
            if match and match[1] in constants:
                frame += constants[match[1]]
        # jal/jalr直调用及函数末尾尾调用均记录；忽略函数内部目标。
        target = re.search(r"(?:\bjal\b|\bjalr\b|\bj\b).*?(?:# )?([0-9a-f]+) <", op)
        if target:
            dest = int(target[1], 16)
            if dest in functions and dest != addr:
                calls.add(dest)
        elif re.match(r"(?:jalr|jr)\b", op) and op != "jr\tra":
            unresolved.append(hex(pc))
    f.update(frame=frame, calls=calls, unresolved=unresolved)

memo = {}
def longest(addr, seen):
    if addr in seen:
        return 0, [], True
    if addr in memo:
        return memo[addr]
    f = functions[addr]
    best, path, cycle = 0, [], False
    for child in f["calls"]:
        size, chain, cyc = longest(child, seen | {addr})
        cycle |= cyc
        if size > best:
            best, path = size, chain
    value = f["frame"] + best, [addr] + path, cycle
    # 递归路径的结果依赖seen，不缓存。
    if not cycle:
        memo[addr] = value
    return value

print("Fixed prologue estimates from final ELF; conservative direct-call sums (tail calls also summed).")
print("Not a stack safety proof: indirect calls, recursion, later sp changes, FreeRTOS/interrupt context excluded.")
for name in ["app_main", "balabala_app_create", "balabala_app_render"]:
    addr = next(a for a, f in functions.items() if f["name"] == name)
    size, chain, cycle = longest(addr, set())
    print(f"\n{name}: frame={functions[addr]['frame']}; longest resolved sum={size}; cycle={cycle}")
    for child in chain:
        f = functions[child]
        print(f"  {child:08x} frame={f['frame']:5} {f['name']}")
    reachable, pending = set(), [addr]
    while pending:
        child = pending.pop()
        if child in reachable:
            continue
        reachable.add(child)
        pending.extend(functions[child]["calls"])
    unresolved = [(functions[a]["name"], functions[a]["unresolved"]) for a in reachable if functions[a]["unresolved"]]
    print(f"reachable functions={len(reachable)}; unresolved indirect sites={sum(len(v) for _, v in unresolved)}")
    for fn, pcs in unresolved:
        print(f"  indirect {fn}: {','.join(pcs)}")
print("\nLargest fixed prologues:")
for addr, f in sorted(functions.items(), key=lambda pair: pair[1]["frame"], reverse=True)[:15]:
    print(f"{addr:08x} {f['frame']:5} {f['name']}")
