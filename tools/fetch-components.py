#!/usr/bin/env python3
"""在忽略目录按完整ZIP摘要下载并校验派生组件；不解析浮动版本。"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

root = Path(__file__).resolve().parent.parent
lock = json.loads((root / "tools/components.lock.json").read_text())
for name, spec in lock.items():
    archive = root / ".tools/downloads" / f"{name}-{spec['version']}.zip"
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        partial = archive.with_suffix(".partial")
        subprocess.run(["curl", "-fL", "--retry", "2", spec["url"], "-o", str(partial)], check=True)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != spec["sha256"]:
            partial.unlink()
            raise SystemExit(f"{name}: ZIP SHA256 mismatch")
        partial.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
        raise SystemExit(f"{name}: ZIP SHA256 mismatch")
    destination = root / ".tools/components" / name
    with zipfile.ZipFile(archive) as source:
        files = [entry for entry in source.infolist() if not entry.is_dir()]
        for entry in files:
            path = Path(entry.filename)
            if path.is_absolute() or ".." in path.parts:
                raise SystemExit(f"{name}: unsafe archive path")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
            extracted = Path(temporary) / name
            source.extractall(extracted)
            for patch_spec in spec.get("patches", []):
                patch = root / patch_spec["patch"]
                if hashlib.sha256(patch.read_bytes()).hexdigest() != patch_spec["sha256"]:
                    raise SystemExit(f"{name}: patch SHA256 mismatch")
                for file_spec in patch_spec["files"]:
                    path = extracted / file_spec["path"]
                    if hashlib.sha256(path.read_bytes()).hexdigest() != file_spec["before"]:
                        raise SystemExit(f"{name}: patch input SHA256 mismatch")
                subprocess.run(["patch", "-s", "--batch", "-p1", "-i", str(patch)],
                               cwd=extracted, check=True)
                for file_spec in patch_spec["files"]:
                    path = extracted / file_spec["path"]
                    if hashlib.sha256(path.read_bytes()).hexdigest() != file_spec["after"]:
                        raise SystemExit(f"{name}: patch output SHA256 mismatch")
            expected = {str(p.relative_to(extracted)): p.read_bytes()
                        for p in extracted.rglob("*") if p.is_file()}
            actual = {str(p.relative_to(destination)): p.read_bytes()
                      for p in destination.rglob("*") if p.is_file()} if destination.exists() else {}
            if actual != expected:
                if destination.exists():
                    shutil.rmtree(destination)
                extracted.rename(destination)
    print(f"{name} {spec['version']}: ZIP, pinned patches and extracted files verified")
