#pragma once

// ---------------------------------------------------------------------------
// TasselMesh — CPU triangle mesh built from a grown TasselGrowthModel.
//
// EvoEngine renders the tassel on the GPU (mesh shaders) and has no CPU mesh
// code, so this is new: it walks the grown L-system graph and emits a triangle
// soup (positions, normals, UVs, indices) in this tool's Vect3d space.
//
//   * Each TasselInternode  -> a tapered cylinder along its growth direction.
//   * Each TasselSpikeletPair -> proximal ellipsoid + (optional) pair-internode
//     cylinder + distal ellipsoid, using the exact organ layout ported from
//     MaizeTassel.cpp's CollectSpikeletPairsFromGraph.
//
// The mesh is generated in the grown model's local frame; the caller applies
// the attach transform (position/scale at the top of the maize stem).
// ---------------------------------------------------------------------------

#include <math/vect3d.h>
#include <vector>

namespace l_system_plugin {

// Forward-declared so Maize.h can include this header without pulling in the
// whole L-system engine (and glm). The full type is only needed in TasselMesh.cpp.
class TasselGrowthModel;

struct TasselMesh {
  std::vector<Vect3d> pos;          // vertex positions
  std::vector<Vect3d> nrm;          // vertex normals
  std::vector<float>  u;            // texture u (parallel to pos)
  std::vector<float>  v;            // texture v
  std::vector<unsigned int> indices;// triangle list (3 per face)

  // Skeleton: rachis + branch centerlines as line segments (consecutive pairs:
  // [s0,e0, s1,e1, ...]). Used by the skeleton viewport overlay and OBJ export.
  std::vector<Vect3d> skeleton;

  void clear() { pos.clear(); nrm.clear(); u.clear(); v.clear(); indices.clear(); skeleton.clear(); }
  [[nodiscard]] size_t triangleCount() const { return indices.size() / 3; }
};

// Build a triangle mesh from a grown tassel. `radialSegments` controls cylinder
// tessellation; `sphereRings`/`sphereSegments` control ellipsoid tessellation.
TasselMesh BuildTasselMesh(const TasselGrowthModel& model,
                           int radialSegments = 5,
                           int sphereRings = 4,
                           int sphereSegments = 6);

}  // namespace l_system_plugin
