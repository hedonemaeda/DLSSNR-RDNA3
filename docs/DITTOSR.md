# Ditto SR in NR Control

Implementation base: `188324b759e9d34d4bed7361946bf034d69db0b9`
(the RDNA3 0.0.8.3 port with the NR Control cursor/FG fixes).
DittoSR reference: release `v0.4.0`, commit
`d37519ce85a84a4cc3083cad17a83335bee0f6d4`.

## Feature and acceptance matrix

| Area | Intended behavior | Verification |
| --- | --- | --- |
| NR Control | Add Ditto SR to the existing Upsampling selection; persist per layer as NRLookUpsample=4 | Config and UI checks; target menu test |
| Before SR | Resolve NR at its working extent, then run Ditto SR to the layer's 100% extent; leave OptiScaler's next SR stage and dimensions intact | CPU dimensions/contracts; Vulkan integration test; target FSR 4 chain |
| Example | 1280x720 at 0.33 rounds to 422x238; NR -> Ditto SR -> 1280x720 -> OptiScaler SR -> 1920x1080 | Explicit dimension check |
| Temporal inputs | Use the game's depth, motion and jitter, mapped to NR's grid; keep each layer's history separate | Shader compilation; source/CPU checks; target motion/camera-cut tests |
| Scope | Linux/Proton NR Control, RenoDX processing, Before SR only; explain unavailable conditions | Build; target unsupported placement check |
| Lifecycle | Reset on mode/settings changes, cuts and resize; retire GPU resources using existing completion tracking | Integration review and software Vulkan lifecycle checks |
| Existing behavior | Keep default modes, NR network kernels, weights, RDNA4 defaults and Windows sources unchanged | Existing shader byte comparison; builds; Git diff |
| Distribution | Ship all needed shaders and MIT/BSD notices; no NVIDIA DLL/model/weights | Package audit |

## Use

Install the update's ASI into `OptiScaler/plugins/` and its five SPIR-V files
into the game's `dlssnr-amd/shaders/renodx/`, with their notices. Keep the
existing NR models, other shaders, OptiScaler and FG provider. Back up the ASI
and shader folder before replacing them. The update's README gives the paths.

In NR Control, choose a RenoDX layer, **Placement: Before the upscaler**,
**Resolution scale: 0.33**, **Upsampling: Ditto SR**. Disable **Follow render
resolution**, which otherwise overrides the scale. Keep FSR 4 (or another SR)
selected in OptiScaler. At 100%, the normal NR path runs with no Ditto dispatch.
An unsupported placement or unavailable guides/jitter is reported as a layer
error; the chosen setting is kept. This integration is in the Linux/Proton tree.

## Temporal contract

The final active model pass resolves into a decoded image at NR's resolution.
Ditto's convert, motion, upscale and sharpening passes reconstruct that image
to the layer's original extent. SDR uses the legacy decoder and optional black
restore; HDR uses the v6 resolve into linear colour, followed by the usual HDR
commit. Earlier model passes retain their existing codec processing.

The reference and guides honor their extents and guide subrect origins. Guide
preparation keeps foreground depth and the matching motion; motion is in
normalized screen units. Raw depth views use the depth aspect. The preparation
shader uses RGBA32F motion storage so it introduces no requirement for the
optional StorageImageExtendedFormats capability.

Both the reduced input and the enlarged output retain the game's jitter in
normalized screen coordinates. Ditto's **relative** sample jitter is zero;
history reprojection includes the phase difference between game frames (already
present when NGX declares jittered vectors). This keeps the downstream SR's
input consistent with the original jitter parameters without changing the
game's NGX block. Standalone Ditto's de-jittered output would not meet that
contract. This adaptation still needs assessment in a real cascaded SR run.

Each layer owns its Ditto history. Reset/cuts, stage resize, NR off/on,
preprocessing, settings affecting reconstruction and mode changes invalidate
it. Unselected Ditto images retire under the stage's existing GPU completion
tracking. Guide views and descriptor pools use that same recording lifetime;
constants are uploaded with recorded Vulkan commands and explicit barriers.

## Checks on homeserver

Completed with worktree-local dependencies from `bash fetch_deps.sh` and
`bash linux/build/bootstrap_ubuntu_tools.sh`:

- The native integration fixture executes real codec/Ditto shaders with a
  deterministic identity network under llvmpipe. SDR/HDR, 33%/50%/100%, odd
  extents, guide subrects, true D32 depth, multiple model passes, an inactive
  final pass, Preprocess, temporal look, history-ring reuse, resets and live
  mode changes are covered. The test does not load any model weights.
- The phase fixture checks jittered and unjittered vector contracts and the
  1280x720 at 0.33 -> 422x238 rounding.
- All five new SPIR-V files pass `spirv-val --target-env vulkan1.1`.
- Original RenoDX SPIR-V files compare **EXACT (29/29)** before and after.
  Ditto's five vendored HLSL files match its pinned upstream source unchanged.
- `check_paths.sh`, `check_imgui.sh` and the existing NR Control presentation
  fixture pass. Network source, planner, goldens and Windows source are unchanged.

Run the fixture with a Vulkan driver available:

```sh
source build/local-tools.env
bash linux/build/build_renodx.sh build/shaders-renodx
g++ -std=c++17 -O1 -Itoolchain/Vulkan-Headers/include -Ilinux/src/core \
    -Ilinux/src/pe linux/test/dittosr_stage.cpp linux/src/core/nr_renodx.cpp \
    -lvulkan -o build/dittosr-stage
NR_RENODX_ROBUST=1 build/dittosr-stage build/shaders-renodx
NR_GPU=rdna3 bash linux/build/build_asi.sh
bash linux/build/package_dittosr_update.sh
```

For the software run, Mesa 25.2.8's llvmpipe and validation layers were downloaded
and extracted inside `toolchain/vulkan-test/`, without changing the system.
`VK_DRIVER_FILES`, `LD_LIBRARY_PATH` and `VK_LAYER_PATH` select those local files.
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` and
`VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`
enable API and synchronization checks. `NR_RENODX_ROBUST=1` disables the existing
RenoDX pipeline-robustness override: the fixture's device does not enable that
optional feature. This test exercises the new passes without that dependency.

## Target acceptance still required

AMD GPU image quality, frame times and game/FSR 4 compatibility are **unrun**.
The reported target is Z1 Extreme / Dragon's Dogma 2 / DX12 / Proton CachyOS /
Mesa 26.2.5; see NR-CONTROL-FIX.md. Software Vulkan establishes wiring and API
contracts only. It does not establish numerical correctness of an NR network,
its speed or the quality of two temporal upscalers in a game.

After installing the update:

1. Start with FG off. Set the game's/SR input to 1280x720 and display to
   1920x1080, then select the settings above. The log must contain
   `Ditto SR: NR output 422x238 -> 1280x720 before OptiScaler SR`.
2. Compare Upsampling Classic/Ditto SR at 0.33, 0.5 and 1. Inspect still detail,
   pans, foliage, hair, motion silhouettes, particles, cuts and dark/bright menus.
   Check SDR and HDR if available. At 1 no Ditto enlargement should run.
3. Switch mode, disable/re-enable NR, change scale, resize and change layer
   count. Check that no stale history, flicker or allocation failure persists.
4. Enable the existing FG provider and exercise its multipliers. The Ditto
   Before SR layer must run on game evaluations, and preserve NR Control's
   pointer, FG status and resolution display fixes.
5. Try After SR/Present and verify the explicit placement error. Restore Before
   SR and verify it recovers. Record the log, screenshots and per-frame GPU
   timings over repeated runs at the same clocks.

The existing RDNA3 network checks remain available, using locally extracted
weights and the target AMD GPU; do not update their goldens for this feature:

```sh
python3 linux/test/check_rdna3.py --model /path/to/CC_SILVER_AARDWOLD.bin
python3 linux/test/check_rdna3_small.py --model /path/to/CC_SMALL_FP8.bin
python3 linux/test/check_rdna3_int4.py --model /path/to/CC_SILVER_AARDWOLD.bin
```
