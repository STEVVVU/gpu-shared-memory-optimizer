"""Validate the separate dense-stencil campaign without rewriting historical results."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

MODES = ("stencil", "stencil-r4", "stencil-r8", "stencil-r16")
SIZES = (16384, 524288, 4194304, 16777216)
SIDES = ("original", "promoted")


def parse(text):
    samples = {"sample": {}, "transaction": {}}
    order = {"sample": {}, "transaction": {}}
    for line in text.splitlines():
        kind = line.split(" ", 1)[0]
        if kind not in samples:
            continue
        row = dict(part.split("=", 1) for part in line.split()[1:])
        mode, size, side, repeat = row["mode"], int(row["elements"]), row["side"], int(row["repeat"])
        if mode not in MODES or size not in SIZES or side not in SIDES or repeat not in range(7):
            raise ValueError("unexpected comparison dimension")
        if row["oracle"] != "PASS" or row["failed"] != "0" or row["finished"] != ("64" if kind == "sample" else "1"):
            raise ValueError("failed or incomplete timed work")
        field = "event_us" if kind == "sample" else "host_transaction_us"
        value = float(row[field])
        if not math.isfinite(value) or value <= 0:
            raise ValueError("nonfinite or nonpositive time")
        key = f"{mode}/{size}/{side}"
        values = samples[kind].setdefault(key, {})
        if repeat in values:
            raise ValueError("duplicate sample")
        values[repeat] = value
        order[kind].setdefault((mode, size), []).append((repeat, side))
    expected_order = [(repeat, SIDES[(repeat + position) % 2]) for repeat in range(7) for position in range(2)]
    result = {}
    for kind in samples:
        if len(samples[kind]) != 32:
            raise ValueError(f"missing {kind} mode, size or side")
        for mode in MODES:
            for size in SIZES:
                if order[kind].get((mode, size)) != expected_order:
                    raise ValueError("nonalternating or incomplete repeats")
        result[kind] = {}
        for key, values in samples[kind].items():
            if set(values) != set(range(7)):
                raise ValueError("seven distinct repeats required")
            ordered = [values[repeat] for repeat in range(7)]
            result[kind][key] = {
                "samples_us": ordered, "median_us": statistics.median(ordered),
                "range_us": [min(ordered), max(ordered)],
            }
    return result


def self_test():
    lines = []
    for mode in MODES:
        for size in SIZES:
            for repeat in range(7):
                for position in range(2):
                    side = SIDES[(repeat + position) % 2]
                    dims = f"mode={mode} elements={size} side={side} repeat={repeat}"
                    lines += [
                        f"sample {dims} event_us={repeat+1} finished=64 failed=0 oracle=PASS",
                        f"transaction {dims} host_transaction_us={repeat+10} finished=1 failed=0 oracle=PASS",
                    ]
    good = "\n".join(lines)
    assert parse(good)["sample"]["stencil/16384/original"]["median_us"] == 4
    for bad in (good.replace("oracle=PASS", "oracle=FAIL", 1),
                good.replace("event_us=1 ", "event_us=nan ", 1),
                good.replace("finished=64", "finished=0", 1),
                "\n".join(lines[1:]), good + "\n" + lines[0],
                good.replace("side=original", "side=unknown", 1),
                "\n".join(line for line in lines if not line.startswith("transaction "))):
        try:
            parse(bad)
        except (ValueError, KeyError):
            continue
        raise AssertionError("bad/incomplete campaign accepted")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", type=Path, default=Path("evidence/benchmark-stencil-radius"))
    parser.add_argument("--output", type=Path, default=Path("docs/evidence/p20/stencil-radius-summary.json"))
    parser.add_argument("--accepted-window", default="unconfirmed; diagnostic only")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    self_test()
    if args.self_test:
        print("PASS campaign dimensions, alternation, finite timings, failure and completeness checks")
        return
    manifest_path = args.run / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    hashes = {str(manifest_path): hashlib.sha256(manifest_path.read_bytes()).hexdigest()}
    text = ""
    for radius in (1, 4, 8, 16):
        path = args.run / f"R{radius}.Measure.log"
        text += path.read_text(encoding="utf-8-sig") + "\n"
        hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    values = parse(text)
    comparisons = {}
    for kind, groups in values.items():
        comparisons[kind] = {}
        for mode in MODES:
            for size in SIZES:
                original, promoted = (groups[f"{mode}/{size}/{side}"] for side in SIDES)
                low, high = original["range_us"], promoted["range_us"]
                verdict = "overlapping full ranges"
                if high[1] < low[0]:
                    verdict = "promoted lower; nonoverlapping full ranges"
                elif high[0] > low[1]:
                    verdict = "promoted higher; nonoverlapping full ranges"
                comparisons[kind][f"{mode}/{size}"] = {
                    "original_over_promoted_median_ratio": original["median_us"] / promoted["median_us"],
                    "range_comparison": verdict,
                }
    record = {
        "scope": "resident CUDA-event batches and separate warm application transactions, microseconds",
        "event_scope": "64 reused-array launches/sample; excludes allocation/transfers/readback; may include host submission gaps",
        "transaction_scope": manifest["transaction"],
        "campaign_window": args.accepted_window,
        "limiter": "unestablished; requires separate profiling evidence before an overall performance conclusion",
        "manifest": manifest, "raw_sha256": hashes,
        "measurements": values, "comparisons": comparisons,
    }
    if args.output.exists():
        raise FileExistsError("summary exists; choose a new output to preserve it")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2) + "\n")
    print(args.output)


if __name__ == "__main__":
    main()
