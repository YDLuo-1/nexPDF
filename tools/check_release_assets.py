#!/usr/bin/env python3
"""Create the measured bilingual package-size report and enforce the 80 MiB target."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


LIMIT_MIB = 80.0
PACKAGE_SUFFIXES = (".zip", ".exe", ".AppImage", ".dmg")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--markdown", type=Path, required=True)
    args = parser.parse_args()

    records = []
    for path in sorted(args.directory.iterdir()):
        if path.is_file() and path.name.endswith(PACKAGE_SUFFIXES):
            size_mib = path.stat().st_size / (1024 * 1024)
            records.append({"file": path.name, "size_mib": round(size_mib, 3), "within_target": size_mib <= LIMIT_MIB})

    args.json.write_text(json.dumps({"limit_mib": LIMIT_MIB, "assets": records}, indent=2) + "\n", encoding="utf-8")
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
    args.markdown.write_text("\n".join(lines) + "\n", encoding="utf-8")
    if not records:
        return 2
    return 0 if all(record["within_target"] for record in records) else 1


if __name__ == "__main__":
    raise SystemExit(main())
