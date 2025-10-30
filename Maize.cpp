#include "Maize.h"
#include "Descriptor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <map>
#include <random>

#define M_PI 3.14159265358979323846

// Math helpers
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }
static inline Vect3d lerp_v3(const Vect3d& a, const Vect3d& b, float t) { return a * (1.f - t) + b * t; }

// Helper function to interpolate color
static inline Vect3d lerp_color(const Vect3d& a, const Vect3d& b, float t) {
    t = clampf(t, 0.f, 1.f);
    return a * (1.f - t) + b * t;
};

// Color function to get color based on row and stemRows
static Vect3d getColor(int row, int stemRows) {
    const Vect3d stemColor(1.f, 1.f, 0.f); // Yellowish stem
    const Vect3d leafColor(0.f, 0.5f, 0.f); // Green leaf
    const int transitionRows = 5; // Number of rows over which to transition

    // Start transitioning a bit before the sheath ends
    const int transitionStartRow = std::max(0, stemRows - (transitionRows / 2));

    float t = 0.f;
    if (row <= transitionStartRow) {
        t = 0.f;
    }
    else if (row >= transitionStartRow + transitionRows) {
        t = 1.f;
    }
    else {
        // Smoothly interpolate from 0 to 1 over the transitionRows
        t = (float)(row - transitionStartRow) / (float)transitionRows;
    }
    return lerp_color(stemColor, leafColor, t);
};

// Hash for generating a float in [-1, 1]
static float hash_to_float_minus1_1(unsigned int seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed = seed + (seed << 3);
    seed = seed ^ (seed >> 4);
    seed = seed * 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return ((float)(int)(seed & 0xFFFFFF) / (float)(0xFFFFFF / 2)) - 1.0f;
}

// Generate open uniform knot vector
static std::vector<float> openUniformKnots(int nCtrl, int p) {
    int n = nCtrl - 1, m = n + p + 1; std::vector<float> U(m + 1, 0.f);
    for (int j = 0; j <= p; ++j) U[j] = 0.f;
    for (int j = m - p; j <= m; ++j) U[j] = 1.f;
    for (int j = p + 1; j <= m - p - 1; ++j) U[j] = float(j - p) / float(m - 2 * p);
    return U;
}

// Find span for B-spline evaluation
static int findSpan(int n, int p, float u, const std::vector<float>& U) {
    if (u >= U[n + 1]) return n;
    int low = p, high = n + 1, mid;
    while (high - low > 1) { mid = (low + high) / 2; if (u < U[mid]) high = mid; else low = mid; }
    return low;
}

// Helper to get base name from path
static std::string baseNameFromPath(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return (s == std::string::npos) ? p : p.substr(s + 1);
}


// Global model instance
static MaizeModel g_MaizeModelInstance;

namespace Maize {

    // Accessor for the global model instance
    MaizeModel& getModel() {
        return g_MaizeModelInstance;
    }

    // Global parameters access
    int& gLeafRows() { return getModel().m_leafRows; }
    int& gLeafCols() { return getModel().m_leafCols; }

    // State access
    PlantDesc& plant() { return getModel().plant(); }
    const std::vector<LeafGeom>& leafGeoms() { return getModel().leafGeoms(); }

    // Operations
    void rebuildGeometry() { getModel().rebuildGeometry(); }
    int getTriangleCount() { return getModel().getTriangleCount(); }

    // I/O
    bool savePlantOBJ(const char* path, const char* textureFullPath) {
        return getModel().savePlantOBJ(path, textureFullPath);
    }
    bool saveMTL(const char* mtlPath, const char* textureFileNameOnly) {
        return getModel().saveMTL(mtlPath, textureFileNameOnly);
    }

    // Cubic B-spline de Boor evaluation
    Vect3d deBoor(float u, const std::vector<Vect3d>& P) {
        if (P.size() < 2) return P.empty() ? Vect3d() : P.front();
        if (P.size() < 4) {
            int i = (int)clampf(u * (P.size() - 1), 0, (float)P.size() - 2);
            float t = u * (P.size() - 1) - i;
            return lerp_v3(P[i], P[i + 1], t);
        }
        int p = 3;
        int n = (int)P.size() - 1;
        auto U = openUniformKnots((int)P.size(), p);
        int span = findSpan(n, p, u, U);
        std::vector<Vect3d> d(p + 1);
        for (int j = 0; j <= p; ++j) d[j] = P[span - p + j];
        for (int r = 1; r <= p; ++r) {
            for (int j = p; j >= r; --j) {
                float denom = U[span + 1 + j - r] - U[span - p + j];
                float a = (denom > 1e-8f) ? (u - U[span - p + j]) / denom : 0.f;
                d[j] = d[j - 1] * (1.f - a) + d[j] * a;
            }
        }
        return d[p];
    }

    // Create default leaf splines
    void makeDefaultLeafSplines(float L, float W,
        std::vector<Vect3d>& outCenter, std::vector<Vect3d>& outLeft,
        std::vector<Vect3d>& outRight, std::vector<Vect3d>& outOpening)
    {
        // Simple default splines
        outCenter = { {0, 0, 0}, {0, 0.12f * L, 0.30f * L}, {0, 0.02f * L, 0.65f * L}, {0, 0, L} };
        outLeft.clear(); outRight.clear();
        Vect3d side = { W * 0.5f, 0, 0 };
        const Vect3d& tip = outCenter.back();

        // Generate left and right splines based on center
        for (size_t i = 0; i < outCenter.size() - 1; ++i) {
            const auto& p = outCenter[i];
            outLeft.push_back(p - side);
            outRight.push_back(p + side);
        }
        outLeft.push_back(tip);
        outRight.push_back(tip);

        outOpening = {
            {0.01f, 2.0f, 0},
            {0.01f, 2.0f, 0},
            {0.01f, 2.0f, 0},
            {0.01f, 2.0f, 0}
        };
    }

    // Helper to get directory from path
    std::string dirFromPath(const std::string& p) {
        size_t pos = p.find_last_of("/\\");
        return (pos == std::string::npos) ? std::string() : p.substr(0, pos);
    }

	// Generate plant files (XML and OBJ)
    void GeneratePlant(const PlantDesc& plantDesc, const std::string& baseFileName, const std::string& texturePath) {
        // Set the global plant description to the one provided
        Maize::plant() = plantDesc;

        // Rebuild the geometry
        Maize::rebuildGeometry();

        // Create file paths
        std::string xmlPath = baseFileName + ".xml";
        std::string objPath = baseFileName + ".obj";

        // Save the XML descriptor
        Descriptor::savePlantXML(xmlPath.c_str());

        // Save the OBJ model
        Maize::savePlantOBJ(objPath.c_str(), texturePath.c_str());
    }
}

// Rebuild geometry for the entire plant
void MaizeModel::rebuildGeometry() {
    m_leafGeoms.clear();

    const Vect3d globalUp = { 0,1,0 };
    const float leafLengthDefault = 0.7f;
    const float leafWidthDefault = 0.08f;

    // Generate geometry for each tiller and its leaves
    for (size_t ti = 0; ti < m_plant.tillers.size(); ++ti) {
        const auto& td = m_plant.tillers[ti];
        const float tilt = radiansf(td.alphaDeg);
        const float ct = std::cos(tilt), st = std::sin(tilt);
        auto tiltP = [&](const Vect3d& p)->Vect3d { return { p.x() * ct - p.y() * st, p.x() * st + p.y() * ct, p.z() }; };

        // Precompute y positions for each leaf base
        std::vector<float> yAt;
        float current_y = 0.f;
        for (const auto& leaf : td.leaves) {
            current_y += std::max(0.f, leaf.distance);
            yAt.push_back(current_y);
        }

        // Generate geometry for each leaf
        for (size_t li = 0; li < td.leaves.size(); ++li) {
            const auto& L = td.leaves[li];

            // Determine leaf azimuth with noise
            unsigned int leaf_seed = td.randomSeed + (unsigned int)li;
            float noise_val = hash_to_float_minus1_1(leaf_seed);
            float leafAzimuth = td.azimuthDeg * (li)+(noise_val * td.azimuthNoise);

            float phi = radiansf(leafAzimuth);
            Vect3d rhat = { std::cos(phi), 0.f, std::sin(phi) };

            // Control points
            std::vector<Vect3d> localCtrlC, localCtrlL, localCtrlR, localCtrlOpening;
            localCtrlC = L.ctrlCenter;
            localCtrlL = L.ctrlLeft;
            localCtrlR = L.ctrlRight;
            localCtrlOpening = L.ctrlOpening;

            LeafGeom G;
            G.rows = m_leafRows; G.cols = m_leafCols;
            G.pos.resize(G.rows * G.cols);
            G.col.resize(G.rows * G.cols);
            G.uv.resize(G.rows * G.cols);
            G.nrm.resize(G.rows * G.cols);
            G.stemRows = 0;

            // Sheath parameters
            float y_sheath_start = 0.f;
            float y_sheath_end = yAt[li];
            float sheath_world_len = y_sheath_end - y_sheath_start;

            Vect3d sheath_exit_pos = { 0, y_sheath_end, 0 };

            // Redefine the local coordinate system to be intuitive
            Vect3d blade_y_axis = globalUp; // local Z maps to global Y
            Vect3d blade_z_axis = rhat;     // local Y maps to azimuth
            Vect3d blade_x_axis = blade_y_axis.Cross(blade_z_axis).GetNormalized(); // local X is perpendicular

            auto bladeLocalToWorld = [&](const Vect3d& p_local) {
                // Map UI z to blade_y_axis and UI y to blade_z_axis 
                return sheath_exit_pos + blade_x_axis * p_local.x() + blade_y_axis * p_local.z() + blade_z_axis * p_local.y();
                };

            // Generate geometry
            for (int i = 0; i < G.rows; ++i) {
                float s01 = (G.rows <= 1) ? 0.f : (float)i / (G.rows - 1);

                if (s01 < L.sheathLength) {
                    G.stemRows++;
                    float s_sheath = s01 / L.sheathLength;
                    float current_y = y_sheath_start + s_sheath * sheath_world_len;
                    Vect3d pC = { 0, current_y, 0 };

                    Vect3d stem_tangent = globalUp;
                    Vect3d stem_normal = rhat;
                    Vect3d stem_binormal = Vect3d::Cross(stem_tangent, stem_normal).GetNormalized();

                    Vect3d openingParams = Maize::deBoor(s_sheath, localCtrlOpening);
                    float global_radius = std::max(0.001f, td.radius - td.stemShrink * li);
                    float final_radius = global_radius + openingParams.x();
                    float openAngle = (openingParams.y()) * M_PI;

                    for (int j = 0; j < G.cols; ++j) {
                        float u01 = (G.cols <= 1) ? 0.f : float(j) / (G.cols - 1);
                        float angle = (u01 - 0.5f) * openAngle;
                        Vect3d offset = stem_normal * cos(angle) + stem_binormal * sin(angle);
                        G.pos[i * G.cols + j] = pC + offset * final_radius;
                        G.uv[i * G.cols + j] = { u01, s01 };
                    }
                }
                else {
                    float s_blade = (s01 - L.sheathLength) / (1.f - L.sheathLength);

                    // Get blade points in local space
                    Vect3d pL_local = Maize::deBoor(s_blade, localCtrlL);
                    Vect3d pC_local = Maize::deBoor(s_blade, localCtrlC);
                    Vect3d pR_local = Maize::deBoor(s_blade, localCtrlR);

                    // Transform to world space
                    Vect3d pL = bladeLocalToWorld(pL_local);
                    Vect3d pC = bladeLocalToWorld(pC_local);
                    Vect3d pR = bladeLocalToWorld(pR_local);

                    // Generate points across the width
                    for (int j = 0; j < G.cols; ++j) {
                        float u01 = (G.cols <= 1) ? 0.f : float(j) / (G.cols - 1);
                        Vect3d p = (u01 < 0.5f) ? lerp_v3(pL, pC, u01 * 2.f) : lerp_v3(pC, pR, (u01 - 0.5f) * 2.f);
                        G.pos[i * G.cols + j] = p;
                        G.uv[i * G.cols + j] = { u01, s01 };
                    }
                }
            }

            // Apply tilt to all positions
            for (auto& p : G.pos) {
                p = tiltP(p);
            }

            // Assign colors based on row
            for (int i = 0; i < G.rows; ++i) {
                Vect3d rowColor = getColor(i, G.stemRows);
                for (int j = 0; j < G.cols; ++j) {
                    G.col[i * G.cols + j] = rowColor;
                }
            }

            // Compute normals
            for (int i = 0; i < G.rows; ++i) {
                for (int j = 0; j < G.cols; ++j) {
                    int ip = std::min(G.rows - 1, i + 1), im = std::max(0, i - 1);
                    int jp = std::min(G.cols - 1, j + 1), jm = std::max(0, j - 1);
                    Vect3d dx = G.pos[i * G.cols + jp] - G.pos[i * G.cols + jm];
                    Vect3d dy = G.pos[ip * G.cols + j] - G.pos[im * G.cols + j];
                    G.nrm[i * G.cols + j] = dy.Cross(dx).GetNormalized();
                }
            }

            // Store control points in world space for visualization
            auto applyTiltToCtrl = [&](const std::vector<Vect3d>& src) {
                std::vector<Vect3d> tilted; tilted.reserve(src.size());
                for (const auto& p : src) tilted.push_back(tiltP(p)); return tilted;
                };

            // Update visualization to also use the anchored origin.
            auto transformBladeSplineForViz = [&](const std::vector<Vect3d>& local_ctrl) {
                std::vector<Vect3d> world_ctrl; world_ctrl.reserve(local_ctrl.size());
                for (const auto& p : local_ctrl) {
                    world_ctrl.push_back(bladeLocalToWorld(p));
                }
                return world_ctrl;
                };

            // Store tilted control points
            G.ctrlCenter = applyTiltToCtrl(transformBladeSplineForViz(localCtrlC));
            G.ctrlLeft = applyTiltToCtrl(transformBladeSplineForViz(localCtrlL));
            G.ctrlRight = applyTiltToCtrl(transformBladeSplineForViz(localCtrlR));

            G.ctrlOpening_world.clear();

            // For ctrlOpening, we visualize it in a fixed position relative to the leaf base
            if (!localCtrlOpening.empty()) {
                float plot_scale_x = 2.0f; float plot_scale_y = 2.0f;
                Vect3d plot_origin = { -1.5f, 0.5f, 0.0f };
                for (const auto& p_local : localCtrlOpening) {
                    G.ctrlOpening_world.push_back(plot_origin + Vect3d(p_local.x() * plot_scale_x, p_local.y() * plot_scale_y, 0.0f));
                }
            }

            m_leafGeoms.push_back(std::move(G));
        }
    }
}

// Get total triangle count across all leaf geometries
int MaizeModel::getTriangleCount() const {
    int count = 0;
    for (const auto& geom : m_leafGeoms) {
        if (geom.rows > 1 && geom.cols > 1) {
            count += (geom.rows - 1) * (geom.cols - 1) * 2;
        }
    }
    return count;
}

// Save OBJ file for the plant
bool MaizeModel::savePlantOBJ(const char* path, const char* textureFullPath) {
    if (!path) return false;
    const std::string objPath(path), objDir = Maize::dirFromPath(objPath);
    const std::string mtlFileName = "maize.mtl", mtlPath = objDir.empty() ? mtlFileName : (objDir + "/" + mtlFileName);
    FILE* f = nullptr;
#ifdef _MSC_VER
    fopen_s(&f, path, "w");
#else
    f = std::fopen(path, "w");
#endif
    if (!f) return false;
    std::fprintf(f, "# Maize export\nmtllib %s\nusemtl maize\n", mtlFileName.c_str());

    // Write all geometry
    for (const auto& G : m_leafGeoms) {
        for (size_t i = 0; i < G.pos.size(); ++i) {
            const auto& p = G.pos[i];
            const auto& c = G.col[i];
            // Write vertex position and color (v x y z r g b)
            std::fprintf(f, "v %.9g %.9g %.9g %.9g %.9g %.9g\n", p.x(), p.y(), p.z(), c.x(), c.y(), c.z());
        }
        for (const auto& t : G.uv)  std::fprintf(f, "vt %.9g %.9g\n", t.u, t.v);
        for (const auto& n : G.nrm) std::fprintf(f, "vn %.9g %.9g %.9g\n", n.x(), n.y(), n.z());
    }

    size_t vOffset = 1, vtOffset = 1, vnOffset = 1;
    for (const auto& G : m_leafGeoms) {
        for (int i = 0; i < G.rows - 1; ++i) {
            for (int j = 0; j < G.cols - 1; ++j) {
                // Vertex indices
                const int a = int(vOffset + i * G.cols + j), b = int(vOffset + (i + 1) * G.cols + j);
                const int c = int(vOffset + (i + 1) * G.cols + (j + 1)), d = int(vOffset + i * G.cols + (j + 1));
                // Texture indices
                const int ta = int(vtOffset + i * G.cols + j), tb = int(vtOffset + (i + 1) * G.cols + j);
                const int tc = int(vtOffset + (i + 1) * G.cols + (j + 1)), td = int(vtOffset + i * G.cols + (j + 1));
                // Normal indices
                const int na = int(vnOffset + i * G.cols + j), nb = int(vnOffset + (i + 1) * G.cols + j);
                const int nc = int(vnOffset + (i + 1) * G.cols + (j + 1)), nd = int(vnOffset + i * G.cols + (j + 1));

                // Write faces as v/vt/vn
                std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, ta, na, b, tb, nb, c, tc, nc);
                std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, ta, na, c, tc, nc, d, td, nd);
            }
        }
        vOffset += G.pos.size();
        vtOffset += G.uv.size();
        vnOffset += G.nrm.size();
    }

    std::fclose(f);
    const char* texName = nullptr; std::string texTmp;
    if (textureFullPath && textureFullPath[0]) {
        texTmp = baseNameFromPath(textureFullPath); texName = texTmp.c_str();
    }
    saveMTL(mtlPath.c_str(), texName);
    return true;
}

// Save MTL file for the plant
bool MaizeModel::saveMTL(const char* mtlPath, const char* textureFileNameOnly) {
    FILE* m = nullptr;
#ifdef _MSC_VER
    fopen_s(&m, mtlPath, "w");
#else
    m = std::fopen(mtlPath, "w");
#endif
    if (!m) return false;
    std::fprintf(m, "newmtl maize\nKa 0 0 0\nKd 1 1 1\nKs 0.04 0.04 0.04\nNs 18\nillum 2\n");
    if (textureFileNameOnly && textureFileNameOnly[0]) {
        std::fprintf(m, "map_Kd %s\n", textureFileNameOnly);
    }
    std::fclose(m);
    return true;
}