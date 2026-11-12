# CharisOS wallpaper source manifest

These are candidate bundled desktop backgrounds. Every source page below identifies the particular file as CC0 1.0, not merely as a free-to-download preview. The original images were visually checked from their Commons previews; no visible logos or watermarks appear in those previews.

Do not replace this manifest with a generic link to an image search result. Keep the source page, creator, dimensions, license and downloaded-file checksum together with every vendored asset.

| Asset ID | Title | Creator | Original dimensions | License | Source page | Original file |
|---|---|---|---:|---|---|---|
| `lake-mountain-landscape` | Lake Mountain Landscape | Bonnie Moreland | 5184 × 3456 | CC0 1.0 | [Wikimedia file page](https://commons.wikimedia.org/wiki/File:Lake_Mountain_Landscape.jpg) | [Original JPEG](https://upload.wikimedia.org/wikipedia/commons/7/75/Lake_Mountain_Landscape.jpg) |
| `forest-lake-autumn` | Landscape-nature-forest-lake | Pixel.la Free Stock Photos | 4368 × 2912 | CC0 1.0 | [Wikimedia file page](https://commons.wikimedia.org/wiki/File:Landscape-nature-forest-lake_(24300004076).jpg) | [Original JPEG](https://upload.wikimedia.org/wikipedia/commons/e/e5/Landscape-nature-forest-lake_%2824300004076%29.jpg) |
| `view-lake-forest` | View of lake in forest | mirobo | 4288 × 2704 | CC0 1.0 | [Wikimedia file page](https://commons.wikimedia.org/wiki/File:View_of_lake_in_forest.jpg) | [Original JPEG](https://upload.wikimedia.org/wikipedia/commons/c/cf/View_of_lake_in_forest.jpg) |
| `peaceful-lake-greenery` | Peaceful Lake Landscape with Lush Greenery | Vexago1 | 5760 × 3840 | CC0 1.0 | [Wikimedia file page](https://commons.wikimedia.org/wiki/File:Peaceful_Lake_Landscape_with_Lush_Greenery.jpg) | [Original JPEG](https://upload.wikimedia.org/wikipedia/commons/7/77/Peaceful_Lake_Landscape_with_Lush_Greenery.jpg) |

## Asset ingestion requirements

- Bundle the exact original or a documented derivative of the listed file. Do not silently substitute thumbnails or search-result previews.
- Preserve the original source page URL and license note in the repository with the asset.
- Verify the downloaded file's actual dimensions, file signature and checksum before accepting it.
- Produce a 16:9 presentation derivative at 3840 × 2160 where the source resolution allows it. Keep a high-quality original for other display aspect ratios.
- Use aspect-ratio-aware center-crop or contain behavior. Never stretch a 3:2 photograph to fill a 16:9 display.
- Do not add CharisOS branding, UI mockups, captions or watermarks to the artwork.
- Do not decode untrusted image files directly in privileged kernel code. Prefer a bounded, validated asset format or an isolated decoder with strict input-size and output-size caps.

## Current status

The source pages, license declarations and original dimensions have been checked. The files are **not yet vendored into the repository**: raw image downloading from this environment was unavailable. Do not describe the backgrounds as bundled or selectable until their bytes, checksums and decoder path are committed and tested.
