#!/usr/bin/env python3
"""获取并严格校验固定课程来源；所有下载和派生资料只进入 .tools。"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import unicodedata
import urllib.request
import zipfile


def han(char):
    name = unicodedata.name(char, "")
    return name.startswith(("CJK UNIFIED IDEOGRAPH", "CJK COMPATIBILITY IDEOGRAPH")) or char == "〇"


def validate(path):
    units = json.loads(path.read_text())
    count = 0
    chars = set()
    for unit in units:
        for card in unit["cards"]:
            for field in ("front", "back", "question", "options", "explanation"):
                values = card[field] if field == "options" else [card[field]]
                for text in values:
                    for char in text:
                        if han(char):
                            try:
                                char.encode("gb2312", errors="strict")
                            except UnicodeEncodeError as exc:
                                raise ValueError(f'{card["id"]}.{field}: 非GB2312汉字 {char}') from exc
                            count += 1
                            chars.add(char)
    return {"hanOccurrences": count, "distinctHan": len(chars), "encoding": "Python gb2312 strict", "violations": 0}


def prepare(root, verify):
    lock = json.loads((root / "content/curriculum/sources.lock.json").read_text())
    out = root / ".tools/learning-data"
    out.mkdir(parents=True, exist_ok=True)
    for source in lock["sources"]:
        path = out / source["file"]
        if not path.exists():
            if verify:
                raise ValueError(f"缺少固定资料 {path}；先运行 python3 tools/fetch-learning-data.py")
            with urllib.request.urlopen(source["url"], timeout=60) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != source["sha256"]:
                raise ValueError(f'下载摘要不匹配：{source["file"]}')
            path.write_bytes(data)
        if hashlib.sha256(path.read_bytes()).hexdigest() != source["sha256"]:
            raise ValueError(f'固定资料摘要不匹配：{source["file"]}；不会覆盖已有文件')
    head = (out / "ngsl-headwords.txt").read_text().split("\na\n", 1)
    if len(head) != 2:
        raise ValueError("NGSL文件头发生变化")
    headwords = ["a"] + head[1].lower().split()
    supplement = [row[0].lower() for row in csv.reader((out / "ngsl-supplement.csv").read_text(encoding="utf-8-sig").splitlines())]
    if len(headwords) != 2809 or len(supplement) != 52 or any(not re.fullmatch("[a-z]+", w) for w in headwords + supplement):
        raise ValueError("NGSL原形表数量/结构错误")
    readings = {}
    with zipfile.ZipFile(out / "Unihan.zip") as archive:
        for line in archive.read("Unihan_Readings.txt").decode().splitlines():
            if line.startswith("#") or not line:
                continue
            code, field, value = line.split("\t")
            char = chr(int(code[2:], 16))
            if field == "kMandarin":
                readings.setdefault(char, {})["common"] = value.split()[0]
            if field in ("kMandarin", "kHanyuPinyin", "kTGHZ2013", "kXHC1983"):
                pronunciations = []
                for part in value.split():
                    pronunciations.extend(part.split(":")[-1].split(","))
                readings.setdefault(char, {}).setdefault("all", []).extend(pronunciations)
    allowed = []
    for cp in range(0x10000):
        char = chr(cp)
        if han(char):
            try:
                char.encode("gb2312", errors="strict")
                allowed.append(char)
            except UnicodeEncodeError:
                pass
    payload = {"sources": lock["sources"], "headwordCount": len(headwords), "supplementRows": len(supplement),
               "whitelist": sorted(set(headwords + supplement)), "gb2312Han": "".join(allowed), "readings": readings}
    (out / "validated.json").write_text(json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n")
    return {"headwords": len(headwords), "supplementRows": len(supplement), "whitelist": len(payload["whitelist"]), "verifiedFiles": len(lock["sources"])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--verify", action="store_true", help="仅校验已有下载，禁止网络请求")
    parser.add_argument("--validate", type=Path, help="逐卡以Python gb2312 strict检查所有汉字，不限制拼音声调")
    args = parser.parse_args()
    result = validate(args.validate) if args.validate else prepare(args.root.resolve(), args.verify)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
