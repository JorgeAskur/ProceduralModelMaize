# Maize Procedural Generator

An interactive C++ tool for generating procedural maize (*Zea mays*) plant geometry. Plants are described by a hierarchical parameter descriptor (Plant → Tiller → Leaf), rendered in real time via OpenGL, and exported as 3D meshes (OBJ) and structured XML files for downstream trait analysis pipelines.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [Build Instructions](#build-instructions)
- [Running the App](#running-the-app)
- [UI Overview](#ui-overview)
- [Using the Generator](#using-the-generator)
- [Export Formats](#export-formats)
- [Generator Config File](#generator-config-file)
- [Known Limitations](#known-limitations)

---

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| Windows | 10 or later | Linux/macOS not currently supported |
| Visual Studio | 2022 (v17+) | Desktop development with C++ workload |
| vcpkg | any recent | For managing freeglut and tinyxml2 |
| OpenGL | 2.0+ | Any discrete or integrated GPU with OpenGL 2.0 support |

---

## Build Instructions

### 1. Install vcpkg (if you do not already have it)

```bat
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat
```

### 2. Install dependencies

The project requires **freeglut** and **tinyxml2** for x64-windows:

```bat
vcpkg install freeglut:x64-windows
vcpkg install tinyxml2:x64-windows
```

Integrate vcpkg with Visual Studio so the project can find the installed headers and libraries automatically:

```bat
vcpkg integrate install
```

### 3. Open and build

1. Open `Maize.sln` in Visual Studio 2022.
2. Set the target to **Release | x64** (or Debug | x64).
3. Build → Build Solution (`Ctrl+Shift+B`).

The output executable (`Maize.exe`) is placed in `x64\Release\` (or `x64\Debug\`).

### 4. Copy runtime DLLs

Before running, copy the following DLLs from your vcpkg installation into the same directory as `Maize.exe`:

```
vcpkg_installed\x64-windows\bin\freeglut.dll
```

Alternatively, add `vcpkg_installed\x64-windows\bin` to your `PATH`.

---

## Running the App

```bat
x64\Release\Maize.exe
```

The app saves all UI paths and settings to `app_settings.xml` in the working directory when the window is closed, and restores them on the next launch. On first launch (before `app_settings.xml` exists) it falls back to these defaults:

| Setting | Default |
|---|---|
| Leaf texture | `maize_leaf.png` |
| Stem texture | `maize_stem_texture.png` |
| Generator config | `maize_generator_config.xml` |
| Plant XML | `plant_spline.xml` |
| Skeleton XML | `plant_skeleton.xml` |
| OBJ output | `plant_spline.obj` |
| Skeleton OBJ | `plant_skeleton.obj` |
| Set base path | `plant_export/plant` |

All paths are shown in the UI and can be changed at runtime via the browse buttons.

**Viewport controls**

| Input | Action |
|---|---|
| Left-click drag | Rotate camera |
| Scroll wheel | Zoom in / out |
| W A S D | Pan camera target |
| Q / E | Move camera target up / down |
| Shift + move | 4× movement speed |

---

## UI Overview

The side panel is organized into collapsible sections:

| Section | Purpose |
|---|---|
| **Global Traits** | Edit per-plant distributions (height, stem radius, leaf count, etc.) and the latent trait correlation matrix |
| **Set Generation** | Configure and launch multi-plant batch export |
| **Generator Config** | Load / save the current generator settings as an XML file |
| **Leaf Traits** | Directly edit individual leaf parameters and spline control points for the currently loaded plant |
| **Export** | Load and save plant XML, skeleton XML, OBJ mesh, and skeleton OBJ |
| **Visual Settings** | Renderer toggles (wireframe, normals, skeleton overlay) and shading parameters |

---

## Using the Generator

### Previewing a plant

- **Preview Mean Plant** — generates a plant using the mean value of every distribution (no random sampling). Use this to inspect the effect of parameter changes without noise.
- **Preview Random Sample** — generates one plant by sampling all distributions with a random seed.

### Parameter structure

Each leaf parameter has a **mean** and a **std dev**. Additionally, the **Correlation Matrix** panel controls how four latent trait dimensions (vigor, slenderness, posture, texture) correlate parameters across a plant. Setting `traitCorrelationStrength` to 0 makes all parameters independent; setting it to 1 applies the full loading matrix.

**Canopy curves** (Bottom / Middle / Top) shape how a trait changes from the lowest leaf to the highest. For example, `Leaf Length Scale Curve` allows upper leaves to be shorter or longer than lower ones relative to the sampled base length.

### Batch generation

1. Set **Set Base Path / Prefix** to the desired output directory and filename prefix (e.g. `output/plant`).
2. Set **Set Count** (1–32000) and **Start Index** (for appending to an existing set).
3. Optionally check **Export Skeleton** to also write skeleton XML and OBJ alongside each full plant.
4. Click **Generate Plant Set**.

Output files are named `{prefix}_{index:04d}.xml` and `{prefix}_{index:04d}.obj`. Plant index `i` always uses seed `i`, so individual plants are deterministically reproducible. The batch runs on all available CPU cores concurrently.

---

## Export Formats

### Plant XML (`plant_spline.xml`)

Full lossless descriptor. Stores all leaf parameters and, when control-point overrides are active, the explicit B-spline control points for each leaf's center, left, and right curves.

```xml
<plant species="Maize" phenotypeId="BTx99">
  <Tiller type="main" radius="0.012" alpha="0" stemShrink="0.002">
    <leaves number="12">
      <leaf id="0" distance="0.05"
            leafLength="0.45" leafWidth="0.066" leafAzimuthDeg="0"
            leafAngle="43.97" droopiness="-20" stemInclinationDeg="0"
            splinePoints="40" widthTaper="2.86" leafTwist="14.7" leafCurl="0.11"
            waveLAmp="0.005" waveLFreq="7.6" ... />
      ...
    </leaves>
  </Tiller>
</plant>
```

### Skeleton XML (`plant_skeleton.xml`)

Lightweight format for point-cloud or image-derived skeletons. **Lossy** — stores only the leaf center B-spline control points and azimuth angle. On reload, left/right edges are approximated from the center using the leaf width and taper parameters, and internode distances are re-estimated from successive control-point bases. All other leaf parameters revert to defaults.

```xml
<plantSkeleton species="Maize" phenotypeId="BTx99">
  <Tiller type="main" radius="0.012" alpha="0" stemShrink="0.002"
          useStemCtrlOverrides="1">
    <stemCtrl count="13" p0x="0" p0y="0" p0z="0" ... />
    <leaves number="12">
      <leaf id="0" leafAzimuthDeg="0">
        <ctrlCenter count="40" p0x="..." ... />
      </leaf>
      ...
    </leaves>
  </Tiller>
</plantSkeleton>
```

### OBJ / MTL

Full triangulated mesh for rendering or downstream 3D analysis. A single `maize.mtl` material file is shared across all plants in a batch. Material slots:
- `leaf_blade` — uses the leaf texture
- `leaf_sheath` — uses the stem texture

### Skeleton OBJ

Exports the B-spline centerlines as line segments (no mesh). Useful for skeleton visualization and registration against real point cloud data.

---

## Generator Config File

The generator state (all distribution means/std devs, curve shapes, and the correlation matrix) can be saved and loaded as XML. The bundled `maize_generator_config.xml` contains parameters fitted to maize field data. `small_maize_generator_config.xml` contains a smaller-plant variant.

Config files carry a `version` attribute (currently `4`). Partial configs are supported — any parameter absent from the file keeps its current in-memory value.

---

## Known Limitations

- **Single tiller only.** The data model and XML format support multiple tillers, but the procedural generator always produces a single main tiller. Multi-tiller generation (e.g. for sorghum ratoon shoots) is a planned future extension.
- **Windows only.** The build system and windowing layer depend on Win32 APIs. A cross-platform CMake build is planned.
- **No online reconstruction.** The tool takes a finished descriptor as input; real-time plant reconstruction from live sensor data is out of scope.
