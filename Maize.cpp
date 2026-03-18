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

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }
static inline Vect3d lerp_v3(const Vect3d& a, const Vect3d& b, float t) { return a * (1.f - t) + b * t; }
static inline float fractf(float x) { return x - std::floor(x); }


static float pseudoNoise(float x, float y) {
    return std::sin(x + y);
}

static float hashNoise(float x, float y) {
    float s = std::sin(x * 12.9898f + y * 78.233f) * 43758.5453f;
    return fractf(s) * 2.0f - 1.0f;
}

static Vect3d getColor(LeafGeom::GeomType type, float t_param) {
    if (type == LeafGeom::GEOM_SHEATH) {
        float t = clampf(t_param, 0.0f, 1.0f);
        return lerp_v3(Vect3d(0.36f, 0.56f, 0.24f), Vect3d(0.46f, 0.66f, 0.32f), t);
    }
    float t = clampf(t_param, 0.0f, 1.0f);
    return lerp_v3(Vect3d(0.18f, 0.46f, 0.14f), Vect3d(0.30f, 0.62f, 0.22f), t * 0.6f);
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
    float& gStemDensityScale() { return getModel().m_stemDensityScale; }
    int& gStemRowOverride() { return getModel().m_stemRowOverride; }
    float& gStemRibbonSpacing() { return getModel().m_stemRibbonSpacing; }

    PlantDesc& plant() { return getModel().plant(); }
    const std::vector<LeafGeom>& leafGeoms() { return getModel().leafGeoms(); }
    void rebuildGeometry() { getModel().rebuildGeometry(); }
    int getTriangleCount() { return getModel().getTriangleCount(); }
    bool savePlantOBJ(const char* path, const char* leafTextureFullPath, const char* stemTextureFullPath) { return getModel().savePlantOBJ(path, leafTextureFullPath, stemTextureFullPath); }
    bool saveSkeletonOBJ(const char* path) { return getModel().saveSkeletonOBJ(path); }
    bool saveMTL(const char* mtlPath, const char* leafTextureFileNameOnly, const char* stemTextureFileNameOnly) { return getModel().saveMTL(mtlPath, leafTextureFileNameOnly, stemTextureFileNameOnly); }

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

    void generateLeafSplines(const LeafDesc& L, float stemRadiusAtExit, std::vector<Vect3d>& outCenter, std::vector<Vect3d>& outLeft, std::vector<Vect3d>& outRight) {
        outCenter.clear(); outLeft.clear(); outRight.clear();
        if (L.splinePoints < 2) return;

        // Start parallel to the stem for a smooth attachment.
        Vect3d startDir(0.0f, 1.0f, 0.0f);

        // Target direction based on leaf angle (0 = aligned with stem).
        float angleRad = radiansf(L.leafAngle);

        Vect3d targetDir(0.0f, std::cos(angleRad), std::sin(angleRad));

        // First control point follows the sheath arc.
        const float sheathEndAngle = 1.5f;
        float halfAngle = (sheathEndAngle * M_PI) * 0.5f;
        outCenter.push_back({ 0.0f, stemRadiusAtExit, 0.0f });

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
        float curlAmount = clampf(L.leafCurl, -1.f, 1.f);
        auto applyCurl = [&](float x, float z) {
            if (std::abs(curlAmount) <= 1e-5f) return Vect3d(x, 0.f, z);
            float absX = std::abs(x);
            float newX = std::copysign(absX * (1.f - std::abs(curlAmount)), x);
            float newZ = z - absX * curlAmount;
            return Vect3d(newX, 0.f, newZ);
        };

        Vect3d currentDir = startDir;

        const int extraCtrlPoints = 2;
        const bool insertExtra = L.splinePoints > 2;
        float halfWidth = L.leafWidth * 0.5f;

        float t_first = (L.splinePoints > 1) ? 1.0f / (L.splinePoints - 1) : 1.0f;
        float blendEnd = (stemRadiusAtExit * 2.0f) / std::max(L.leafLength, 1e-6f);
        blendEnd = clampf(blendEnd, 0.001f, 1.0f);
        Vect3d baseDirFirst = targetDir.GetNormalized();
        float droopinessFactorFirst = t_first * t_first;
        float rotAmtFirst = radiansf(gravityFactor * droopinessFactorFirst);
        float cyFirst = baseDirFirst.y(), czFirst = baseDirFirst.z();
        float newYFirst = cyFirst * std::cos(rotAmtFirst) - czFirst * std::sin(rotAmtFirst);
        float newZFirst = cyFirst * std::sin(rotAmtFirst) + czFirst * std::cos(rotAmtFirst);
        Vect3d firstDir = baseDirFirst;
        firstDir.SetY(newYFirst); firstDir.SetZ(newZFirst);
        firstDir = firstDir.GetNormalized();
        Vect3d firstPos = currentPos + firstDir * segmentLen;

        if (insertExtra) {
            for (int k = 1; k <= extraCtrlPoints; ++k) {
                float t_seg = (float)k / (float)(extraCtrlPoints + 1);
                Vect3d interpPos = lerp_v3(currentPos, firstPos, t_seg);
                float target_x_left = -halfWidth;
                float target_x_right = halfWidth;
                float blended_x_left = lerp_v3(Vect3d(p0_lx, 0, 0), Vect3d(target_x_left, 0, 0), t_seg).x();
                float blended_x_right = lerp_v3(Vect3d(p0_rx, 0, 0), Vect3d(target_x_right, 0, 0), t_seg).x();
                float y_seg = lerp_v3(Vect3d(0, p0_ly, 0), Vect3d(0, interpPos.y(), 0), t_seg).y();
                Vect3d leftCurl = applyCurl(blended_x_left, interpPos.z());
                Vect3d rightCurl = applyCurl(blended_x_right, interpPos.z());
                outCenter.push_back(interpPos);
                outLeft.push_back({ leftCurl.x(), y_seg, leftCurl.z() });
                outRight.push_back({ rightCurl.x(), y_seg, rightCurl.z() });
            }
        }

        currentDir = firstDir;
        currentPos = firstPos;
        outCenter.push_back(currentPos);
        {
            float t_seg = 1.0f;
            float target_x_left = -halfWidth;
            float target_x_right = halfWidth;
            float blended_x_left = lerp_v3(Vect3d(p0_lx, 0, 0), Vect3d(target_x_left, 0, 0), t_seg).x();
            float blended_x_right = lerp_v3(Vect3d(p0_rx, 0, 0), Vect3d(target_x_right, 0, 0), t_seg).x();
            float y_seg = lerp_v3(Vect3d(0, p0_ly, 0), Vect3d(0, currentPos.y(), 0), t_seg).y();
            Vect3d leftCurl = applyCurl(blended_x_left, currentPos.z());
            Vect3d rightCurl = applyCurl(blended_x_right, currentPos.z());
            outLeft.push_back({ leftCurl.x(), y_seg, leftCurl.z() });
            outRight.push_back({ rightCurl.x(), y_seg, rightCurl.z() });
        }

        for (int i = 2; i < L.splinePoints; ++i) {
            float t = (float)i / (L.splinePoints - 1);

            // Blend direction from sheath to blade angle.
            float t_blend = clampf(t / blendEnd, 0.f, 1.f);

            t_blend = t_blend * t_blend * (3.0f - 2.0f * t_blend);

            Vect3d baseDir = targetDir.GetNormalized();
            float droopinessFactor = t * t;
            float rotAmt = radiansf(gravityFactor * droopinessFactor);
            float cy = baseDir.y(), cz = baseDir.z();
            float newY = cy * std::cos(rotAmt) - cz * std::sin(rotAmt);
            float newZ = cy * std::sin(rotAmt) + cz * std::cos(rotAmt);
            Vect3d targetStepDir = baseDir;
            targetStepDir.SetY(newY); targetStepDir.SetZ(newZ);
            targetStepDir = targetStepDir.GetNormalized();
            currentDir = lerp_v3(currentDir, targetStepDir, 0.5f).GetNormalized();

            currentPos = currentPos + currentDir * segmentLen;
            outCenter.push_back(currentPos);

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

                Vect3d leftCurl = applyCurl(blended_x_left, z_s);
                Vect3d rightCurl = applyCurl(blended_x_right, z_s);
                outLeft.push_back({ leftCurl.x(), y_s, leftCurl.z() });
                outRight.push_back({ rightCurl.x(), y_s, rightCurl.z() });
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
                tipMask = tipMask * tipMask * tipMask;

                float baseMask = clampf((t - 0.1f) / 0.2f, 0.0f, 1.0f);
                baseMask = baseMask * baseMask * (3.0f - 2.0f * baseMask);
                float edgeMask = baseMask * tipMask;
                float widthMask = clampf(current_halfWidth_x / std::max(halfWidth, 1e-6f), 0.0f, 1.0f);
                edgeMask *= (widthMask * widthMask);

                if (L.waveLAmp > 0.0f) {
                    float lowFreq = std::sin((t * L.waveLFreq * 0.25f + L.waveLPhase) * (float)M_PI * 2.0f);
                    float highFreq = hashNoise(t * L.waveLFreq, (float)L.id + L.waveLPhase);
                    noiseL = (0.6f * lowFreq + 0.4f * highFreq) * L.waveLAmp * edgeMask;
                }
                if (L.waveRAmp > 0.0f) {
                    float lowFreq = std::sin((t * L.waveRFreq * 0.25f + L.waveRPhase + 0.37f) * (float)M_PI * 2.0f);
                    float highFreq = hashNoise(t * L.waveRFreq, (float)L.id + 100.0f + L.waveRPhase);
                    noiseR = (0.55f * lowFreq + 0.45f * highFreq) * (L.waveRAmp * 0.9f) * edgeMask;
                }

                Vect3d leftCurl = applyCurl(-current_halfWidth_x, z_s + noiseL);
                Vect3d rightCurl = applyCurl(current_halfWidth_x, z_s + noiseR);
                outLeft.push_back({ leftCurl.x(), y_s, leftCurl.z() });
                outRight.push_back({ rightCurl.x(), y_s, rightCurl.z() });
            }
        }

        if (L.useCtrlOverrides && !L.ctrlCenterOverride.empty() && L.ctrlCenterOverride.size() == L.ctrlLeftOverride.size() && L.ctrlCenterOverride.size() == L.ctrlRightOverride.size()) {
            outCenter = L.ctrlCenterOverride;
            outLeft = L.ctrlLeftOverride;
            outRight = L.ctrlRightOverride;
            return;
        }

        float tipHalfWidth = std::max(0.0005f, halfWidth * std::max(0.0f, L.tipWidthFactor));
        if (outCenter.size() >= 2) {
            int preTip = (int)outCenter.size() - 2;
            Vect3d tipP = outCenter.back();
            outLeft[preTip].SetX(-tipHalfWidth);
            outRight[preTip].SetX(tipHalfWidth);
            outLeft[preTip].SetY(0.5f * (outLeft[preTip].y() + tipP.y()));
            outRight[preTip].SetY(0.5f * (outRight[preTip].y() + tipP.y()));
            outLeft[preTip].SetZ(0.5f * (outLeft[preTip].z() + tipP.z()));
            outRight[preTip].SetZ(0.5f * (outRight[preTip].z() + tipP.z()));
        }
        outLeft.back() = outCenter.back();
        outRight.back() = outCenter.back();
    }

    void makeDefaultLeafSplines(float L, float W, std::vector<Vect3d>& outCenter, std::vector<Vect3d>& outLeft, std::vector<Vect3d>& outRight) {
        outCenter.clear(); outLeft.clear(); outRight.clear();
    }

    std::string dirFromPath(const std::string& p) {
        size_t pos = p.find_last_of("/\\");
        return (pos == std::string::npos) ? std::string() : p.substr(0, pos);
    }

    void GeneratePlant(const PlantDesc& plantDesc, const std::string& baseFileName, const std::string& leafTexturePath, const std::string& stemTexturePath) {
        Maize::plant() = plantDesc;
        Maize::rebuildGeometry();
        std::string xmlPath = baseFileName + ".xml";
        std::string objPath = baseFileName + ".obj";
        Descriptor::savePlantXML(xmlPath.c_str());
        Maize::savePlantOBJ(objPath.c_str(), leafTexturePath.c_str(), stemTexturePath.empty() ? nullptr : stemTexturePath.c_str());
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

        std::vector<Vect3d> stemCtrl;
        std::vector<Vect3d> leafStemPos;
        std::vector<float> leafStemLen;
        std::vector<float> leafInclinations;
        Vect3d stemPos(0.0f, 0.0f, 0.0f);
        stemCtrl.push_back(stemPos);

        float cumulativeInclinationDeg = 0.0f;
        float cumulativeLen = 0.0f;
        if (td.useStemCtrlOverrides && td.stemCtrlOverride.size() >= 2) {
            stemCtrl = td.stemCtrlOverride;
            for (size_t i = 1; i < stemCtrl.size(); ++i) {
                cumulativeLen += (stemCtrl[i] - stemCtrl[i - 1]).Length();
            }
            for (size_t i = 0; i < td.leaves.size(); ++i) {
                size_t ctrlIndex = std::min(i + 1, stemCtrl.size() - 1);
                leafStemPos.push_back(stemCtrl[ctrlIndex]);
                float len = 0.0f;
                for (size_t s = 1; s <= ctrlIndex; ++s) {
                    len += (stemCtrl[s] - stemCtrl[s - 1]).Length();
                }
                leafStemLen.push_back(len);
                cumulativeInclinationDeg += td.leaves[i].stemInclinationDeg;
                leafInclinations.push_back(cumulativeInclinationDeg);
            }
        } else {
            for (const auto& leaf : td.leaves) {
                float dist = std::max(0.f, leaf.distance);
                cumulativeLen += dist;
                cumulativeInclinationDeg += leaf.stemInclinationDeg;
                float incRad = radiansf(cumulativeInclinationDeg);
                Vect3d dir(std::sin(incRad), std::cos(incRad), 0.0f);
                stemPos = stemPos + dir * dist;
                stemCtrl.push_back(stemPos);
                leafStemPos.push_back(stemPos);
                leafStemLen.push_back(cumulativeLen);
                leafInclinations.push_back(cumulativeInclinationDeg);
            }
        }

        float stemLength = std::max(0.001f, cumulativeLen);
        auto stemRadiusAt = [&](float s) {
            float taper = std::pow(clampf(s, 0.0f, 1.0f), 1.35f);
            return std::max(0.001f, td.radius - td.stemShrink * taper);
        };
        auto stemPosAt = [&](float t) { return Maize::deBoor(t, stemCtrl); };
        auto stemTangentAt = [&](float t) {
            float dt = 1.0f / std::max(2, (int)stemCtrl.size());
            Vect3d p0 = stemPosAt(clampf(t - dt, 0.0f, 1.0f));
            Vect3d p1 = stemPosAt(clampf(t + dt, 0.0f, 1.0f));
            Vect3d tan = p1 - p0;
            if (tan.Length() < 1e-6f) tan = globalUp;
            return tan.GetNormalized();
        };

        if (!leafStemPos.empty()) {
            LeafGeom StemG;
            StemG.type = LeafGeom::GEOM_SHEATH;

            float sheathDensityV = m_densityV * m_stemDensityScale;
            float sheathDensityU = m_densityU * m_stemDensityScale;
            StemG.rows = std::max(4, (int)(stemLength * sheathDensityV));
            if (m_stemRowOverride > 0)
                StemG.rows = m_stemRowOverride;
            StemG.cols = std::max(12, (int)std::round(sheathDensityU));

            StemG.pos.resize(StemG.rows * StemG.cols);
            StemG.col.resize(StemG.rows * StemG.cols);
            StemG.uv.resize(StemG.rows * StemG.cols);
            StemG.nrm.resize(StemG.rows * StemG.cols);

            Vect3d prevNormal(1.0f, 0.0f, 0.0f);
            for (int i = 0; i < StemG.rows; ++i) {
                float s_stem = (StemG.rows == 1) ? 0.0f : (float)i / (float)(StemG.rows - 1);
                float ribbonSpacing = std::max(0.01f, m_stemRibbonSpacing);
                float stemRepeats = std::max(1.0f, stemLength / ribbonSpacing);
                float v_repeat = s_stem * stemRepeats;
                float v_wrapped = v_repeat - std::floor(v_repeat);
                Vect3d pC = stemPosAt(s_stem);
                Vect3d tangent = stemTangentAt(s_stem);
                Vect3d normal = Vect3d::Cross(globalUp, tangent);
                if (normal.Length() < 1e-6f) normal = prevNormal;
                normal = normal.GetNormalized();
                if (normal.Dot(prevNormal) < 0.0f) normal = -normal;
                prevNormal = normal;
                Vect3d binormal = tangent.Cross(normal).GetNormalized();

                float radius = stemRadiusAt(s_stem);
                float openAngle = 2.0f * (float)M_PI;

                for (int j = 0; j < StemG.cols; ++j) {
                    float u01 = (StemG.cols == 1) ? 0.0f : (float)j / (float)(StemG.cols - 1);
                    float angle = u01 * openAngle;
                    Vect3d offset = normal * std::cos(angle) + binormal * std::sin(angle);

                    int idx = i * StemG.cols + j;
                    StemG.pos[idx] = tiltP(pC + offset * radius);
                    StemG.col[idx] = getColor(LeafGeom::GEOM_SHEATH, s_stem);
                    StemG.uv[idx] = { u01, v_wrapped };
                    StemG.nrm[idx] = tiltP(offset.GetNormalized());
                }
            }
            StemG.ctrlCenter.clear();
            for (const auto& p : stemCtrl) StemG.ctrlCenter.push_back(tiltP(p));
            m_leafGeoms.push_back(std::move(StemG));
        }

        cumulativeInclinationDeg = 0.0f;

        for (size_t li = 0; li < td.leaves.size(); ++li) {
            const auto& L = td.leaves[li];
            LeafDesc LInclined = L;
            if (li < leafInclinations.size()) {
                LInclined.stemInclinationDeg = leafInclinations[li];
            }
            unsigned int leaf_seed = td.randomSeed + (unsigned int)li;
            float noise_val = hash_to_float_minus1_1(leaf_seed);
            float leafAzimuth = td.azimuthDeg * (li)+(noise_val * td.azimuthNoise);
            float phi = radiansf(leafAzimuth);
            Vect3d rhat = { std::cos(phi), 0.f, std::sin(phi) };

            std::vector<Vect3d> localCtrlC, localCtrlL, localCtrlR;
            float tLeaf = (stemLength > 1e-6f && li < leafStemLen.size()) ? (leafStemLen[li] / stemLength) : 0.0f;
            tLeaf = clampf(tLeaf, 0.0f, 1.0f);
            float global_radius = stemRadiusAt(tLeaf);

            Maize::generateLeafSplines(LInclined, global_radius, localCtrlC, localCtrlL, localCtrlR);

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
            Vect3d sheath_exit_pos = (li < leafStemPos.size()) ? leafStemPos[li] : Vect3d(0.0f, 0.0f, 0.0f);

            Vect3d blade_y_axis = stemTangentAt(tLeaf);
            Vect3d blade_z_axis = rhat;
            Vect3d blade_x_axis = blade_y_axis.Cross(blade_z_axis);
            if (blade_x_axis.Length() < 1e-6f) {
                blade_z_axis = Vect3d(0.0f, 0.0f, 1.0f);
                blade_x_axis = blade_y_axis.Cross(blade_z_axis);
            }
            blade_x_axis = blade_x_axis.GetNormalized();
            blade_z_axis = blade_x_axis.Cross(blade_y_axis).GetNormalized();

            auto bladeLocalToWorld = [&](const Vect3d& p_local) {
                return sheath_exit_pos + blade_x_axis * p_local.x() + blade_y_axis * p_local.z() + blade_z_axis * p_local.y();
                };

            // Blade.
            LeafGeom BladeG;
            BladeG.type = LeafGeom::GEOM_BLADE;

            BladeG.rows = std::max(6, (int)(L.leafLength * m_densityV));
            BladeG.cols = std::max(5, (int)(L.leafWidth * m_densityU));

            BladeG.pos.resize(BladeG.rows * BladeG.cols);
            BladeG.col.resize(BladeG.rows * BladeG.cols);
            BladeG.uv.resize(BladeG.rows * BladeG.cols);
            BladeG.nrm.resize(BladeG.rows * BladeG.cols);

            const float sheathEndAngle = 2.0f;
            float openAngle = sheathEndAngle * M_PI;
            Vect3d stem_axis = blade_y_axis.GetNormalized();
            Vect3d stem_normal = rhat - stem_axis * stem_axis.Dot(rhat);
            if (stem_normal.Length() < 1e-6f) stem_normal = blade_x_axis;
            stem_normal = stem_normal.GetNormalized();
            Vect3d stem_binormal = stem_axis.Cross(stem_normal).GetNormalized();

            float sheathOuterScale = clampf(L.sheathOuterScale, 1.0f, 1.6f);
            float final_radius_at_collar = std::max(0.001f, global_radius * sheathOuterScale);
            float sheathBelowOffset = std::max(final_radius_at_collar * 1.5f, L.leafLength * 0.04f);
            Vect3d sheath_center_pos = sheath_exit_pos - stem_axis * sheathBelowOffset;

            float blendLength = std::max(0.001f, final_radius_at_collar * 2.0f);
            float approxRowLen = L.leafLength / (float)std::max(1, BladeG.rows - 1);
            blendLength = std::max(blendLength, approxRowLen * 6.0f);
            float liguleWrapLength = std::max(0.001f, final_radius_at_collar * std::max(0.5f, L.liguleWrapLengthScale));
            float liguleUnfoldPower = std::max(0.2f, L.liguleUnfoldSharpness);
            float sheathFrac = 0.0f;
            float sheathGrowEndFrac = std::min(sheathFrac, std::max(0.04f, sheathFrac * 0.55f));
            float unfoldStartFrac = sheathFrac;
            float unfoldEndFrac = clampf(unfoldStartFrac + 0.22f, unfoldStartFrac + 0.02f, 0.99f);
            float arcLen = 0.0f;
            Vect3d prevCenter = sheath_exit_pos;

            for (int i = 0; i < BladeG.rows; ++i) {
                float s_blade = (float)i / (float)(BladeG.rows - 1);

                Vect3d pL_local = Maize::deBoor(s_blade, localCtrlL);
                Vect3d pC_local = Maize::deBoor(s_blade, localCtrlC);
                Vect3d pR_local = Maize::deBoor(s_blade, localCtrlR);
                Vect3d pL = bladeLocalToWorld(pL_local);
                Vect3d pC = bladeLocalToWorld(pC_local);
                Vect3d pR = bladeLocalToWorld(pR_local);

                if (i > 0) {
                    arcLen += (pC - prevCenter).Length();
                }
                prevCenter = pC;
                float t_shape = clampf(arcLen / blendLength, 0.f, 1.f);

                t_shape = 1.0f - (1.0f - t_shape) * (1.0f - t_shape);

                for (int j = 0; j < BladeG.cols; ++j) {
                    float u01 = (float)j / (float)(BladeG.cols - 1);

                    Vect3d p_spline = (u01 < 0.5f) ? lerp_v3(pL, pC, u01 * 2.f) : lerp_v3(pC, pR, (u01 - 0.5f) * 2.f);

                    float blend = t_shape;
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

                    float midribMask = std::exp(-std::pow((u01 - 0.5f) / 0.075f, 2.0f));
                    float midribTipStart = clampf(L.midribTipTaperStart, 0.2f, 0.98f);
                    float midribTipFade = 1.0f;
                    if (s_blade > midribTipStart) {
                        midribTipFade = clampf((1.0f - s_blade) / std::max(0.01f, 1.0f - midribTipStart), 0.0f, 1.0f);
                        midribTipFade = midribTipFade * midribTipFade * (3.0f - 2.0f * midribTipFade);
                    }
                    float midribHeight = (0.045f * L.leafWidth) * (0.35f + 0.65f * t_shape) * midribMask * midribTipFade;
                    p_spline = p_spline + finalN * midribHeight;

                    float surfaceNoiseFreq = std::max(0.1f, L.surfaceNoiseFreq);
                    float surfaceNoiseAmp = std::max(0.0f, L.surfaceNoiseAmp);
                    float surfaceNoise = hashNoise(u01 * surfaceNoiseFreq, s_blade * surfaceNoiseFreq + (float)L.id * 0.13f);
                    float surfaceAmp = surfaceNoiseAmp * L.leafWidth;
                    float tipSurfaceMask = 1.0f;
                    if (s_blade > 0.75f) {
                        tipSurfaceMask = clampf((1.0f - s_blade) / 0.25f, 0.0f, 1.0f);
                        tipSurfaceMask = tipSurfaceMask * tipSurfaceMask * tipSurfaceMask;
                    }
                    p_spline = p_spline + finalN * (surfaceNoise * surfaceAmp * (1.0f - 0.85f * midribMask) * tipSurfaceMask);

                    // Sheath extension around the collar.
                    float angle = (u01 - 0.5f) * openAngle;
                    Vect3d offset = stem_normal * cos(angle) + stem_binormal * sin(angle);

                    float growOut = 1.0f;
                    if (sheathGrowEndFrac > 0.001f && s_blade < sheathGrowEndFrac) {
                        growOut = clampf(s_blade / sheathGrowEndFrac, 0.0f, 1.0f);
                        growOut = growOut * growOut * (3.0f - 2.0f * growOut);
                    }
                    float sheathRadius = global_radius + (final_radius_at_collar - global_radius) * growOut;
                    Vect3d p_cylinder = sheath_center_pos + offset * sheathRadius;

                    float unfoldPhase = 1.0f;
                    if (s_blade <= unfoldStartFrac) {
                        unfoldPhase = 0.0f;
                    } else if (s_blade < unfoldEndFrac) {
                        unfoldPhase = clampf((s_blade - unfoldStartFrac) / std::max(0.001f, unfoldEndFrac - unfoldStartFrac), 0.0f, 1.0f);
                        unfoldPhase = unfoldPhase * unfoldPhase * (3.0f - 2.0f * unfoldPhase);
                    }

                    float liguleUnfold = clampf(arcLen / liguleWrapLength, 0.0f, 1.0f);
                    liguleUnfold = std::pow(liguleUnfold, liguleUnfoldPower);
                    float sheathBlend = unfoldPhase * liguleUnfold;

                    Vect3d p_final = lerp_v3(p_cylinder, p_spline, sheathBlend);

                    // Keep sheath/collar outside the stem while unwrapping.
                    float unwrapProtect = 1.0f - sheathBlend;
                    float collarProtect = clampf((unfoldEndFrac + 0.12f - s_blade) / 0.12f, 0.0f, 1.0f);
                    float baseRegionProtect = clampf((0.45f - s_blade) / 0.45f, 0.0f, 1.0f);
                    float protect = (std::max)(unwrapProtect, (std::max)(collarProtect, baseRegionProtect));
                    if (protect > 0.001f) {
                        Vect3d rel = p_final - sheath_center_pos;
                        Vect3d relPerp = rel - stem_axis * rel.Dot(stem_axis);
                        float relPerpLen = relPerp.Length();
                        float minWrapRadius = global_radius + (final_radius_at_collar - global_radius) * (0.55f + 0.45f * protect);
                        minWrapRadius = (std::max)(minWrapRadius, global_radius * 1.08f);
                        if (relPerpLen < minWrapRadius) {
                            Vect3d outDir = (relPerpLen > 1e-6f) ? (relPerp / relPerpLen) : stem_normal;
                            p_final = sheath_center_pos + stem_axis * rel.Dot(stem_axis) + outDir * minWrapRadius;
                        }
                    }

                    int idx = i * BladeG.cols + j;
                    BladeG.pos[idx] = tiltP(p_final);
                    Vect3d bladeColor = getColor(LeafGeom::GEOM_BLADE, s_blade);
                    float midribTipFadeColor = clampf((1.0f - s_blade) / std::max(0.01f, 1.0f - midribTipStart), 0.0f, 1.0f);
                    midribTipFadeColor = midribTipFadeColor * midribTipFadeColor * (3.0f - 2.0f * midribTipFadeColor);
                    float midribColorBoost = 0.16f * midribMask * (0.25f + 0.75f * t_shape) * midribTipFadeColor;
                    bladeColor = bladeColor + Vect3d(midribColorBoost, midribColorBoost, midribColorBoost * 0.9f);
                    bladeColor.SetX(clampf(bladeColor.x(), 0.0f, 1.0f));
                    bladeColor.SetY(clampf(bladeColor.y(), 0.0f, 1.0f));
                    bladeColor.SetZ(clampf(bladeColor.z(), 0.0f, 1.0f));
                    BladeG.col[idx] = bladeColor;

                    float midribTexStrength = clampf(L.midribTextureStrength, 0.0f, 0.95f);
                    float uvU = 0.5f + (u01 - 0.5f) * (1.0f - midribTexStrength * midribMask);
                    BladeG.uv[idx] = { clampf(uvU, 0.0f, 1.0f), s_blade };
                }

            }

            for (int i = 0; i < BladeG.rows; ++i) {
                for (int j = 0; j < BladeG.cols; ++j) {
                    int ip = std::min(BladeG.rows - 1, i + 1), im = std::max(0, i - 1);
                    int jp = std::min(BladeG.cols - 1, j + 1), jm = std::max(0, j - 1);
                    Vect3d dx = BladeG.pos[i * BladeG.cols + jp] - BladeG.pos[i * BladeG.cols + jm];
                    Vect3d dy = BladeG.pos[ip * BladeG.cols + j] - BladeG.pos[im * BladeG.cols + j];
                    BladeG.nrm[i * BladeG.cols + j] = dy.Cross(dx).GetNormalized();
                }
            }

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

bool MaizeModel::savePlantOBJ(const char* path, const char* leafTextureFullPath, const char* stemTextureFullPath) {
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
    std::fprintf(f, "# Maize export\nmtllib %s\n", mtlFileName.c_str());

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
        const char* material = (G.type == LeafGeom::GEOM_SHEATH) ? "maize_stem" : "maize_leaf";
        std::fprintf(f, "usemtl %s\n", material);
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
    const char* leafTexName = nullptr;
    const char* stemTexName = nullptr;
    std::string leafTexTmp;
    std::string stemTexTmp;
    if (leafTextureFullPath && leafTextureFullPath[0]) {
        leafTexTmp = baseNameFromPath(leafTextureFullPath);
        leafTexName = leafTexTmp.c_str();
    }
    if (stemTextureFullPath && stemTextureFullPath[0]) {
        stemTexTmp = baseNameFromPath(stemTextureFullPath);
        stemTexName = stemTexTmp.c_str();
    } else {
        stemTexName = leafTexName;
    }
    saveMTL(mtlPath.c_str(), leafTexName, stemTexName);
    return true;
}

bool MaizeModel::saveMTL(const char* mtlPath, const char* leafTextureFileNameOnly, const char* stemTextureFileNameOnly) {
    FILE* m = nullptr;
#ifdef _MSC_VER
    fopen_s(&m, mtlPath, "w");
#else
    m = std::fopen(mtlPath, "w");
#endif
    if (!m) return false;
    std::fprintf(m, "newmtl maize_leaf\nKa 0 0 0\nKd 1 1 1\nKs 0.04 0.04 0.04\nNs 18\nillum 2\n");
    if (leafTextureFileNameOnly && leafTextureFileNameOnly[0]) std::fprintf(m, "map_Kd %s\n", leafTextureFileNameOnly);
    std::fprintf(m, "\nnewmtl maize_stem\nKa 0 0 0\nKd 1 1 1\nKs 0.04 0.04 0.04\nNs 18\nillum 2\n");
    if (stemTextureFileNameOnly && stemTextureFileNameOnly[0]) std::fprintf(m, "map_Kd %s\n", stemTextureFileNameOnly);
    std::fclose(m);
    return true;
}

bool MaizeModel::saveSkeletonOBJ(const char* path) const {
    if (!path) return false;
    FILE* f = nullptr;
#ifdef _MSC_VER
    fopen_s(&f, path, "w");
#else
    f = std::fopen(path, "w");
#endif
    if (!f) return false;

    std::fprintf(f, "# Plant skeleton (center splines only)\n");

    int vertexOffset = 1;
    for (const auto& G : m_leafGeoms) {
        if (G.ctrlCenter.size() < 2) continue;
        for (const auto& p : G.ctrlCenter) {
            std::fprintf(f, "v %.9g %.9g %.9g\n", p.x(), p.y(), p.z());
        }
        std::fprintf(f, "l");
        for (size_t i = 0; i < G.ctrlCenter.size(); ++i) {
            std::fprintf(f, " %d", vertexOffset + (int)i);
        }
        std::fprintf(f, "\n");
        vertexOffset += (int)G.ctrlCenter.size();
    }

    std::fclose(f);
    return true;
}