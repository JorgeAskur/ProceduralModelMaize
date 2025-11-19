#include "Maize.h"
#include "Descriptor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <map>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- HELPER FUNCTIONS ---
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }
static inline Vect3d lerp_v3(const Vect3d& a, const Vect3d& b, float t) { return a * (1.f - t) + b * t; }

static inline Vect3d lerp_color(const Vect3d& a, const Vect3d& b, float t) {
    t = clampf(t, 0.f, 1.f);
    return a * (1.f - t) + b * t;
};

static float pseudoNoise(float x, float y) {
    int n = (int)(x * 57 + y * 131);
    n = (n << 13) ^ n;
    return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

// Updated Color Function: Constant Color (Lighter Green)
static Vect3d getColor(LeafGeom::GeomType type, float t_param) {
    // Uniform Lighter Green for the whole plant
    return Vect3d(0.6f, 0.85f, 0.4f);
};

static float hash_to_float_minus1_1(unsigned int seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed = seed + (seed << 3);
    seed = seed ^ (seed >> 4);
    seed = seed * 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return ((float)(int)(seed & 0xFFFFFF) / (float)(0xFFFFFF / 2)) - 1.0f;
}

static std::vector<float> openUniformKnots(int nCtrl, int p) {
    int n = nCtrl - 1, m = n + p + 1; std::vector<float> U(m + 1, 0.f);
    for (int j = 0; j <= p; ++j) U[j] = 0.f;
    for (int j = m - p; j <= m; ++j) U[j] = 1.f;
    for (int j = p + 1; j <= m - p - 1; ++j) U[j] = float(j - p) / float(m - 2 * p);
    return U;
}

static int findSpan(int n, int p, float u, const std::vector<float>& U) {
    if (u >= U[n + 1]) return n;
    int low = p, high = n + 1, mid;
    while (high - low > 1) { mid = (low + high) / 2; if (u < U[mid]) high = mid; else low = mid; }
    return low;
}

static std::string baseNameFromPath(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return (s == std::string::npos) ? p : p.substr(s + 1);
}

static MaizeModel g_MaizeModelInstance;

namespace Maize {
    MaizeModel& getModel() { return g_MaizeModelInstance; }

    float& gMeshDensityV() { return getModel().m_densityV; }
    float& gMeshDensityU() { return getModel().m_densityU; }

    PlantDesc& plant() { return getModel().plant(); }
    const std::vector<LeafGeom>& leafGeoms() { return getModel().leafGeoms(); }
    void rebuildGeometry() { getModel().rebuildGeometry(); }
    int getTriangleCount() { return getModel().getTriangleCount(); }
    bool savePlantOBJ(const char* path, const char* textureFullPath) { return getModel().savePlantOBJ(path, textureFullPath); }
    bool saveMTL(const char* mtlPath, const char* textureFileNameOnly) { return getModel().saveMTL(mtlPath, textureFileNameOnly); }

    Vect3d deBoor(float u, const std::vector<Vect3d>& P) {
        if (P.size() < 2) return P.empty() ? Vect3d() : P.front();
        if (P.size() < 4) {
            int i = (int)clampf(u * (P.size() - 1), 0, (float)P.size() - 2);
            float t = u * (P.size() - 1) - i;
            return lerp_v3(P[i], P[i + 1], t);
        }
        int p = 3; int n = (int)P.size() - 1;
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

    void generateOpeningSpline(const LeafDesc& L, std::vector<Vect3d>& outOpening) {
        outOpening.clear();
        outOpening.push_back({ 0.0f, 2.0f, 0 });
        outOpening.push_back({ 0.0f, L.sheathEndAngle, 0 });
    }

    // --- IMPROVED SPLINE GENERATION FOR SMOOTH TANGENTS ---
    void generateLeafSplines(const LeafDesc& L, float stemRadiusAtExit, std::vector<Vect3d>& outCenter, std::vector<Vect3d>& outLeft, std::vector<Vect3d>& outRight) {
        outCenter.clear(); outLeft.clear(); outRight.clear();
        if (L.splinePoints < 2) return;

        // Initial Vector Calculation
        // Instead of immediately using leafAngle, we start VERTICAL (Local Z) to match the sheath.
        Vect3d startDir(0.0f, 0.0f, 1.0f);

        // Target Vector based on user angle (Local Y=Radial, Z=Up)
        float angleRad = radiansf(L.leafAngle);

        Vect3d targetDir(0.0f, std::cos(angleRad), std::sin(angleRad));

        // P0 Calculation: Match Sheath Arc
        float halfAngle = (L.sheathEndAngle * M_PI) * 0.5f;
        outCenter.push_back({ 0.0f, stemRadiusAtExit, 0.0f });

        // Base Nudge Application for P0
        float nudgeRadius = stemRadiusAtExit;

        float p0_lx = nudgeRadius * std::sin(-halfAngle);
        float p0_ly = nudgeRadius * std::cos(-halfAngle);
        outLeft.push_back({ p0_lx, p0_ly, 0.0f });

        float p0_rx = nudgeRadius * std::sin(halfAngle);
        float p0_ry = nudgeRadius * std::cos(halfAngle);
        outRight.push_back({ p0_rx, p0_ry, 0.0f });

        Vect3d currentPos(0.0f, stemRadiusAtExit, 0.0f);
        float segmentLen = L.leafLength / (float)(L.splinePoints - 1);
        float gravityFactor = L.droopiness * 5.0f;
        float t_p1 = (L.splinePoints > 1) ? 1.0f / (float)(L.splinePoints - 1) : 1.0f;

        // Direction State
        Vect3d currentDir = startDir;

        for (int i = 1; i < L.splinePoints; ++i) {
            float t = (float)i / (L.splinePoints - 1);

            // --- TANGENT SMOOTHING START ---
            // Blend from Vertical (startDir) to Target Angle (targetDir)
            // CRITICAL FIX: Blend distance must match Geometry Blend (0.15f)
            // This ensures the direction is fully set to 'leafAngle' exactly when the mesh becomes a blade.
            float blendEnd = 0.15f;
            float t_blend = clampf(t / blendEnd, 0.f, 1.f);

            // Smoothstep for nicer curve
            t_blend = t_blend * t_blend * (3.0f - 2.0f * t_blend);

            Vect3d baseDir = lerp_v3(startDir, targetDir, t_blend).GetNormalized();
            // --- TANGENT SMOOTHING END ---

            // Apply Gravity/Droopiness to the blended direction
            float rotAmt = radiansf(gravityFactor * t);
            float cy = baseDir.y(), cz = baseDir.z();
            float newY = cy * std::cos(rotAmt) - cz * std::sin(rotAmt);
            float newZ = cy * std::sin(rotAmt) + cz * std::cos(rotAmt);
            currentDir.SetY(newY); currentDir.SetZ(newZ);
            currentDir = currentDir.GetNormalized();

            currentPos = currentPos + currentDir * segmentLen;
            outCenter.push_back(currentPos);

            // Widths
            float halfWidth = L.leafWidth * 0.5f;
            float current_halfWidth_x = halfWidth;
            float y_s = currentPos.y();
            float z_s = currentPos.z();

            if (t <= t_p1 + 0.0001f) {
                float t_seg = t / t_p1;
                float target_x_left = -halfWidth;
                float target_x_right = halfWidth;
                float blended_x_left = lerp_v3(Vect3d(p0_lx, 0, 0), Vect3d(target_x_left, 0, 0), t_seg).x();
                float blended_x_right = lerp_v3(Vect3d(p0_rx, 0, 0), Vect3d(target_x_right, 0, 0), t_seg).x();
                y_s = lerp_v3(Vect3d(0, p0_ly, 0), Vect3d(0, currentPos.y(), 0), t_seg).y();

                outLeft.push_back({ blended_x_left, y_s, z_s });
                outRight.push_back({ blended_x_right, y_s, z_s });
            }
            else {
                float t_remap = (t - t_p1) / (1.0f - t_p1);
                float taperVal = 1.0f - pow(t_remap, L.widthTaper);
                current_halfWidth_x = halfWidth * taperVal;
                y_s = currentPos.y();

                float noiseL = 0.0f, noiseR = 0.0f;
                float tipMask = 1.0f;
                if (t > 0.8f) tipMask = (1.0f - t) / 0.2f;
                else if (t < 0.1f) tipMask = t / 0.1f;

                if (L.waveLAmp > 0.0f) noiseL = pseudoNoise(t * L.waveLFreq, (float)L.id) * L.waveLAmp * tipMask;
                if (L.waveRAmp > 0.0f) noiseR = pseudoNoise(t * L.waveRFreq, (float)L.id + 100.0f) * L.waveRAmp * tipMask;

                outLeft.push_back({ -current_halfWidth_x, y_s, z_s + noiseL });
                outRight.push_back({ current_halfWidth_x, y_s, z_s + noiseR });
            }
        }

        outLeft.back().SetX(0.0f); outLeft.back().SetZ(outCenter.back().z());
        outRight.back().SetX(0.0f); outRight.back().SetZ(outCenter.back().z());
    }

    void makeDefaultLeafSplines(float L, float W, std::vector<Vect3d>& outCenter, std::vector<Vect3d>& outLeft, std::vector<Vect3d>& outRight) {
        outCenter.clear(); outLeft.clear(); outRight.clear();
    }

    std::string dirFromPath(const std::string& p) {
        size_t pos = p.find_last_of("/\\");
        return (pos == std::string::npos) ? std::string() : p.substr(0, pos);
    }

    void GeneratePlant(const PlantDesc& plantDesc, const std::string& baseFileName, const std::string& texturePath) {
        Maize::plant() = plantDesc;
        Maize::rebuildGeometry();
        std::string xmlPath = baseFileName + ".xml";
        std::string objPath = baseFileName + ".obj";
        Descriptor::savePlantXML(xmlPath.c_str());
        Maize::savePlantOBJ(objPath.c_str(), texturePath.c_str());
    }
}

void MaizeModel::rebuildGeometry() {
    m_leafGeoms.clear();
    const Vect3d globalUp = { 0,1,0 };

    for (size_t ti = 0; ti < m_plant.tillers.size(); ++ti) {
        const auto& td = m_plant.tillers[ti];
        const float tilt = radiansf(td.alphaDeg);
        const float ct = std::cos(tilt), st = std::sin(tilt);
        auto tiltP = [&](const Vect3d& p)->Vect3d { return { p.x() * ct - p.y() * st, p.x() * st + p.y() * ct, p.z() }; };

        std::vector<float> yAt;
        float current_y = 0.f;
        for (const auto& leaf : td.leaves) {
            current_y += std::max(0.f, leaf.distance);
            yAt.push_back(current_y);
        }

        for (size_t li = 0; li < td.leaves.size(); ++li) {
            const auto& L = td.leaves[li];
            unsigned int leaf_seed = td.randomSeed + (unsigned int)li;
            float noise_val = hash_to_float_minus1_1(leaf_seed);
            float leafAzimuth = td.azimuthDeg * (li)+(noise_val * td.azimuthNoise);
            float phi = radiansf(leafAzimuth);
            Vect3d rhat = { std::cos(phi), 0.f, std::sin(phi) };

            std::vector<Vect3d> localCtrlC, localCtrlL, localCtrlR, localCtrlOpening;
            float global_radius = std::max(0.001f, td.radius - td.stemShrink * li);

            Maize::generateLeafSplines(L, global_radius, localCtrlC, localCtrlL, localCtrlR);
            Maize::generateOpeningSpline(L, localCtrlOpening);

            if (L.leafTwist != 0.0f && L.splinePoints > 1) {
                auto twistSpline = [&](std::vector<Vect3d>& ctrl) {
                    if (ctrl.empty()) return;
                    for (size_t i = 0; i < ctrl.size(); ++i) {
                        float t = (ctrl.size() == 1) ? 1.0f : (float)i / (ctrl.size() - 1);
                        float twistRad = radiansf(L.leafTwist) * t;
                        float cT = cos(twistRad), sT = sin(twistRad);
                        Vect3d p = ctrl[i];
                        Vect3d p_center = localCtrlC[i];
                        float local_x = p.x() - p_center.x();
                        float local_z = p.z() - p_center.z();
                        float new_local_x = local_x * cT - local_z * sT;
                        float new_local_z = local_x * sT + local_z * cT;
                        ctrl[i].SetX(p_center.x() + new_local_x);
                        ctrl[i].SetZ(p_center.z() + new_local_z);
                    }
                    };
                twistSpline(localCtrlL); twistSpline(localCtrlR);
            }

            // World Transform Helpers
            float y_sheath_start = 0.f;
            float y_sheath_end = yAt[li];
            float sheath_world_len = y_sheath_end - y_sheath_start;
            Vect3d sheath_exit_pos = { 0, y_sheath_end, 0 };

            Vect3d blade_y_axis = globalUp;
            Vect3d blade_z_axis = rhat;
            Vect3d blade_x_axis = blade_y_axis.Cross(blade_z_axis).GetNormalized();

            auto bladeLocalToWorld = [&](const Vect3d& p_local) {
                return sheath_exit_pos + blade_x_axis * p_local.x() + blade_y_axis * p_local.z() + blade_z_axis * p_local.y();
                };

            // =========================================================
            // PART 1: SHEATH (Open Cylinder)
            // =========================================================
            LeafGeom SheathG;
            SheathG.type = LeafGeom::GEOM_SHEATH;

            SheathG.rows = std::max(4, (int)(L.leafLength * m_densityV));
            SheathG.cols = std::max(6, (int)(L.leafWidth * m_densityU));

            SheathG.pos.resize(SheathG.rows * SheathG.cols);
            SheathG.col.resize(SheathG.rows * SheathG.cols);
            SheathG.uv.resize(SheathG.rows * SheathG.cols);
            SheathG.nrm.resize(SheathG.rows * SheathG.cols);

            for (int i = 0; i < SheathG.rows; ++i) {
                float s_sheath = (float)i / (float)(SheathG.rows - 1);
                float current_y = y_sheath_start + s_sheath * sheath_world_len;
                Vect3d pC = { 0, current_y, 0 };
                Vect3d stem_normal = rhat;
                Vect3d stem_binormal = Vect3d::Cross(globalUp, stem_normal).GetNormalized();

                float s_open;
                if (s_sheath < L.sheathOpeningStart) s_open = 0.0f;
                else {
                    float denom = 1.0f - L.sheathOpeningStart;
                    s_open = (denom > 1e-6f) ? (s_sheath - L.sheathOpeningStart) / denom : 1.0f;
                }

                Vect3d openingParams = lerp_v3(localCtrlOpening[0], localCtrlOpening[1], s_open);
                float final_radius = global_radius + openingParams.x();
                float openAngle = (openingParams.y()) * M_PI;

                for (int j = 0; j < SheathG.cols; ++j) {
                    float u01 = (float)j / (float)(SheathG.cols - 1);
                    float angle = (u01 - 0.5f) * openAngle;
                    Vect3d offset = stem_normal * cos(angle) + stem_binormal * sin(angle);

                    int idx = i * SheathG.cols + j;
                    SheathG.pos[idx] = pC + offset * final_radius;
                    SheathG.pos[idx] = tiltP(SheathG.pos[idx]);
                    SheathG.col[idx] = getColor(LeafGeom::GEOM_SHEATH, s_sheath);

                    // --- UV FIX FOR OBJ EXPORT ---
                    // Normalize V to 0..1 range based on s_sheath.
                    // Prevents texture repetition/wrapping issues in exported files.
                    SheathG.uv[idx] = { u01, s_sheath };
                    SheathG.nrm[idx] = tiltP(offset.GetNormalized());
                }
            }
            m_leafGeoms.push_back(std::move(SheathG));

            // =========================================================
            // PART 2: BLADE
            // =========================================================
            LeafGeom BladeG;
            BladeG.type = LeafGeom::GEOM_BLADE;

            BladeG.rows = std::max(6, (int)(L.leafLength * m_densityV));
            BladeG.cols = std::max(5, (int)(L.leafWidth * m_densityU));

            BladeG.pos.resize(BladeG.rows * BladeG.cols);
            BladeG.col.resize(BladeG.rows * BladeG.cols);
            BladeG.uv.resize(BladeG.rows * BladeG.cols);
            BladeG.nrm.resize(BladeG.rows * BladeG.cols);

            float current_arc_len = sheath_world_len;
            Vect3d prev_row_center_world = sheath_exit_pos;

            float openAngle = L.sheathEndAngle * M_PI;
            Vect3d stem_normal = rhat;
            Vect3d stem_binormal = Vect3d::Cross(globalUp, stem_normal).GetNormalized();
            Vect3d sheath_center_pos = { 0, y_sheath_end, 0 };

            Vect3d openingParamsLast = localCtrlOpening[1];
            float final_radius_at_collar = global_radius + openingParamsLast.x();

            for (int i = 0; i < BladeG.rows; ++i) {
                float s_blade = (float)i / (float)(BladeG.rows - 1);

                Vect3d pL_local = Maize::deBoor(s_blade, localCtrlL);
                Vect3d pC_local = Maize::deBoor(s_blade, localCtrlC);
                Vect3d pR_local = Maize::deBoor(s_blade, localCtrlR);
                Vect3d pL = bladeLocalToWorld(pL_local);
                Vect3d pC = bladeLocalToWorld(pC_local);
                Vect3d pR = bladeLocalToWorld(pR_local);

                // --- NEW GEOMETRIC BLEND LOGIC ---
                // Instead of hard switch at i==0, blend from Cylinder (Arc) to Spline (Linear/V)
                // SYNCED WITH SPLINE BLEND: Both must be 0.15f to ensure angle is met exactly at shape transition.
                float blend_dist = 0.15f;
                float t_shape = clampf(s_blade / blend_dist, 0.f, 1.f);

                // Quadratic ease-in for smoother transition
                t_shape = 1.0f - (1.0f - t_shape) * (1.0f - t_shape);

                for (int j = 0; j < BladeG.cols; ++j) {
                    float u01 = (float)j / (float)(BladeG.cols - 1);

                    // 1. Calculate Standard Spline Position (The "Blade" shape)
                    Vect3d p_spline = (u01 < 0.5f) ? lerp_v3(pL, pC, u01 * 2.f) : lerp_v3(pC, pR, (u01 - 0.5f) * 2.f);

                    // Apply standard curvature/displacement to spline
                    float blend = clampf(s_blade * 4.0f, 0.f, 1.f);
                    float sagitta = global_radius * (1.0f - cos(openAngle * 0.5f));
                    float fromCenter = (u01 - 0.5f) * 2.0f;
                    float curveAmt = fromCenter * fromCenter;
                    float bladeDepth = 0.02f * L.leafWidth * curveAmt;
                    float sheathDepth = sagitta * (1.0f - curveAmt);

                    Vect3d sheathN = blade_z_axis;
                    Vect3d bladeN = blade_y_axis;
                    Vect3d finalN = lerp_v3(sheathN, bladeN, blend).GetNormalized();
                    float displacement = lerp_v3(Vect3d(sheathDepth, 0, 0), Vect3d(bladeDepth, 0, 0), blend).x();

                    p_spline = p_spline + finalN * displacement;

                    // 2. Calculate Ideal Cylinder Position (The "Sheath Extension" shape)
                    // We project this around pC to ensure it follows the spine, even if bent
                    float angle = (u01 - 0.5f) * openAngle;
                    Vect3d offset = stem_normal * cos(angle) + stem_binormal * sin(angle);

                    // Blend Source: Static cylinder cap at the connection point
                    Vect3d p_cylinder = sheath_center_pos + offset * final_radius_at_collar;

                    // 3. Final Blend
                    Vect3d p_final = lerp_v3(p_cylinder, p_spline, t_shape);

                    int idx = i * BladeG.cols + j;
                    BladeG.pos[idx] = tiltP(p_final);
                    BladeG.col[idx] = getColor(LeafGeom::GEOM_BLADE, s_blade);

                    // --- UV FIX FOR OBJ EXPORT ---
                    // Normalize V to 0..1 range based on s_blade.
                    // Prevents texture repetition/wrapping issues in exported files.
                    BladeG.uv[idx] = { u01, s_blade };
                }

                if (i > 0) current_arc_len += (pC - prev_row_center_world).Length();
                prev_row_center_world = pC;
            }

            // Normals
            for (int i = 0; i < BladeG.rows; ++i) {
                for (int j = 0; j < BladeG.cols; ++j) {
                    int ip = std::min(BladeG.rows - 1, i + 1), im = std::max(0, i - 1);
                    int jp = std::min(BladeG.cols - 1, j + 1), jm = std::max(0, j - 1);
                    Vect3d dx = BladeG.pos[i * BladeG.cols + jp] - BladeG.pos[i * BladeG.cols + jm];
                    Vect3d dy = BladeG.pos[ip * BladeG.cols + j] - BladeG.pos[im * BladeG.cols + j];
                    BladeG.nrm[i * BladeG.cols + j] = dy.Cross(dx).GetNormalized();
                }
            }

            // Viz
            auto transformBladeSplineForViz = [&](const std::vector<Vect3d>& local_ctrl) {
                std::vector<Vect3d> world_ctrl;
                for (const auto& p : local_ctrl) world_ctrl.push_back(tiltP(bladeLocalToWorld(p)));
                return world_ctrl;
                };
            BladeG.ctrlCenter = transformBladeSplineForViz(localCtrlC);
            BladeG.ctrlLeft = transformBladeSplineForViz(localCtrlL);
            BladeG.ctrlRight = transformBladeSplineForViz(localCtrlR);

            m_leafGeoms.push_back(std::move(BladeG));
        }
    }
}

int MaizeModel::getTriangleCount() const {
    int count = 0;
    for (const auto& geom : m_leafGeoms) {
        if (geom.rows > 1 && geom.cols > 1) count += (geom.rows - 1) * (geom.cols - 1) * 2;
    }
    return count;
}

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

    for (const auto& G : m_leafGeoms) {
        for (size_t i = 0; i < G.pos.size(); ++i) {
            const auto& p = G.pos[i];
            const auto& c = G.col[i];
            std::fprintf(f, "v %.9g %.9g %.9g %.9g %.9g %.9g\n", p.x(), p.y(), p.z(), c.x(), c.y(), c.z());
        }
        for (const auto& t : G.uv)  std::fprintf(f, "vt %.9g %.9g\n", t.u, t.v);
        for (const auto& n : G.nrm) std::fprintf(f, "vn %.9g %.9g %.9g\n", n.x(), n.y(), n.z());
    }

    size_t vOffset = 1, vtOffset = 1, vnOffset = 1;
    for (const auto& G : m_leafGeoms) {
        for (int i = 0; i < G.rows - 1; ++i) {
            for (int j = 0; j < G.cols - 1; ++j) {
                const int a = int(vOffset + i * G.cols + j), b = int(vOffset + (i + 1) * G.cols + j);
                const int c = int(vOffset + (i + 1) * G.cols + (j + 1)), d = int(vOffset + i * G.cols + (j + 1));
                const int ta = int(vtOffset + i * G.cols + j), tb = int(vtOffset + (i + 1) * G.cols + j);
                const int tc = int(vtOffset + (i + 1) * G.cols + (j + 1)), td = int(vtOffset + i * G.cols + (j + 1));
                const int na = int(vnOffset + i * G.cols + j), nb = int(vnOffset + (i + 1) * G.cols + j);
                const int nc = int(vnOffset + (i + 1) * G.cols + (j + 1)), nd = int(vnOffset + i * G.cols + (j + 1));
                std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, ta, na, b, tb, nb, c, tc, nc);
                std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, ta, na, c, tc, nc, d, td, nd);
            }
        }
        vOffset += G.pos.size(); vtOffset += G.uv.size(); vnOffset += G.nrm.size();
    }
    std::fclose(f);
    const char* texName = nullptr; std::string texTmp;
    if (textureFullPath && textureFullPath[0]) {
        texTmp = baseNameFromPath(textureFullPath); texName = texTmp.c_str();
    }
    saveMTL(mtlPath.c_str(), texName);
    return true;
}

bool MaizeModel::saveMTL(const char* mtlPath, const char* textureFileNameOnly) {
    FILE* m = nullptr;
#ifdef _MSC_VER
    fopen_s(&m, mtlPath, "w");
#else
    m = std::fopen(mtlPath, "w");
#endif
    if (!m) return false;
    std::fprintf(m, "newmtl maize\nKa 0 0 0\nKd 1 1 1\nKs 0.04 0.04 0.04\nNs 18\nillum 2\n");
    if (textureFileNameOnly && textureFileNameOnly[0]) std::fprintf(m, "map_Kd %s\n", textureFileNameOnly);
    std::fclose(m);
    return true;
}