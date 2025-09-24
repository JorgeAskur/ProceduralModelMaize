#include "Maize.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#define M_PI 3.14159265358979323846

#ifdef _WIN32
    #define sinf std::sinf;
#else
    // Linux: map fopen_s -> fopen and errno_t -> int
    typedef int errno_t;
    inline errno_t fopen_s(FILE **file, const char *filename, const char *mode) {
        *file = fopen(filename, mode);
        return (*file == nullptr) ? 1 : 0;
    }
#endif

// Math helpers
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }
static inline float lerpf(float a, float b, float t) { return a * (1.f - t) + b * t; }
static inline float smooth01(float x) { x = clampf(x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
static inline float smoothstepf(float a, float b, float x) {
    float t = clampf((x - a) / std::max(1e-6f, b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
static inline float logistic01(float x, float k) {
    x = clampf(x, 0.f, 1.f);
    float y0 = 1.f / (1.f + std::exp(+k * 0.5f));
    float y1 = 1.f / (1.f + std::exp(-k * 0.5f));
    float y = 1.f / (1.f + std::exp(-k * (x - 0.5f)));
    return (y - y0) / std::max(1e-6f, (y1 - y0));
}

// Module state
static PlantDesc   gPlant;
static std::map<LeafKey, LeafEdit> gEdits;
static std::map<LeafKey, LeafParams> gLeafParams;

static std::vector<LeafGeom>      gLeafGeoms;
static std::vector<StemLayerGeom> gStemLayers;

// Some global display/UV parameters that geometry depends on
static float gTexURepeat = 1.0f, gTexVRepeat = 1.0f;
static int   gStemSides = 48;
static bool  gOverrideBaseRadius = false;
static char gLeafTexPath[256] = "maize_leaf.png";
static bool gTexCopyOK = false;


// Maize API
namespace Maize {
    PlantDesc& plant() { return gPlant; }
    std::map<LeafKey, LeafEdit>& edits() { return gEdits; }
    std::map<LeafKey, LeafParams>& leafParams() { return gLeafParams; }

    const std::vector<LeafGeom>& leafGeoms() { return gLeafGeoms; }
    const std::vector<StemLayerGeom>& stemLayers() { return gStemLayers; }

	// Parameter helpers
    LeafParams defaultLeafParams() { return LeafParams(); }
    LeafParams& paramsFor(const LeafKey& key) {
        auto it = gLeafParams.find(key);
        if (it == gLeafParams.end()) it = gLeafParams.emplace(key, defaultLeafParams()).first;
        return it->second;
    }
	// Build all geometry from current state
    void rebuildGeometry() {
        gLeafGeoms.clear();
        gStemLayers.clear();

        const Vect3d stemAxis = { 0,1,0 };
        const float twoPi = 6.28318530718f;

        // Helpers
        auto unwrapNear = [](float a, float ref)->float {
            float d = a - ref;
            if (d > M_PI) a -= 2.0f * M_PI;
            if (d < -M_PI) a += 2.0f * M_PI;
            return a;
            };
        auto u_from_angle01 = [](float ang)->float {
            float u = (ang + float(M_PI)) / (2.0f * float(M_PI));
            u -= std::floor(u);
            return u;
            };
        auto fracf = [](float x)->float { return x - std::floor(x); };
        auto edgeCluster = [](float u)->float { return 0.5f * (1.0f - std::cos(float(M_PI) * u)); };
        auto yCluster = [](float u)->float { return 0.5f * (1.0f - std::cos(float(M_PI) * u)); };

        for (size_t ti = 0; ti < gPlant.tillers.size(); ++ti) {
            const auto& td = gPlant.tillers[ti];

            // Tilt for current tiller
            const float tilt = radiansf(td.alphaDeg);
            const float ct = std::cos(tilt), st = std::sin(tilt);
            auto tiltP = [&](const Vect3d& p)->Vect3d {
                return { p.x() * ct - p.y() * st, p.x() * st + p.y() * ct, p.z() };
                };

            // Cumulative heights
            std::vector<float> yAt; yAt.reserve(td.leaves.size());
            float accY = 0.f;
            for (const auto& L : td.leaves) { accY += std::max(0.f, L.distance); yAt.push_back(accY); }

            float baseR = td.radius;

			// Stem layers for each leaf
            for (size_t k = 0; k < td.leaves.size(); ++k) {
                const LeafKey kkey{ (int)ti, (int)k };
                const LeafParams& KP = paramsFor(kkey);

				// Compute stem layer geometry
                float r = std::max(0.001f, baseR - (float)k * td.stemShrink);
                float yTop = std::max(0.f, yAt[k] - KP.ringDrop);

				// Seam angle (radians) at fractional height fy (0..1)
                const float phiOut = radiansf(td.leaves[k].azimuthDeg);
                const float seamCtr = phiOut + float(M_PI);

				// Seam angle interpolation function
                auto seamAt = [&](float fy)->float { return clampf(seamAngleRadAtFrac(fy, KP), 0.f, float(M_PI)); };
                float seam0 = seamAt(0.f), seamS = seamAt(clampf(KP.openStartFrac, 0.f, 1.f)), seam1 = seamAt(1.f);
                float seamMin = std::min(seam0, std::min(seamS, seam1));
                float arcMax = std::max(1e-4f, twoPi - seamMin);

				// Determine number of sides based on radius and seam opening
                float radiusScale = r / std::max(1e-6f, baseR);
                int   angSteps = std::max(6, (int)std::round(gStemSides * radiusScale * std::sqrt(arcMax / twoPi)));

				// Fractional height at which the seam opening begins
                float fSplit = clampf(KP.openStartFrac, 0.f, 0.99f);

				// Vertical segmentation, more segments for larger radius, and more segments for larger seam opening
                int ySegsTotal = std::max(18, (int)std::round(90 * (r / std::max(1e-6f, baseR))));
                int segsSeam = std::max(8, (int)std::round(ySegsTotal * lerpf(0.05f, 0.15f, fSplit)));
                int segsRest = std::max(4, ySegsTotal - segsSeam);

				// Vertical clustering function
                auto yCluster = [](float u)->float { return 0.5f * (1.0f - std::cos(float(M_PI) * u)); };

				// Vertical interpolation in [fA,fB] with clustering
                auto fyInRange = [&](int seg, int segs, float fA, float fB) {
                    float u = (float)seg / (float)segs;
                    return lerpf(fA, fB, yCluster(u));
                    };

				// Generate a ring at fractional height fy (0..1) and absolute height y
                auto makeRing = [&](float fy, float y, std::vector<Vect3d>& ringOut) {
                    ringOut.resize(angSteps + 1);
                    float seam = seamAt(fy);                       
                    float arc = std::max(1e-4f, twoPi - seam);
                    float aStart = seamCtr + 0.5f * seam;
                    for (int i = 0; i <= angSteps; ++i) {
                        float s = 0.5f * (1.f - std::cos(float(M_PI) * (float(i) / angSteps)));
                        float a = aStart + arc * s;
                        Vect3d p = { std::cos(a) * r, y, std::sin(a) * r };
                        ringOut[i] = tiltP(p);
                    }
                    };

				// Build the stem layer
                StemLayerGeom layer;
                layer.radius = r; layer.y0 = 0.f; layer.y1 = yTop;
                layer.sides = angSteps;
                layer.uPhase = (k % 2 == 1) ? float(M_PI * 1.75) : 0.f;
                layer.band.reserve((size_t)(angSteps + 1) * 2 * (segsSeam + segsRest) + 8);

				// Precompute the shared ring at the seam split height
                std::vector<Vect3d> ringSplit;
                makeRing(fSplit, lerpf(0.f, yTop, fSplit), ringSplit);

				// Emit a vertical region of the stem, from fA to fB, with nSegs subdivisions
                auto emitRegion = [&](int segs, float fA, float fB, const std::vector<Vect3d>* firstLowerOverride) {
                    if (segs <= 0) return;

                    std::vector<Vect3d> r0, r1;
                    for (int seg = 0; seg < segs; ++seg) {
                        float f0 = fyInRange(seg, segs, fA, fB);
                        float f1 = fyInRange(seg + 1, segs, fA, fB);
                        float y0 = lerpf(0.f, yTop, f0);
                        float y1 = lerpf(0.f, yTop, f1);

                        if (firstLowerOverride && seg == 0) r0 = *firstLowerOverride;
                        else                                makeRing(f0, y0, r0);

						// Upper ring
                        makeRing(f1, y1, r1);

                        for (int i = 0; i <= angSteps; ++i) {
                            layer.band.push_back(r0[i]);
                            layer.band.push_back(r1[i]);
                        }

						// Degenerate triangles to connect to next segment, except for last segment of region
                        if (seg != segs - 1) {
                            const Vect3d& uN = r1[angSteps];
                            const Vect3d& l0n = r0[0];
                            layer.band.push_back(uN);
                            layer.band.push_back(l0n);
                            layer.band.push_back(l0n);
                            layer.band.push_back(l0n);
                        }
						// Swap rings for next iteration
                        r0.swap(r1);
                    }
                    };

				// Emit the two regions of the stem layer, below and above the seam split
                emitRegion(segsRest, 0.f, fSplit, nullptr);
                
				// Add triangles to duplicate the last ring of the lower region
                if (!layer.band.empty()) {
                    const Vect3d last = layer.band.back();
                    layer.band.push_back(last);
                    layer.band.push_back(last);
                }

                // Emit seam region, reusing the exact shared ring as its first lower ring
                emitRegion(segsSeam, fSplit, 1.f, &ringSplit);

                gStemLayers.push_back(std::move(layer));
            }

            // Leaves
            for (size_t li = 0; li < td.leaves.size(); ++li) {
                const auto& L = td.leaves[li];
                float phi = radiansf(L.azimuthDeg), pitch = radiansf(L.alphaDeg);

                Vect3d rhat = { std::cos(phi), 0.f, std::sin(phi) };
                Vect3d base = rhat * baseR + Vect3d{ 0.f, yAt[li], 0.f };
                Vect3d dir = (rhat * std::cos(pitch) + stemAxis * std::sin(pitch)).GetNormalized();

                LeafParams& LP = paramsFor({ (int)ti,(int)li });

                float rLeaf = std::max(0.001f, baseR - (float)(li)*td.stemShrink);
                base = rhat * (rLeaf + LP.attachEps) + Vect3d{ 0.f, yAt[li], 0.f };
                float yRingTop = base.y() - LP.ringDrop;

                // Control points
                auto it = gEdits.find({ (int)ti,(int)li });
                std::vector<Vect3d> ctrl =
                    (it != gEdits.end() && it->second.hasCustom && it->second.customCtrl.size() >= 4)
                    ? it->second.customCtrl
                    : makeLeafCtrl(base, stemAxis, dir, LP.length);
                if (it != gEdits.end() && it->second.hasCustom && it->second.pinC0ToStem) ctrl[0] = base;

                // Curvature-adaptive rows
                int rowsTarget = std::max(L.spines, LP.samplesMin);
                auto midDense = resampleMidrib(ctrl, rowsTarget * 3);
                float angTol = radiansf(2.0f);
                float minStep = std::max(0.0015f, 0.01f * LP.length);
                auto rowIdx = pickRowsByCurvature(midDense, angTol, minStep);
                std::vector<Vect3d> mid; mid.reserve(rowIdx.size());
                for (int id : rowIdx) mid.push_back(midDense[id]);

                int samples = (int)mid.size();
                std::vector<Vect3d> T, N, B; buildPTFrames(mid, T, N, B);

                // Roll and twist
                std::vector<Vect3d> N2 = N, B2 = B;
                float baseRoll = 0.0f;
                baseRoll += Vect3d::autoRollTheta(B[0], T[0], rhat);
                for (int i = 0; i < samples; ++i) {
                    float s = (samples == 1) ? 0.f : float(i) / (samples - 1);
                    float twist = radiansf(LP.twistMaxD) * std::pow(s, LP.twistPow);
                    float th = baseRoll + twist;
                    N2[i] = Vect3d::rotateAroundAxis(N[i], T[i], th);
                    B2[i] = Vect3d::rotateAroundAxis(B[i], T[i], th);
                }

                LeafGeom G; G.ctrl = ctrl; G.rows = samples; G.cols = std::max(7, (LP.lipCols | 1));
                G.pos.resize((size_t)G.rows * G.cols);
                G.uv.resize((size_t)G.rows * G.cols);

                for (int i = 0; i < G.rows; ++i) {
                    float s01 = (G.rows == 1) ? 0.f : float(i) / (G.rows - 1);

                    // Midrib
                    Vect3d C = mid[i];

                    // Stem hugging
                    if (LP.attachFrac > 0.f) {
                        float x = clampf(s01 / std::max(1e-4f, LP.attachFrac), 0.f, 1.f);
                        float t = 1.f - (x * x * (3.f - 2.f * x));
                        Vect3d Ccyl = rhat * rLeaf + Vect3d{ 0.f, C.y(), 0.f };
                        C = C * (1.f - t) + Ccyl * t;
                    }

					// Droop
                    if (i == 0) C.SetY(base.y());
                    C.SetY(C.y() - LP.droopMax * s01 * s01);

                    Vect3d Nrow = N2[i];
                    bool flipAcross = false;

					// Flip normal if it points inward toward stem center and we are near the leaf base
                    if (s01 <= LP.lipLenFrac + 1e-6f && Nrow.Dot(rhat) < 0.0f) {
                        Nrow = Nrow * -1.f;
                        flipAcross = true;
                    }

					// Leaf width and lip arc
                    const float sEnd = clampf(LP.lipLenFrac, 0.f, 1.f);
                    float b = unwrapAmountAtS(s01, LP);
                    float dphi = radiansf(lerpf(LP.lipArc0Deg, LP.lipArc1Deg, b));
                    float wLeaf = widthBaseMax(s01, LP.wmax, LP.widthKnee, LP.widthAtKnee, LP.earlyPow, LP.tipPow);
                    float baseRamp = smooth01(s01 / std::max(1e-4f, LP.baseRampFrac));
                    float halfWLeaf = 0.5f * wLeaf * baseRamp;
                    float halfChord = (rLeaf + LP.attachEps) * sinf(0.5f * dphi);
                    float halfW = lerpf(halfChord, halfWLeaf, b);

                    // Weld arc endpoints
                    const float dphi0 = radiansf(LP.lipArc0Deg);
                    const float phL_raw = (phi - 0.5f * dphi0);
                    const float phR_raw = phL_raw + dphi0;

                    // Pick row left/right after flip
                    float phEdge0 = flipAcross ? phR_raw : phL_raw;
                    float phEdge1 = flipAcross ? phL_raw : phR_raw;

                    // Unwrap once in angle space
                    float ph0 = phEdge0;
                    float ph1 = unwrapNear(phEdge1, ph0);

                    // Cylindrical UV model anchored at row center
                    float midAng = ph0 + 0.5f * (ph1 - ph0);
                    float uCenter = u_from_angle01(midAng);
                    float slopeAbs = std::fabs((ph1 - ph0) / (2.0f * float(M_PI)));
                    float vCylRow = (i == 0) ? yRingTop : C.y();

                    for (int j = 0; j < G.cols; ++j) {
                        float u = (G.cols == 1) ? 0.f : float(j) / (G.cols - 1);

						// Across-leaf coordinate in [-1,1]
                        float uAcross = flipAcross ? (1.0f - u) : u;
                        float uLR = (uAcross - 0.5f) * 2.f;
                        float ringY = (i == 0) ? yRingTop : C.y();

						// Weld and blade angles
                        float phWeld = ph0 + (ph1 - ph0) * uAcross;
                        float phBlade = phi + uLR * dphi;

						// Blend between cylindrical and flat leaf
                        Vect3d rw = { std::cos(phWeld), 0.f, std::sin(phWeld) };
                        Vect3d rb = { std::cos(phBlade), 0.f, std::sin(phBlade) };
                        float  tAng = clampf(b, 0.f, 1.f);
                        Vect3d rmix = (rw * (1.0f - tAng) + rb * tAng);
                        if (rmix.Length() < 1e-8f) rmix = rw;
                        rmix = rmix.GetNormalized();

                        Vect3d Pcyl = { rmix.x() * rLeaf, ringY, rmix.z() * rLeaf };
                        Vect3d Pleaf = C + Nrow * (uLR * halfW);
                        Vect3d Pv = Pcyl * (1.f - b) + Pleaf * b;

                        if (i == 0) Pv = { rw.x() * rLeaf, yRingTop, rw.z() * rLeaf };

                        // Waviness
                        if (i > 0) {
                            Vect3d Bblend = (T[i].Cross(Nrow)).GetNormalized();
                            float fadeIn = smoothstepf(0.0f, std::max(1e-3f, LP.waveFadeIn), s01);
                            float fadeOut = 1.0f - smoothstepf(1.0f - LP.waveFadeOut, 1.0f, s01);
                            float env = fadeIn * fadeOut;
                            float basePhase = twoPi * (LP.waveCycles * s01);
                            float acrossPhase = twoPi * 0.25f * LP.waveAcross * uLR;
                            float amp = LP.waveAmp * env * sinf(basePhase + acrossPhase);
                            Pv = Pv + Bblend * amp;
                        }

                        // UVs
                        float dTex = (u - 0.5f);
                        float uCyl = uCenter + slopeAbs * dTex;
                        float uLin = uCenter + dTex;
                        float tUV = clampf(b, 0.f, 1.f);
                        float uMix = (1.0f - tUV) * uCyl + tUV * uLin;
                        float vMix = (1.0f - tUV) * vCylRow + tUV * s01;

                        float U = uMix * gTexURepeat;
                        float V = vMix * gTexVRepeat;
                        if ((li & 1) == 1) U += 0.5f;

                        int id = i * G.cols + j;
                        G.pos[id] = tiltP(Pv);    
                        G.uv[id] = { U, V };
                    }
                }

                // Normals
                G.nrm.resize((size_t)G.rows * G.cols);
                for (int i = 0; i < G.rows; ++i) {
                    int im = (i == 0) ? 0 : i - 1;
                    int ip = (i == G.rows - 1) ? G.rows - 1 : i + 1;
                    for (int j = 0; j < G.cols; ++j) {
                        int jm = (j == 0) ? 0 : j - 1;
                        int jp = (j == G.cols - 1) ? G.cols - 1 : j + 1;
                        const Vect3d& P_im = G.pos[im * G.cols + j];
                        const Vect3d& P_ip = G.pos[ip * G.cols + j];
                        const Vect3d& P_jm = G.pos[i * G.cols + jm];
                        const Vect3d& P_jp = G.pos[i * G.cols + jp];
                        Vect3d dU = P_jp - P_jm, dV = P_ip - P_im, n = dV.Cross(dU);
                        float L = n.Length();
                        G.nrm[i * G.cols + j] = (L > 1e-12f) ? n * (1.0f / L) : Vect3d{ 0,1,0 };
                    }
                }

                // Also tilt control points so the overlay matches the geometry
                for (auto& p : G.ctrl) p = tiltP(p);

                gLeafGeoms.push_back(std::move(G));
            }
        }
    }

	// B-spline utilities (degree 3)
    std::vector<float> openUniformKnots(int nCtrl, int p = 3) {
        int n = nCtrl - 1, m = n + p + 1; std::vector<float> U(m + 1, 0.f);
        for (int j = 0; j <= p; ++j) U[j] = 0.f;
        for (int j = m - p; j <= m; ++j) U[j] = 1.f;
        for (int j = p + 1; j <= m - p - 1; ++j) U[j] = float(j - p) / float(m - 2 * p);
        return U;
    }
	// Find knot span index
    int findSpan(int n, int p, float u, const std::vector<float>& U) {
        if (u >= U[n + 1]) return n;
        int low = p, high = n + 1, mid = (low + high) / 2;
        while (u < U[mid] || u >= U[mid + 1]) { if (u < U[mid]) high = mid; else low = mid; mid = (low + high) / 2; }
        return mid;
    }
	// de Boor's algorithm
    Vect3d deBoor(float u, const std::vector<Vect3d>& P, const std::vector<float>& U, int p = 3) {
        int n = (int)P.size() - 1; if (n < p) return P.empty() ? Vect3d{ 0,0,0 } : P.back();
        int i = findSpan(n, p, u, U);
        Vect3d d[4]; for (int j = 0; j <= p; ++j) d[j] = P[i - p + j];
        for (int r = 1; r <= p; ++r)
            for (int j = p; j >= r; --j) {
                float denom = U[i + 1 + j - r] - U[i - p + j];
                float a = (denom > 1e-12f) ? (u - U[i - p + j]) / denom : 0.f;
                d[j] = d[j - 1] * (1.f - a) + d[j] * a;
            }
        return d[p];
    }

    // Dense arc-length resample of the midrib curve
    std::vector<Vect3d> resampleMidrib(const std::vector<Vect3d>& ctrl, int N) {
        const int p = 3; auto U = openUniformKnots((int)ctrl.size(), p);
        const int dense = std::max(512, 4 * N);
        std::vector<Vect3d> pts; pts.reserve(dense);
        float total = 0.f; Vect3d prev{};
        for (int k = 0; k < dense; ++k) {
            float u = (float)k / (dense - 1);
            Vect3d C = deBoor(u, ctrl, U, p);
            if (k > 0) total += (C - prev).Length();
            pts.push_back(C); prev = C;
        }
        auto pointAtS = [&](float s)->Vect3d {
            float target = s * total, acc = 0.f;
            for (int k = 1; k < dense; ++k) {
                Vect3d d = pts[k] - pts[k - 1]; float seg = d.Length();
                if (acc + seg >= target) { float a = (seg > 1e-8f) ? (target - acc) / seg : 0.f; return pts[k - 1] * (1 - a) + pts[k] * a; }
                acc += seg;
            }
            return pts.back();
            };
        std::vector<Vect3d> mid(N);
        for (int i = 0; i < N; ++i) { float s = (N == 1) ? 0.f : float(i) / (N - 1); mid[i] = pointAtS(s); }
        return mid;
    }

	// Seam angle at fractional height fy
    std::vector<int> pickRowsByCurvature(const std::vector<Vect3d>& mid,
        float angTolRad, float minStep)
    {
        std::vector<int> keep;
        if (mid.size() <= 2) { for (int i = 0; i < (int)mid.size(); ++i) keep.push_back(i); return keep; }

        keep.push_back(0);
        Vect3d prevP = mid[0];
        Vect3d prevT = (mid[1] - mid[0]).GetNormalized();
        float accAng = 0.f, accDist = 0.f;

        for (int i = 1; i < (int)mid.size() - 1; ++i) {
            Vect3d Ti = (mid[i + 1] - mid[i]).GetNormalized();
            float ang = std::acos(clampf(prevT.Dot(Ti), -1.f, 1.f)); // 0..π
            accAng += ang;
            accDist += (mid[i] - prevP).Length();

            if (accAng >= angTolRad || accDist >= minStep) {
                keep.push_back(i);
                prevT = Ti;
                prevP = mid[i];
                accAng = 0.f;
                accDist = 0.f;
            }
        }
        keep.push_back((int)mid.size() - 1);
        return keep;
    }

    // Parallel-transport frames along a polyline mid
    void buildPTFrames(const std::vector<Vect3d>& mid,
        std::vector<Vect3d>& T, std::vector<Vect3d>& N, std::vector<Vect3d>& B) {
        int n = (int)mid.size(); T.resize(n); N.resize(n); B.resize(n);
        for (int i = 0; i < n; ++i) {
            Vect3d Tl = (i > 0) ? mid[i] - mid[i - 1] : mid[i + 1] - mid[i];
            Vect3d Tr = (i < n - 1) ? mid[i + 1] - mid[i] : mid[i] - mid[i - 1];
            T[i] = (Tl + Tr).GetNormalized();
        }
        Vect3d up = { 0,1,0 }; if (T[0].Cross(up).Length() < 1e-4f) up = { 1,0,0 };
        B[0] = (T[0].Cross(up)).GetNormalized(); N[0] = (B[0].Cross(T[0])).GetNormalized();
        for (int i = 1; i < n; ++i) {
            Vect3d a = T[i - 1].Cross(T[i]); float L = a.Length();
            if (L > 1e-8f) {
                a = a * (1.f / L); float c = clampf(T[i - 1].Dot(T[i]), -1.f, 1.f);
                float ang = std::acos(c);
                N[i] = Vect3d::rotateAroundAxis(N[i - 1], a, ang);
                B[i] = Vect3d::rotateAroundAxis(B[i - 1], a, ang);
            }
            else { N[i] = N[i - 1]; B[i] = B[i - 1]; }
        }
        for (int i = 0; i < n; ++i) { B[i] = T[i].Cross(N[i]).GetNormalized(); N[i] = B[i].Cross(T[i]).GetNormalized(); }
    }

	// Leaf width at fractional length s
    static inline float widthBaseMax(float s, float wMax,
        float knee, float wAtKnee, float earlyPow,
        float tipPow) {
        s = clampf(s, 0.f, 1.f);
        const float k = clampf(knee, 0.40f, 0.90f);
        const float wk = clampf(wAtKnee, 0.05f, 0.95f);
        const float p0 = std::max(0.1f, earlyPow);
        const float p1 = std::max(0.1f, tipPow);

        float wr;
        if (s <= k) {
            float t = (k > 1e-6f) ? (s / k) : 1.f;
            wr = lerpf(1.f, wk, powf(t, p0));
        }
        else {
            float t = (1.f - k > 1e-6f) ? ((s - k) / (1.f - k)) : 1.f;
            wr = lerpf(wk, 0.0, powf(t, p1));
        }
        return wMax * wr;
    }

    // Unwrap amount b(s) in [0,1] for a specific leaf
    static inline float unwrapAmountAtS(float s01, const LeafParams& P) {
        const float sEnd = clampf(P.lipLenFrac, 0.f, 1.f);
        const float sHold = 0.0f * sEnd;
        if (s01 <= sHold) return 0.f;
        if (s01 >= sEnd)  return 1.f;
        float t = clampf((s01 - sHold) / std::max(1e-4f, sEnd - sHold), 0.f, 1.f);
        const float x0 = clampf(P.unwrapMidFrac, 0.05f, 0.95f);
        const float k = std::max(0.1f, P.unwrapSharpness);
        auto L = [&](float x) { return 1.f / (1.f + std::exp(-k * (x - x0))); };
        float y = L(t), y0 = L(0.f), y1 = L(1.f);
        float b = (y - y0) / std::max(1e-6f, (y1 - y0));
        return b * b * (3.f - 2.f * b);
    }

	// Seam angle at fractional height frac
    float seamAngleRadAtFrac(float frac, const LeafParams& P) {
        float start = clampf(P.openStartFrac, 0.f, 0.99f);
        if (frac <= start) return 0.f; // closed tube below start
        float u = (frac - start) / std::max(1e-6f, 1.f - start);
        float aDeg = clampf(P.seamMaxDeg, 0.f, 179.9f) * logistic01(u, std::max(0.1f, P.openSharpness));
        return radiansf(aDeg);
    }

	// Default leaf control points if none are specified by user
    static std::vector<Vect3d> makeLeafCtrl(Vect3d base, Vect3d stemUp, Vect3d dir, float L) {
        Vect3d p0 = base;
        Vect3d p1 = base + dir * (0.30f * L) + stemUp * (0.10f * L);
        Vect3d p2 = base + dir * (0.65f * L) + stemUp * (0.05f * L);
        Vect3d p3 = base + dir * (0.95f * L) + stemUp * (-0.05f * L);
        Vect3d p4 = base + dir * (1.10f * L) + stemUp * (-0.12f * L);
        return { p0,p1,p2,p3,p4 };
    }

    // OBJ Export
	bool savePlantOBJ(const char* path, const char* textureFullPath) {
        // Paths
        std::string objPath(path);
        std::string objDir = dirFromPath(objPath);
        std::string mtlFileName = objPath + ".mtl";
		std::string texFileName = textureFullPath;

        // Write OBJ
        FILE* f;
        errno_t err_f = fopen_s(&f, path, "w");
        if (!f) return false;

        std::fprintf(f, "# Exported plant\n# units: meters\n");
        std::fprintf(f, "mtllib %s\n\n", mtlFileName.c_str());

        int vCount = 0, vtCount = 0, vnCount = 0;

        const float twoPi = 6.28318530718f;

        // Stem layers
        int stemIdx = 0;
        for (const auto& S : gStemLayers) {
            if (S.band.empty()) continue;

            std::fprintf(f, "o stem_%02d\nusemtl stem_mat\n", stemIdx++);
            const int vBase = vCount + 1;
            const int vtBase = vtCount + 1;
            const int vnBase = vnCount + 1;

            // Seam-stable angular unwrapping
            float prevTheta = 0.0f;
            bool  havePrev = false;

            // Vertices / UVs / Normals
            for (size_t k = 0; k < S.band.size(); ++k) {
                const Vect3d& v = S.band[k];

				// Vertices
                std::fprintf(f, "v %.6f %.6f %.6f\n", v.x(), v.y(), v.z());

				// Vertices UVs
                float ang = std::atan2((float)v.z(), (float)v.x());
                float theta = ang;
                if (havePrev) {
                    if (theta - prevTheta > float(M_PI))       theta -= twoPi;
                    else if (prevTheta - theta > float(M_PI))  theta += twoPi;
                }
                else {
                    havePrev = true;
                }
                prevTheta = theta;

                float uCycles = (theta + float(M_PI)) / twoPi;
                uCycles += S.uPhase;
                
                uCycles = uCycles - std::floor(uCycles);
                
                float u = uCycles * gTexURepeat;
                float vv = v.y() * gTexVRepeat;
                std::fprintf(f, "vt %.6f %.6f\n", u, vv);

				// Vertex normal
                Vect3d n = Vect3d(v.x(), 0.0, v.z());
                float L = n.Length();
                n = (L > 1e-12f) ? n * (1.0 / L) : Vect3d{ 1,0,0 };
                std::fprintf(f, "vn %.6f %.6f %.6f\n", n.x(), n.y(), n.z());
            }
            vCount += (int)S.band.size();
            vtCount += (int)S.band.size();
            vnCount += (int)S.band.size();

			// Faces as a triangle strip
            auto samePos = [](const Vect3d& a, const Vect3d& b) {
                Vect3d d = a - b;
                return (d.x() * d.x() + d.y() * d.y() + d.z() * d.z()) < 1e-16;
                };

            bool even = true;
            for (size_t i = 0; i + 2 < S.band.size(); ++i) {
                const Vect3d& a = S.band[i];
                const Vect3d& b = S.band[i + 1];
                const Vect3d& c = S.band[i + 2];

				// Skip degenerate triangles
                if (samePos(a, b) || samePos(b, c) || samePos(a, c)) {
                    even = true;
                    continue;
                }

                int ia = vBase + (int)i;
                int ib = vBase + (int)i + 1;
                int ic = vBase + (int)i + 2;

                int ta = vtBase + (int)i;
                int tb = vtBase + (int)i + 1;
                int tc = vtBase + (int)i + 2;

                int na = vnBase + (int)i;
                int nb = vnBase + (int)i + 1;
                int nc = vnBase + (int)i + 2;

                if (even) {
                    std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        ia, ta, na, ib, tb, nb, ic, tc, nc);
                }
                else {
					// Reverse order of the uneven tris
                    std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        ia, ta, na, ic, tc, nc, ib, tb, nb);
                }
                even = !even;
            }
            std::fprintf(f, "\n");
        }

        // Leaves
        int leafIdx = 0;
        for (const auto& G : gLeafGeoms) {
            if (G.rows <= 1 || G.cols <= 1) continue;

            std::fprintf(f, "o leaf_%02d\nusemtl leaf_mat\n", leafIdx++);
            const int vBase = vCount + 1;
            const int vtBase = vtCount + 1;
            const int vnBase = vnCount + 1;

            const int total = G.rows * G.cols;
            for (int i = 0; i < total; ++i) {
                const Vect3d& p = G.pos[i];
                std::fprintf(f, "v %.6f %.6f %.6f\n", p.x(), p.y(), p.z());
                const Vec2& t = G.uv[i];
                std::fprintf(f, "vt %.6f %.6f\n", t.u, t.v);
                const Vect3d& n = G.nrm[i];
                std::fprintf(f, "vn %.6f %.6f %.6f\n", n.x(), n.y(), n.z());
            }
            vCount += total;
            vtCount += total;
            vnCount += total;

            auto idx = [&](int r, int c) { return r * G.cols + c; };
            for (int r = 0; r < G.rows - 1; ++r) {
                for (int c = 0; c < G.cols - 1; ++c) {
                    int i00 = vBase + idx(r, c);
                    int i10 = vBase + idx(r + 1, c);
                    int i01 = vBase + idx(r, c + 1);
                    int i11 = vBase + idx(r + 1, c + 1);

                    int t00 = vtBase + idx(r, c);
                    int t10 = vtBase + idx(r + 1, c);
                    int t01 = vtBase + idx(r, c + 1);
                    int t11 = vtBase + idx(r + 1, c + 1);

                    int n00 = vnBase + idx(r, c);
                    int n10 = vnBase + idx(r + 1, c);
                    int n01 = vnBase + idx(r, c + 1);
                    int n11 = vnBase + idx(r + 1, c + 1);

					// Two tris per quad
                    std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        i00, t00, n00, i10, t10, n10, i01, t01, n01);
                    std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        i10, t10, n10, i11, t11, n11, i01, t01, n01);
                }
            }
            std::fprintf(f, "\n");
        }

        std::fclose(f);

        // Write MTL
        bool mtlOK = saveMTL(mtlFileName.c_str(), texFileName.c_str());

        gTexCopyOK = true;
        return mtlOK;
    }
	// Write MTL file with leaf and stem materials
    static bool saveMTL(const char* mtlPath, const char* textureFileNameOnly) {
        FILE* m;
        errno_t err_m = fopen_s(&m, mtlPath, "w");
        if (!m) return false;
        std::fprintf(m, "# Plant materials\n\n");

        // Leaf
        std::fprintf(m, "newmtl leaf_mat\n");
        std::fprintf(m, "Ns 18.0\nKa 0 0 0\nKd 1 1 1\nKs 0.1 0.1 0.1\nillum 2\n");
        if (textureFileNameOnly && textureFileNameOnly[0]) {
            std::fprintf(m, "map_Kd %s\n", textureFileNameOnly);
            std::fprintf(m, "map_d %s\n", textureFileNameOnly);
        }
        std::fprintf(m, "\n");

        // Stem
        std::fprintf(m, "newmtl stem_mat\n");
        std::fprintf(m, "Ns 18.0\nKa 0 0 0\nKd 1 1 1\nKs 0.05 0.05 0.05\nillum 2\n");
        if (textureFileNameOnly && textureFileNameOnly[0]) {
            std::fprintf(m, "map_Kd %s\n", textureFileNameOnly);
            std::fprintf(m, "map_d %s\n", textureFileNameOnly);
        }
        std::fclose(m);
        return true;
    }
	// Path utilities
    static std::string dirFromPath(const std::string& p) {
        size_t s = p.find_last_of("/\\"); return (s == std::string::npos) ? std::string() : p.substr(0, s);
    }
    static std::string fileFromPath(const std::string& p) {
        size_t s = p.find_last_of("/\\"); return (s == std::string::npos) ? p : p.substr(s + 1);
    }
    static std::string replaceExt(const std::string& p, const char* newExtWithDot) {
        size_t s = p.find_last_of('.'); size_t slash = p.find_last_of("/\\");
        if (s == std::string::npos || (slash != std::string::npos && s < slash)) return p + newExtWithDot;
        return p.substr(0, s) + newExtWithDot;
    }
    static bool samePath(const std::string& a, const std::string& b) {
        std::string na = normPath(a), nb = normPath(b);
#if defined(_WIN32) || defined(_WIN64)
        std::transform(na.begin(), na.end(), na.begin(), ::tolower);
        std::transform(nb.begin(), nb.end(), nb.begin(), ::tolower);
#endif
        return na == nb;
    }
    static std::string normPath(std::string p) {
        for (auto& c : p) if (c == '\\') c = '/';
        return p;
    }
    static bool copyFile(const char* src, const char* dst) {
        FILE* in;
        errno_t err_in = fopen_s(&in, src, "rb"); if (!in) return false;
        FILE* out;
        errno_t err_out = fopen_s(&out, dst, "wb"); if (!out) { std::fclose(in); return false; }
        char buf[1 << 15];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) {
            if (std::fwrite(buf, 1, n, out) != n) { std::fclose(in); std::fclose(out); return false; }
        }
        std::fclose(in); std::fclose(out); return true;
    }
}