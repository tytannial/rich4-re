"""[NEW] headless 测试编排器（docs/testing.md）。

- ctest（L0 纯函数单测）+ tests/scenarios/*.txt（L1 headless 场景）批量运行
- 超时看门狗 / --filter / --repeat / --matrix（覆盖矩阵与未覆盖清单）
- 控制台表格 + build/test-report.json；任一失败 → 退出码 1

用法：
  python tools/run_tests.py                       # 全部
  python tools/run_tests.py --filter rent         # 名称子串
  python tools/run_tests.py --matrix              # 覆盖矩阵报告
  python tools/run_tests.py --exe build/rich4.exe --game resources/MultiverseJourney
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCEN_DIR = ROOT / "tests" / "scenarios"
HELP_LIST = ROOT / "docs" / "help-checklist.md"

HEAD_ID = re.compile(r"^#\s*id:\s*(.+)$", re.M)
HEAD_COVERS = re.compile(r"^#\s*covers:\s*(.+)$", re.M)
HEAD_TIMEOUT = re.compile(r"^#\s*timeout:\s*(\d+)$", re.M)
HEAD_QUICKSTART = re.compile(r"^#\s*quickstart:\s*(\d+,\d+,\d+)$", re.M)
HEAD_SKIP = re.compile(r"^#\s*skip:\s*(.+)$", re.M)
HELP_REF = re.compile(r"HELP\s*(\d+)")


def read_text(p: Path) -> str:
    for enc in ("utf-8-sig", "utf-8", "gbk"):
        try:
            return p.read_text(encoding=enc)
        except UnicodeDecodeError:
            continue
    return p.read_text(encoding="latin1")


def parse_scenario(path: Path) -> dict:
    txt = read_text(path)
    mid = HEAD_ID.search(txt)
    cov = HEAD_COVERS.search(txt)
    tout = HEAD_TIMEOUT.search(txt)
    qs = HEAD_QUICKSTART.search(txt)
    sk = HEAD_SKIP.search(txt)
    covers = sorted(int(x) for x in HELP_REF.findall(cov.group(1))) if cov else []
    return {
        "file": path,
        "id": mid.group(1).strip() if mid else path.stem,
        "covers": covers,
        "quickstart": qs.group(1) if qs else "0,4,1",
        "skip": sk.group(1).strip() if sk else "",
        "wall_timeout": int(tout.group(1)) // 100 + 30 if tout else 120,
    }


def run_scenario(sc: dict, exe: str, game: str, seed: int, wall_cap: int = 0,
                 extra: str = "") -> dict:
    cmd = [
        exe, "--game", game, "--headless", "--seed", str(seed),
        "--quickstart", sc["quickstart"], "--script", str(sc["file"]),
    ]
    if extra:
        cmd += extra.split()
    t0 = time.time()
    cap = sc["wall_timeout"]
    if wall_cap > 0:
        cap = min(cap, wall_cap)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                           errors="replace", timeout=cap, cwd=str(ROOT))
        out = p.stdout + "\n" + p.stderr
        rc = p.returncode
        timeout = False
    except subprocess.TimeoutExpired:
        out = "TIMEOUT"
        rc = -1
        timeout = True
    except FileNotFoundError:
        out = f"EXE NOT FOUND: {exe}"
        rc = -1
        timeout = False
    m = re.search(r"TEST asserts=(\d+) failures=(\d+)", out)
    asserts, fails = (int(m.group(1)), int(m.group(2))) if m else (0, -1)
    return {
        "id": sc["id"],
        "covers": sc["covers"],
        "ok": rc == 0 and not timeout and fails == 0,
        "rc": rc,
        "asserts": asserts,
        "fails": fails,
        "secs": round(time.time() - t0, 2),
        "tail": tail_fails(out),
    }


def tail_fails(out: str) -> str:
    lines = [l for l in out.splitlines() if "ASSERT FAIL" in l or "script error" in l
             or "TIMEOUT" in l]
    return "\n".join(lines[:8])


def run_ctest(exe_dir: Path) -> dict:
    try:
        p = subprocess.run(["ctest", "--output-on-failure"], capture_output=True,
                           text=True, encoding="utf-8", errors="replace",
                           cwd=str(exe_dir), timeout=120)
        ok = p.returncode == 0
        return {"id": "ctest-unit", "ok": ok, "tail": p.stdout[-600:] if not ok else ""}
    except FileNotFoundError:
        return {"id": "ctest-unit", "ok": False, "tail": "ctest not found"}


def matrix(all_sc: list[dict], results: list[dict]) -> None:
    idxs = set()
    if HELP_LIST.exists():
        txt = read_text(HELP_LIST)
        for line in txt.splitlines():
            m = re.match(r"^\|\s*(\d+)\s*\|", line)
            if m and 1 <= int(m.group(1)) <= 99:  # 总览表 0 行/合计行不算功能条目
                idxs.add(int(m.group(1)))
    covered = {}
    for sc, res in zip(all_sc, results):
        for i in sc["covers"]:
            covered.setdefault(i, []).append(sc["id"] + ("[ok]" if res.get("ok") else "[fail]"))
    print(f"\n=== 覆盖矩阵（help-checklist {len(idxs)} 条）===")
    miss = sorted(idxs - set(covered))
    for i in sorted(covered):
        print(f"  HELP {i:>2}  <- {', '.join(covered[i])}")
    print(f"  未覆盖 {len(miss)} 条: {miss}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=str(ROOT / "build" /
                    ("rich4.exe" if sys.platform == "win32" else "rich4")))
    ap.add_argument("--game", default=str(ROOT / "resources" / "MultiverseJourney"))
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--filter", default="")
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--matrix", action="store_true")
    ap.add_argument("--no-ctest", action="store_true")
    ap.add_argument("--wall-cap", type=int, default=0,
                    help="每场景墙钟秒数上限（0=仅按 # timeout 推算）；定位挂死用如 90")
    ap.add_argument("--extra", default="",
                    help="附加给每个场景的 rich4 参数（如 --canvas 1024x768，M4 宽屏回归）")
    args = ap.parse_args()

    scripts = sorted(SCEN_DIR.glob("*.txt")) if SCEN_DIR.exists() else []
    all_sc = [parse_scenario(p) for p in scripts]
    if args.filter:
        all_sc = [s for s in all_sc if args.filter in s["id"] or args.filter in s["file"].name]

    results = []
    print(f"=== L1 headless 场景 ×{len(all_sc)} (exe={Path(args.exe).name}, seed={args.seed}) ===")
    for sc in all_sc:
        if sc.get("skip"):
            print(f"  [SKIP] {sc['id']:<28} {sc['skip']}")
            continue
        for rep in range(args.repeat):
            r = run_scenario(sc, args.exe, args.game, args.seed + rep, args.wall_cap,
                             args.extra)
            r["rep"] = rep
            results.append(r)
            flag = "PASS" if r["ok"] else "FAIL"
            print(f"  [{flag}] {r['id']:<28} asserts={r['asserts']} fails={r['fails']} "
                  f"{r['secs']}s")
            if not r["ok"] and r["tail"]:
                print("    " + r["tail"].replace("\n", "\n    "))

    ctest = None
    if not args.no_ctest:
        ctest = run_ctest(Path(args.exe).resolve().parent)
        print(f"  [{('PASS') if ctest['ok'] else 'FAIL'}] ctest-unit")
        if not ctest["ok"]:
            print("    " + (ctest["tail"] or "").replace("\n", "\n    "))

    n_fail = sum(1 for r in results if not r["ok"]) + (0 if (ctest and ctest["ok"]) or args.no_ctest else 1)
    report = ROOT / "build" / "test-report.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps({"scenarios": results, "ctest": ctest}, ensure_ascii=False, indent=2),
                      encoding="utf-8", newline="\n")
    print(f"\nTOTAL: scenarios={len(results)} fail={n_fail} -> {'PASS' if n_fail == 0 else 'FAIL'}")
    print(f"report: {report}")

    if args.matrix:
        matrix(all_sc, [next((x for x in results if x["id"] == s["id"]), {"ok": False}) for s in all_sc])
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
