#include "lsystem/TasselMesh.hpp"

#include "lsystem/TasselGrowthModel.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace l_system_plugin {

namespace {

inline Vect3d ToVect(const glm::vec3& v) { return Vect3d(v.x, v.y, v.z); }

// Pick any unit vector perpendicular to `axis`.
glm::vec3 PerpendicularTo(const glm::vec3& axis) {
  const glm::vec3 a = glm::abs(axis);
  const glm::vec3 reference =
      (a.x <= a.y && a.x <= a.z) ? glm::vec3(1, 0, 0) : (a.y <= a.z ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1));
  glm::vec3 p = glm::cross(axis, reference);
  const float len = glm::length(p);
  return len > 1e-6f ? p / len : glm::vec3(1, 0, 0);
}

// Match MaizeTassel.cpp's ellipsoid orientation: rotate local +Y onto `direction`.
glm::quat MakeYToDirectionRotation(const glm::vec3& direction) {
  const glm::vec3 y_axis(0.0f, 1.0f, 0.0f);
  const glm::vec3 dir = glm::normalize(direction);
  const float dot = std::clamp(glm::dot(y_axis, dir), -1.0f, 1.0f);
  if (dot > 0.9999f) return glm::quat(1, 0, 0, 0);
  if (dot < -0.9999f) return glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
  const glm::vec3 axis = glm::normalize(glm::cross(y_axis, dir));
  return glm::normalize(glm::angleAxis(std::acos(dot), axis));
}

// Append a (optionally tapered) cylinder from p0 (radius r0) to p1 (radius r1).
void AppendCylinder(TasselMesh& mesh, const glm::vec3& p0, const glm::vec3& p1,
                    float r0, float r1, int segments, float vBase, float vTop) {
  glm::vec3 axis = p1 - p0;
  const float height = glm::length(axis);
  if (height < 1e-6f) return;
  axis /= height;

  const glm::vec3 side = PerpendicularTo(axis);
  const glm::vec3 fwd = glm::normalize(glm::cross(axis, side));

  const unsigned int base = static_cast<unsigned int>(mesh.pos.size());
  for (int s = 0; s <= segments; ++s) {
    const float t = static_cast<float>(s) / static_cast<float>(segments);
    const float ang = t * glm::two_pi<float>();
    const glm::vec3 radial = side * std::cos(ang) + fwd * std::sin(ang);

    const glm::vec3 bottom = p0 + radial * r0;
    const glm::vec3 top = p1 + radial * r1;
    // Normal accounts for taper so the lit silhouette stays smooth.
    glm::vec3 n = glm::normalize(radial - axis * ((r1 - r0) / height));

    mesh.pos.push_back(ToVect(bottom));
    mesh.nrm.push_back(ToVect(n));
    mesh.u.push_back(t);
    mesh.v.push_back(vBase);

    mesh.pos.push_back(ToVect(top));
    mesh.nrm.push_back(ToVect(n));
    mesh.u.push_back(t);
    mesh.v.push_back(vTop);
  }

  for (int s = 0; s < segments; ++s) {
    const unsigned int b0 = base + static_cast<unsigned int>(s) * 2u;
    const unsigned int t0 = b0 + 1u;
    const unsigned int b1 = b0 + 2u;
    const unsigned int t1 = b0 + 3u;
    mesh.indices.push_back(b0); mesh.indices.push_back(b1); mesh.indices.push_back(t0);
    mesh.indices.push_back(t0); mesh.indices.push_back(b1); mesh.indices.push_back(t1);
  }
}

// Append an ellipsoid: unit sphere scaled by `scale`, oriented by `rot`, centered at `center`.
void AppendEllipsoid(TasselMesh& mesh, const glm::vec3& center, const glm::quat& rot,
                     const glm::vec3& scale, int rings, int segments) {
  const glm::vec3 safe_scale = glm::max(scale, glm::vec3(1e-4f));
  const unsigned int base = static_cast<unsigned int>(mesh.pos.size());

  for (int r = 0; r <= rings; ++r) {
    const float vt = static_cast<float>(r) / static_cast<float>(rings);
    const float phi = vt * glm::pi<float>();          // 0..pi (south->north)
    const float sp = std::sin(phi), cp = std::cos(phi);
    for (int s = 0; s <= segments; ++s) {
      const float ut = static_cast<float>(s) / static_cast<float>(segments);
      const float theta = ut * glm::two_pi<float>();
      const glm::vec3 unit(sp * std::cos(theta), cp, sp * std::sin(theta));  // +Y is the pole

      const glm::vec3 local_pos = safe_scale * unit;
      const glm::vec3 local_nrm = glm::normalize(unit / safe_scale);  // ellipsoid gradient
      const glm::vec3 world_pos = center + rot * local_pos;
      const glm::vec3 world_nrm = glm::normalize(rot * local_nrm);

      mesh.pos.push_back(ToVect(world_pos));
      mesh.nrm.push_back(ToVect(world_nrm));
      mesh.u.push_back(ut);
      mesh.v.push_back(vt);
    }
  }

  const int stride = segments + 1;
  for (int r = 0; r < rings; ++r) {
    for (int s = 0; s < segments; ++s) {
      const unsigned int a = base + static_cast<unsigned int>(r * stride + s);
      const unsigned int b = a + 1u;
      const unsigned int c = a + static_cast<unsigned int>(stride);
      const unsigned int d = c + 1u;
      mesh.indices.push_back(a); mesh.indices.push_back(c); mesh.indices.push_back(b);
      mesh.indices.push_back(b); mesh.indices.push_back(c); mesh.indices.push_back(d);
    }
  }
}

}  // namespace

TasselMesh BuildTasselMesh(const TasselGrowthModel& model, int radialSegments,
                           int sphereRings, int sphereSegments) {
  TasselMesh mesh;
  radialSegments = std::max(3, radialSegments);
  sphereRings = std::max(3, sphereRings);
  sphereSegments = std::max(3, sphereSegments);

  const auto& graph = model.graph;
  const auto& sorted = graph.PeekSortedNodeList();

  // --- Rachis / branch internodes: tapered cylinders ---
  for (const auto handle : sorted) {
    const auto& node = graph.PeekNode(handle);
    if (!node.data.Is<TasselInternode>()) continue;
    const auto& internode = node.data.Get<TasselInternode>();
    if (internode.length <= 1e-5f) continue;

    const glm::vec3 start = node.info.global_position;
    const glm::vec3 end = node.info.GetGlobalEndPosition();
    const float r = std::max(1e-4f, internode.thickness * 0.5f);
    // Slight taper toward the tip reads as a natural rachis.
    AppendCylinder(mesh, start, end, r, r * 0.85f, radialSegments, 0.0f, 1.0f);

    // Centerline segment for the skeleton view/export.
    mesh.skeleton.push_back(ToVect(start));
    mesh.skeleton.push_back(ToVect(end));
  }

  // --- Spikelet pairs: proximal ellipsoid + pair internode + distal ellipsoid ---
  // Layout ported verbatim from MaizeTassel.cpp::CollectSpikeletPairsFromGraph.
  for (const auto handle : sorted) {
    const auto& node = graph.PeekNode(handle);
    if (!node.data.Is<TasselSpikeletPair>()) continue;
    const auto& pair = node.data.Get<TasselSpikeletPair>();

    const glm::vec3 base_pos = node.info.global_position;
    const glm::vec3 axis_dir = glm::normalize(node.info.global_rotation * glm::vec3(0, 0, -1));
    glm::vec3 radial_dir = node.info.global_rotation * glm::vec3(1, 0, 0);
    radial_dir -= axis_dir * glm::dot(radial_dir, axis_dir);
    float radial_len = glm::length(radial_dir);
    if (radial_len <= 1e-5f) {
      radial_dir = glm::cross(axis_dir, glm::vec3(0, 1, 0));
      radial_len = glm::length(radial_dir);
      if (radial_len <= 1e-5f) {
        radial_dir = glm::cross(axis_dir, glm::vec3(1, 0, 0));
        radial_len = glm::length(radial_dir);
      }
    }
    radial_dir = (radial_len > 1e-5f) ? radial_dir / radial_len : glm::vec3(1.0f, 0.0f, 0.0f);

    const float proximal_angle_rad = glm::radians(pair.proximal_outward_angle);
    const glm::vec3 proximal_dir = glm::normalize(axis_dir * std::cos(proximal_angle_rad) +
                                                  radial_dir * std::sin(proximal_angle_rad));
    const float proximal_half_height = std::max(0.0f, pair.proximal_scale.y);
    const glm::vec3 proximal_pos = base_pos + proximal_dir * proximal_half_height;

    const float internode_angle_rad = glm::radians(pair.pair_internode_angle);
    const glm::vec3 internode_dir = glm::normalize(proximal_dir * std::cos(internode_angle_rad) +
                                                   radial_dir * std::sin(internode_angle_rad));
    const glm::vec3 internode_start = base_pos;
    const glm::vec3 internode_end = internode_start + internode_dir * std::max(0.0f, pair.pair_internode_length);

    const float distal_angle_rad = glm::radians(pair.distal_outward_angle);
    const glm::vec3 distal_dir = glm::normalize(internode_dir * std::cos(distal_angle_rad) +
                                                radial_dir * std::sin(distal_angle_rad));
    const float distal_half_height = std::max(0.0f, pair.distal_scale.y);
    const glm::vec3 distal_pos = internode_end + distal_dir * distal_half_height;

    AppendEllipsoid(mesh, proximal_pos, MakeYToDirectionRotation(proximal_dir), pair.proximal_scale,
                    sphereRings, sphereSegments);
    AppendEllipsoid(mesh, distal_pos, MakeYToDirectionRotation(distal_dir), pair.distal_scale,
                    sphereRings, sphereSegments);

    if (pair.pair_internode_length > 0.0f && pair.pair_internode_thickness > 0.0f) {
      const float r = std::max(1e-4f, pair.pair_internode_thickness * 0.5f);
      AppendCylinder(mesh, internode_start, internode_end, r, r, radialSegments, 0.0f, 1.0f);
    }
  }

  return mesh;
}

}  // namespace l_system_plugin
