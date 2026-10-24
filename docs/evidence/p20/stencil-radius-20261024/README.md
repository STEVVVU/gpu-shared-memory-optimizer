# Fixed-radius stencil extension evidence

## Verdict

The radius 1/4/8/16 matcher and replacement passed the local CPU legality, LLVM verifier, NVPTX, and native CUDA checks. The performance result is inconclusive for an overall win. Resident kernel batches show a lower median for radius 16 at 16,777,216 elements in both independent campaigns, but the complete transaction ranges overlap in both and other radius/size cases include slower or overlapping results. These observations do not establish a general speedup.

## Correctness and scope

Verification run `run-20261024-150156901` completed Launch, Doctor, Drive, Evidence and Cleanup. The radius extension passed 132 CPU legality cases, LLVM verification and `llc` lowering to sm_120 PTX. The native RTX 5080 Laptop GPU runner checked radii 4, 8 and 16 at 1, 3 and 17 CTAs. For each size it tested ordinary values, full-bit random values, and distinct halos with wrapping values, poisoned outputs, and matched the independent modulo-i32 CPU oracle. Radius 1 is included in the same maintained verification flow. The preserved GPU receipts and source hash list are in this directory.

The transform remains limited to exact padded scalar-i32 sums at radii 1, 4, 8 or 16, a 128-thread one-dimensional CTA, aligned separate input/output arrays, and the documented host halo and length preconditions. The pass does not add host checks or support generic loop transformations.

## Measurement method

Campaigns B and C are separate complete seven-repeat alternating runs on the RTX 5080 Laptop GPU with CUDA 13.0 and LLVM 23.1.4. Both use `llc -O3 -mcpu=sm_120` PTX, an `nvcc -O2` host runner, 16,384 / 524,288 / 4,194,304 / 16,777,216 output elements, and 64 launches per CUDA-event sample. Every measurement checks the output against the same CPU oracle. The table reports medians and full seven-sample ranges in microseconds. Each cell is baseline then transformed.

CUDA-event rows time batches on resident arrays. They exclude allocation, transfers, output readback and oracle work, and can include host-submission gaps. Transaction rows time a warm-context and warm-module application path containing allocation, H2D, output poisoning, one kernel, synchronization, D2H and free; the oracle runs outside that timer. The warm transaction still excludes context and module initialization. `logical_effective_GBps` counts logical input and output bytes and does not measure DRAM bandwidth.

## Campaign B

| Radius | Elements | CUDA event, baseline to transformed (µs median [range]) | Full transaction, baseline to transformed (µs median [range]) |
|---:|---:|---|---|
| 1 | 16,384 | 6.121 [5.441, 8.710] -> 6.042 [5.160, 6.505] | 65.100 [61.100, 97.800] -> 64.500 [57.300, 70.900] |
| 1 | 524,288 | 7.761 [7.312, 8.368] -> 8.373 [7.728, 10.129] | 980.000 [792.000, 1397.100] -> 1024.400 [854.600, 6041.100] |
| 1 | 4,194,304 | 20.048 [18.334, 21.471] -> 23.023 [20.656, 27.137] | 4097.800 [4004.800, 7212.000] -> 4759.700 [4118.600, 6419.800] |
| 1 | 16,777,216 | 281.810 [269.055, 285.999] -> 281.922 [269.136, 286.624] | 15700.900 [14180.900, 19046.600] -> 16549.800 [15404.700, 17357.300] |
| 4 | 16,384 | 5.808 [5.353, 7.544] -> 5.774 [5.580, 6.711] | 66.000 [60.000, 182.200] -> 65.000 [64.300, 67.800] |
| 4 | 524,288 | 8.993 [8.033, 22.352] -> 8.736 [8.193, 9.584] | 888.100 [778.600, 1171.300] -> 1007.200 [957.700, 1128.000] |
| 4 | 4,194,304 | 21.073 [21.024, 23.808] -> 29.050 [28.907, 31.760] | 4033.800 [3763.400, 5273.100] -> 3905.900 [3727.200, 5366.300] |
| 4 | 16,777,216 | 270.753 [177.996, 297.520] -> 269.879 [180.288, 311.936] | 17112.800 [14790.600, 32653.400] -> 18037.100 [14757.100, 20285.300] |
| 8 | 16,384 | 6.531 [5.693, 7.530] -> 6.145 [5.835, 6.941] | 63.700 [58.500, 67.000] -> 64.100 [63.500, 68.800] |
| 8 | 524,288 | 10.265 [8.569, 12.518] -> 10.696 [9.143, 13.472] | 956.200 [835.400, 1011.800] -> 936.000 [905.400, 1868.200] |
| 8 | 4,194,304 | 34.593 [33.264, 38.672] -> 43.840 [43.248, 56.592] | 4111.300 [3778.900, 5653.400] -> 4395.400 [3936.400, 5565.900] |
| 8 | 16,777,216 | 312.782 [305.089, 338.427] -> 323.521 [285.347, 353.754] | 20536.200 [16629.800, 23146.500] -> 20355.200 [16287.100, 21740.400] |
| 16 | 16,384 | 9.252 [8.834, 12.601] -> 9.693 [8.756, 12.187] | 102.600 [99.800, 113.100] -> 103.500 [100.200, 109.700] |
| 16 | 524,288 | 10.781 [10.672, 12.352] -> 11.066 [10.659, 14.158] | 875.200 [792.700, 901.200] -> 895.000 [855.200, 1062.200] |
| 16 | 4,194,304 | 62.815 [57.807, 78.935] -> 72.048 [70.701, 86.256] | 3954.200 [3743.100, 8837.200] -> 4173.300 [3845.900, 6136.200] |
| 16 | 16,777,216 | 372.206 [362.004, 375.574] -> 341.648 [320.668, 357.182] | 15849.200 [14459.900, 17343.900] -> 16358.900 [15379.800, 42505.400] |

## Campaign C

| Radius | Elements | CUDA event, baseline to transformed (µs median [range]) | Full transaction, baseline to transformed (µs median [range]) |
|---:|---:|---|---|
| 1 | 16,384 | 5.936 [5.197, 12.139] -> 6.085 [5.882, 12.352] | 68.200 [60.700, 212.300] -> 66.000 [65.400, 250.200] |
| 1 | 524,288 | 7.048 [6.596, 8.035] -> 8.456 [7.942, 9.511] | 891.300 [857.400, 1425.100] -> 897.600 [854.300, 1052.500] |
| 1 | 4,194,304 | 18.291 [17.942, 20.044] -> 22.558 [20.674, 29.834] | 5051.400 [4417.200, 6230.900] -> 4225.800 [4005.000, 5836.800] |
| 1 | 16,777,216 | 194.832 [180.478, 269.375] -> 192.993 [178.016, 297.952] | 15733.600 [14764.100, 18190.700] -> 16975.900 [14915.800, 17455.900] |
| 4 | 16,384 | 7.029 [6.142, 7.513] -> 7.149 [6.598, 7.718] | 72.500 [65.300, 79.300] -> 73.400 [71.100, 215.000] |
| 4 | 524,288 | 9.514 [8.083, 21.761] -> 10.953 [9.807, 12.578] | 1054.000 [879.300, 1110.100] -> 906.400 [811.500, 1141.000] |
| 4 | 4,194,304 | 34.063 [30.913, 43.853] -> 37.504 [34.864, 53.665] | 4185.200 [3900.300, 11514.400] -> 4205.000 [3909.000, 5981.200] |
| 4 | 16,777,216 | 285.204 [272.190, 299.727] -> 284.657 [269.153, 328.113] | 14485.700 [13618.000, 17519.500] -> 14131.500 [13827.000, 14301.900] |
| 8 | 16,384 | 5.856 [5.534, 7.649] -> 5.968 [5.122, 6.960] | 68.500 [63.700, 94.300] -> 64.600 [61.700, 112.100] |
| 8 | 524,288 | 8.656 [8.001, 9.713] -> 11.062 [8.896, 19.875] | 957.200 [834.800, 4421.200] -> 947.100 [864.700, 1161.000] |
| 8 | 4,194,304 | 35.837 [33.187, 48.432] -> 41.486 [41.169, 53.743] | 4108.600 [3971.800, 9048.600] -> 4836.000 [3797.900, 7665.600] |
| 8 | 16,777,216 | 284.095 [277.215, 293.725] -> 269.598 [268.834, 284.559] | 14552.500 [13497.300, 17723.000] -> 15657.600 [13771.600, 24210.900] |
| 16 | 16,384 | 7.325 [7.122, 17.590] -> 7.263 [6.527, 9.011] | 68.600 [64.600, 156.000] -> 66.500 [64.000, 274.700] |
| 16 | 524,288 | 11.950 [10.753, 14.801] -> 11.072 [10.741, 11.536] | 925.500 [827.600, 1032.900] -> 1051.700 [804.000, 15886.700] |
| 16 | 4,194,304 | 60.864 [57.754, 77.667] -> 68.575 [65.755, 78.240] | 5309.600 [4614.200, 9022.000] -> 5172.700 [4116.600, 6623.600] |
| 16 | 16,777,216 | 372.703 [362.799, 405.614] -> 337.615 [319.855, 357.297] | 17431.700 [15486.100, 19413.100] -> 16724.300 [15845.500, 21510.900] |

## Interpretation and limits

At radius 16 and 16,777,216 elements, B's event median fell from 372.206 to 341.648 µs, and C's fell from 372.703 to 337.615 µs. Both seven-sample event ranges are nonoverlapping. This is a repeatable result for that resident-array microbenchmark cell on this machine. It does not carry through as a demonstrated transaction win: B's transaction median was 15,849.2 to 16,358.9 µs, with overlapping full ranges, while C's was 17,431.7 to 16,724.3 µs, also with overlapping ranges. Many smaller event cases overlap or have a slower transformed median. The radius 4 and 8 4M cases in B are slower with nonoverlapping ranges; in C the radius 1 4M case is slower with nonoverlapping ranges, while radius 4 and 8 4M ranges overlap. No combined or pooled campaign result is used.

Nsight Compute 2025.3.1.4 initially exited with `ERR_NVGPUCTRPERM`. After the operator enabled performance-counter access, its injected runner failed CUDA initialization and its metric query reported `Driver could not be loaded`. An isolated, hash-verified 2025.4.1.2 tool returned the same driver-query error. The unchanged CUDA runner passed native smoke checks outside the profiler. Both failed profiler attempts remain retained. The hardware bottleneck is unestablished; the failure cause is unresolved. Logical effective GB/s is not evidence of DRAM bandwidth, and theoretical occupancy and static instruction counts do not identify a hardware limiter. The host used an exclusive owned GPU slot without concurrent owned GPU jobs; normal desktop GPU clients remained active, so residual device noise is possible. Linux CPU verification runs through the linked repository workflow separately from these local GPU measurements.

## Reproduction and retained artifacts

From PowerShell 7 at the repository root, with the configured LLVM 23.1.4 and CUDA 13.0 toolchains, build and validate with `tools/verify.ps1 Launch`, `Doctor`, `Drive`, `Evidence`, and `Cleanup`. In an exclusive GPU slot, reproduce a fresh campaign with `tools/benchmark-stencils.ps1 Prepare -RunDirectory evidence/benchmark-stencil-radius-<new-id>`, then `Smoke` and `Measure`; summarize it with `python tools/summarize-stencils.py --run evidence/benchmark-stencil-radius-<new-id> --output evidence/benchmark-stencil-radius-<new-id>/summary.json --accepted-window <confirmed-slot>`. The run directory must be new. The maintained campaign and historical manifests remain unchanged.

This report directory preserves B and C inputs and outputs as original/transformed LLVM IR and PTX, source/build manifests, per-radius Measure logs, B Smoke receipts, both parsed summaries, native GPU correctness receipts, and the failed counter-profile attempt. `native-hashes.tsv` maps every published file to its source and records both native-source SHA-256 and published LF-copy SHA-256. Frozen source manifests and their internal hashes were copied without changing their contents; line endings were normalized for publication. The host-specific executable path was removed from the copied profiler log and provenance record. The downloaded Nsight archive SHA-256 was `0ad1c310f38cdfc17e85f6efa2da60e7628bd2db81eba8ec487ab6d49c1bcdef`; its official CUDA redistribution manifest is linked in `ncu-provenance.json`.
