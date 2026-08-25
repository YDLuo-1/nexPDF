#!/usr/bin/env python3
"""Measure binary release packages and enforce the 80 MiB target."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


LIMIT_MIB = 80.0
PACKAGE_SUFFIXES = (".zip", ".exe", ".AppImage", ".dmg")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    parser.add_argument("--over-limit-analysis", type=Path)
    args = parser.parse_args()

    records = []
    for path in sorted(args.directory.iterdir()):
        if path.is_file() and path.name.endswith(PACKAGE_SUFFIXES):
            size_mib = path.stat().st_size / (1024 * 1024)
            records.append({"file": path.name, "size_mib": round(size_mib, 3), "within_target": size_mib <= LIMIT_MIB})

    over_limit = [record for record in records if not record["within_target"]]
    analysis = args.over_limit_analysis if over_limit else None
    report = {
        "limit_mib": LIMIT_MIB,
        "assets": records,
        "over_limit_analysis": str(analysis) if analysis else None,
    }
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# Package sizes / 发布包体积",
        "",
        f"Target / 目标: no more than {LIMIT_MIB:.0f} MiB per binary package / 每个二进制包不超过 {LIMIT_MIB:.0f} MiB。",
        "",
        "| Asset / 资产 | MiB | Result / 结果 |",
        "|---|---:|---|",
    ]
    for record in records:
        result = "PASS / 通过" if record["within_target"] else "OVER / 超出"
        lines.append(f"| `{record['file']}` | {record['size_mib']:.3f} | {result} |")
    if analysis:
        lines.extend(["", f"Over-target analysis / 超标组成分析: `{analysis}`"])
    if args.markdown:
        args.markdown.write_text("\n".join(lines) + "\n", encoding="utf-8")
    if not records:
        print("No binary release packages were found.", file=sys.stderr)
        return 2
    for record in records:
        print(f"{record['file']}: {record['size_mib']:.3f} MiB")
    if over_limit and (analysis is None or not analysis.is_file() or analysis.stat().st_size == 0):
        print("A non-empty --over-limit-analysis document is required for packages above 80 MiB.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
