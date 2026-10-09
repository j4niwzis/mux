"""Generate About metadata from the configured provider and Cargo graph."""
import argparse
import json
from pathlib import Path
import subprocess


def git(source, *args):
    if not source:
        return ""
    # Archives inside build/ belong to the outer checkout from git's point
    # of view. Never report mux's commit as a dependency's revision.
    root = subprocess.run(["git", "-C", str(source), "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    if root.returncode != 0 or Path(root.stdout.strip()).resolve() != Path(source).resolve():
        return ""
    result = subprocess.run(["git", "-C", str(source), *args], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ""


def license_text(source, explicit=""):
    root = Path(source) if source else None
    if root is None or not root.is_dir():
        return ""
    candidates = [root / explicit] if explicit else []
    candidates += sorted(p for p in root.iterdir() if p.is_file() and
                         p.name.upper().split(".")[0] in {"LICENSE", "LICENCE", "COPYING", "NOTICE", "COPYRIGHT"})
    candidates += sorted(p for p in root.glob("LICENSES/*") if p.is_file())
    return "\n\n".join(p.name + "\n" + p.read_text(errors="replace") for p in dict.fromkeys(candidates) if p.is_file())


def cargo_records(record):
    root = Path(record["source"])
    manifest = root / (record.get("CARGO_MANIFEST") or "Cargo.toml")
    if record.get("IMPORT") != "cargo" or not manifest.is_file():
        return []
    command = ["cargo", "metadata", "--format-version", "1", "--locked", "--manifest-path", str(manifest)]
    if record.get("CARGO_TARGET"):
        command += ["--filter-platform", record["CARGO_TARGET"]]
    if record.get("CARGO_NO_DEFAULT_FEATURES") in ("ON", "TRUE", "1"):
        command += ["--no-default-features"]
    if record.get("CARGO_FEATURES"):
        command += ["--features", record["CARGO_FEATURES"].replace(";", ",")]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    metadata = json.loads(result.stdout)
    packages = {p["id"]: p for p in metadata["packages"]}
    nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
    roots = [p["id"] for p in packages.values() if p["name"] == record["CARGO_PACKAGE"]]
    if not roots:
        raise ValueError("Cargo package missing from metadata: " + record["name"])
    ids = {key: "rust:" + p["name"] + "@" + p["version"] for key, p in packages.items()}
    record["dependencies"] += [ids[key] for key in roots]
    pending, seen, out = list(roots), set(), []
    while pending:
        key = pending.pop()
        if key in seen:
            continue
        seen.add(key)
        p = packages[key]
        deps = [d["pkg"] for d in nodes[key]["deps"] if any(k["kind"] != "dev" for k in d["dep_kinds"])]
        pending.extend(deps)
        out.append(dict(name=ids[key], label=p["name"], version=p["version"], revision=p.get("source") or "",
                        license=p.get("license") or "See license text", repository=p.get("repository") or "",
                        license_text=license_text(Path(p["manifest_path"]).parent, p.get("license_file") or ""),
                        dependencies=[ids[d] for d in deps]))
    return out


def literal(value):
    # Raw literals preserve UTF-8 without C++ hex-escape adjacency hazards.
    delimiter = "mux_about"
    while ")" + delimiter + '"' in value:
        delimiter += "x"
    if len(delimiter) > 16:
        raise ValueError("Cannot delimit metadata string")
    return 'R"' + delimiter + "(" + value + ")" + delimiter + '"'


def generate(args):
    records = json.loads(Path(args.input).read_text())
    crates = []
    for record in records:
        crates += cargo_records(record)
        record["label"] = record["name"]
        record["revision"] = git(record["source"], "rev-parse", "HEAD") or record.get("GIT_TAG", "")
        record["license"] = record.get("LICENSE") or "See license text"
        record["repository"] = ("https://github.com/" + record["GITHUB_REPOSITORY"] if record.get("GITHUB_REPOSITORY")
                                else record.get("GIT_REPOSITORY") or record.get("URL") or "")
        record["license_text"] = license_text(record["source"])
        record["version"] = record["version"] or record["revision"] or "Not reported by installed package"
    records += crates
    records = sorted({r["name"]: r for r in records}.values(), key=lambda r: r["label"].casefold())
    commit = git(args.source, "rev-parse", "HEAD") or "Unknown"
    branch = git(args.source, "describe", "--tags", "--exact-match") or git(args.source, "symbolic-ref", "--short", "HEAD")
    # CI checkouts are detached: retain the ref that actually selected HEAD.
    import os
    branch = branch or os.environ.get("GITHUB_HEAD_REF") or os.environ.get("GITHUB_REF_NAME") or "Detached HEAD"
    metadata = {"version": args.version, "commit": commit, "branch": branch,
                "license": "AGPL-3.0-only", "repository": "https://github.com/j4niwzis/mux",
                "license_text": license_text(args.source)}
    text = "// Generated from this build's resolved dependencies.\nnamespace about_data {\n"
    for key, value in metadata.items():
        text += "inline constexpr std::string_view " + key + " = " + literal(value) + ";\n"
    text += "inline const std::vector<library_info> libraries{\n"
    for record in records:
        fields = [record[k] for k in ("name", "label", "version", "revision", "license", "repository", "license_text")]
        text += "  {" + ", ".join(map(literal, fields)) + ", {" + ", ".join(map(literal, record["dependencies"])) + "}},\n"
    text += "};\n"
    primary = [r["name"] for r in records if r.get("primary", False)]
    text += "inline constexpr std::array<std::string_view, " + str(len(primary)) + "> primary_libraries{" + ", ".join(map(literal, primary)) + "};\n}\n"
    path = Path(args.output)
    if not path.is_file() or path.read_text() != text:
        path.write_text(text)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for option in ("source", "version", "input", "output"):
        parser.add_argument("--" + option, required=True)
    generate(parser.parse_args())
