"""Inventory Compas plugin references without executing Lua or upstream scripts.

This is a static dependency aid, not a compatibility certificate or sandbox.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import subprocess


@dataclass
class Token:
    kind: str
    value: str
    line: int


def lua_tokens(source: str) -> list[Token]:
    result = []
    i, line = 0, 1
    while i < len(source):
        ch = source[i]
        if ch.isspace():
            line += ch == "\n"
            i += 1
            continue
        comment = source.startswith("--", i)
        start = i + 2 if comment else i
        long = re.match(r"\[(=*)\[", source[start:])
        if long:
            close = "]" + long[1] + "]"
            end = source.find(close, start + long.end())
            if end < 0:
                raise ValueError(f"Unclosed long string/comment at line {line}")
            end += len(close)
            if not comment:
                result.append(Token("string", source[start + long.end():end - len(close)], line))
            line += source[i:end].count("\n")
            i = end
            continue
        if comment:
            end = source.find("\n", i)
            i = len(source) if end < 0 else end
            continue
        if ch in "\"'":
            end, value = i + 1, []
            while end < len(source) and source[end] != ch:
                if source[end] == "\\" and end + 1 < len(source):
                    end += 1
                    value.append(source[end])
                else:
                    value.append(source[end])
                end += 1
            if end >= len(source):
                raise ValueError(f"Unclosed string at line {line}")
            result.append(Token("string", "".join(value), line))
            line += source[i:end + 1].count("\n")
            i = end + 1
            continue
        match = re.match(r"[A-Za-z_][A-Za-z_0-9]*|[0-9]+", source[i:])
        if match:
            value = match[0]
            result.append(Token("number" if value[0].isdigit() else "identifier", value, line))
            i += len(value)
        else:
            result.append(Token("punctuation", ch, line))
            i += 1
    return result


def plugin_references(source: str) -> dict:
    tokens = lua_tokens(source)
    refs, events, capabilities, targets = set(), set(), set(), set()
    api_min = None
    dynamic = []
    for i, token in enumerate(tokens):
        if token.kind != "identifier":
            continue
        if token.value == "api_min" and i + 2 < len(tokens) and tokens[i + 1].value == "=" and tokens[i + 2].kind == "number":
            api_min = int(tokens[i + 2].value)
        if token.value == "plugin" and i and tokens[i - 1].value == "=" and (i + 1 == len(tokens) or tokens[i + 1].value not in (".", "[")):
            dynamic.append({"line": token.line, "reason": "alias or function-reference assignment; inspect manually"})
        if token.value == "plugin" and i + 1 < len(tokens) and tokens[i + 1].value == "[":
            dynamic.append({"line": token.line, "reason": "computed plugin table access; inspect manually"})
        if token.value != "plugin" or i + 2 >= len(tokens) or tokens[i + 1].value != "." or tokens[i + 2].kind != "identifier":
            continue
        name = tokens[i + 2].value
        end = i + 3
        if name in ("storage", "secrets"):
            if end + 1 < len(tokens) and tokens[end].value == "." and tokens[end + 1].kind == "identifier":
                name += "." + tokens[end + 1].value
                end += 2
            else:
                dynamic.append({"line": token.line, "reason": f"plugin.{name} table referenced indirectly"})
        refs.add(name)
        if i and tokens[i - 1].value == "=" and end < len(tokens) and tokens[end].value not in ("(", "{") and tokens[end].kind != "string":
            dynamic.append({"line": token.line, "reason": f"plugin.{name} referenced without a call; inspect alias use"})
        if end + 1 < len(tokens) and tokens[end].value == "(" and tokens[end + 1].kind == "string":
            value = tokens[end + 1].value
            if name == "on": events.add(value)
            if name == "has_capability": capabilities.add(value)
            if name == "register_list_item": targets.add(value)
    raw_io = sorted({tokens[i].value + "." + tokens[i + 2].value
                     for i in range(len(tokens) - 2)
                     if tokens[i].kind == "identifier" and tokens[i].value in ("io", "os") and
                     tokens[i + 1].value == "." and tokens[i + 2].kind == "identifier"})
    return {"api_min_literal": api_min, "references": sorted(refs), "events": sorted(events),
            "capabilities_checked": sorted(capabilities), "list_targets": sorted(targets),
            "raw_io_os": raw_io, "dynamic_access": dynamic}


def git(repo: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(repo), *args], encoding="utf-8").strip()


def api_exports(manager: str) -> set[str]:
    result = set()
    for table, prefix in (("plugin_funcs", ""), ("plugin_storage_funcs", "storage."), ("plugin_secrets_funcs", "secrets.")):
        body = re.search(r"static const luaL_Reg\s+" + table + r"\[\]\s*=\s*\{(.*?)\n\};", manager, re.S)
        if not body:
            raise ValueError(f"API registry {table} not found; upstream structure changed")
        result.update(prefix + name for name in re.findall(r'\{\s*"([A-Za-z_0-9]+)"\s*,', body[1]))
    return result


def inventory(player: Path, catalog: Path) -> dict:
    manager = (player / "src/plugins/plugin_manager.c").read_text(encoding="utf-8")
    header = (player / "src/plugins/plugin_manager.h").read_text(encoding="utf-8")
    exports = api_exports(manager)
    packages = []
    for folder in sorted((catalog / "plugins").iterdir()):
        script, manifest = folder / (folder.name + ".lua"), folder / "store.json"
        if not script.is_file() or not manifest.is_file():
            raise ValueError(f"Unexpected package layout: {folder.name}")
        store = json.loads(manifest.read_text(encoding="utf-8-sig"))
        data = plugin_references(script.read_text(encoding="utf-8-sig"))
        data.update({"package": folder.name, "published": store.get("publish", True),
                     "category": store.get("category", ""), "description": store.get("description", ""),
                     "companion_destinations": [entry["dest"] for entry in store.get("files", [])],
                     "script_bytes": script.stat().st_size,
                     "script_sha256": hashlib.sha256(script.read_bytes()).hexdigest(),
                     "unregistered_direct_references": sorted(set(data["references"]) - exports - {"storage", "secrets"})})
        packages.append(data)
    return {"player_revision": git(player, "rev-parse", "HEAD"),
            "catalog_revision": git(catalog, "rev-parse", "HEAD"),
            "player_api": int(re.search(r"#define PLUGIN_API_VERSION\s+(\d+)", header)[1]),
            "exported_functions": sorted(exports), "exported_function_count": len(exports),
            "package_count": len(packages), "published_package_count": sum(p["published"] for p in packages),
            "limitations": ["Static references only; not execution or functional compatibility.",
                            "Optional/fallback branches are counted too; aliases and computed references need manual inspection.",
                            "Argument/return schemas, side effects, resource paths and native DSP/decoder features need separate checks.",
                            "Git HEAD is recorded; uncommitted upstream files would invalidate reproducibility."],
            "packages": packages}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--player", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    for repo in (args.player, args.catalog):
        if git(repo, "status", "--porcelain"):
            raise SystemExit(f"Upstream checkout must be clean: {repo}")
    result = inventory(args.player, args.catalog)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"API {result['player_api']}; {result['exported_function_count']} exports; "
          f"{result['package_count']} packages, {result['published_package_count']} published. Static inventory only.")
