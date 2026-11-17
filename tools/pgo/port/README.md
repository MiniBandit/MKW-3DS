# Port profiles (PGO), Old 3DS builds

Profile counts for the port's own sources (`src/`, one `.gcda` per source, same
paths), used by `tools/diag/compile.py` in the Old 3DS variants (`MKW_DIAG_VARIANT`
`o3dstest` and `o3dsrelease`). There the port's sources are also
built with LTO (`MKW_PORT_LTO=off`: without), so Kinoko's small getters inline into the
port's calls. A source with a profile here is compiled with the same flags as Kinoko's
(`-fprofile-use -fprofile-partial-training -fno-unroll-loops -fno-peel-loops`), plus
`-fprofile-correction` (the port's threads update counters without atomics); code the
training never ran is optimized as without a profile. `MKW_PORT_PGO=off` ignores them.
Profiles and LTO only steer code generation: physics and the picture are unchanged
(checked with the 12-kart state digest and byte-compared race frames). `tools/build.py`
(the New 3DS release) does not use them.

Current profiles: old3ds ba218cd, DIAG o3dstest, Azahar Old 3DS, Mario Circuit and Coconut
Mall with 12 racers (CPU AI driving the player), default settings (core-1 jobs off).

A profile that no longer matches its source is ignored for the functions that changed.
Regenerate together with Kinoko's (`tools/pgo/kinoko/README.md`): one training build
(`MKW_KINOKO_PGO=generate`) profiles both; copy `sdmc:/3ds/mkw-3ds/pgo/src/` here.
Train on two races (`flower2-autoplay.txt`: Mario Circuit, Coconut Mall): the fully
instrumented build hung loading a third race. Training runs value profiling only in Kinoko.
