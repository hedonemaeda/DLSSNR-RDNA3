# Ditto SR

`common.hlsli`, `convert.hlsl`, `motion.hlsl`, `upscale.hlsl` and `sharpen.hlsl`
are unmodified from [DittoSR v0.4.0](https://github.com/Vancete/DittoSR/tree/d37519ce85a84a4cc3083cad17a83335bee0f6d4).
The host's settings and constant builder in `linux/src/core/dittosr/` come from
the same source, with the D3D12 dependencies removed. DittoSR is MIT licensed;
the GSR 2 attribution and BSD terms are in `licenses/`. The NR package carries
both notices next to the compiled shaders.

`prepare.comp` belongs to this integration. It samples the reference on NR's
grid before codec resolve, and maps the game's guide subrects to that grid.
Motion remains in normalized screen units. Ditto SR consumes decoded NR colour,
never the network's encoded proxy. HDR stays linear until the usual final commit.

The Vulkan path uses the reference's FP32 arithmetic and RGBA16F history, with
four separate passes. Its D3D12-only fused sharpening, 10-bit history and
anti-shimmer variants are not enabled here. Slang is pinned by fetch_deps.sh.
