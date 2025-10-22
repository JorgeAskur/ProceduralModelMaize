#include "Maize.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <map>
#include <random>

#define M_PI 3.14159265358979323846

/* ----------------------------- math helpers ----------------------------- */
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }
static inline Vect3d lerp_v3(const Vect3d& a, const Vect3d& b, float t) { return a * (1.f - t) + b * t; }

static float hash_to_float_minus1_1(unsigned int seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed = seed + (seed << 3);
    seed = seed ^ (seed >> 4);
    seed = seed * 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return ((float)(int)(seed & 0xFFFFFF) / (float)(0xFFFFFF / 2)) - 1.0f;
}

/* ----------------------------- module state ----------------------------- */
static PlantDesc   gPlant;
static std::map<LeafKey, LeafEdit>   gEdits;
static std::vector<LeafGeom>      gLeafGeoms;
static std::vector<StemLayerGeom> gStemLayers;

/* ----------------------------- API impl ----------------------------- */
namespace Maize {

    int gLeafRows = 50;
    int gLeafCols = 11;
    float gBaseShapeFade = 0.1f;

    PlantDesc& plant() { return gPlant; }
    std::map<LeafKey, LeafEdit>& edits() { return gEdits; }
    const std::vector<LeafGeom>& leafGeoms() { return gLeafGeoms; }
    const std::vector<StemLayerGeom>& stemLayers() { return gStemLayers; }

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

    Vect3d deBoor(float u, const std::vector<Vect3d>& P) {
        if (P.size() < 4) return P.empty() ? Vect3d() : P.front();
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

    static std::vector<Vect3d> resampleSpline(const std::vector<Vect3d>& ctrl, int N) {
        std::vector<Vect3d> out;
        if (ctrl.empty() || N <= 0) return out;
        out.reserve(N);
        for (int i = 0; i < N; ++i) {
            float u = (N == 1) ? 0.f : (float)i / (N - 1);
            out.push_back(deBoor(u, ctrl));
        }
        return out;
    }

    void rebuildGeometry() {
        gLeafGeoms.clear();
        gStemLayers.clear();

        const Vect3d globalUp = { 0,1,0 };
        const float twoPi = 6.28318530718f;
        const float leafLengthDefault = 0.7f;
        const float leafWidthDefault = 0.08f;
        const int stemSides = 24;

        for (size_t ti = 0; ti < gPlant.tillers.size(); ++ti) {
            const auto& td = gPlant.tillers[ti];
            const float tilt = radiansf(td.alphaDeg);
            const float ct = std::cos(tilt), st = std::sin(tilt);
            auto tiltP = [&](const Vect3d& p)->Vect3d { return { p.x() * ct - p.y() * st, p.x() * st + p.y() * ct, p.z() }; };

            std::vector<float> yAt;
            float current_y = 0.f;
            for (const auto& leaf : td.leaves) {
                current_y += std::max(0.f, leaf.distance);
                yAt.push_back(current_y);
            }

            if (!td.leaves.empty()) {
                float last_y = 0.f;
                for (size_t k = 0; k < td.leaves.size(); ++k) {
                    float r = std::max(0.001f, td.radius - td.stemShrink * k);
                    float y0 = last_y;
                    float y1 = yAt[k];
                    StemLayerGeom layer;
                    layer.radius = r; layer.y0 = y0; layer.y1 = y1; layer.sides = stemSides;
                    for (int i = 0; i <= stemSides; ++i) {
                        float a = (float)i / stemSides * twoPi;
                        float u = (float)i / stemSides;

                        if (k % 2 == 0) u = 1.0f - u;

                        Vect3d p0 = { std::cos(a) * r, y0, std::sin(a) * r };
                        Vect3d p1 = { std::cos(a) * r, y1, std::sin(a) * r };
                        layer.band.push_back(tiltP(p0)); layer.band.push_back(tiltP(p1));
                        layer.uv.push_back({ u, y0 }); layer.uv.push_back({ u, y1 });
                    }
                    gStemLayers.push_back(std::move(layer));
                    last_y = y1;
                }
            }

            for (size_t li = 0; li < td.leaves.size(); ++li) {
                const auto& L = td.leaves[li];
                const LeafKey key{ (int)ti, (int)li };

                unsigned int leaf_seed = td.randomSeed + (unsigned int)li;
                float noise_val = hash_to_float_minus1_1(leaf_seed);
                float leafAzimuth = td.azimuthDeg * (li)+(noise_val * td.azimuthNoise);

                float phi = radiansf(leafAzimuth);
                Vect3d rhat = { std::cos(phi), 0.f, std::sin(phi) };

                std::vector<Vect3d> localCtrlC, localCtrlL, localCtrlR, localCtrlBase;
                auto it = gEdits.find(key);
                bool hasCustom = (it != gEdits.end() && it->second.hasCustom);

                if (hasCustom) {
                    localCtrlC = it->second.ctrlCenter;
                    localCtrlL = it->second.ctrlLeft;
                    localCtrlR = it->second.ctrlRight;
                    localCtrlBase = it->second.ctrlBase;
                }
                else {
                    makeDefaultLeafSplines(leafLengthDefault, leafWidthDefault, localCtrlC, localCtrlL, localCtrlR, localCtrlBase);
                }

                Vect3d local_dir = { 0,0,1 };
                if (localCtrlC.size() >= 2) {
                    local_dir = (localCtrlC[1] - localCtrlC[0]).GetNormalized();
                }

                const float baseY = yAt[li];
                float rLeaf_here = std::max(0.001f, td.radius - td.stemShrink * li);
                Vect3d base = Vect3d{ 0.f, baseY, 0.f } + rhat * rLeaf_here;

                Vect3d z_axis = (rhat * local_dir.z() + globalUp * local_dir.y()).GetNormalized();
                Vect3d x_axis = globalUp.Cross(z_axis).GetNormalized();
                Vect3d y_axis = z_axis.Cross(x_axis).GetNormalized();

                auto localToWorld = [&](const Vect3d& p_local) {
                    return base + x_axis * p_local.x() + y_axis * p_local.y() + z_axis * p_local.z();
                    };

                auto transformSpline = [&](const std::vector<Vect3d>& local_ctrl) {
                    std::vector<Vect3d> world_ctrl;
                    world_ctrl.reserve(local_ctrl.size());
                    for (const auto& p : local_ctrl) world_ctrl.push_back(localToWorld(p));
                    return world_ctrl;
                    };

                std::vector<Vect3d> worldCtrlC = transformSpline(localCtrlC);
                std::vector<Vect3d> worldCtrlL = transformSpline(localCtrlL);
                std::vector<Vect3d> worldCtrlR = transformSpline(localCtrlR);

                if (L.age > 0.01f) {
                    float droopAmount = L.age * L.age * 0.5f * leafLengthDefault;
                    auto applyDroop = [&](std::vector<Vect3d>& ctrl) {
                        for (size_t i = 1; i < ctrl.size(); ++i) {
                            float s = (float)i / (ctrl.size() - 1);
                            ctrl[i].SetY(ctrl[i].y() - droopAmount * s);
                        }
                        };
                    applyDroop(worldCtrlC); applyDroop(worldCtrlL); applyDroop(worldCtrlR);
                }

                std::vector<Vect3d> lineC = resampleSpline(worldCtrlC, gLeafRows);
                std::vector<Vect3d> lineL = resampleSpline(worldCtrlL, gLeafRows);
                std::vector<Vect3d> lineR = resampleSpline(worldCtrlR, gLeafRows);

                LeafGeom G;
                G.rows = gLeafRows; G.cols = gLeafCols;
                G.pos.resize(G.rows * G.cols); G.uv.resize(G.rows * G.cols); G.nrm.resize(G.rows * G.cols);

                for (int i = 0; i < G.rows; ++i) {
                    Vect3d pL, pC, pR;
                    if (i < lineL.size()) pL = lineL[i];
                    if (i < lineC.size()) pC = lineC[i];
                    if (i < lineR.size()) pR = lineR[i];
                    for (int j = 0; j < G.cols; ++j) {
                        float u01 = (G.cols <= 1) ? 0.f : float(j) / (G.cols - 1);
                        Vect3d p = (u01 < 0.5f) ? lerp_v3(pL, pC, u01 * 2.f) : lerp_v3(pC, pR, (u01 - 0.5f) * 2.f);
                        G.pos[i * G.cols + j] = p;
                        G.uv[i * G.cols + j] = { u01, (G.rows <= 1) ? 0.f : (float)i / (G.rows - 1) };
                    }
                }

                if (!localCtrlBase.empty()) {
                    for (int i = 0; i < G.rows; ++i) {
                        float s01 = (G.rows <= 1) ? 0.f : (float)i / (G.rows - 1);
                        float fade = 1.0f - clampf(s01 / std::max(0.001f, gBaseShapeFade), 0.f, 1.f);
                        if (fade > 0.001f) {
                            for (int j = 0; j < G.cols; ++j) {
                                float u01 = (G.cols <= 1) ? 0.f : float(j) / (G.cols - 1);
                                Vect3d disp = deBoor(u01, localCtrlBase);

                                // **FIX**: Apply full 3D offset vector
                                Vect3d offset_world = x_axis * disp.x() + y_axis * disp.y() + z_axis * disp.z();
                                G.pos[i * G.cols + j] += offset_world * fade;
                            }
                        }
                    }
                }

                for (auto& p : G.pos) {
                    p = tiltP(p);
                }

                for (int i = 0; i < G.rows; ++i) {
                    for (int j = 0; j < G.cols; ++j) {
                        int ip = std::min(G.rows - 1, i + 1), im = std::max(0, i - 1);
                        int jp = std::min(G.cols - 1, j + 1), jm = std::max(0, j - 1);
                        Vect3d dx = G.pos[i * G.cols + jp] - G.pos[i * G.cols + jm];
                        Vect3d dy = G.pos[ip * G.cols + j] - G.pos[im * G.cols + j];
                        G.nrm[i * G.cols + j] = dy.Cross(dx).GetNormalized();
                    }
                }

                auto applyTiltToCtrl = [&](const std::vector<Vect3d>& src) {
                    std::vector<Vect3d> tilted;
                    tilted.reserve(src.size());
                    for (const auto& p : src) tilted.push_back(tiltP(p));
                    return tilted;
                    };
                G.ctrlCenter = applyTiltToCtrl(worldCtrlC);
                G.ctrlLeft = applyTiltToCtrl(worldCtrlL);
                G.ctrlRight = applyTiltToCtrl(worldCtrlR);

                // **FIX**: Create world-space visualization for the 3D base curve
                G.ctrlBase_world.clear();
                if (!localCtrlBase.empty()) {
                    for (size_t i = 0; i < localCtrlBase.size(); ++i) {
                        float u = (localCtrlBase.size() > 1) ? (float)i / (localCtrlBase.size() - 1) : 0.5f;
                        Vect3d pos_on_base = lerp_v3(worldCtrlL[0], worldCtrlR[0], u);

                        const Vect3d& offset_local = localCtrlBase[i];
                        Vect3d offset_world = x_axis * offset_local.x() + y_axis * offset_local.y() + z_axis * offset_local.z();

                        G.ctrlBase_world.push_back(pos_on_base + offset_world);
                    }
                }
                G.ctrlBase_world = applyTiltToCtrl(G.ctrlBase_world);


                gLeafGeoms.push_back(std::move(G));
            }
        }
    }

    int getTriangleCount() {
        int count = 0;
        for (const auto& geom : gLeafGeoms) {
            if (geom.rows > 1 && geom.cols > 1) {
                count += (geom.rows - 1) * (geom.cols - 1) * 2;
            }
        }
        for (const auto& s_geom : gStemLayers) {
            if (s_geom.sides > 0) {
                count += s_geom.sides * 2;
            }
        }
        return count;
    }


    void makeDefaultLeafSplines(float L, float W,
        std::vector<Vect3d>& outCenter, std::vector<Vect3d>& outLeft,
        std::vector<Vect3d>& outRight, std::vector<Vect3d>& outBase)
    {
        outCenter = { {0, 0, 0}, {0, 0.12f * L, 0.30f * L}, {0, 0.02f * L, 0.65f * L}, {0, 0, L} };
        outLeft.clear(); outRight.clear();
        Vect3d side = { W * 0.5f, 0, 0 };
        const Vect3d& tip = outCenter.back();
        for (size_t i = 0; i < outCenter.size() - 1; ++i) {
            const auto& p = outCenter[i];
            outLeft.push_back(p - side);
            outRight.push_back(p + side);
        }
        outLeft.push_back(tip);
        outRight.push_back(tip);

        // **FIX**: Default base curve is now 4 points of zero-offset
        outBase = { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} };
    }

    static std::string baseNameFromPath(const std::string& p) {
        size_t s = p.find_last_of("/\\");
        return (s == std::string::npos) ? p : p.substr(s + 1);
    }
    std::string Maize::dirFromPath(const std::string& p) {
        size_t pos = p.find_last_of("/\\");
        return (pos == std::string::npos) ? std::string() : p.substr(0, pos);
    }
    bool Maize::savePlantOBJ(const char* path, const char* textureFullPath) {
        if (!path) return false;
        const std::string objPath(path), objDir = dirFromPath(objPath);
        const std::string mtlFileName = "maize.mtl", mtlPath = objDir.empty() ? mtlFileName : (objDir + "/" + mtlFileName);
        FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, path, "w");
#else
        f = std::fopen(path, "w");
#endif
        if (!f) return false;
        std::fprintf(f, "# Maize export\nmtllib %s\nusemtl maize\n", mtlFileName.c_str());
        size_t vOffset = 1, vtOffset = 1;
        for (const auto& G : gLeafGeoms) {
            for (const auto& p : G.pos) std::fprintf(f, "v %.9g %.9g %.9g\n", p.x(), p.y(), p.z());
            for (const auto& t : G.uv)  std::fprintf(f, "vt %.9g %.9g\n", t.u, t.v);
        }
        for (const auto& S : gStemLayers) {
            for (const auto& p : S.band) std::fprintf(f, "v %.9g %.9g %.9g\n", p.x(), p.y(), p.z());
            for (const auto& t : S.uv) std::fprintf(f, "vt %.9g %.9g\n", t.u, t.v);
        }
        for (const auto& G : gLeafGeoms) {
            for (int i = 0; i < G.rows - 1; ++i) {
                for (int j = 0; j < G.cols - 1; ++j) {
                    const int a = int(vOffset + i * G.cols + j), b = int(vOffset + (i + 1) * G.cols + j);
                    const int c = int(vOffset + (i + 1) * G.cols + (j + 1)), d = int(vOffset + i * G.cols + (j + 1));
                    const int ta = int(vtOffset + i * G.cols + j), tb = int(vtOffset + (i + 1) * G.cols + j);
                    const int tc = int(vtOffset + (i + 1) * G.cols + (j + 1)), td = int(vtOffset + i * G.cols + (j + 1));
                    std::fprintf(f, "f %d/%d %d/%d %d/%d\n", a, ta, b, tb, c, tc);
                    std::fprintf(f, "f %d/%d %d/%d %d/%d\n", a, ta, c, tc, d, td);
                }
            }
            vOffset += G.pos.size(); vtOffset += G.uv.size();
        }
        size_t stemVBase = vOffset, stemVTBase = vtOffset;
        for (const auto& S : gStemLayers) {
            for (size_t i = 0; i + 3 < S.band.size(); i += 2) {
                const size_t a = stemVBase + i, b = stemVBase + i + 1, c2 = stemVBase + i + 2, d = stemVBase + i + 3;
                const size_t ta = stemVTBase + i, tb = stemVTBase + i + 1, tc2 = stemVTBase + i + 2, td = stemVTBase + i + 3;
                std::fprintf(f, "f %zu/%zu %zu/%zu %zu/%zu\n", a, ta, b, tb, d, td);
                std::fprintf(f, "f %zu/%zu %zu/%zu %zu/%zu\n", a, ta, d, td, c2, tc2);
            }
            stemVBase += S.band.size(); stemVTBase += S.uv.size();
        }
        std::fclose(f);
        const char* texName = nullptr; std::string texTmp;
        if (textureFullPath && textureFullPath[0]) {
            texTmp = baseNameFromPath(textureFullPath); texName = texTmp.c_str();
        }
        saveMTL(mtlPath.c_str(), texName);
        return true;
    }
    bool Maize::saveMTL(const char* mtlPath, const char* textureFileNameOnly) {
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
}