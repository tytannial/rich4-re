#!/usr/bin/env python3
"""扫描重写源码中的逆向标注注释，校验格式并生成地址映射表。

标注格式（详见 docs/reverse/README.md）:

    // [RE 0x4015D6] gameInit
    // 依据: IDA 反编译 0x4015D6; 字符串 "data.mkf" 引用
    // 迁移: DirectDrawCreate -> SDL_CreateWindow

    // [PORT DDraw:CreateSurface] 640x480x16 后台缓冲
    // 替换依据: 0x4015D6 CreateSurface -> SDL_CreateTexture

    // [NEW] 用途说明

用法:
    python tools/re_map.py            # 校验并生成 docs/reverse/address-map.md
    python tools/re_map.py --report   # 只打印覆盖率报告
    python tools/re_map.py --check    # 只校验，不写文件（供 CI 使用）
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIRS = [ROOT / "src", ROOT / "include"]
OUT_MD = ROOT / "docs" / "reverse" / "address-map.md"

RE_HEAD = re.compile(r"\[RE\s+0x([0-9A-Fa-f]{6,8})\]\s*([^\s:：]*)\s*(.*)")
PORT_HEAD = re.compile(r"\[PORT\s+([^\]]+)\]\s*(.*)")
NEW_HEAD = re.compile(r"\[NEW\]\s*(.*)")
FIELD_RE = re.compile(r"^(依据|迁移|替换依据|说明)\s*[:：]\s*(.*)$")

KIND_RE = "RE"
KIND_PORT = "PORT"
KIND_NEW = "NEW"


def iter_annotations(path: Path):
    """产出一个文件中的全部标注块: (kind, head, name_or_desc, fields, line_no)。"""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except UnicodeDecodeError:
        lines = path.read_text(encoding="gbk", errors="replace").splitlines()

    i = 0
    while i < len(lines):
        line = lines[i]
        head = RE_HEAD.search(line) or PORT_HEAD.search(line) or NEW_HEAD.search(line)
        if not head:
            i += 1
            continue

        rest = ""
        if RE_HEAD.search(line):
            m = RE_HEAD.search(line)
            kind, name, rest = KIND_RE, m.group(2), m.group(3)
        elif PORT_HEAD.search(line):
            m = PORT_HEAD.search(line)
            kind, name, rest = KIND_PORT, m.group(1), m.group(2)
        else:
            m = NEW_HEAD.search(line)
            kind, name = KIND_NEW, m.group(1)

        fields: dict[str, str] = {}
        rest = rest.strip()
        fm = FIELD_RE.match(rest)
        if fm:
            fields[fm.group(1)] = fm.group(2)
        elif rest and kind == KIND_PORT:
            name = f"{name} {rest}"

        j = i + 1
        while j < len(lines):
            nxt = lines[j].lstrip()
            if not nxt.startswith("//"):
                break
            body = nxt[2:].strip()
            fm = FIELD_RE.match(body)
            if fm:
                fields[fm.group(1)] = fm.group(2)
            elif body:
                last = next(reversed(fields), None)
                if last:
                    fields[last] += " " + body
            j += 1

        yield kind, head.group(0), name, fields, i + 1
        i = j


def is_definition(kind: str, fields: dict) -> bool:
    """带依据行的注释块是"定义"，其余是行内"引用"（不登记映射表）。"""
    if kind == KIND_RE:
        return "依据" in fields
    if kind == KIND_PORT:
        return "替换依据" in fields
    return True


def scan() -> tuple[list[dict], list[str]]:
    entries: list[dict] = []
    errors: list[str] = []
    for src_dir in SRC_DIRS:
        for path in sorted(src_dir.rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue
            rel = path.relative_to(ROOT).as_posix()
            for kind, head, name, fields, line_no in iter_annotations(path):
                if not is_definition(kind, fields):
                    continue
                entry = {
                    "kind": kind,
                    "head": head,
                    "name": name,
                    "fields": fields,
                    "file": rel,
                    "line": line_no,
                }
                if kind == KIND_RE:
                    m = RE_HEAD.search(head)
                    entry["addr"] = int(m.group(1), 16)
                    if not name:
                        errors.append(f"{rel}:{line_no}: [RE] 缺少原符号名")
                entries.append(entry)
    return entries, errors


def print_report(entries: list[dict], errors: list[str]) -> None:
    re_entries = [e for e in entries if e["kind"] == KIND_RE]
    port_entries = [e for e in entries if e["kind"] == KIND_PORT]
    new_entries = [e for e in entries if e["kind"] == KIND_NEW]

    seen: dict[int, list[dict]] = {}
    for e in re_entries:
        seen.setdefault(e["addr"], []).append(e)
    dup = {a: v for a, v in seen.items() if len(v) > 1}

    print(f"RE   标注: {len(re_entries)} 条, 覆盖 {len(seen)} 个原始函数")
    print(f"PORT 标注: {len(port_entries)} 条")
    print(f"NEW  标注: {len(new_entries)} 条")
    if dup:
        print(f"重复地址: {len(dup)} 个")
        for addr, items in sorted(dup.items()):
            locs = ", ".join(f"{it['file']}:{it['line']}" for it in items)
            print(f"  0x{addr:06X}: {locs}")
    if errors:
        print(f"\n错误 {len(errors)} 条:")
        for err in errors:
            print(f"  {err}")
    else:
        print("\n校验通过")


def write_md(entries: list[dict]) -> None:
    re_entries = sorted(
        (e for e in entries if e["kind"] == KIND_RE), key=lambda e: (e["addr"], e["file"])
    )
    port_entries = [e for e in entries if e["kind"] == KIND_PORT]

    lines = [
        "# 地址映射表",
        "",
        "> 由 `python tools/re_map.py` 自动生成，勿手改。",
        "",
        f"## RE 映射（{len(re_entries)} 条）",
        "",
        "| 地址 | 原符号 | 重写位置 | 依据 |",
        "|------|--------|----------|------|",
    ]
    for e in re_entries:
        evidence = e["fields"].get("依据", "").replace("|", "\\|")
        port = e["fields"].get("迁移", "")
        if port:
            evidence += f"（迁移: {port.replace('|', chr(92) + '|')}）"
        loc = f"{e['file']}:{e['line']}"
        lines.append(f"| `0x{e['addr']:06X}` | {e['name']} | `{loc}` | {evidence} |")

    lines += [
        "",
        f"## PORT 映射（{len(port_entries)} 条）",
        "",
        "| 原调用点/API | 替换位置 | 替换依据 |",
        "|--------------|----------|----------|",
    ]
    for e in port_entries:
        evidence = e["fields"].get("替换依据", "").replace("|", "\\|")
        loc = f"{e['file']}:{e['line']}"
        lines.append(f"| {e['name']} | `{loc}` | {evidence} |")

    lines.append("")
    OUT_MD.parent.mkdir(parents=True, exist_ok=True)
    OUT_MD.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"wrote {OUT_MD.relative_to(ROOT)}")


def main(argv: list[str]) -> int:
    entries, errors = scan()
    print_report(entries, errors)
    if "--report" in argv:
        return 1 if errors else 0
    if "--check" in argv:
        return 1 if errors else 0
    write_md(entries)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
