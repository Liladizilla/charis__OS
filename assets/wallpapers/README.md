# CharisOS wallpaper collection

The desktop ships with four photographic choices and four lightweight procedural themes. The photographs are sourced from Wikimedia Commons and each source is individually dedicated under CC0 1.0. The build downloads the exact original bytes, verifies the published byte count and SHA-1, crops without stretching, converts to RGB565 and packages the images into one bounded CWP2 Multiboot2 module. The kernel never parses JPEG and has no runtime network dependency.

## Photographs

| Wallpaper | Source / creator | Original size | License | Source byte length | SHA-1 |
|---|---|---:|---|---:|---|
| Charis Lake | [Lake Mountain Landscape](https://commons.wikimedia.org/wiki/File:Lake_Mountain_Landscape.jpg), Bonnie Moreland | 5184 × 3456 | CC0 1.0 | 2,915,869 | `60eab74e7704db6375f1426fb11421ba29d7603e` |
| Alpine Village | [Mountains Landscape](https://commons.wikimedia.org/wiki/File:Mountains_Landscape.jpg), original author listed as unknown | 5760 × 3840 | CC0 1.0 | 3,878,306 | `4992bece12e6923dc91031912b060b99f1a75a94` |
| Forest Sky | [Forest sky](https://commons.wikimedia.org/wiki/File:Forest_sky.jpg), Tuyishimejosue | 6720 × 4480 | CC0 1.0 | 6,267,779 | `4fa3700f75e8d53e3c331e0e22e5a73b966f78a2` |
| Cityscape | [Cityscape (Unsplash)](https://commons.wikimedia.org/wiki/File:Cityscape_%28Unsplash%29.jpg), Anurag Arora | 6000 × 4000 | CC0 1.0 | 2,501,465 | `9ee17e1fc528b49810205b1ac64aa694ad59f3fe` |

The Cityscape photograph was published under Unsplash's earlier CC0 terms before 5 June 2017; its Wikimedia Commons file page records that provenance and the CC0 dedication. The other three photo pages explicitly document CC0 1.0. The collection uses no logos, watermarks, screenshots or UI overlays.

## Built-in choices

| Stable settings ID | Label | Rendering |
|---:|---|---|
| 0 | Charis Lake | 2560 × 1440 photograph |
| 1 | Ember | Procedural dark preset |
| 2 | Forest | Procedural dark preset |
| 3 | Dusk | Procedural dark preset |
| 4 | Mono | Procedural dark preset |
| 5 | Alpine Village | 1920 × 1080 photograph |
| 6 | Forest Sky | 1920 × 1080 photograph |
| 7 | Cityscape | 1920 × 1080 photograph |

IDs 0–4 retain their previous meaning so existing configuration files continue to select the same background. The new photo IDs are appended.

## Build and boot path

1. The Makefile downloads each original from its pinned Wikimedia URL.
2. It checks the exact source byte length and SHA-1 before conversion.
3. `tools/prepare_wallpaper.py` converts images to a 16:9 RGB565 CWP1 payload using a center crop and Lanczos resampling. The default remains 2560 × 1440; the three additional images are 1920 × 1080 to keep the combined module within a 24 MiB limit.
4. `tools/pack_wallpapers.py` places the four payloads and their stable wallpaper IDs in one CWP2 collection.
5. GRUB loads the collection as the named Multiboot2 module `charisos-wallpaper`.
6. The kernel reserves the module pages, validates the collection directory, image IDs, dimensions, row strides, payload boundaries and non-overlap, then renders only the selected RGB565 image.

CWP1 remains supported for older boot modules. CWP2 is validated before any selected pixel is read. The renderer uses integer, aspect-preserving center-crop mapping and does not decode JPEG in privileged code.

A clean ISO build needs internet access to retrieve the pinned originals and Python Pillow. It fails closed when a download, byte count or SHA-1 check fails.

## Related files

- `tools/prepare_wallpaper.py` — deterministic JPEG-to-CWP1 conversion
- `tools/pack_wallpapers.py` — deterministic CWP1-to-CWP2 collection packer
- `tests/wallpaper_module_test.c` — host tests for CWP1 compatibility and CWP2 validation/rendering
- `tests/wallpaper_pack_test.py` — host regression for the collection packer
- `kernel/bootmem.c` — bounded Multiboot2 module discovery
- `kernel/pmm.c` — wallpaper module page reservation
- `kernel/wallpaper.c` — validated CWP1/CWP2 framebuffer renderer
- `iso/boot/grub/grub.cfg` — GRUB module loading
