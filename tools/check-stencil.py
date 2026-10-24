"""CPU-only exact stencil legality, LLVM verifier and NVPTX checks."""
import argparse
from pathlib import Path
import re
import subprocess


def run(command, log=None):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True)
    if log:
        log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {command}\n{result.stderr}")
    return result


def variants(text, radius):
    first = "left" if radius == 1 else f"value.m{radius}"
    second = "center" if radius == 1 else f"value.m{radius-1}"
    offset = "left.index" if radius == 1 else f"offset.m{radius}"
    first_ptr = "left.ptr" if radius == 1 else f"ptr.m{radius}"
    sum_name = "partial" if radius == 1 else "sum.1"
    store = next(line for line in text.splitlines() if line.strip().startswith("store i32"))
    load = next(line for line in text.splitlines() if f"%{first} = load " in line)
    return {
        "wrong-contract": text.replace(f"i32-{2*radius+1}point;", "i32-7point;"),
        "wrong-geometry": text.replace('"128,1,1"', '"128,2,1"'),
        "missing-tid": text.replace("%index = add i32 %block.base, %tid", "%index = add i32 %block.base, 0"),
        "wrong-tid": text.replace("%index = add i32 %block.base, %tid", "%index = add i32 %block.base, %bid"),
        "weak-input-align": text.replace(load, load.replace("align 4", "align 1")),
        "weak-output-align": text.replace(store, store.replace("align 4", "align 1")),
        "alias": text.replace("noalias readonly", "readonly"),
        "nosync": text.replace('"p20.stencil-contract"=', 'nosync "p20.stencil-contract"='),
        "restricted-memory-effects": text.replace('"p20.stencil-contract"=', 'memory(argmem: readwrite) "p20.stencil-contract"='),
        "explicit-unknown-memory-effects": text.replace('"p20.stencil-contract"=', 'memory(readwrite) "p20.stencil-contract"='),
        "writeonly": text.replace("noalias writeonly", "noalias"),
        "readonly": text.replace("noalias readonly", "noalias"),
        "volatile": text.replace(load, load.replace("load i32", "load volatile i32")),
        "atomic": text.replace(load, load.replace("load i32", "load atomic i32").replace(", align 4", " monotonic, align 4")),
        "atomic-store": text.replace(store, store.replace("store i32", "store atomic i32").replace(", align 4", " monotonic, align 4")),
        "flagged-sum": text.replace(f"%{sum_name} = add i32", f"%{sum_name} = add nsw i32"),
        "flagged-sum-nuw": text.replace(f"%{sum_name} = add i32", f"%{sum_name} = add nuw i32"),
        "flagged-index": text.replace("%index = add i32", "%index = add nuw i32"),
        "duplicate-offset": text.replace(f"%{offset} = sub i64 %index64, {radius}", f"%{offset} = add i64 %index64, 0"),
        "extra-offset": text.replace(f"%{offset} = sub i64 %index64, {radius}", f"%{offset} = sub i64 %index64, {radius+1}"),
        "missing-load": text.replace(f"%{sum_name} = add i32 %{first}, %{second}", f"%{sum_name} = add i32 %{second}, %{second}").replace(load + "\n", ""),
        "duplicate-load": text.replace(f"%{sum_name} = add i32 %{first}, %{second}", f"%{sum_name} = add i32 %{first}, %{first}"),
        "extra-load": text.replace("  ret void", f"  %extra = load i32, ptr addrspace(1) %{first_ptr}, align 4\n  ret void"),
        "shifted-output": text.replace("  %output.ptr =", "  %shift = add i64 %index64, 1\n  %output.ptr =").replace("%output, i64 %index64", "%output, i64 %shift"),
        "byte-input-gep": text.replace("getelementptr inbounds i32, ptr addrspace(1) %input", "getelementptr inbounds i8, ptr addrspace(1) %input"),
        "byte-output-gep": text.replace("getelementptr inbounds i32, ptr addrspace(1) %output", "getelementptr inbounds i8, ptr addrspace(1) %output"),
        "extra-op": text.replace("  ret void", "  %extra = xor i32 %tid, %bid\n  ret void"),
        "extra-store": text.replace("  ret void", store + "\n  ret void"),
        "extra-call": text.replace("  ret void", "  %extra.tid = call i32 @llvm.nvvm.read.ptx.sreg.tid.x()\n  ret void"),
        "extra-effect": text.replace("  ret void", "  fence seq_cst\n  ret void"),
        "offset-before-zext": text.replace(f"%{offset} = sub i64 %index64, {radius}", f"%before = sub i32 %index, {radius}\n  %{offset} = zext i32 %before to i64"),
    }


def normalize(text):
    return "\n".join(line for line in text.splitlines() if not line.startswith("; ModuleID"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--optimizer", type=Path)
    parser.add_argument("--llvm-bin", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--fixtures-only", action="store_true")
    args = parser.parse_args()
    if not args.fixtures_only and not args.optimizer:
        parser.error("--optimizer is required unless --fixtures-only")
    args.evidence.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[1]
    opt = args.llvm_bin / ("opt.exe" if (args.llvm_bin / "opt.exe").exists() else "opt")
    llc = args.llvm_bin / ("llc.exe" if (args.llvm_bin / "llc.exe").exists() else "llc")
    cases = 0
    for radius in (1, 4, 8, 16):
        fixture = root / "test/Inputs" / ("P20Stencil.ll" if radius == 1 else f"P20StencilR{radius}.ll")
        source = fixture.read_text()
        commuted = source.replace("mul i32 %bid, 128", "mul i32 128, %bid").replace("add i32 %block.base, %tid", "add i32 %tid, %block.base")
        for name, text, accepted in [("positive", source, True), ("commuted", commuted, True), ("explicit-unknown-memory-effects", variants(source, radius)["explicit-unknown-memory-effects"], True)] + [(name, text, False) for name, text in variants(source, radius).items() if name != "explicit-unknown-memory-effects"]:
            stem = args.evidence / f"r{radius}-{name}"
            original = stem.with_suffix(".ll")
            original.write_text(text)
            canonical = Path(str(stem) + ".canonical.ll")
            run([opt, "-S", "-passes=verify", original, "-o", canonical])
            if args.fixtures_only:
                cases += 1
                continue
            output = Path(str(stem) + ".optimized.ll")
            run([args.optimizer, original, "-o", output], Path(str(stem) + ".log"))
            run([opt, "-passes=verify", "-disable-output", output])
            transformed = output.read_text()
            if accepted:
                assert f"[{128+2*radius} x i32]" in transformed, name
                assert len(re.findall(r"load i32, ptr addrspace\(1\)", transformed)) == 3, name
                assert len(re.findall(r"load i32, ptr addrspace\(3\)", transformed)) == 2*radius, name
                assert transformed.count("call void @llvm.nvvm.barrier.cta.sync.aligned.all(i32 0)") == 1, name
                assert transformed.count("store i32") == 4, name
                for side, ir in (("original", original), ("optimized", output)):
                    ptx = Path(str(stem) + f".{side}.ptx")
                    run([llc, "-O3", "-march=nvptx64", "-mcpu=sm_120", ir, "-o", ptx])
                    if side == "optimized":
                        ptx_text = ptx.read_text()
                        assert "bar.sync" in ptx_text and "ld.shared.b32" in ptx_text and "st.shared.b32" in ptx_text
            else:
                output_canonical = Path(str(stem) + ".optimized.canonical.ll")
                run([opt, "-S", "-passes=verify", output, "-o", output_canonical])
                assert normalize(canonical.read_text()) == normalize(output_canonical.read_text()), f"unsupported r{radius} {name} changed"
            cases += 1
    print(f"PASS {cases} stencil {'fixture verifier' if args.fixtures_only else 'legality/verifier/PTX'} cases")


if __name__ == "__main__":
    main()
