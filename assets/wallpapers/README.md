# CharisOS wallpaper asset

The default **Charis** desktop background is the CC0 1.0 photograph below:

| Field | Value |
|---|---|
| Title | Lake Mountain Landscape |
| Creator | Bonnie Moreland |
| Original dimensions | 5184 × 3456 |
| License | CC0 1.0 |
| File page | https://commons.wikimedia.org/wiki/File:Lake_Mountain_Landscape.jpg |
| Original JPEG | https://upload.wikimedia.org/wikipedia/commons/7/75/Lake_Mountain_Landscape.jpg |
| Original byte length | 2,915,869 |
| Original SHA-1 | `60eab74e7704db6375f1426fb11421ba29d7603e` |

## Build and boot path

The repository does not parse JPEG in kernel space. During `make`, the build:

1. Downloads the exact original file from the URL above.
2. Verifies the documented byte length and pinned SHA-1 before using the bytes.
3. Uses `tools/prepare_wallpaper.py` and Pillow to center-crop without stretching, resize to 1024×768 and encode RGB565 pixels in the bounded CWP1 format.
4. Places `wallpaper.cwp` in the ISO and asks GRUB to load it as the named Multiboot2 module `charisos-wallpaper`.
5. Lets the kernel reserve the module pages and validate the CWP1 signature, version, dimensions, stride and exact payload length before reading any pixel.

The kernel scales the decoded RGB565 pixels to the framebuffer with an aspect-preserving center crop. There is no JPEG parser in privileged kernel code and no network access at runtime.

A clean image build needs internet access to retrieve the pinned source and the Python Pillow package. It fails closed when download, byte-length or checksum validation fails rather than silently generating an empty wallpaper.

## Related files

- `tools/prepare_wallpaper.py` — deterministic CWP1 conversion
- `kernel/bootmem.c` — bounded Multiboot2 module discovery
- `kernel/pmm.c` — wallpaper module page reservation
- `kernel/wallpaper.c` — validated CWP1 framebuffer renderer
- `iso/boot/grub/grub.cfg` — GRUB module loading
