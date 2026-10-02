#include "Maize.h"
#include "Descriptor.h"
#include "lsystem/TasselDescriptor.hpp"
#include "lsystem/TasselGrowthModel.hpp"
#include "lsystem/TasselMesh.hpp"
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

struct StemBuildData {
    // stemCtrl is the spline base for the stem centerline
    std::vector<Vect3d> stemCtrl;
    std::vector<Vect3d> leafStemPos;
    std::vector<float> leafStemLen;
    std::vector<float> leafInclinations;
    float stemLength = 0.0f;
};

struct StemSampler {
    // Small wrapper that turns the discrete stem control points into a continuous radius
    const TillerDesc& tiller;
    const StemBuildData& data;
    Vect3d globalUp;

    float radiusAt(float s) const {
        // Exponent 1.35 produces a slightly concave taper profile (radius shrinks
        // faster near the tip than the base), consistent with measured maize culm profiles.
        float taper = std::pow(clampf(s, 0.0f, 1.0f), 1.35f);
        return std::max(0.001f, tiller.radius - tiller.stemShrink * taper);
    }

    Vect3d posAt(float t) const {
        return Maize::deBoor(t, data.stemCtrl);
    }

    Vect3d tangentAt(float t) const {
        float dt = 1.0f / std::max(2, (int)data.stemCtrl.size());
        Vect3d p0 = posAt(clampf(t - dt, 0.0f, 1.0f));
        Vect3d p1 = posAt(clampf(t + dt, 0.0f, 1.0f));
        Vect3d tan = p1 - p0;
        if (tan.Length() < 1e-6f) tan = globalUp;
        return tan.GetNormalized();
    }
};

struct BladeFrame {
    // World-space frame at the collar where a leaf exits the stem
    Vect3d sheathExitPos;
    Vect3d xAxis;
    Vect3d yAxis;
    Vect3d zAxis;
};

static Vect3d tiltPoint(const Vect3d& p, float ct, float st) {
    return { p.x() * ct - p.y() * st, p.x() * st + p.y() * ct, p.z() };
}

static StemBuildData buildStemData(const TillerDesc& td) {
    // Build the stem centerline once up front
    StemBuildData data;
    Vect3d stemPos(0.0f, 0.0f, 0.0f);
    data.stemCtrl.push_back(stemPos);

    float cumulativeInclinationDeg = 0.0f;
    float cumulativeLen = 0.0f;
    if (td.useStemCtrlOverrides && td.stemCtrlOverride.size() >= 2) {
        data.stemCtrl = td.stemCtrlOverride;
        for (size_t i = 1; i < data.stemCtrl.size(); ++i) {
            cumulativeLen += (data.stemCtrl[i] - data.stemCtrl[i - 1]).Length();
        }
        for (size_t i = 0; i < td.leaves.size(); ++i) {
            size_t ctrlIndex = std::min(i + 1, data.stemCtrl.size() - 1);
            data.leafStemPos.push_back(data.stemCtrl[ctrlIndex]);
            float len = 0.0f;
            for (size_t s = 1; s <= ctrlIndex; ++s) {
                len += (data.stemCtrl[s] - data.stemCtrl[s - 1]).Length();
            }
            data.leafStemLen.push_back(len);
            cumulativeInclinationDeg += td.leaves[i].stemInclinationDeg;
            data.leafInclinations.push_back(cumulativeInclinationDeg);
        }
    } else {
        for (const auto& leaf : td.leaves) {
            float dist = std::max(0.f, leaf.distance);
            cumulativeLen += dist;
            cumulativeInclinationDeg += leaf.stemInclinationDeg;
            float incRad = radiansf(cumulativeInclinationDeg);
            Vect3d dir(std::sin(incRad), std::cos(incRad), 0.0f);
            stemPos = stemPos + dir * dist;
            data.stemCtrl.push_back(stemPos);
            data.leafStemPos.push_back(stemPos);
            data.leafStemLen.push_back(cumulativeLen);
            data.leafInclinations.push_back(cumulativeInclinationDeg);
        }
    }

    data.stemLength = std::max(0.001f, cumulativeLen);
    return data;
}

static void applyLeafTwist(std::vector<Vect3d>& ctrl, const std::vector<Vect3d>& centerCtrl, float leafTwistDeg,
                           const std::vector<float>* sectionYawRad = nullptr) {
    if (leafTwistDeg == 0.0f || ctrl.empty()) return;
    const bool haveYaw = sectionYawRad && sectionYawRad->size() == ctrl.size();

    // Twist is applied progressively from base to tip by rotating the left/right splines around the center spline
    for (size_t i = 0; i < ctrl.size(); ++i) {
        float t = (ctrl.size() == 1) ? 1.0f : (float)i / (ctrl.size() - 1);
        float twistRad = radiansf(leafTwistDeg) * t;
        float cT = std::cos(twistRad);
        float sT = std::sin(twistRad);
        Vect3d p = ctrl[i];
        Vect3d center = centerCtrl[i];
        float yaw = haveYaw ? (*sectionYawRad)[i] : 0.0f;
        if (yaw != 0.0f) {
            // Section turned sideways by sideBend: its width axis is (cos, -sin, 0) and its
            // own radial axis (sin, cos, 0). Twist within the turned (width, vertical) plane,
            // i.e. about the turned radial axis, so the twist still spins the edges about the
            // blade rather than skewing them out of the cross-section.
            Vect3d w(std::cos(yaw), -std::sin(yaw), 0.0f);
            Vect3d f(std::sin(yaw), std::cos(yaw), 0.0f);
            Vect3d d = p - center;
            float u = d.Dot(w);
            float v = d.Dot(f);
            float z = d.z();
            float u2 = u * cT - z * sT;
            float z2 = u * sT + z * cT;
            ctrl[i] = center + w * u2 + f * v + Vect3d(0.0f, 0.0f, z2);
            continue;
        }
        float localX = p.x() - center.x();
        float localZ = p.z() - center.z();
        ctrl[i].SetX(center.x() + localX * cT - localZ * sT);
        ctrl[i].SetZ(center.z() + localX * sT + localZ * cT);
    }
}

// Sideways turn (radians) of each edge-control section, matching the order in which
// generateLeafSplines emits them: the collar section, the two interpolated base sections
// (when splinePoints > 2) and the first blade section are unturned; section i = 2..n-1 is
// turned by sideBend * i/(n-1). Empty when the leaf is not bent sideways or uses
// explicit control overrides (applyLeafTwist then keeps its original behaviour).
static std::vector<float> leafSectionYaws(const LeafDesc& L, size_t sectionCount) {
    std::vector<float> yaw;
    if (L.sideBend == 0.0f || L.splinePoints < 3 || L.useCtrlOverrides) return yaw;
    const int n = L.splinePoints;
    const size_t baseSections = 1 + 2 + 1;
    if (sectionCount != baseSections + (size_t)(n - 2)) return yaw;
    yaw.assign(sectionCount, 0.0f);
    for (int i = 2; i < n; ++i) {
        float t = (float)i / (n - 1);
        yaw[baseSections + (size_t)(i - 2)] = radiansf(L.sideBend * t);
    }
    return yaw;
}

static BladeFrame buildBladeFrame(const Vect3d& sheathExitPos, const Vect3d& stemTangent, const Vect3d& rhat) {
    // The blade frame anchors every local leaf spline to the stem
    BladeFrame frame;
    frame.sheathExitPos = sheathExitPos;
    frame.yAxis = stemTangent;
    frame.zAxis = rhat;
    frame.xAxis = frame.yAxis.Cross(frame.zAxis);
    if (frame.xAxis.Length() < 1e-6f) {
        frame.zAxis = Vect3d(0.0f, 0.0f, 1.0f);
        frame.xAxis = frame.yAxis.Cross(frame.zAxis);
    }
    frame.xAxis = frame.xAxis.GetNormalized();
    frame.zAxis = frame.xAxis.Cross(frame.yAxis).GetNormalized();
    return frame;
}

static Vect3d bladeLocalToWorld(const BladeFrame& frame, const Vect3d& pLocal) {
    return frame.sheathExitPos + frame.xAxis * pLocal.x() + frame.yAxis * pLocal.z() + frame.zAxis * pLocal.y();
}

static std::vector<Vect3d> transformSplineForViz(const std::vector<Vect3d>& localCtrl, const BladeFrame& frame, float ct, float st) {
    std::vector<Vect3d> worldCtrl;
    worldCtrl.reserve(localCtrl.size());
    for (const auto& p : localCtrl) {
        worldCtrl.push_back(tiltPoint(bladeLocalToWorld(frame, p), ct, st));
    }
    return worldCtrl;
}

static LeafGeom buildStemGeometry(const StemSampler& stemSampler, const StemBuildData& stemData, const Vect3d& globalUp, float densityV, float densityU, float stemDensityScale, float ct, float st) {
    // The stem is meshed as a tapered tube sampled along the same centerline

    LeafGeom stemGeom;
    stemGeom.type = LeafGeom::GEOM_SHEATH;

    float sheathDensityV = densityV * stemDensityScale;
    float sheathDensityU = densityU * stemDensityScale;
    stemGeom.rows = std::max(4, (int)(stemData.stemLength * sheathDensityV));
    stemGeom.cols = std::max(12, (int)std::round(sheathDensityU));

    stemGeom.pos.resize(stemGeom.rows * stemGeom.cols);
    stemGeom.col.resize(stemGeom.rows * stemGeom.cols);
    stemGeom.uv.resize(stemGeom.rows * stemGeom.cols);
    stemGeom.nrm.resize(stemGeom.rows * stemGeom.cols);

    Vect3d prevNormal(1.0f, 0.0f, 0.0f);
    for (int i = 0; i < stemGeom.rows; ++i) {
        float sStem = (stemGeom.rows == 1) ? 0.0f : (float)i / (float)(stemGeom.rows - 1);
        Vect3d pC = stemSampler.posAt(sStem);
        Vect3d tangent = stemSampler.tangentAt(sStem);
        Vect3d normal = Vect3d::Cross(globalUp, tangent);
        if (normal.Length() < 1e-6f) normal = prevNormal;
        normal = normal.GetNormalized();
        if (normal.Dot(prevNormal) < 0.0f) normal = -normal;
        prevNormal = normal;
        Vect3d binormal = tangent.Cross(normal).GetNormalized();

        float radius = stemSampler.radiusAt(sStem);
        float openAngle = 2.0f * (float)M_PI;
        for (int j = 0; j < stemGeom.cols; ++j) {
            float u01 = (stemGeom.cols == 1) ? 0.0f : (float)j / (float)(stemGeom.cols - 1);
            float angle = u01 * openAngle;
            Vect3d offset = normal * std::cos(angle) + binormal * std::sin(angle);

            int idx = i * stemGeom.cols + j;
            stemGeom.pos[idx] = tiltPoint(pC + offset * radius, ct, st);
            stemGeom.col[idx] = getColor(LeafGeom::GEOM_SHEATH, sStem);
            stemGeom.uv[idx] = { u01, 0 };
            stemGeom.nrm[idx] = tiltPoint(offset.GetNormalized(), ct, st);
        }
    }

    stemGeom.ctrlCenter.reserve(stemData.stemCtrl.size());
    for (const auto& p : stemData.stemCtrl) {
        stemGeom.ctrlCenter.push_back(tiltPoint(p, ct, st));
    }
    return stemGeom;
}

struct LeafSplineBaseState {
    // The first control section is a wrapped collar around the stem and then transitions into a normal leaf
    Vect3d currentPos;
    float baseLeftX = 0.0f;
    float baseLeftY = 0.0f;
    float baseRightX = 0.0f;
    float baseRightY = 0.0f;
};

struct LeafSectionEmitter {
    std::vector<Vect3d>& outLeft;
    std::vector<Vect3d>& outRight;
    float curlAmount = 0.0f;

    void append(float leftX, float rightX, float baseY, float baseZ, float waveOffsetL, float waveOffsetR, const Vect3d& centerP, const Vect3d& tangentHint, float yawRad = 0.0f) const {
        if (yawRad != 0.0f) {
            appendTurned(leftX, rightX, waveOffsetL, waveOffsetR, centerP, tangentHint, yawRad);
            return;
        }
        // Build one local cross-section around the center spline
        Vect3d leftP(leftX, baseY, baseZ);
        Vect3d rightP(rightX, baseY, baseZ);

        Vect3d mid = (leftP + rightP) * 0.5f;
        Vect3d recenter = centerP - mid;
        leftP = leftP + recenter;
        rightP = rightP + recenter;

        Vect3d tangent = tangentHint.GetNormalized();
        Vect3d widthAxis(1.0f, 0.0f, 0.0f);
        Vect3d normalAxis = widthAxis.Cross(tangent);
        if (normalAxis.Length() < 1e-6f) normalAxis = Vect3d(0.0f, 0.0f, 1.0f);
        normalAxis = normalAxis.GetNormalized();

        // 0.35 gives ~35% apparent width reduction at maximum curl; derived from
        // the geometric projection of a rolled cylinder onto the viewing plane.
        float widthShrink = 1.0f - 0.35f * std::abs(curlAmount);
        leftP.SetX(centerP.x() + (leftP.x() - centerP.x()) * widthShrink);
        rightP.SetX(centerP.x() + (rightP.x() - centerP.x()) * widthShrink);

        float curlLiftL = std::abs(leftP.x() - centerP.x()) * curlAmount;
        float curlLiftR = std::abs(rightP.x() - centerP.x()) * curlAmount;
        leftP = leftP + normalAxis * (curlLiftL + waveOffsetL);
        rightP = rightP + normalAxis * (curlLiftR + waveOffsetR);

        outLeft.push_back(leftP);
        outRight.push_back(rightP);
    }

    // The same cross-section with the width axis turned by the sideways bend (about
    // local z, the vertical of the leaf frame), so a leaf turning sideways keeps its width.
    void appendTurned(float leftX, float rightX, float waveOffsetL, float waveOffsetR, const Vect3d& centerP, const Vect3d& tangentHint, float yawRad) const {
        Vect3d widthAxis(std::cos(yawRad), -std::sin(yawRad), 0.0f);
        float half = 0.5f * (rightX - leftX);
        Vect3d tangent = tangentHint.GetNormalized();
        Vect3d normalAxis = widthAxis.Cross(tangent);
        if (normalAxis.Length() < 1e-6f) normalAxis = Vect3d(0.0f, 0.0f, 1.0f);
        normalAxis = normalAxis.GetNormalized();
        float widthShrink = 1.0f - 0.35f * std::abs(curlAmount);
        float offset = half * widthShrink;
        float curlLift = offset * curlAmount;
        outLeft.push_back(centerP - widthAxis * offset + normalAxis * (curlLift + waveOffsetL));
        outRight.push_back(centerP + widthAxis * offset + normalAxis * (curlLift + waveOffsetR));
    }
};

static LeafSplineBaseState initializeLeafSplineBase(float stemRadiusAtExit, std::vector<Vect3d>& outCenter, std::vector<Vect3d>& outLeft, std::vector<Vect3d>& outRight) {
    // Seed the spline with one section wrapped partway around the stem
    const float sheathEndAngle = 1.5f;
    float halfAngle = (sheathEndAngle * static_cast<float>(M_PI)) * 0.5f;

    LeafSplineBaseState base;
    base.currentPos = Vect3d(0.0f, stemRadiusAtExit, 0.0f);
    base.baseLeftX = stemRadiusAtExit * std::sin(-halfAngle);
    base.baseLeftY = stemRadiusAtExit * std::cos(-halfAngle);
    base.baseRightX = stemRadiusAtExit * std::sin(halfAngle);
    base.baseRightY = stemRadiusAtExit * std::cos(halfAngle);

    outCenter.push_back(base.currentPos);
    outLeft.push_back({ base.baseLeftX, base.baseLeftY, 0.0f });
    outRight.push_back({ base.baseRightX, base.baseRightY, 0.0f });
    return base;
}

static Vect3d computeDroppedLeafDirection(const Vect3d& targetDir, float gravityFactor, float t,
                                          float droopExponent = 2.0f, float sideBendDeg = 0.0f) {
    // droopiness is applied as a rotation in the local y/z plane
    Vect3d baseDir = targetDir.GetNormalized();
    // droop turn = droopiness * t^droopExponent; the classic t * t is kept bit for bit at
    // the default 2. Exponents are clamped to [0.25, 8]: 0 would apply the full droop from
    // the first segment and negative values blow up like 1/t near the base
    float droopinessFactor = (droopExponent == 2.0f) ? t * t
                           : std::pow(t, std::min(8.0f, std::max(0.25f, droopExponent)));
    float rotAmt = radiansf(gravityFactor * droopinessFactor);
    float cy = baseDir.y();
    float cz = baseDir.z();
    Vect3d dropped = baseDir;
    dropped.SetY(cy * std::cos(rotAmt) - cz * std::sin(rotAmt));
    dropped.SetZ(cy * std::sin(rotAmt) + cz * std::cos(rotAmt));
    if (sideBendDeg != 0.0f) {
        // turn the heading sideways (about local z, the leaf frame's vertical) by sideBend * t degrees
        float yaw = radiansf(sideBendDeg * t);
        float dx = dropped.x(), dy = dropped.y();
        dropped.SetX(dx * std::cos(yaw) + dy * std::sin(yaw));
        dropped.SetY(-dx * std::sin(yaw) + dy * std::cos(yaw));
    }
    return dropped.GetNormalized();
}

static void appendInterpolatedBaseSection(const LeafSplineBaseState& base, const Vect3d& centerPos, float tSeg, float halfWidth, const LeafSectionEmitter& emitter, const Vect3d& tangent, std::vector<Vect3d>& outCenter, bool appendCenter) {
    // Interpolate between the circular collar section and the flat blade
    float leftX = lerp_v3(Vect3d(base.baseLeftX, 0, 0), Vect3d(-halfWidth, 0, 0), tSeg).x();
    float rightX = lerp_v3(Vect3d(base.baseRightX, 0, 0), Vect3d(halfWidth, 0, 0), tSeg).x();
    float y = lerp_v3(Vect3d(0, base.baseLeftY, 0), Vect3d(0, centerPos.y(), 0), tSeg).y();
    if (appendCenter) outCenter.push_back(centerPos);
    emitter.append(leftX, rightX, y, centerPos.z(), 0.0f, 0.0f, centerPos, tangent);
}

// Margin waviness. Grass-blade margins grow slightly longer than the midrib and buckle out
// of plane, so the waves live on the outer part of each half-blade and die out toward the
// rib. They are applied per mesh vertex in buildBladeGeometry (not to the edge control
// points, which tilted the whole half-blade about the midrib like a hinged panel and were
// too sparse to carry the detail).
//
// Along the blade: a quasi-periodic sum of three incommensurate sines (wavelengths vary
// from wave to wave instead of repeating exactly), keeping the descriptor's meaning of
// waveFreq (phase = s * freq * pi/2) and waveAmp (requested peak margin displacement, m).
//
// The descriptor values are made physically plausible here rather than trusted blindly
// (generated plants can request waves that are far too tall and too short for a blade):
//  * wavelength: margin buckles scale with the width of the blade, so the shortest
//    allowed wavelength is max(2.5 cm, 1.6 x half-width). A too-high waveFreq is clamped
//    to that wavelength (the leaf stays wavy, just not crinkled like kale); faster
//    harmonics fade out by band-limiting;
//  * amplitude: soft-saturated at ~12% of the half-width (tanh, so small requests pass
//    unchanged) and slope-limited so the steepest flank stays under ~30 degrees;
//  * patchiness: a slow modulation makes some stretches of the margin wavy and others
//    nearly flat, as on real leaves, instead of a uniform ruffle tip to tip.
struct MarginWaveSide {
    float amp = 0.0f;            // effective peak displacement (m), 0 = no waves
    float freq = 0.0f;           // effective waveFreq after the wavelength clamp
    float phase = 0.0f;
    float seed = 0.0f;
    float minWavelength = 1.0f;  // m
};

static MarginWaveSide makeMarginWaveSide(float amp, float freq, float phase, float seed, const LeafDesc& leaf) {
    MarginWaveSide w;
    if (amp <= 0.0f || leaf.leafLength <= 0.0f || std::fabs(freq) < 1e-4f) return w;
    float halfW = 0.5f * leaf.leafWidth;
    w.minWavelength = std::max(0.025f, 1.6f * halfW);
    float maxFreq = 4.0f * leaf.leafLength / w.minWavelength;      // main component at minWavelength
    w.freq = (freq < 0.0f ? -1.0f : 1.0f) * std::min(std::fabs(freq), maxFreq);
    w.phase = phase;
    w.seed = seed;
    float aMax = 0.12f * halfW;
    float a = aMax * std::tanh(amp / aMax);
    float lambda = leaf.leafLength / (0.25f * std::fabs(w.freq));
    a = std::min(a, 0.09f * lambda);                               // 2*pi*A/lambda <= ~0.57
    w.amp = a;
    return w;
}

static float bandWeight(float cyclesPerBlade, float leafLength, float minWavelength) {
    float lambda = leafLength / std::max(1e-3f, std::fabs(cyclesPerBlade));
    float w = clampf((lambda - minWavelength) / (0.6f * minWavelength), 0.0f, 1.0f);
    return w * w * (3.0f - 2.0f * w);
}

// Ruffle (along the blade) for one side at blade position s, in metres.
static float marginWave(float s, const MarginWaveSide& w, float leafLength) {
    if (w.amp <= 0.0f) return 0.0f;
    float phi = s * w.freq * static_cast<float>(M_PI) * 0.5f + w.phase;
    float c1 = std::fabs(w.freq) * 0.25f;                          // cycles per blade
    float shape = 0.66f * std::sin(phi)
                + 0.24f * bandWeight(2.13f * c1, leafLength, w.minWavelength) * std::sin(2.13f * phi + w.seed * 1.7f)
                + 0.10f * bandWeight(3.71f * c1, leafLength, w.minWavelength) * std::sin(3.71f * phi + w.seed * 4.3f);
    float p = 0.5f + 0.5f * std::sin(s * 7.3f + w.seed * 2.3f) * std::cos(s * 2.9f + w.seed * 0.9f);
    p = clampf((p - 0.15f) / 0.7f, 0.0f, 1.0f);
    float patch = 0.2f + 0.8f * p * p * (3.0f - 2.0f * p);
    return w.amp * patch * shape;
}

// Small crinkle right at the margin, a bit shorter than the shortest main wave.
static float marginCrinkle(float s, const MarginWaveSide& w, float leafLength) {
    if (w.amp <= 0.0f) return 0.0f;
    float cycles = leafLength / (0.75f * w.minWavelength);
    return 0.12f * w.amp * std::sin(s * cycles * 2.0f * static_cast<float>(M_PI) + w.seed * 3.1f);
}

// Fades waves in after the collar and out toward the pinched tip.
static float marginWaveEnvelope(float s) {
    float base = clampf((s - 0.06f) / 0.16f, 0.0f, 1.0f);
    base = base * base * (3.0f - 2.0f * base);
    float tip = clampf((1.0f - s) / 0.2f, 0.0f, 1.0f);
    return base * tip * tip * tip;
}

static void finalizeLeafSplineTip(std::vector<Vect3d>& outCenter, std::vector<Vect3d>& outLeft, std::vector<Vect3d>& outRight) {
    // Force the tip to a single point
    if (outCenter.size() >= 2) {
        int preTip = (int)outCenter.size() - 2;
        Vect3d tipP = outCenter.back();
        outLeft[preTip].SetY(0.5f * (outLeft[preTip].y() + tipP.y()));
        outRight[preTip].SetY(0.5f * (outRight[preTip].y() + tipP.y()));
        outLeft[preTip].SetZ(0.5f * (outLeft[preTip].z() + tipP.z()));
        outRight[preTip].SetZ(0.5f * (outRight[preTip].z() + tipP.z()));
    }
    outLeft.back() = outCenter.back();
    outRight.back() = outCenter.back();
}

static void computeGridNormals(LeafGeom& geom) {
    for (int i = 0; i < geom.rows; ++i) {
        for (int j = 0; j < geom.cols; ++j) {
            int ip = std::min(geom.rows - 1, i + 1);
            int im = std::max(0, i - 1);
            int jp = std::min(geom.cols - 1, j + 1);
            int jm = std::max(0, j - 1);
            Vect3d dx = geom.pos[i * geom.cols + jp] - geom.pos[i * geom.cols + jm];
            Vect3d dy = geom.pos[ip * geom.cols + j] - geom.pos[im * geom.cols + j];
            geom.nrm[i * geom.cols + j] = dy.Cross(dx).GetNormalized();
        }
    }
}

// Cross-blade parameter u in [0,1] for each of `cols` columns (odd, so one sits exactly on
// the midrib at u = 0.5). Columns are placed by inverting a density over the distance from
// the rib that is raised at the rib (resolves the midrib keel, only a few percent of the
// width) and at the margins (resolves the margin ruffle), and uniform in between.
static std::vector<float> bladeColumnLayout(int cols) {
    const int N = 512;
    std::vector<float> cdf(N + 1, 0.0f);
    for (int k = 1; k <= N; ++k) {
        float a = (k - 0.5f) / N;                                    // 0 = midrib, 1 = margin
        float edge = clampf((a - 0.55f) / 0.45f, 0.0f, 1.0f);
        float rho = 1.0f + 2.2f * std::exp(-(a / 0.18f) * (a / 0.18f)) + 1.6f * edge * edge;
        cdf[k] = cdf[k - 1] + rho / N;
    }
    for (float& c : cdf) c /= cdf[N];
    int half = (cols - 1) / 2;
    std::vector<float> u(cols);
    for (int j = 0; j < cols; ++j) {
        float t = (float)(j - half) / (float)half;                  // -1..1
        float target = std::fabs(t);
        int k = (int)(std::lower_bound(cdf.begin(), cdf.end(), target) - cdf.begin());
        k = std::max(1, std::min(N, k));
        float f = (target - cdf[k - 1]) / std::max(1e-9f, cdf[k] - cdf[k - 1]);
        float a = ((float)(k - 1) + clampf(f, 0.0f, 1.0f)) / N;
        u[j] = 0.5f + 0.5f * (t < 0.0f ? -a : a);
    }
    return u;
}

static LeafGeom buildBladeGeometry(const LeafDesc& leaf, const BladeFrame& bladeFrame, const Vect3d& rhat, float globalRadius, float densityV, float densityU, float ct, float st, const std::vector<Vect3d>& localCtrlC, const std::vector<Vect3d>& localCtrlL, const std::vector<Vect3d>& localCtrlR) {
    // This converts the local left/center/right splines into the final render surface
    LeafGeom bladeGeom;
    bladeGeom.type = LeafGeom::GEOM_BLADE;
    bladeGeom.rows = std::max(6, (int)(leaf.leafLength * densityV));
    const MarginWaveSide waveSideL = makeMarginWaveSide(leaf.waveLAmp, leaf.waveLFreq, leaf.waveLPhase,
                                                        (float)leaf.id * 0.618f + leaf.waveLPhase, leaf);
    const MarginWaveSide waveSideR = makeMarginWaveSide(0.9f * leaf.waveRAmp, leaf.waveRFreq, leaf.waveRPhase + 0.37f,
                                                        (float)leaf.id * 0.618f + leaf.waveRPhase + 2.4f, leaf);
    // >= 6 rows per cycle of the shortest margin component (the crinkle, 0.75 x minWavelength)
    if (waveSideL.amp > 0.0f || waveSideR.amp > 0.0f) {
        float minWl = std::min(waveSideL.amp > 0.0f ? waveSideL.minWavelength : 1.0f,
                               waveSideR.amp > 0.0f ? waveSideR.minWavelength : 1.0f);
        bladeGeom.rows = std::max(bladeGeom.rows, (int)std::ceil(6.0f * leaf.leafLength / (0.75f * minWl)));
    }
    bladeGeom.cols = 2 * std::max(9, (int)std::ceil(leaf.leafWidth * densityU * 0.6f)) + 1;
    const std::vector<float> columnU = bladeColumnLayout(bladeGeom.cols);

    bladeGeom.pos.resize(bladeGeom.rows * bladeGeom.cols);
    bladeGeom.col.resize(bladeGeom.rows * bladeGeom.cols);
    bladeGeom.uv.resize(bladeGeom.rows * bladeGeom.cols);
    bladeGeom.nrm.resize(bladeGeom.rows * bladeGeom.cols);

    const float sheathEndAngle = 2.0f;
    float openAngle = sheathEndAngle * static_cast<float>(M_PI);
    Vect3d stemAxis = bladeFrame.yAxis.GetNormalized();
    Vect3d stemNormal = rhat - stemAxis * stemAxis.Dot(rhat);
    if (stemNormal.Length() < 1e-6f) stemNormal = bladeFrame.xAxis;
    stemNormal = stemNormal.GetNormalized();
    Vect3d stemBinormal = stemAxis.Cross(stemNormal).GetNormalized();

    float sheathOuterScale = clampf(leaf.sheathOuterScale, 1.0f, 1.6f);
    float finalRadiusAtCollar = std::max(0.001f, globalRadius * sheathOuterScale);
    float sheathBelowOffset = std::max(finalRadiusAtCollar * 1.5f, leaf.leafLength * 0.04f);
    Vect3d sheathCenterPos = bladeFrame.sheathExitPos - stemAxis * sheathBelowOffset;

    float blendLength = std::max(0.001f, finalRadiusAtCollar * 2.0f);
    float approxRowLen = leaf.leafLength / (float)std::max(1, bladeGeom.rows - 1);
    blendLength = std::max(blendLength, approxRowLen * 6.0f);
    float liguleWrapLength = std::max(0.001f, finalRadiusAtCollar * std::max(0.5f, leaf.liguleWrapLengthScale));
    float liguleUnfoldPower = std::max(0.2f, leaf.liguleUnfoldSharpness);
    float sheathGrowEndFrac = clampf(0.08f, 0.04f, 0.18f);
    float unfoldStartFrac = 0.0f;
    float unfoldEndFrac = clampf(unfoldStartFrac + 0.22f, unfoldStartFrac + 0.02f, 0.99f);
    float arcLen = 0.0f;
    Vect3d prevCenter = bladeFrame.sheathExitPos;

    for (int i = 0; i < bladeGeom.rows; ++i) {
        float sBlade = (float)i / (float)(bladeGeom.rows - 1);

        Vect3d pLLocal = Maize::deBoor(sBlade, localCtrlL);
        Vect3d pCLocal = Maize::deBoor(sBlade, localCtrlC);
        Vect3d pRLocal = Maize::deBoor(sBlade, localCtrlR);
        Vect3d pL = bladeLocalToWorld(bladeFrame, pLLocal);
        Vect3d pC = bladeLocalToWorld(bladeFrame, pCLocal);
        Vect3d pR = bladeLocalToWorld(bladeFrame, pRLocal);

        if (i > 0) arcLen += (pC - prevCenter).Length();
        prevCenter = pC;
        // shapeBlend is driven by traveled arc length instead of raw row index
        // so the collar unwrap behaves similarly even when leaves have different
        // lengths or local curvature
        float shapeBlend = clampf(arcLen / blendLength, 0.f, 1.f);
        shapeBlend = 1.0f - (1.0f - shapeBlend) * (1.0f - shapeBlend);

        // Per-row margin ruffle for each side. Narrow parts of the blade (taper, tip) carry
        // proportionally less, so the waves never fold the margin back over the blade.
        float rowWidthFrac = clampf((pR - pL).Length() / std::max(1e-6f, leaf.leafWidth), 0.0f, 1.0f);
        float waveEnv = marginWaveEnvelope(sBlade) * rowWidthFrac * rowWidthFrac * shapeBlend;
        float waveL = waveEnv * marginWave(sBlade, waveSideL, leaf.leafLength);
        float waveR = waveEnv * marginWave(sBlade, waveSideR, leaf.leafLength);
        // fine crinkle right at the margin, about three times shorter than the main waves
        float crinkleL = waveEnv * marginCrinkle(sBlade, waveSideL, leaf.leafLength);
        float crinkleR = waveEnv * marginCrinkle(sBlade, waveSideR, leaf.leafLength);

        for (int j = 0; j < bladeGeom.cols; ++j) {
            float u01 = columnU[j];
            Vect3d splineP = (u01 < 0.5f) ? lerp_v3(pL, pC, u01 * 2.f) : lerp_v3(pC, pR, (u01 - 0.5f) * 2.f);

            float fromCenter = (u01 - 0.5f) * 2.0f;
            float curveAmt = fromCenter * fromCenter;
            float sagitta = globalRadius * (1.0f - std::cos(openAngle * 0.5f));

            // Midrib footprint: wider at the base, narrowing toward the tip, then fading
            // out entirely so the blade can still resolve to a point
            float ribSigma = std::max(0.01f, leaf.midribWidth * (1.2f - 0.6f * sBlade));
            float midribMask = std::exp(-std::pow((u01 - 0.5f) / ribSigma, 2.0f));
            float midribTipStart = clampf(leaf.midribTipTaperStart, 0.2f, 0.98f);
            float midribTipFade = 1.0f;
            if (sBlade > midribTipStart) {
                midribTipFade = clampf((1.0f - sBlade) / std::max(0.01f, 1.0f - midribTipStart), 0.0f, 1.0f);
                midribTipFade = midribTipFade * midribTipFade * (3.0f - 2.0f * midribTipFade);
            }

            // Blade cross-section (finalN is the adaxial/upper side). A grass blade is
            // folded along its midrib: the two halves rise from it in a shallow V that is
            // strongest near the collar and relaxes toward the tip, on top of a gentle
            // camber. The midrib is a thick rib that bulges on the abaxial side, so it is
            // pushed *down*: from above it reads as a pale channel, from below as a keel.
            // (The previous version raised a ridge on the upper side, which is inverted.)
            const float foldEps = 0.08f;                                     // rounds the V at the rib
            float vFold = std::sqrt(curveAmt + foldEps * foldEps) - foldEps;  // smooth |fromCenter|
            float foldDepth = 0.035f * leaf.leafWidth * (1.0f - 0.6f * sBlade);    // leaf curl already cups the blade
            float keelDepth = 0.02f * leaf.leafWidth * midribMask * midribTipFade;
            float bladeDepth = 0.008f * leaf.leafWidth * curveAmt + foldDepth * vFold - keelDepth;

            // Margin ruffle: zero over the inner 30% of each half-blade, growing
            // quadratically to the full wave at the margin; the crinkle hugs the edge
            float distFromRib = std::fabs(fromCenter);
            float ruffle = clampf((distFromRib - 0.3f) / 0.7f, 0.0f, 1.0f);
            ruffle *= ruffle;
            float edgeOnly = distFromRib * distFromRib * distFromRib * distFromRib;
            edgeOnly *= edgeOnly;                                           // |x|^8
            bladeDepth += (fromCenter < 0.0f) ? (waveL * ruffle + crinkleL * edgeOnly)
                                              : (waveR * ruffle + crinkleR * edgeOnly);
            float sheathDepth = sagitta * (1.0f - curveAmt);

            Vect3d sheathN = bladeFrame.zAxis;
            Vect3d bladeN = bladeFrame.yAxis;
            Vect3d finalN = lerp_v3(sheathN, bladeN, shapeBlend).GetNormalized();
            // The same blend that unwraps the collar also rotates the local
            // surface normal from the stem-facing sheath orientation into the
            // blade-facing orientation
            float displacement = lerp_v3(Vect3d(sheathDepth, 0, 0), Vect3d(bladeDepth, 0, 0), shapeBlend).x();
            splineP = splineP + finalN * displacement;

            float surfaceNoiseFreq = std::max(0.1f, leaf.surfaceNoiseFreq);
            float surfaceNoiseAmp = std::max(0.0f, leaf.surfaceNoiseAmp);
            float surfaceNoise = hashNoise(u01 * surfaceNoiseFreq, sBlade * surfaceNoiseFreq + (float)leaf.id * 0.13f);
            float surfaceAmp = surfaceNoiseAmp * leaf.leafWidth;
            // Surface ripples avoid the midrib and fade out at the tip, which
            // keeps the silhouette cleaner than adding undulation everywhere
            float tipSurfaceMask = 1.0f;
            if (sBlade > 0.75f) {
                tipSurfaceMask = clampf((1.0f - sBlade) / 0.25f, 0.0f, 1.0f);
                tipSurfaceMask = tipSurfaceMask * tipSurfaceMask * tipSurfaceMask;
            }
            splineP = splineP + finalN * (surfaceNoise * surfaceAmp * (1.0f - 0.85f * midribMask) * tipSurfaceMask);

            float angle = (u01 - 0.5f) * openAngle;
            Vect3d offset = stemNormal * std::cos(angle) + stemBinormal * std::sin(angle);

            float growOut = 1.0f;
            if (sheathGrowEndFrac > 0.001f && sBlade < sheathGrowEndFrac) {
                growOut = clampf(sBlade / sheathGrowEndFrac, 0.0f, 1.0f);
                growOut = growOut * growOut * (3.0f - 2.0f * growOut);
            }
            float sheathRadius = globalRadius + (finalRadiusAtCollar - globalRadius) * growOut;
            Vect3d cylinderP = sheathCenterPos + offset * sheathRadius;

            // Two-stage unwrap:
            // 1. unfoldPhase uses normalized blade position
            // 2. liguleUnfold uses actual arc length from the collar
            // The product keeps the first rows attached longer on short leaves
            float unfoldPhase = 1.0f;
            if (sBlade <= unfoldStartFrac) {
                unfoldPhase = 0.0f;
            } else if (sBlade < unfoldEndFrac) {
                unfoldPhase = clampf((sBlade - unfoldStartFrac) / std::max(0.001f, unfoldEndFrac - unfoldStartFrac), 0.0f, 1.0f);
                unfoldPhase = unfoldPhase * unfoldPhase * (3.0f - 2.0f * unfoldPhase);
            }

            float liguleUnfold = clampf(arcLen / liguleWrapLength, 0.0f, 1.0f);
            liguleUnfold = std::pow(liguleUnfold, liguleUnfoldPower);
            float sheathBlend = unfoldPhase * liguleUnfold;

            Vect3d finalP = lerp_v3(cylinderP, splineP, sheathBlend);

            // While the blade is still partially wrapped, enforce a minimum
            // radius from the stem so surface noise/camber do not self-intersect
            // with the collar cylinder
            float unwrapProtect = 1.0f - sheathBlend;
            float collarProtect = clampf((unfoldEndFrac + 0.12f - sBlade) / 0.12f, 0.0f, 1.0f);
            float baseRegionProtect = clampf((0.45f - sBlade) / 0.45f, 0.0f, 1.0f);
            float protect = (std::max)(unwrapProtect, (std::max)(collarProtect, baseRegionProtect));
            if (protect > 0.001f) {
                Vect3d rel = finalP - sheathCenterPos;
                Vect3d relPerp = rel - stemAxis * rel.Dot(stemAxis);
                float relPerpLen = relPerp.Length();
                float attachRelax = 1.0f;
                if (sheathGrowEndFrac > 1e-6f && sBlade < sheathGrowEndFrac) {
                    attachRelax = clampf(sBlade / sheathGrowEndFrac, 0.0f, 1.0f);
                    attachRelax = attachRelax * attachRelax * (3.0f - 2.0f * attachRelax);
                }
                float minWrapRadius = globalRadius + (finalRadiusAtCollar - globalRadius) * ((0.55f + 0.45f * protect) * attachRelax);
                minWrapRadius = (std::max)(minWrapRadius, globalRadius * (1.0f + 0.08f * attachRelax));
                if (relPerpLen < minWrapRadius) {
                    Vect3d outDir = (relPerpLen > 1e-6f) ? (relPerp / relPerpLen) : stemNormal;
                    finalP = sheathCenterPos + stemAxis * rel.Dot(stemAxis) + outDir * minWrapRadius;
                }
            }

            int idx = i * bladeGeom.cols + j;
            bladeGeom.pos[idx] = tiltPoint(finalP, ct, st);
            Vect3d bladeColor = getColor(LeafGeom::GEOM_BLADE, sBlade);
            float midribTipFadeColor = clampf((1.0f - sBlade) / std::max(0.01f, 1.0f - midribTipStart), 0.0f, 1.0f);
            midribTipFadeColor = midribTipFadeColor * midribTipFadeColor * (3.0f - 2.0f * midribTipFadeColor);
            // The rib is paler and slightly yellower than the lamina (less chlorophyll,
            // more sclerenchyma), most visibly toward the base
            float midribColorBoost = 0.2f * midribMask * (0.25f + 0.75f * shapeBlend) * midribTipFadeColor * (1.1f - 0.4f * sBlade);
            bladeColor = bladeColor + Vect3d(midribColorBoost, midribColorBoost * 1.05f, midribColorBoost * 0.7f);
            bladeColor.SetX(clampf(bladeColor.x(), 0.0f, 1.0f));
            bladeColor.SetY(clampf(bladeColor.y(), 0.0f, 1.0f));
            bladeColor.SetZ(clampf(bladeColor.z(), 0.0f, 1.0f));
            bladeGeom.col[idx] = bladeColor;

            // Compress U coordinates slightly around the midrib so textures
            // visually "tighten" over the raised center ridge
            float midribTexStrength = clampf(leaf.midribTextureStrength, 0.0f, 0.95f);
            float uvU = 0.5f + (u01 - 0.5f) * (1.0f - midribTexStrength * midribMask);
            bladeGeom.uv[idx] = { clampf(uvU, 0.0f, 1.0f), sBlade };
        }
    }

    computeGridNormals(bladeGeom);
    bladeGeom.ctrlCenter = transformSplineForViz(localCtrlC, bladeFrame, ct, st);
    bladeGeom.ctrlLeft = transformSplineForViz(localCtrlL, bladeFrame, ct, st);
    bladeGeom.ctrlRight = transformSplineForViz(localCtrlR, bladeFrame, ct, st);
    return bladeGeom;
}

static MaizeModel g_MaizeModelInstance;

namespace Maize {
    MaizeModel& getModel() { return g_MaizeModelInstance; }

    float& gMeshDensityV() { return getModel().m_densityV; }
    float& gMeshDensityU() { return getModel().m_densityU; }
    float& gStemDensityScale() { return getModel().m_stemDensityScale; }

    PlantDesc& plant() { return getModel().plant(); }
    const std::vector<LeafGeom>& leafGeoms() { return getModel().leafGeoms(); }
    const l_system_plugin::TasselMesh& tasselMesh() { return getModel().tasselMesh(); }
    void rebuildGeometry() { getModel().rebuildGeometry(); }
    int getTriangleCount() { return getModel().getTriangleCount(); }
    bool savePlantOBJ(const char* path, const char* leafTextureFullPath, const char* stemTextureFullPath) { return getModel().savePlantOBJ(path, leafTextureFullPath, stemTextureFullPath); }
    bool saveSkeletonOBJ(const char* path, bool includeTassel) { return getModel().saveSkeletonOBJ(path, includeTassel); }
    bool saveMTL(const char* mtlPath, const char* leafTextureFileNameOnly, const char* stemTextureFileNameOnly) { return getModel().saveMTL(mtlPath, leafTextureFileNameOnly, stemTextureFileNameOnly); }

    // Evaluate a cubic open-uniform B-spline via de Boor's algorithm
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

        // Local leaf coordinate system:
        // x = width, y = collar wrap direction, z = forward along the centerline
        // The first sections are handled specially so the blade emerges from a
        // wrapped sheath instead of appearing as a flat ribbon from row 0
        float segmentLen = L.leafLength / (float)(L.splinePoints - 1);
        float gravityFactor = L.droopiness;
        float t_p1 = (L.splinePoints > 1) ? 1.0f / (float)(L.splinePoints - 1) : 1.0f;
        float halfWidth = L.leafWidth * 0.5f;
        LeafSectionEmitter emitter{ outLeft, outRight, clampf(L.leafCurl, -1.f, 1.f) };
        LeafSplineBaseState base = initializeLeafSplineBase(stemRadiusAtExit, outCenter, outLeft, outRight);

        Vect3d startDir(0.0f, 1.0f, 0.0f);
        Vect3d targetDir(0.0f, std::cos(radiansf(L.leafAngle)), std::sin(radiansf(L.leafAngle)));
        Vect3d currentDir = startDir;

        const bool insertExtra = L.splinePoints > 2;
        const int extraCtrlPoints = 2;
        float t_first = (L.splinePoints > 1) ? 1.0f / (L.splinePoints - 1) : 1.0f;
        Vect3d firstDir = computeDroppedLeafDirection(targetDir, gravityFactor, t_first, L.droopExponent, L.sideBend);
        Vect3d firstPos = base.currentPos + firstDir * segmentLen;

        if (insertExtra) {
            for (int k = 1; k <= extraCtrlPoints; ++k) {
                float t_seg = (float)k / (float)(extraCtrlPoints + 1);
                Vect3d interpPos = lerp_v3(base.currentPos, firstPos, t_seg);
                appendInterpolatedBaseSection(base, interpPos, t_seg, halfWidth, emitter, firstDir, outCenter, true);
            }
        }

        currentDir = firstDir;
        base.currentPos = firstPos;
        appendInterpolatedBaseSection(base, base.currentPos, 1.0f, halfWidth, emitter, firstDir, outCenter, true);

        for (int i = 2; i < L.splinePoints; ++i) {
            float t = (float)i / (L.splinePoints - 1);

            Vect3d targetStepDir = computeDroppedLeafDirection(targetDir, gravityFactor, t, L.droopExponent, L.sideBend);
            currentDir = lerp_v3(currentDir, targetStepDir, 0.5f).GetNormalized();

            base.currentPos = base.currentPos + currentDir * segmentLen;
            outCenter.push_back(base.currentPos);

            float current_halfWidth_x = halfWidth;
            float y_s = base.currentPos.y();
            float z_s = base.currentPos.z();

            if (t <= t_p1 + 0.0001f) {
                float t_seg = t / t_p1;
                float leftX = lerp_v3(Vect3d(base.baseLeftX, 0, 0), Vect3d(-halfWidth, 0, 0), t_seg).x();
                float rightX = lerp_v3(Vect3d(base.baseRightX, 0, 0), Vect3d(halfWidth, 0, 0), t_seg).x();
                y_s = lerp_v3(Vect3d(0, base.baseLeftY, 0), Vect3d(0, base.currentPos.y(), 0), t_seg).y();
                emitter.append(leftX, rightX, y_s, z_s, 0.0f, 0.0f, base.currentPos, currentDir);
            }
            else {
                float t_remap = (t - t_p1) / (1.0f - t_p1);
                float taperVal = 1.0f - std::pow(t_remap, L.widthTaper);
                current_halfWidth_x = halfWidth * taperVal;
                y_s = base.currentPos.y();

                // Margin waviness is no longer baked into the edge control points: it is
                // a per-vertex ruffle in buildBladeGeometry (see marginWave)
                emitter.append(-current_halfWidth_x, current_halfWidth_x, y_s, z_s, 0.0f, 0.0f, base.currentPos, currentDir,
                               radiansf(L.sideBend * t));
            }
        }

        if (L.useCtrlOverrides && !L.ctrlCenterOverride.empty() && L.ctrlCenterOverride.size() == L.ctrlLeftOverride.size() && L.ctrlCenterOverride.size() == L.ctrlRightOverride.size()) {
            // The editor can replace the procedural spline set entirely
            outCenter = L.ctrlCenterOverride;
            outLeft = L.ctrlLeftOverride;
            outRight = L.ctrlRightOverride;
            return;
        }

        finalizeLeafSplineTip(outCenter, outLeft, outRight);
    }

    float normalizeAzimuthDeg(float deg) {
        float result = std::fmod(deg, 360.0f);
        if (result < 0.0f) result += 360.0f;
        return result;
    }

    std::string dirFromPath(const std::string& p) {
        size_t pos = p.find_last_of("/\\");
        return (pos == std::string::npos) ? std::string() : p.substr(0, pos);
    }

    void GeneratePlant(const PlantDesc& plantDesc, const std::string& baseFileName, const std::string& leafTexturePath, const std::string& stemTexturePath, bool exportSkeleton, bool skeletonIncludeTassel) {
        Maize::plant() = plantDesc;
        Maize::rebuildGeometry();
        std::string xmlPath = baseFileName + ".xml";
        std::string objPath = baseFileName + ".obj";
        Descriptor::savePlantXML(xmlPath.c_str());
        // Full mesh always includes the tassel; only the skeleton is optional.
        Maize::savePlantOBJ(objPath.c_str(), leafTexturePath.c_str(), stemTexturePath.empty() ? nullptr : stemTexturePath.c_str());
        if (exportSkeleton) {
            std::string skelObjPath = baseFileName + "_skeleton.obj";
            std::string skelXmlPath = baseFileName + "_skeleton.xml";
            Maize::saveSkeletonOBJ(skelObjPath.c_str(), skeletonIncludeTassel);
            Descriptor::saveSkeletonXML(skelXmlPath.c_str());
        }
    }

    void GeneratePlantWith(MaizeModel& model, const PlantDesc& plantDesc, const std::string& baseFileName, const std::string& leafTexturePath, const std::string& stemTexturePath, bool exportSkeleton, bool writeMTL, bool skeletonIncludeTassel) {
        // Thread-safe variant: mutates only the caller-supplied MaizeModel
        // and serializes the PlantDesc directly, so multiple workers can run
        // concurrently without touching the Maize singleton or racing on shared
        // geometry/plant state. MTL is shared across the batch, so by default
        // workers do not emit it (writeMTL = false).
        model.plant() = plantDesc;
        model.rebuildGeometry();
        std::string xmlPath = baseFileName + ".xml";
        std::string objPath = baseFileName + ".obj";
        Descriptor::savePlantXML(plantDesc, xmlPath.c_str());
        // Full mesh always includes the tassel; only the skeleton is optional.
        model.savePlantOBJ(objPath.c_str(), leafTexturePath.c_str(), stemTexturePath.empty() ? nullptr : stemTexturePath.c_str(), writeMTL);
        if (exportSkeleton) {
            std::string skelObjPath = baseFileName + "_skeleton.obj";
            std::string skelXmlPath = baseFileName + "_skeleton.xml";
            model.saveSkeletonOBJ(skelObjPath.c_str(), skeletonIncludeTassel);
            Descriptor::saveSkeletonXML(plantDesc, skelXmlPath.c_str());
        }
    }
}

void MaizeModel::rebuildGeometry() {
    // RebuildGeometry is the single conversion from PlantDesc to render/export meshes
    m_leafGeoms.clear();
    const Vect3d globalUp = { 0,1,0 };

    // Captured from the main tiller so a tassel can be attached at its tip.
    bool   haveMainTop = false;
    Vect3d mainTopPos = { 0,0,0 };
    Vect3d mainTopTangent = globalUp;

    for (size_t ti = 0; ti < m_plant.tillers.size(); ++ti) {
        const auto& td = m_plant.tillers[ti];
        const float tilt = radiansf(td.alphaDeg);
        const float ct = std::cos(tilt), st = std::sin(tilt);
        StemBuildData stemData = buildStemData(td);
        StemSampler stemSampler{ td, stemData, globalUp };

        if (ti == 0) {
            // Top of the main stem (world space, including tiller tilt).
            Vect3d top = tiltPoint(stemSampler.posAt(1.0f), ct, st);
            Vect3d belowTop = tiltPoint(stemSampler.posAt(0.97f), ct, st);
            Vect3d tan = top - belowTop;
            mainTopTangent = (tan.Length() > 1e-6f) ? tan.GetNormalized() : globalUp;
            mainTopPos = top;
            haveMainTop = true;
        }

        if (!stemData.leafStemPos.empty()) {
            LeafGeom stemGeom = buildStemGeometry(stemSampler, stemData, globalUp, m_densityV, m_densityU, m_stemDensityScale, ct, st);
            int nSkelSamples = std::max(30, (int)(stemData.stemLength * 50));
            stemGeom.stemCurve.reserve(nSkelSamples + 1);
            for (int s = 0; s <= nSkelSamples; ++s) {
                float t = (float)s / (float)nSkelSamples;
                stemGeom.stemCurve.push_back(tiltPoint(stemSampler.posAt(t), ct, st));
            }
            m_leafGeoms.push_back(std::move(stemGeom));
        }

        for (size_t li = 0; li < td.leaves.size(); ++li) {
            const auto& L = td.leaves[li];
            LeafDesc LInclined = L;
            if (li < stemData.leafInclinations.size()) {
                LInclined.stemInclinationDeg = stemData.leafInclinations[li];
            }
            float phi = radiansf(L.azimuthDeg);
            Vect3d rhat = { std::cos(phi), 0.f, std::sin(phi) };

            std::vector<Vect3d> localCtrlC, localCtrlL, localCtrlR;
            float tLeaf = (stemData.stemLength > 1e-6f && li < stemData.leafStemLen.size()) ? (stemData.leafStemLen[li] / stemData.stemLength) : 0.0f;
            tLeaf = clampf(tLeaf, 0.0f, 1.0f);
            float global_radius = stemSampler.radiusAt(tLeaf);

            Maize::generateLeafSplines(LInclined, global_radius, localCtrlC, localCtrlL, localCtrlR);

            if (L.leafTwist != 0.0f && L.splinePoints > 1) {
                // Twist is applied after the local splines are built so it can
                // rotate the left/right edges around the centerline
                const std::vector<float> sectionYaw = leafSectionYaws(L, localCtrlL.size());
                applyLeafTwist(localCtrlL, localCtrlC, L.leafTwist, &sectionYaw);
                applyLeafTwist(localCtrlR, localCtrlC, L.leafTwist, &sectionYaw);
            }

            // Build a local orthonormal frame at the stem exit point so the leaf
            // spline can be transformed from leaf-local coordinates into world
            Vect3d sheath_exit_pos = (li < stemData.leafStemPos.size()) ? stemData.leafStemPos[li] : Vect3d(0.0f, 0.0f, 0.0f);
            BladeFrame bladeFrame = buildBladeFrame(sheath_exit_pos, stemSampler.tangentAt(tLeaf), rhat);

            LeafGeom bladeGeom = buildBladeGeometry(L, bladeFrame, rhat, global_radius, m_densityV, m_densityU, ct, st, localCtrlC, localCtrlL, localCtrlR);
            // Place the branch point on the stem centerline at the
            // visual sheath-stem junction (sheathExitPos lowered by
            // sheathBelowOffset, matching buildBladeGeometry)
            if (!bladeGeom.ctrlCenter.empty()) {
                float sOS = clampf(L.sheathOuterScale, 1.0f, 1.6f);
                float fRC = std::max(0.001f, global_radius * sOS);
                float sheathBelowOffset = std::max(fRC * 1.5f, L.leafLength * 0.04f);
                Vect3d stemAxis = bladeFrame.yAxis.GetNormalized();
                Vect3d visualJunction = sheath_exit_pos - stemAxis * sheathBelowOffset;
                Vect3d branchPt = tiltPoint(visualJunction, ct, st);
                bladeGeom.stemCurve.push_back(branchPt);
                for (const auto& p : bladeGeom.ctrlCenter) {
                    bladeGeom.stemCurve.push_back(p);
                }
            }
            m_leafGeoms.push_back(std::move(bladeGeom));
        }
    }

    // Tassel atop the main stem.
    m_tasselMesh.clear();
    if (m_plant.tassel.enabled && haveMainTop) {
        rebuildTasselGeometry(mainTopPos, mainTopTangent);
    } else {
        m_tasselCacheValid = false;
    }
}

// Tassel parameters that affect growth (everything except scale, which is only
// a placement transform). When any of these change the cached mesh is regrown.
static bool tasselGrowthEqual(const TasselDesc& a, const TasselDesc& b) {
    return a.seed == b.seed &&
           a.branchNodeCount == b.branchNodeCount &&
           a.spikeNodeCount == b.spikeNodeCount &&
           a.secondaryNodeCount == b.secondaryNodeCount &&
           a.phyllotaxisAngle == b.phyllotaxisAngle &&
           a.lateralThicknessRatio == b.lateralThicknessRatio &&
           a.secondaryInsertionAngle == b.secondaryInsertionAngle &&
           a.plastochronGdd == b.plastochronGdd &&
           a.maturityGdd == b.maturityGdd &&
           a.branchAngleRelaxation == b.branchAngleRelaxation &&
           a.matureDroopStrength == b.matureDroopStrength &&
           a.matureDroopStartT == b.matureDroopStartT &&
           a.gravitropism == b.gravitropism;
}

void MaizeModel::rebuildTasselGeometry(const Vect3d& topPos, const Vect3d& topTangent) {
    using namespace l_system_plugin;
    const TasselDesc& T = m_plant.tassel;

    // Growth is expensive (thousands of steps); only regrow when a growth-
    // affecting parameter changes. Scale-only edits reuse the cached mesh.
    if (!m_tasselCacheValid || !tasselGrowthEqual(m_tasselCacheDesc, T)) {
        TasselDescriptor desc;

        // Start from the tuned genotype, then override the exposed scalar knobs.
        desc.branch_node_count.mean = (float)T.branchNodeCount;
        desc.branch_node_count.deviation = 0.0f;
        desc.spike_node_count.mean = (float)T.spikeNodeCount;
        desc.spike_node_count.deviation = 0.0f;
        desc.secondary_node_count.mean = (float)T.secondaryNodeCount;
        desc.secondary_node_count.deviation = 0.0f;
        desc.phyllotaxis_angle.mean = T.phyllotaxisAngle;
        desc.lateral_thickness_ratio.mean = T.lateralThicknessRatio;
        desc.secondary_insertion_angle.mean = T.secondaryInsertionAngle;
        desc.plastochron_gdd.mean = T.plastochronGdd;
        desc.maturity_gdd.mean = T.maturityGdd;
        desc.branch_angle_relaxation.mean = T.branchAngleRelaxation;
        desc.mature_droop_strength.mean = T.matureDroopStrength;
        desc.mature_droop_start_t.mean = T.matureDroopStartT;

        // Gravitropism scales the baked tropisms (applied post-sampling inside
        // Initialize, so structure is seed-stable; 0 stops the random collapse).
        TasselGrowthModel model;
        model.Initialize(desc, T.seed, glm::vec3(0), kDefaultRootRotation, T.gravitropism);

        // "Mature tassel" = grow until topology is fully derived (all meristems
        // consumed), then a little extra GDD so the last-born spikelets reach
        // full size. maturity_gdd in the genotype is a per-organ value, not a
        // whole-plant cap, so we drive topology completion directly.
        const int kStepCap = 8000;
        int steps = 0;
        while (steps < kStepCap) {
            model.GrowStep();
            ++steps;
            if (model.IsTopologyComplete()) break;
        }
        model.GrowToGDD(model.accumulated_gdd + 400.0f, (unsigned int)kStepCap);

        m_tasselLocal = BuildTasselMesh(model);
        m_tasselCacheValid = true;
        m_tasselCacheDesc = T;
    }

    // Transform the cached local mesh onto the stem tip: scale, rotate local +Y
    // onto the stem tangent, then translate to the tip.
    const Vect3d up = { 0,1,0 };
    Vect3d tan = (topTangent.Length() > 1e-6f) ? topTangent.GetNormalized() : up;
    const float d = clampf(up.Dot(tan), -1.0f, 1.0f);
    Vect3d axis = up.Cross(tan);
    const float axisLen = axis.Length();
    const bool doRot = (axisLen > 1e-5f) && (d < 0.9999f);
    if (doRot) axis = axis / axisLen;
    const double angle = std::acos((double)d);
    const float s = T.scale;

    const size_t n = m_tasselLocal.pos.size();
    m_tasselMesh.clear();
    m_tasselMesh.pos.resize(n);
    m_tasselMesh.nrm.resize(n);
    m_tasselMesh.u = m_tasselLocal.u;
    m_tasselMesh.v = m_tasselLocal.v;
    m_tasselMesh.indices = m_tasselLocal.indices;
    for (size_t i = 0; i < n; ++i) {
        Vect3d p = m_tasselLocal.pos[i] * s;
        Vect3d nv = m_tasselLocal.nrm[i];
        if (doRot) {
            p = p.GetRotatedAxis(angle, axis);
            nv = nv.GetRotatedAxis(angle, axis);
        }
        m_tasselMesh.pos[i] = topPos + p;
        m_tasselMesh.nrm[i] = nv.GetNormalized();
    }

    // Same transform for the skeleton centerline segments.
    m_tasselMesh.skeleton.resize(m_tasselLocal.skeleton.size());
    for (size_t i = 0; i < m_tasselLocal.skeleton.size(); ++i) {
        Vect3d p = m_tasselLocal.skeleton[i] * s;
        if (doRot) p = p.GetRotatedAxis(angle, axis);
        m_tasselMesh.skeleton[i] = topPos + p;
    }
}

int MaizeModel::getTriangleCount() const {
    int count = 0;
    for (const auto& geom : m_leafGeoms) {
        if (geom.rows > 1 && geom.cols > 1) count += (geom.rows - 1) * (geom.cols - 1) * 2;
    }
    count += (int)m_tasselMesh.triangleCount();
    return count;
}
