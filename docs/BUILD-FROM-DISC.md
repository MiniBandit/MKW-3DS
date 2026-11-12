# Building from your disc

Start with the [README build guide](../README.md#building), then follow
[manual build operations](MANUAL-BUILD.md) and the
[resource-stage reference](MANUAL-RESOURCE-STAGES.md).

The game, shaders, libraries, resources and CIA are built in separate manual
steps. There is no automatic dependency download or build-all entrypoint.
Supply your own USA RMCE01 disc image and native 3DS CFL_Res.dat.
The approved banner sound is included in tools/release/artwork/.
The HOME icon and logo are supplied; generate the main model from your disc
using the separately documented banner workflow in [HOME-BANNER.md](HOME-BANNER.md).
The current manual procedure has not been fully revalidated from a fresh disc.
