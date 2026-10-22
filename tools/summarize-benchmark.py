"""Validate retained raw benchmark logs and curate the reviewable measurements."""

import argparse
import hashlib
import json
from pathlib import Path
import statistics


def parse(text):
    groups = {}
    for line in text.splitlines():
        if not line.startswith("sample "):
            continue
        row = dict(part.split("=", 1) for part in line.split()[1:])
        if row["oracle"] != "PASS" or row["failed"] != "0" or row["finished"] != "64":
            raise ValueError("failed or incomplete timed work")
        key = f"{row['mode']}/{row['elements']}/{row['side']}"
        value = float(row["event_us"])
        if value <= 0:
            raise ValueError("nonpositive event time")
        groups.setdefault(key, []).append(value)
    if not groups or any(len(values) != 7 for values in groups.values()):
        raise ValueError("every comparison requires seven samples")
    return {key: {"samples_us": values, "median_us": statistics.median(values), "range_us": [min(values), max(values)], "p95_nearest_rank_us": max(values)} for key, values in groups.items()}


def self_test():
    good = "\n".join(f"sample mode=stencil elements=128 side=original repeat={i} event_us={i+1} finished=64 failed=0 oracle=PASS" for i in range(7))
    assert parse(good)["stencil/128/original"]["median_us"] == 4
    for bad in (good.replace("oracle=PASS", "oracle=FAIL", 1), "\n".join(good.splitlines()[1:])):
        try:
            parse(bad)
        except ValueError:
            continue
        raise AssertionError("failed work or missing samples were accepted")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--before", type=Path, default=Path("evidence/benchmark"))
    parser.add_argument("--after", type=Path, default=Path("evidence/benchmark-release"))
    parser.add_argument("--output", type=Path, default=Path("docs/evidence/p20/metrics-release-summary.json"))
    parser.add_argument("--accepted-window", default="coordinated Release-host slot 2026-10-22T06:41:52.3585897Z to06:42:49.3300454Z; user applications preserved")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    self_test()
    if args.self_test:
        print("PASS parser rejects failed and missing samples")
        return
    record = {"scope": "CUDA event batch time per launch, resident reused arrays,64 launches/sample; logical bandwidth is not measured DRAM; occupancy is theoretical", "repeats": 7, "campaign_isolation": {"before": "historical diagnostic; resource isolation unconfirmed; excluded from accepted numeric claims", "after": args.accepted_window}, "campaigns": {}, "raw_sha256": {}}
    host_build = args.after / "host-build.txt"
    record["host_build"] = host_build.read_text().strip()
    record["raw_sha256"][str(host_build)] = hashlib.sha256(host_build.read_bytes()).hexdigest()
    manifest = args.after / "hashes.txt"
    record["binary_and_source_manifest"] = manifest.read_text()
    record["raw_sha256"][str(manifest)] = hashlib.sha256(manifest.read_bytes()).hexdigest()
    for name, root in (("before", args.before), ("after", args.after)):
        ptx = root / "Stencil.optimized.ptx"
        shared_reads = ptx.read_text().count("ld.shared.b32")
        if shared_reads != (3 if name == "before" else 2) or "bar.sync" not in ptx.read_text():
            raise ValueError("PTX does not support the claimed load removal and barrier preservation")
        record["raw_sha256"][str(ptx)] = hashlib.sha256(ptx.read_bytes()).hexdigest()
        text = ""
        for mode in ("Stencil", "PrivateSlot"):
            path = root / f"{mode}.Measure.log"
            record["raw_sha256"][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
            text += path.read_text(encoding="utf-8-sig")
        values = parse(text)
        if len(values) != 16:
            raise ValueError("missing mode, size or comparison side")
        record["campaigns"][name] = values
    matched = args.after / "old-versus-new-stencil.log"
    record["raw_sha256"][str(matched)] = hashlib.sha256(matched.read_bytes()).hexdigest()
    record["matched_old_new"] = parse(matched.read_text(encoding="utf-8-sig"))
    record["matched_range_comparison"] = {}
    for size in (16384, 524288, 4194304, 16777216):
        old = record["matched_old_new"][f"stencil/{size}/original"]["range_us"]
        new = record["matched_old_new"][f"stencil/{size}/promoted"]["range_us"]
        verdict = "overlap"
        if new[1] < old[0]:
            verdict = "new faster; nonoverlapping full ranges"
        elif new[0] > old[1]:
            verdict = "new slower; nonoverlapping full ranges"
        record["matched_range_comparison"][str(size)] = verdict
    record["mechanism"] = {"shared_reads_before": 3, "shared_reads_after": 2, "center_store_and_CTA_barrier": "preserved", "verdict": "Consult matched_range_comparison; a cleanup win does not establish an overall tile-transform advantage over untreated PTX"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2) + "\n")
    print(args.output)


if __name__ == "__main__":
    main()
