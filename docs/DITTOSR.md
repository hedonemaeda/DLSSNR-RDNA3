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

GPU validation on AMD hardware and game compatibility are pending. Software
Vulkan checks, if available, establish API/contracts only, not game quality or
performance. The target report is Z1 Extreme / Dragon's Dogma 2 / DX12 /
Proton CachyOS / Mesa 26.2.5; see NR-CONTROL-FIX.md.
