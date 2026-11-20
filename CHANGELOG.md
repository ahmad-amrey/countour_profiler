# Changelog

What changed in each release. The section for a version is published as that
version's release notes, so write it before tagging (see *Making a release* in
the README).

## [0.1.1] - 2026-11-20

Fix release for the Arabic interface.

### Fixed

- **Arabic interface:** Qt's standard texts are now in Arabic on Windows, macOS
  and Linux: dialog buttons (OK, Cancel, Yes, No, Save…), the right-click menu
  of text fields (Copy, Paste, Select All…) and the colour picker. In 0.1.0
  they stayed in English.

### Changed

- The release packages no longer depend on Qt's separate translation files;
  all Arabic texts are built into the app.

## [0.1.0] - 2026-11-20

First public release. Contour Profiler copies the cross-section of a real
object into CAD: bend a wire around the shape, photograph it on a grid from any
angle, and get an accurate 2D DXF.

### Highlights

- **Automatic from photo to DXF.** Open a photo and the grid is found, the
  perspective and scale are corrected, and the wire is traced. You only check
  the result and export.
- **Clean CAD geometry.** The traced or hand-placed points are rebuilt as the
  fewest straight lines and true circular arcs within a tolerance (default
  0.10 mm). Arcs export as real arcs.
- **Works from any angle.** Tested on four photos of the same wire, from
  top-down to strongly oblique. The profiles agree to 0.03–0.05 mm.
- **Exports** DXF (R12, millimetres), SVG and CSV.

### Also included

- Grids: cutting mats and graph paper (light or dark lines) and checkerboards.
  With no grid, click the 4 corners of any known rectangle.
- A ready-to-print 10 mm calibration sheet (A4 and US Letter) for anyone
  without a cutting mat.
- A simple default view with an *Adjust* panel for the details: reference
  points, detection settings, picking the wire or its colour, editing and
  drawing points, and the line/arc tolerance.
- English and Arabic interface, with a full right-to-left layout.
- Windows, macOS (Apple Silicon) and Linux builds.

### Known limitations

- Lens distortion is not corrected: use the phone's main camera, not the
  ultra-wide one.
- The wire's tips are the least precise part (about ±0.4 mm), because of the
  cut ends, shadows and glare. Drag the end points if the exact tip matters.
- The builds are not code-signed, so Windows and macOS warn on the first start.
