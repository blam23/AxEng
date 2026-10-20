# Testing

AxEng's test executable uses GoogleTest's built-in filtering. Run one test by passing a filter:

```powershell
xmake run Tests --gtest_filter=SpriteShaderTest.WritesSdfFontRenderSamples
```

The same filter can be set with `GTEST_FILTER` when running the test executable:

```powershell
$env:GTEST_FILTER = 'SpriteShaderTest.WritesSdfFontRenderSamples'
xmake run Tests
```

To generate the SDF font comparison images, set `AXENG_SDF_FONT_SAMPLES` while running that test:

```powershell
$env:AXENG_SDF_FONT_SAMPLES = '1'
xmake run Tests --gtest_filter=SpriteShaderTest.WritesSdfFontRenderSamples
```

The images are written to `%TEMP%\AxEngSdfFontSamples`. Set the variable to a directory path to use a
different output location. The test does not write images when the variable is unset, empty, or `0`.
It writes samples at 8, 10, 12, and 16 pixels. Filenames start with the rendered size (for example,
`size_10px_`) and identify the treatment. `msdfgen_settings_comparison.png` is a contact sheet with
font sizes in columns (8, 10, 12, and 16 pixels) and these treatments in labeled rows, top to bottom:

1. SDF reference, 2x render/downsample.
2. SDF thickened by 0.5 reference pixels, 2x render/downsample.
3. Default MSDF (96px generation height, 16px spread, distance-based edge coloring), 2x render/downsample.
4. Baseline msdfgen rendered directly at 1x, with no supersampling.
5. Baseline msdfgen rendered at 4x and downsampled.
6. Narrower msdfgen distance range (96px height, 8px spread), 2x render/downsample.
7. Higher-resolution msdfgen atlas (192px height, 32px spread), 2x render/downsample.
8. msdfgen simple edge coloring instead of distance-based edge coloring, 2x render/downsample.
9. msdfgen error correction disabled, 2x render/downsample.
10. msdfgen edge-only error correction, 2x render/downsample.
11. Baseline msdfgen thickened by 0.5 reference pixels, 2x render/downsample.

The application's built-in text font uses row 3's MSDF settings, with four shader coverage samples
to approximate the 2x render/downsample. The sheet includes SDF rows as visual references; the
other MSDF rows isolate sampling, atlas resolution, distance range, edge coloring, and
error-correction changes. The test also writes
`sdf_msdf_direct_comparison.png`, with default SDF on the top row, baseline unthickened msdfgen below
it, and a 64x-amplified absolute RGB difference below that. Columns show 8, 10, 12, and 16 pixels
from left to right. The render test requires a D3D12 WebGPU device.
