#!/usr/bin/env python3
# What a CI stage's compiles spent their time on, from clang's -ftime-trace:
# the traces the stage wrote (newer than its start), summed -- each unit's
# whole time, then what the units spent it on together: the files parsed,
# the classes and functions instantiated, the modules read, the code made.
# Written as Markdown to the job's summary and beside the traces, which are
# packed for the artifact.
#
#   time_trace.py <stage> <build dir> <file whose time is the stage's start>
import json, os, pathlib, sys, tarfile
from collections import defaultdict

stage, build, marker = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
began = marker.stat().st_mtime
traces = [p for p in build.rglob("*.json")
          if p.stat().st_mtime >= began and "CMakeFiles" in p.parts and not p.name.startswith("compile_commands")]

units = []
by_kind = defaultdict(lambda: defaultdict(lambda: [0, 0, set()]))  # kind -> detail -> [microseconds, count, units]
KINDS = {"Source": "files parsed (headers and module units read as text)",
         "ParseClass": "classes parsed",
         "InstantiateClass": "classes instantiated",
         "InstantiateFunction": "functions instantiated",
         "PerformPendingInstantiations": "pending instantiations",
         "CodeGen Function": "functions made into code",
         "OptFunction": "functions optimised",
         "Module Load": "modules loaded",
         "Module Compile": "modules compiled"}
for path in traces:
    try:
        events = json.loads(path.read_text()).get("traceEvents", [])
    except (OSError, ValueError):
        continue
    total = 0
    for e in events:
        name, dur = e.get("name", ""), e.get("dur", 0)
        if name == "Total ExecuteCompiler":
            total = max(total, dur)
        elif name in KINDS and e.get("ph") == "X":
            detail = (e.get("args") or {}).get("detail", "")[:160]
            slot = by_kind[name][detail]
            slot[0] += dur
            slot[1] += 1
            slot[2].add(path)
    if total:
        units.append((total, str(path.relative_to(build))))

units.sort(reverse=True)
out = [f"## Compile time, {stage}: {len(units)} units, {sum(t for t, _ in units) / 1e6:.0f} s", "",
       "| s | unit |", "|---:|---|"]
out += [f"| {t / 1e6:.1f} | `{u.replace('.json', '')}` |" for t, u in units[:40]]
for kind, said in KINDS.items():
    rows = sorted(by_kind[kind].items(), key=lambda kv: kv[1][0], reverse=True)[:25]
    if not rows:
        continue
    out += ["", f"### {said} (summed over the units)", "", "| s | times | units | what |", "|---:|---:|---:|---|"]
    out += [f"| {us / 1e6:.1f} | {n} | {len(where)} | `{detail or '(none)'}` |" for detail, (us, n, where) in rows]
text = "\n".join(out) + "\n"
print(text)
(build / f"time-trace-{stage}.md").write_text(text)
if os.environ.get("GITHUB_STEP_SUMMARY"):
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
        summary.write(text)
with tarfile.open(build / f"time-trace-{stage}.tar.gz", "w:gz") as pack:
    pack.add(build / f"time-trace-{stage}.md", arcname=f"time-trace-{stage}.md")
    for path in traces:
        pack.add(path, arcname=str(path.relative_to(build)))
