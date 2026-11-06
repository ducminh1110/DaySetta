#!/usr/bin/env python3
"""DaySetta documentation checks.

No third-party packages.  The checker is deliberately conservative: it fails on a
broken relative Markdown link or an unresolvable `path:line` citation, but it only
warns when the external Daylight evidence tree is unavailable (for example on a
GitHub runner that has no `/home/ubuntu/daylight`).

Exit status is 0 only when there are no errors.
"""

from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parent.parent
HANDOFF = ROOT / "AI_AGENT_HANDOFF.md"
STATUS = ROOT / "docs" / "IMPLEMENTATION_STATUS.md"
ADR_DIR = ROOT / "adr"
ARTIFACT_JSON = ROOT / "artifacts" / "macos-27" / "mig-routines.json"
EVIDENCE = Path(os.environ.get("DAYSETTA_EVIDENCE_DIR", "/home/ubuntu/daylight"))

MAX_FILE_BYTES = 2 * 1024 * 1024
MAX_HANDOFF_LINES = 60
EVIDENCE_CLASSES = (
    "VERIFIED",
    "IMPLEMENTED",
    "PARTIAL",
    "OBSERVED",
    "INFERRED",
    "UNKNOWN",
    "PLANNED",
)
CLASS_RE = re.compile(r"\b(" + "|".join(EVIDENCE_CLASSES) + r")\b")
LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
PATH_LINE_RE = re.compile(r"`([^`\n]+?):(\d+)(?:-(\d+))?`")
SCHEMES = ("http", "https", "mailto", "ftp", "data", "tel")

errors: list[str] = []
warnings: list[str] = []
counts = {
    "markdown": 0,
    "links": 0,
    "path_refs": 0,
    "evidence_files": 0,
    "size_files": 0,
}


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def skipped(path: Path) -> bool:
    """Repository build output and VCS metadata are not documentation inputs."""
    try:
        parts = path.relative_to(ROOT).parts
    except ValueError:
        return False
    if not parts:
        return False
    first = parts[0]
    return first == ".git" or first == "build" or first.startswith("build-")


def markdown_files() -> list[Path]:
    return sorted(
        p for p in ROOT.rglob("*.md") if p.is_file() and not skipped(p)
    )


def line_count(path: Path) -> int:
    try:
        return len(path.read_text(encoding="utf-8", errors="replace").splitlines())
    except OSError:
        return 0


def check_markdown_links(path: Path, text: str) -> None:
    for match in LINK_RE.finditer(text):
        raw = match.group(1).strip()
        if not raw:
            continue
        if raw.startswith("<") and raw.endswith(">"):
            raw = raw[1:-1]
        # Drop an optional title: [text](target "title") -> target.
        target = raw.split()[0]
        split = urlsplit(target)
        if split.scheme:
            if split.scheme in SCHEMES:
                continue
            errors.append(f"{rel(path)}: unsupported link scheme '{split.scheme}' in {target}")
            continue
        if not split.path:  # pure fragment or empty
            continue
        counts["links"] += 1
        clean = unquote(split.path)
        candidate = Path(clean)
        if not candidate.is_absolute():
            candidate = path.parent / candidate
        if not candidate.exists():
            errors.append(f"{rel(path)}: broken Markdown link '{target}'")


class Resolver:
    def __init__(self) -> None:
        self.basename_index: dict[str, list[Path]] | None = None

    def _index(self) -> dict[str, list[Path]]:
        if self.basename_index is None:
            self.basename_index = {}
            if EVIDENCE.is_dir():
                for item in EVIDENCE.rglob("*"):
                    if item.is_file() and ".git" not in item.parts:
                        self.basename_index.setdefault(item.name, []).append(item)
        return self.basename_index

    def candidates(self, ref: str) -> list[Path]:
        ref = ref.strip()
        if ref.startswith("./"):
            ref = ref[2:]
        candidates: list[Path] = []
        if ref.startswith("/"):
            candidates.append(Path(ref))
            return candidates

        candidates.append(ROOT / ref)
        if EVIDENCE.is_dir():
            candidates.append(EVIDENCE / ref)
            if ref.startswith("daylight/"):
                candidates.append(EVIDENCE / ref[len("daylight/") :])
        if "/" not in ref:
            candidates.extend(self._index().get(ref, []))
        # Preserve order, drop duplicates.
        seen: set[Path] = set()
        unique: list[Path] = []
        for candidate in candidates:
            if candidate not in seen:
                seen.add(candidate)
                unique.append(candidate)
        return unique

    def resolve(self, ref: str) -> Path | None:
        for candidate in self.candidates(ref):
            if candidate.is_file():
                return candidate
        return None


def check_path_line(path: Path, text: str, resolver: Resolver) -> None:
    for match in PATH_LINE_RE.finditer(text):
        ref, start_text, end_text = match.group(1), match.group(2), match.group(3)
        if ref.startswith(SCHEMES):
            continue
        counts["path_refs"] += 1
        resolved = resolver.resolve(ref)
        if resolved is None:
            if not EVIDENCE.is_dir():
                warnings.append(
                    f"{rel(path)}: evidence citation '{match.group(0)}' not checked "
                    f"(missing {EVIDENCE})"
                )
                continue
            errors.append(f"{rel(path)}: unresolved path citation '{match.group(0)}'")
            continue
        total = line_count(resolved)
        start = int(start_text)
        end = int(end_text) if end_text else start
        if start < 1 or end < start or end > total:
            errors.append(
                f"{rel(path)}: line citation '{match.group(0)}' outside "
                f"{rel(resolved)} ({total} lines)"
            )


def check_evidence_classes(paths: list[Path]) -> None:
    required = [p for p in paths if p == STATUS or ADR_DIR in p.parents]
    for path in required:
        counts["evidence_files"] += 1
        text = path.read_text(encoding="utf-8", errors="replace")
        found = sorted(set(CLASS_RE.findall(text)))
        if not found:
            errors.append(f"{rel(path)}: no evidence class ({', '.join(EVIDENCE_CLASSES)})")


def check_handoff() -> None:
    if not HANDOFF.is_file():
        errors.append("AI_AGENT_HANDOFF.md is missing")
        return
    lines = line_count(HANDOFF)
    if lines > MAX_HANDOFF_LINES:
        errors.append(
            f"AI_AGENT_HANDOFF.md has {lines} lines; limit is {MAX_HANDOFF_LINES}"
        )


def check_file_sizes() -> None:
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file() or skipped(path):
            continue
        counts["size_files"] += 1
        try:
            size = path.stat().st_size
        except OSError as exc:
            errors.append(f"{rel(path)}: cannot stat ({exc})")
            continue
        if size > MAX_FILE_BYTES:
            errors.append(
                f"{rel(path)}: {size} bytes exceeds the 2 MiB limit "
                f"({MAX_FILE_BYTES} bytes)"
            )


def check_artifact_json() -> None:
    if not ARTIFACT_JSON.is_file():
        return
    try:
        with ARTIFACT_JSON.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"{rel(ARTIFACT_JSON)}: invalid JSON ({exc})")
        return
    if not isinstance(data, dict):
        errors.append(f"{rel(ARTIFACT_JSON)}: top-level JSON value must be an object")
    else:
        warnings.append(f"{rel(ARTIFACT_JSON)}: parsed JSON object with {len(data)} keys")


def main() -> int:
    resolver = Resolver()
    files = markdown_files()
    counts["markdown"] = len(files)

    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        check_markdown_links(path, text)
        check_path_line(path, text, resolver)

    check_evidence_classes(files)
    check_handoff()
    check_file_sizes()
    check_artifact_json()

    print("DaySetta documentation check")
    print(f"  root:        {ROOT}")
    print(f"  evidence:    {EVIDENCE} ({'present' if EVIDENCE.is_dir() else 'absent'})")
    print(f"  markdown:    {counts['markdown']} files")
    print(f"  links:       {counts['links']} checked")
    print(f"  path:line:   {counts['path_refs']} checked")
    print(f"  classes:     {counts['evidence_files']} ADR/status files")
    print(f"  size scan:   {counts['size_files']} files")
    print(f"  handoff:     {line_count(HANDOFF) if HANDOFF.is_file() else 'missing'} lines")
    print(
        "  artifact:    "
        + (rel(ARTIFACT_JSON) if ARTIFACT_JSON.is_file() else "absent (skipped)")
    )

    for warning in warnings:
        print(f"WARN  {warning}")
    for error in errors:
        print(f"ERROR {error}", file=sys.stderr)

    if errors:
        print(f"FAILED: {len(errors)} error(s), {len(warnings)} warning(s)")
        return 1
    print(f"OK: 0 errors, {len(warnings)} warning(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
