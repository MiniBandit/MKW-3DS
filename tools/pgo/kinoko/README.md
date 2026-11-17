# Kinoko profiles (PGO)

Profile counts for `vendor/kinoko/source` (one `.gcda` per source, same paths), used
by `tools/build.py` and `tools/diag/compile.py`: a Kinoko source with a profile here
is compiled with `-fprofile-use -fprofile-partial-training -fno-unroll-loops
-fno-peel-loops` (code the training never ran is optimized as without a profile;
no unrolling, since the Old 3DS is instruction-cache bound). `MKW_KINOKO_PGO=off`
builds without them. Profiles only steer optimization (branch layout, hot/cold
code, inlining); floating-point code and physics results are unchanged.

A profile that no longer matches its source (the source changed) is ignored for
the functions that changed (`-Wno-coverage-mismatch`); regenerate after larger
Kinoko changes:

1. `MKW_KINOKO_PGO=generate` with `tools/diag/compile.py` (DIAG variant `o3dstest`
   for Old 3DS paths) builds an instrumented ELF in `build/diag-o3dstest-pgogen/`.
2. Run races with it (Azahar in Old 3DS mode is fine: the counts are the same as on
   the console). At each race end the instrumented code writes its counters to
   `sdmc:/3ds/mkw-3ds/pgo/<path below objects/>`, merged over the races.
3. Replace this folder's `.gcda` files with the contents of `pgo/vendor/kinoko/source/`
   (and `tools/pgo/port/`'s with `pgo/src/`: the same training run profiles both).

Current profile: old3ds ba218cd, Mario Circuit and Coconut Mall, 12 racers (CPU AI driving
the player), Azahar Old 3DS mode, DIAG o3dstest build (trained with the port's profiles).
