

## Upstream integration, 2026-10-19

The integration branch now includes upstream commits 76f187b and 9e0da8b
(base 9e0da8b7f129fffc0608165fc8a181113f6a3329). The working changes remain
uncommitted; main was left at the revision pulled by Dad. GitHub Desktop's
original stash was retained and an independent source archive was saved.

The merged render-quality header retains both the local mipmap switch and
upstream Old 3DS settings (object low-detail distance 1500, particle budget 48).
Upstream release performance-marker support and documentation remain unchanged.
The builder retains all migrated cook steps and upstream manual content-stamp
checks. The manual stamp and Windows compatibility stamp are the previously
validated migration values, rather than upstream's incompatible asset stamps;
the merged recipe is recorded as reviewed without recooking unchanged assets.

All 197 other local files match the pre-update stash byte for byte. The two
overlapping files merged cleanly. Manual-stamp checks, reduced/full-quality
smoke checks, cook-cache fingerprint checks and a native release/external-assets
build passed. No package or runtime test was made. Evidence is in
/Volumes/S10/mkw-3ds-build/changes/main-project-upstream-20261019/.
