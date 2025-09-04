#include <GL/freeglut.h>
#include <GL/glu.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <map>

// ImGui
#include "imgui.h"
#include "imgui_impl_glut.h"
#include "imgui_impl_opengl2.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// tinyxml2
#include "tinyxml2.h"
using namespace tinyxml2;

// Your math type (assumed available in your project)
#include <math/vect3d.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

/* ----------------------------- data ------------------------------ */
struct LeafDesc {
    int   id = 0;           // id from XML
    float distance = 0;     // internode length (used cumulatively)
    float alphaDeg = 30;    // pitch (elevation)
    int   spines = 10;      // sampling suggestion
    float azimuthDeg = 0;   // computed from tiller.alpha + id * tiller.beta
};
struct TillerDesc {
    std::string type = "main";
    float radius = 0.02f;     // base stem radius (outermost)
    float alphaDeg = 0.f;     // base azimuth
    float betaDeg = 180.f;   // phyllotaxy step
    std::vector<LeafDesc> leaves;
};
struct PlantDesc {
    std::string species = "maize";
    std::vector<TillerDesc> tillers;
};
struct Vec2 { float u, v; };

/* ------------------------ editable per-leaf ----------------------- */
struct LeafKey { int tIdx; int lIdx; bool operator<(const LeafKey& o) const { return (tIdx < o.tIdx) || (tIdx == o.tIdx && lIdx < o.lIdx); } };
struct LeafEdit {
    bool hasCustom = false;
    bool pinC0ToStem = true;
    std::vector<Vect3d> customCtrl;   // world-space control points
};

/* ---------------------- generated geometry ----------------------- */
// Whole-leaf parametric grid (rows x cols)
struct LeafGeom {
    int rows = 0, cols = 0;              // grid size
    std::vector<Vect3d> pos;             // rows*cols positions
    std::vector<Vec2>   uv;              // rows*cols texcoords
    std::vector<Vect3d> ctrl;            // control points used
    std::vector<Vect3d> nrm;             // per-vertex normals
};

struct StemLayerGeom {
    float radius = 0.f; float y0 = 0.f, y1 = 0.f; int sides = 32;
    std::vector<Vect3d> band; // triangle strip vertices (cx, y, cz) pairs
};

/* ------------------------------ math ------------------------------ */
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }
static inline float smooth01(float x) { x = clampf(x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
static inline float lerpf(float a, float b, float t) { return a * (1.f - t) + b * t; }
static inline float smoothstepf(float a, float b, float x) {
    float t = clampf((x - a) / std::max(1e-6f, b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
static inline float widthBaseMax(float s, float wMax,
    float knee, float wAtKnee, float earlyPow,
    float tipPow, float tipRel) {
    s = clampf(s, 0.f, 1.f);
    const float k = clampf(knee, 0.40f, 0.90f);
    const float wk = clampf(wAtKnee, 0.05f, 0.95f);
    const float w1 = clampf(tipRel, 0.00f, 0.40f);
    const float p0 = std::max(0.1f, earlyPow);
    const float p1 = std::max(0.1f, tipPow);

    float wr;
    if (s <= k) {
        float t = (k > 1e-6f) ? (s / k) : 1.f;
        wr = lerpf(1.f, wk, powf(t, p0));
    }
    else {
        float t = (1.f - k > 1e-6f) ? ((s - k) / (1.f - k)) : 1.f;
        wr = lerpf(wk, w1, powf(t, p1));
    }
    return wMax * wr;
}

/* ------------------------- cubic B-spline -------------------------- */
static std::vector<float> openUniformKnots(int nCtrl, int p = 3) {
    int n = nCtrl - 1, m = n + p + 1; std::vector<float> U(m + 1, 0.f);
    for (int j = 0; j <= p; ++j) U[j] = 0.f;
    for (int j = m - p; j <= m; ++j) U[j] = 1.f;
    for (int j = p + 1; j <= m - p - 1; ++j) U[j] = float(j - p) / float(m - 2 * p);
    return U;
}
static int findSpan(int n, int p, float u, const std::vector<float>& U) {
    if (u >= U[n + 1]) return n;
    int low = p, high = n + 1, mid = (low + high) / 2;
    while (u < U[mid] || u >= U[mid + 1]) { if (u < U[mid]) high = mid; else low = mid; mid = (low + high) / 2; }
    return mid;
}
static Vect3d deBoor(float u, const std::vector<Vect3d>& P, const std::vector<float>& U, int p = 3) {
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
static std::vector<Vect3d> resampleMidrib(const std::vector<Vect3d>& ctrl, int N) {
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

// Parallel-transport frames along a polyline mid[]
static void buildPTFrames(const std::vector<Vect3d>& mid,
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

/* ----------------------- globals & params ------------------------- */
static PlantDesc   gPlant;
static std::map<LeafKey, LeafEdit> gEdits;
static std::vector<LeafKey> gFlatKeys; static std::vector<std::string> gFlatNames; static int gSelectedFlat = 0;

static std::vector<LeafGeom>      gLeafGeoms;
static std::vector<StemLayerGeom> gStemLayers;

// Leaf shape
static float gLeafLength = 1.00f;
static float gLeafWmax = 0.18f;
static int   gSamplesMin = 16;
static bool  gDroop = true;
static float gDroopMax = 0.06f;
static bool  gTwistOn = true;
static float gTwistMaxD = 20.f;
static float gTwistPow = 1.4f;
static float gRollOffsetD = 180.0f; // rotate about tangent so blades face "up"
static bool  gAutoRollTangency = true;

// --- Waviness (static corrugation along the leaf length) ---
static float gWaveAmp = 0.0030f;       // meters, peak displacement
static float gWaveCycles = 6.0f;       // how many waves from base→tip
static float gWaveAcross = 0.5f;       // 0=uniform across width; 1=edges out of phase with center
static float gWaveFadeIn = 0.12f;      // fraction near base to fade in
static float gWaveFadeOut = 0.10f;     // fraction near tip to fade out

// Width profile (base-max → knee → tip)
static float gWidthKnee = 0.66f;
static float gWidthAtKnee = 0.45f;
static float gEarlyPow = 0.70f;
static float gTipPow = 1.80f;
static float gTipRelWidth = 0.05f;
static float gBaseRampFrac = 0.08f;  // wedge length near base

// Attachment to stem (blend centerline to cylinder near the base)
static float gAttachFrac = 0.10f;   // 0..1 of length
static float gAttachEpsilon = 0.0015f; // keep off the stem (z-fight guard)

// Unfolding along length (cylinder→leaf)
static float gLipLenFrac = 0.08f;   // fraction of leaf length for unwrapping
static int   gLipCols = 11;      // odd, >=7 (grid columns)
static float gLipArc0Deg = 340.f;   // arc at base (<360 to avoid seam)
static float gLipArc1Deg = 40.f;    // arc at end of unwrap (0..180)
static float gCollarHoldFrac = 0.25f;   // portion kept cylindrical
static float gUnwrapMidFrac = 0.55f;   // logistic midpoint
static float gUnwrapSharpness = 2.50f;   // logistic steepness
static float gRingDrop = 0.010f;  // meters (0–5 cm is typical)

// Stem layers (concentric)
static float gStemLayerShrink = 0.0060f; // radius decrease per new inner cylinder (larger default gap)
static int   gStemSides = 48;     // tesselation
static bool  gStemAsWire = false;  // render layers as wires

// Outermost stem radius override
static bool  gOverrideBaseRadius = false;
static float gOutermostRadius = 0.0200f;

// Texture
static bool     gUseLeafTex = true;
static GLuint   gLeafTex = 0;  static int gLeafTexW = 0, gLeafTexH = 0;  static bool gTexOK = false;
static char     gLeafTexPath[256] = "maize_leaf.png";
static float    gTexURepeat = 1.0f, gTexVRepeat = 1.0f;

// Camera/UI
static int   gWinW = 1280, gWinH = 860;
static float gYaw = 30.f, gPitch = 18.f, gDist = 3.5f;
static Vect3d gCamTarget = { 0.5f, 0.25f, 0.0f };
static bool  gWireframe = false; static float gWireWidth = 1.5f;
static bool  gShowCtrlPts = true, gShowCtrlPoly = true, gShowCtrlLabel = true; static float gCtrlPtSize = 10.f;
static bool  gHideSelected = false, gSoloSelected = false;
static bool gSelectedSolid = true;

// XML
static char gXmlPath[256] = "maizeDebug.xml";
static bool gSaveOK = false, gSaveFail = false, gLoadOK = false, gLoadFail = false;

// Lighting 
static bool  gLightingOn = true;
static bool  gTwoSided = true;     // leaves are thin; two-sided looks nicer
static float gLightPos[4] = { 2.0f, 3.0f, 2.0f, 1.0f }; // w=1 positional (0=directional)
static float gGlobalAmb[4] = { 0.15f, 0.15f, 0.15f, 1.0f };
static float gLightDiff[4] = { 0.95f, 0.95f, 0.95f, 1.0f };
static float gLightSpec[4] = { 0.20f, 0.20f, 0.20f, 1.0f };
static float gMatSpec[4] = { 0.12f, 0.12f, 0.12f, 1.0f };
static float gShininess = 18.0f;


/* -------------------- helpers & loading --------------------------- */
static void rebuildFlatKeys() {
    gFlatKeys.clear(); gFlatNames.clear();
    for (size_t ti = 0; ti < gPlant.tillers.size(); ++ti) {
        const auto& td = gPlant.tillers[ti];
        // Base radius for this tiller (allow override)
        float baseR = gOverrideBaseRadius ? std::max(0.001f, gOutermostRadius) : td.radius;
        (void)baseR; // not used here; just documenting availability
        for (size_t li = 0; li < td.leaves.size(); ++li) {
            gFlatKeys.push_back({ (int)ti,(int)li });
            char buf[64]; snprintf(buf, sizeof(buf), "T%zu leaf id=%d", ti, td.leaves[li].id);
            gFlatNames.emplace_back(buf);
        }
    }
    gSelectedFlat = std::min(gSelectedFlat, (int)gFlatKeys.size() - 1);
}

static bool savePlantXMLWithEdits(const char* path) {
    XMLDocument doc;

    XMLElement* root = doc.NewElement("plant");
    doc.InsertFirstChild(root);

    XMLElement* species = doc.NewElement("species");
    species->SetAttribute("name", gPlant.species.c_str());
    root->InsertEndChild(species);

    // ---- Global params (optional but handy) ----
    XMLElement* P = doc.NewElement("params");
    P->SetAttribute("length_m", gLeafLength);
    P->SetAttribute("max_width_m", gLeafWmax);
    P->SetAttribute("samples_min", gSamplesMin);
    P->SetAttribute("droop_on", (int)gDroop);
    P->SetAttribute("droop_max_m", gDroopMax);
    P->SetAttribute("twist_on", (int)gTwistOn);
    P->SetAttribute("twist_max_deg", gTwistMaxD);
    P->SetAttribute("twist_pow", gTwistPow);
    P->SetAttribute("roll_deg", gRollOffsetD);
    P->SetAttribute("base_ramp_frac", gBaseRampFrac);

    P->SetAttribute("attach_frac", gAttachFrac);
    P->SetAttribute("attach_epsilon", gAttachEpsilon);

    P->SetAttribute("lip_len_frac", gLipLenFrac);
    P->SetAttribute("lip_cols", gLipCols);
    P->SetAttribute("lip_arc0_deg", gLipArc0Deg);
    P->SetAttribute("lip_arc1_deg", gLipArc1Deg);
    P->SetAttribute("collar_hold_frac", gCollarHoldFrac);
    P->SetAttribute("unwrap_mid", gUnwrapMidFrac);
    P->SetAttribute("unwrap_sharpness", gUnwrapSharpness);
    P->SetAttribute("ring_drop_m", gRingDrop);

    P->SetAttribute("stem_layer_shrink", gStemLayerShrink);
    P->SetAttribute("stem_sides", gStemSides);
    P->SetAttribute("override_base_radius", (int)gOverrideBaseRadius);
    P->SetAttribute("outermost_radius_m", gOutermostRadius);

    P->SetAttribute("wave_amp_m", gWaveAmp);
    P->SetAttribute("wave_cycles", gWaveCycles);
    P->SetAttribute("wave_across", gWaveAcross);
    P->SetAttribute("wave_fade_in", gWaveFadeIn);
    P->SetAttribute("wave_fade_out", gWaveFadeOut);
    species->InsertEndChild(P);

    // ---- Tillers / Leaves ----
    for (size_t ti = 0; ti < gPlant.tillers.size(); ++ti) {
        const auto& td = gPlant.tillers[ti];
        XMLElement* t = doc.NewElement("tiller");
        t->SetAttribute("type", td.type.c_str());
        t->SetAttribute("radius", td.radius);
        t->SetAttribute("alpha", td.alphaDeg);
        t->SetAttribute("beta", td.betaDeg);

        XMLElement* leaves = doc.NewElement("leaves");
        leaves->SetAttribute("number", (int)td.leaves.size());

        for (size_t li = 0; li < td.leaves.size(); ++li) {
            const auto& L = td.leaves[li];
            XMLElement* lf = doc.NewElement("leaf");
            lf->SetAttribute("id", L.id);
            lf->SetAttribute("distance", L.distance);
            lf->SetAttribute("alpha", L.alphaDeg);
            lf->SetAttribute("spines", L.spines);

            // If an edit exists for this leaf, serialize it
            LeafKey key{ (int)ti, (int)li };
            auto it = gEdits.find(key);
            if (it != gEdits.end() && it->second.hasCustom && it->second.customCtrl.size() >= 4) {
                const LeafEdit& E = it->second;
                XMLElement* ed = doc.NewElement("edit");
                ed->SetAttribute("pinC0ToStem", (int)E.pinC0ToStem);
                for (const Vect3d& p : E.customCtrl) {
                    XMLElement* cp = doc.NewElement("cp");
                    cp->SetAttribute("x", p.x());
                    cp->SetAttribute("y", p.y());
                    cp->SetAttribute("z", p.z());
                    ed->InsertEndChild(cp);
                }
                lf->InsertEndChild(ed);
            }

            leaves->InsertEndChild(lf);
        }

        t->InsertEndChild(leaves);
        species->InsertEndChild(t);
    }

    return (doc.SaveFile(path) == XML_SUCCESS);
}


static bool loadPlantXML(const std::string& path, PlantDesc& out) {
    XMLDocument doc; if (doc.LoadFile(path.c_str()) != XML_SUCCESS) return false;

    const XMLElement* species = doc.FirstChildElement("plant")->FirstChildElement("species");
    if (!species) return false;
    if (const char* sp = species->Attribute("name")) out.species = sp; else out.species = "unknown";

    // ---- Optional global params ----
    if (const XMLElement* P = species->FirstChildElement("params")) {
        P->QueryFloatAttribute("length_m", &gLeafLength);
        P->QueryFloatAttribute("max_width_m", &gLeafWmax);
        P->QueryIntAttribute("samples_min", &gSamplesMin);
        { int v = 0; if (P->QueryIntAttribute("droop_on", &v) == XML_SUCCESS) gDroop = (v != 0); }
        P->QueryFloatAttribute("droop_max_m", &gDroopMax);
        { int v = 0; if (P->QueryIntAttribute("twist_on", &v) == XML_SUCCESS) gTwistOn = (v != 0); }
        P->QueryFloatAttribute("twist_max_deg", &gTwistMaxD);
        P->QueryFloatAttribute("twist_pow", &gTwistPow);
        P->QueryFloatAttribute("roll_deg", &gRollOffsetD);
        P->QueryFloatAttribute("base_ramp_frac", &gBaseRampFrac);

        P->QueryFloatAttribute("attach_frac", &gAttachFrac);
        P->QueryFloatAttribute("attach_epsilon", &gAttachEpsilon);

        P->QueryFloatAttribute("lip_len_frac", &gLipLenFrac);
        P->QueryIntAttribute("lip_cols", &gLipCols);
        P->QueryFloatAttribute("lip_arc0_deg", &gLipArc0Deg);
        P->QueryFloatAttribute("lip_arc1_deg", &gLipArc1Deg);
        P->QueryFloatAttribute("collar_hold_frac", &gCollarHoldFrac);
        P->QueryFloatAttribute("unwrap_mid", &gUnwrapMidFrac);
        P->QueryFloatAttribute("unwrap_sharpness", &gUnwrapSharpness);
        P->QueryFloatAttribute("ring_drop_m", &gRingDrop);

        P->QueryFloatAttribute("stem_layer_shrink", &gStemLayerShrink);
        P->QueryIntAttribute("stem_sides", &gStemSides);
        { int v = 0; if (P->QueryIntAttribute("override_base_radius", &v) == XML_SUCCESS) gOverrideBaseRadius = (v != 0); }
        P->QueryFloatAttribute("outermost_radius_m", &gOutermostRadius);

        P->QueryFloatAttribute("wave_amp_m", &gWaveAmp);
        P->QueryFloatAttribute("wave_cycles", &gWaveCycles);
        P->QueryFloatAttribute("wave_across", &gWaveAcross);
        P->QueryFloatAttribute("wave_fade_in", &gWaveFadeIn);
        P->QueryFloatAttribute("wave_fade_out", &gWaveFadeOut);
    }

    out.tillers.clear();
    gEdits.clear();

    size_t ti = 0;
    for (const XMLElement* t = species->FirstChildElement("tiller"); t; t = t->NextSiblingElement("tiller"), ++ti) {
        TillerDesc td; if (const char* tt = t->Attribute("type")) td.type = tt;
        t->QueryFloatAttribute("radius", &td.radius);
        t->QueryFloatAttribute("alpha", &td.alphaDeg);
        t->QueryFloatAttribute("beta", &td.betaDeg);

        size_t li = 0;
        if (const XMLElement* leaves = t->FirstChildElement("leaves")) {
            for (const XMLElement* lf = leaves->FirstChildElement("leaf"); lf; lf = lf->NextSiblingElement("leaf"), ++li) {
                LeafDesc L; lf->QueryIntAttribute("id", &L.id);
                lf->QueryFloatAttribute("distance", &L.distance);
                lf->QueryFloatAttribute("alpha", &L.alphaDeg);
                lf->QueryIntAttribute("spines", &L.spines);
                L.azimuthDeg = td.alphaDeg + L.id * td.betaDeg;
                td.leaves.push_back(L);

                // ---- Optional per-leaf edit ----
                if (const XMLElement* ed = lf->FirstChildElement("edit")) {
                    LeafEdit E; E.hasCustom = true;
                    int pin = 1; ed->QueryIntAttribute("pinC0ToStem", &pin);
                    E.pinC0ToStem = (pin != 0);
                    for (const XMLElement* cp = ed->FirstChildElement("cp"); cp; cp = cp->NextSiblingElement("cp")) {
                        float x = 0, y = 0, z = 0;
                        cp->QueryFloatAttribute("x", &x);
                        cp->QueryFloatAttribute("y", &y);
                        cp->QueryFloatAttribute("z", &z);
                        E.customCtrl.emplace_back(x, y, z);
                    }
                    if (E.customCtrl.size() >= 4) {
                        gEdits[{(int)ti, (int)li}] = std::move(E);
                    }
                }
            }
        }
        out.tillers.push_back(td);
    }
    return true;
}


static bool loadLeafTexture(const char* path) {
    int w = 0, h = 0, n = 0; stbi_uc* data = stbi_load(path, &w, &h, &n, 4);
    if (!data) return false;
    if (!gLeafTex) glGenTextures(1, &gLeafTex);
    gLeafTexW = w; gLeafTexH = h;
    glBindTexture(GL_TEXTURE_2D, gLeafTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    gTexOK = true; return true;
}

/* -------------- default generator for ctrl (seed) ---------------- */
static std::vector<Vect3d> makeLeafCtrl(Vect3d base, Vect3d stemUp, Vect3d dir, float L) {
    Vect3d p0 = base;
    Vect3d p1 = base + dir * (0.30f * L) + stemUp * (0.10f * L);
    Vect3d p2 = base + dir * (0.65f * L) + stemUp * (0.05f * L);
    Vect3d p3 = base + dir * (0.95f * L) + stemUp * (-0.05f * L);
    Vect3d p4 = base + dir * (1.10f * L) + stemUp * (-0.12f * L);
    return { p0,p1,p2,p3,p4 };
}

// Unwrap amount b(s) in [0,1]: 0=cylinder, 1=flat leaf
static inline float unwrapAmountAtS(float s01) {
    const float sEnd = clampf(gLipLenFrac, 0.f, 1.f);
    const float sHold = clampf(gCollarHoldFrac, 0.f, 0.95f) * sEnd;
    if (s01 <= sHold) return 0.f;
    if (s01 >= sEnd)  return 1.f;
    float t = (s01 - sHold) / std::max(1e-4f, sEnd - sHold);
    t = clampf(t, 0.f, 1.f);
    const float x0 = clampf(gUnwrapMidFrac, 0.05f, 0.95f);
    const float k = std::max(0.1f, gUnwrapSharpness);
    auto L = [&](float x) { return 1.f / (1.f + std::exp(-k * (x - x0))); };
    float y = L(t), y0 = L(0.f), y1 = L(1.f);
    float b = (y - y0) / std::max(1e-6f, (y1 - y0));
    return b * b * (3.f - 2.f * b);
}

/* ----------------------- core: rebuild meshes -------------------- */
static void rebuildGeometry() {
    gLeafGeoms.clear();
    gStemLayers.clear();

    const Vect3d stemAxis = { 0,1,0 };

    for (size_t ti = 0; ti < gPlant.tillers.size(); ++ti) {
        const auto& td = gPlant.tillers[ti];

        // Compute cumulative heights and total stem height
        std::vector<float> yAt; yAt.reserve(td.leaves.size());
        float accY = 0.f;
        for (const auto& L : td.leaves) { accY += std::max(0.f, L.distance); yAt.push_back(accY); }
        float stemTop = accY; (void)stemTop;

        float baseR = gOverrideBaseRadius ? std::max(0.001f, gOutermostRadius) : td.radius;

        // --- Stem layers (stacked from ground to each leaf base) ---
        for (size_t k = 0; k < td.leaves.size(); ++k) {
            float r = std::max(0.001f, baseR - (float)k * gStemLayerShrink);
            StemLayerGeom layer; layer.radius = r; layer.y0 = 0.f; 
            float yTop = std::max(0.f, yAt[k] - gRingDrop);
            layer.y1 = yTop;
            layer.sides = std::max(8, gStemSides);
            for (int i = 0; i <= layer.sides; ++i) {
                float a = (float)i / layer.sides * 2.f * (float)M_PI;
                float cx = std::cos(a) * r, cz = std::sin(a) * r;
                layer.band.push_back({ cx, layer.y0, cz });
                layer.band.push_back({ cx, layer.y1, cz });
            }
            gStemLayers.push_back(std::move(layer));
        }

        // --- Leaves ---
        for (size_t li = 0; li < td.leaves.size(); ++li) {
            const auto& L = td.leaves[li];
            float phi = radiansf(L.azimuthDeg);
            float pitch = radiansf(L.alphaDeg);

            Vect3d rhat = { std::cos(phi), 0.f, std::sin(phi) };               // radial dir
            Vect3d base = rhat * baseR + Vect3d{ 0.f, yAt[li], 0.f };          // initial base
            Vect3d dir = (rhat * std::cos(pitch) + stemAxis * std::sin(pitch)).GetNormalized();

            // Leaf attaches to the *current inner layer* created by this leaf
            float rLeaf = std::max(0.001f, baseR - (float)(li)*gStemLayerShrink);
            base = rhat * (rLeaf + gAttachEpsilon) + Vect3d{ 0.f, yAt[li], 0.f };
            float yRingTop = base.y() - gRingDrop;  // ring top below C0

            // Control points (seed or edited)
            LeafKey key{ (int)ti,(int)li };
            auto it = gEdits.find(key);
            std::vector<Vect3d> ctrl = (it != gEdits.end() && it->second.hasCustom && it->second.customCtrl.size() >= 4)
                ? it->second.customCtrl : makeLeafCtrl(base, stemAxis, dir, gLeafLength);
            if (it != gEdits.end() && it->second.hasCustom && it->second.pinC0ToStem) ctrl[0] = base;

            int samples = std::max(L.spines, gSamplesMin);
            auto mid = resampleMidrib(ctrl, samples);
            std::vector<Vect3d> T, N, B; buildPTFrames(mid, T, N, B);

            // Roll + twist so the blade faces up
            std::vector<Vect3d> N2 = N, B2 = B;
            float baseRoll = radiansf(gRollOffsetD);
            if (gAutoRollTangency) {
                float theta0 = Vect3d::autoRollTheta(B[0], T[0], rhat);
                baseRoll += theta0;
            }
            for (int i = 0; i < samples; ++i) {
                float s = (samples == 1) ? 0.f : float(i) / (samples - 1);
                float twist = gTwistOn ? radiansf(gTwistMaxD) * std::pow(s, gTwistPow) : 0.f;
                float th = baseRoll + twist;
                N2[i] = Vect3d::rotateAroundAxis(N[i], T[i], th);
                B2[i] = Vect3d::rotateAroundAxis(B[i], T[i], th);
            }

            // --- Whole-leaf parametric grid (cylinder → flat) ---
            LeafGeom G;
            G.ctrl = ctrl;
            G.rows = samples;
            G.cols = std::max(7, (gLipCols | 1)); // force odd to have a true center column
            G.pos.resize((size_t)G.rows * G.cols);
            G.uv.resize((size_t)G.rows * G.cols);

            auto idxAt = [&](int i, int j) { return i * G.cols + j; };

            for (int i = 0; i < G.rows; ++i) {
                float s01 = (G.rows == 1) ? 0.f : float(i) / (G.rows - 1);

                // Midrib (optionally hug cylinder near base)
                Vect3d C = mid[i];

                if (gAttachFrac > 0.f) {
                    float x = clampf(s01 / std::max(1e-4f, gAttachFrac), 0.f, 1.f);
                    float t = 1.f - (x * x * (3.f - 2.f * x));
                    Vect3d Ccyl = rhat * (rLeaf + gAttachEpsilon) + Vect3d{ 0.f, C.y(), 0.f };
                    C = C * (1.f - t) + Ccyl * t;
                }

                if (i == 0) C.SetY(base.y()); // seal first row to layer height
                if (gDroop) C.SetY(C.y() - gDroopMax * s01 * s01);

                Vect3d Nrow = N2[i];

                // Unwrap and arc at this s
                float b = unwrapAmountAtS(s01);                      // 0..1
                float dphi = radiansf(lerpf(gLipArc0Deg, gLipArc1Deg, b));

                // Width and anti-hourglass blend
                float wLeaf = widthBaseMax(s01, gLeafWmax, gWidthKnee, gWidthAtKnee, gEarlyPow, gTipPow, gTipRelWidth);
                float baseRamp = smooth01(s01 / std::max(1e-4f, gBaseRampFrac));
                float halfWLeaf = 0.5f * wLeaf * baseRamp;
                float halfChord = (rLeaf + gAttachEpsilon) * std::sinf(0.5f * dphi);
                float halfW = lerpf(halfChord, halfWLeaf, b);

                for (int j = 0; j < G.cols; ++j) {
                    float u = (G.cols == 1) ? 0.f : float(j) / (G.cols - 1);
                    float uLR = (u - 0.5f) * 2.f;
                    float phj = phi + uLR * dphi;

                    // Cylinder and flat cross-section
                    // Let the cylinder's Y start at the dropped ring, then rise to C.y() over the unwrap span
                    const float sEnd = clampf(gLipLenFrac, 0.f, 1.f);
                    float tRise = (sEnd > 1e-6f) ? clampf(s01 / sEnd, 0.f, 1.f) : 1.f;
                    float cylY = lerpf(yRingTop, C.y(), tRise);

                    Vect3d Pcyl = { std::cos(phj) * (rLeaf + gAttachEpsilon),
                                    cylY,
                                    std::sin(phj) * (rLeaf + gAttachEpsilon) };
                    Vect3d Pleaf = C + Nrow * (uLR * halfW);

                    // Blend by unwrap amount
                    Vect3d P = Pcyl * (1.f - b) + Pleaf * b;

                    if (i > 0) { // keep the seam row i==0 sealed to the ring
                        // Binormal from local frame
                        Vect3d Bblend = (T[i].Cross(Nrow)).GetNormalized();

                        // s01 already computed above; build a length envelope (fade in/out)
                        float fadeIn = smoothstepf(0.0f, std::max(1e-3f, gWaveFadeIn), s01);
                        float fadeOut = 1.0f - smoothstepf(1.0f - gWaveFadeOut, 1.0f, s01);
                        float env = fadeIn * fadeOut;

                        // base sinusoid down the length
                        const float twoPi = 6.28318530718f;
                        float basePhase = twoPi * (gWaveCycles * s01);

                        // across-width modulation: uLR in [-1,1]
                        float acrossPhase = twoPi * 0.25f * gWaveAcross * uLR; // quarter-cycle skew toward edges
                        float amp = gWaveAmp * env * std::sinf(basePhase + acrossPhase);

                        // displace vertex along binormal
                        P = P + Bblend * amp;
                    }

                    int id = idxAt(i, j);
                    G.pos[id] = P;
                    G.uv[id] = { u * gTexURepeat, s01 * gTexVRepeat };
                }
            }
            // --- compute per-vertex normals for the leaf grid ---
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

                    Vect3d dU = P_jp - P_jm;   // across columns
                    Vect3d dV = P_ip - P_im;   // along rows
                    Vect3d n = dV.Cross(dU);  // orientation chosen to match triangle order
                    float L = n.Length();
                    if (L > 1e-12f) n = n * (1.0f / L);
                    else            n = { 0,1,0 };
                    G.nrm[i * G.cols + j] = n;
                }
            }
            gLeafGeoms.push_back(std::move(G));
        }
    }
}

/* -------------------------- rendering ---------------------------- */
static void drawGridXZ(float size = 2.5f, int steps = 36) {
    glLineWidth(1.0f);
    glColor3f(0.90f, 0.90f, 0.90f);
    glBegin(GL_LINES);
    for (int i = 0; i <= steps; ++i) {
        float t = float(i) / steps, x = -size + 2 * size * t;
        glVertex3f(x, 0, -size); glVertex3f(x, 0, size);
        glVertex3f(-size, 0, x); glVertex3f(size, 0, x);
    }
    glEnd();
    // axes
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3f(0.9f, 0.1f, 0.1f); glVertex3f(0, 0, 0); glVertex3f(0.5f, 0, 0); // X
    glColor3f(0.1f, 0.6f, 0.1f); glVertex3f(0, 0, 0); glVertex3f(0, 0.5f, 0); // Y
    glColor3f(0.1f, 0.3f, 0.9f); glVertex3f(0, 0, 0); glVertex3f(0, 0, 0.5f); // Z
    glEnd();
}

static void drawStemLayers() {
    if (gStemLayers.empty()) return;

    if (gStemAsWire) {
        glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(gWireWidth);
    }
    else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (gUseLeafTex && gLeafTex) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, gLeafTex);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor3f(0.75f, 0.75f, 0.75f);
        }
        else {
            glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
        }
    }

    int idx = 0;
    for (const auto& S : gStemLayers) {
        if (!gUseLeafTex || !gLeafTex || gStemAsWire) {
            // subtle color variation per layer if not using texture
            float t = (idx == 0) ? 0.f : 0.12f + 0.12f * (idx % 5);
            glColor3f(0.06f + t, 0.45f + 0.2f * t, 0.08f + 0.05f * t);
        }

        glBegin(GL_TRIANGLE_STRIP);
        for (size_t vi = 0; vi < S.band.size(); ++vi) {
            const Vect3d& v = S.band[vi];

            // normal for cylinder around Y axis
            Vect3d n = Vect3d(v.x(), 0.0f, v.z());
            float L = n.Length(); if (L > 1e-12f) n = n * (1.0f / L); else n = { 1,0,0 };
            glNormal3f(n.x(), n.y(), n.z());

            if (!gStemAsWire && gUseLeafTex && gLeafTex) {
                float ang = std::atan2(v.z(), v.x());
                float u = ang / (2.0f * float(M_PI)); if (u < 0.f) u += 1.f;
                float vtex = (S.y1 - S.y0 > 1e-6f) ? (v.y() - S.y0) / (S.y1 - S.y0) : 0.f;
                glTexCoord2f(u * gTexURepeat * 4, vtex * gTexVRepeat);
            }

            glVertex3f(v.x(), v.y(), v.z());
        }
        glEnd();

        idx++;
    }

    if (gStemAsWire) { glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glLineWidth(1.0f); }
    if (!gStemAsWire && gUseLeafTex && gLeafTex) { glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); }
}


static void drawLeaves() {
    int idx = 0;
    for (const auto& G : gLeafGeoms) {
        if (gSoloSelected && idx != gSelectedFlat) { idx++; continue; }
        if (gHideSelected && idx == gSelectedFlat) { idx++; continue; }

        bool wireForThis = gWireframe && !(gSelectedSolid && idx == gSelectedFlat);

        if (wireForThis) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(gWireWidth);
            glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
            glColor3f(0.0f, 0.35f, 0.0f);
        }
        else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            if (gUseLeafTex && gLeafTex) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, gLeafTex);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glColor3f(1, 1, 1);
            }
            else {
                glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
                glColor3f(0.04f, 0.55f, 0.06f);
            }
        }

        // render grid as row triangle strips
        for (int i = 0; i < G.rows - 1; ++i) {
            glBegin(GL_TRIANGLE_STRIP);
            for (int j = 0; j < G.cols; ++j) {
                int i0 = i * G.cols + j;
                int i1 = (i + 1) * G.cols + j;

                if (!wireForThis) {
                    // normals + texcoord + vertex (row i)
                    const Vect3d& n0 = G.nrm[i0]; glNormal3f(n0.x(), n0.y(), n0.z());
                    if (gUseLeafTex && gLeafTex) glTexCoord2f(G.uv[i0].u, G.uv[i0].v);
                }
                const Vect3d& p0 = G.pos[i0]; glVertex3f(p0.x(), p0.y(), p0.z());

                if (!wireForThis) {
                    // normals + texcoord + vertex (row i+1)
                    const Vect3d& n1 = G.nrm[i1]; glNormal3f(n1.x(), n1.y(), n1.z());
                    if (gUseLeafTex && gLeafTex) glTexCoord2f(G.uv[i1].u, G.uv[i1].v);
                }
                const Vect3d& p1 = G.pos[i1]; glVertex3f(p1.x(), p1.y(), p1.z());
            }
            glEnd();
        }


        // clean up per-leaf GL state
        if (!wireForThis && gUseLeafTex && gLeafTex) { glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); }
        if (wireForThis) { glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glLineWidth(1.0f); }

        idx++;
    }

    // Optional highlight overlay – skip if we’re already drawing the selected solid
    if (!gLeafGeoms.empty() && !gHideSelected && !gSoloSelected && !(gSelectedSolid)) {
        int sel = gSelectedFlat;
        if (sel >= 0 && sel < (int)gLeafGeoms.size()) {
            const auto& G = gLeafGeoms[sel];
            glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(gWireWidth + 1.5f);
            glColor3f(0.95f, 0.95f, 0.15f);
            for (int i = 0; i < G.rows - 1; ++i) {
                glBegin(GL_TRIANGLE_STRIP);
                for (int j = 0; j < G.cols; ++j) {
                    int i0 = i * G.cols + j; int i1 = (i + 1) * G.cols + j;
                    const Vect3d& p0 = G.pos[i0]; glVertex3f(p0.x(), p0.y(), p0.z());
                    const Vect3d& p1 = G.pos[i1]; glVertex3f(p1.x(), p1.y(), p1.z());
                }
                glEnd();
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glLineWidth(1.0f);
        }
    }
}


static void drawCtrlViz() {
    glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);

    if (gShowCtrlPoly) {
        glLineWidth(2.f); glColor3f(1.f, 0.45f, 0.05f);
        for (const auto& G : gLeafGeoms) {
            glBegin(GL_LINE_STRIP);
            for (const auto& p : G.ctrl) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
        }
    }
    if (gShowCtrlPts) {
        glPointSize(gCtrlPtSize); glColor3f(0.95f, 0.2f, 0.1f);
        glBegin(GL_POINTS);
        for (const auto& G : gLeafGeoms)
            for (const auto& p : G.ctrl) glVertex3f(p.x(), p.y(), p.z());
        glEnd();
    }
    if (gShowCtrlLabel) {
        glColor3f(0.05f, 0.05f, 0.05f);
        for (const auto& G : gLeafGeoms) {
            for (size_t i = 0; i < G.ctrl.size(); ++i) {
                const Vect3d& p = G.ctrl[i]; glRasterPos3f(p.x(), p.y(), p.z());
                char buf[8]; std::snprintf(buf, sizeof(buf), "C%zu", i);
                for (const char* c = buf; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
            }
        }
    }
    glEnable(GL_DEPTH_TEST);
}
static void setupLighting() {
    if (!gLightingOn) { glDisable(GL_LIGHTING); return; }

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, gLightPos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, gLightDiff);
    glLightfv(GL_LIGHT0, GL_SPECULAR, gLightSpec);

    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gGlobalAmb);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, gTwoSided ? 1 : 0);

    // Materials: use glColor for ambient+diffuse; set specular/shininess explicitly
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, gMatSpec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, gShininess);

    // Normals may be non-unit after transforms; be safe:
    glEnable(GL_NORMALIZE);

    // Smooth shading
    glShadeModel(GL_SMOOTH);
}

/* ------------------------- GLUT / ImGui -------------------------- */
static void display() {
    // ---- ImGui begin ----
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGLUT_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse && io.MouseDown[0]) { gYaw += io.MouseDelta.x * 0.3f; gPitch += io.MouseDelta.y * 0.3f; gPitch = clampf(gPitch, -89.f, 89.f); }
    if (!io.WantCaptureMouse && io.MouseWheel != 0.f) { gDist = clampf(gDist * (io.MouseWheel > 0.f ? 0.90f : 1.10f), 0.3f, 50.f); }
    if (!io.WantCaptureMouse && io.MouseDown[1]) {
        const float PI = 3.1415926535f;
        float cy = std::cos(gYaw * PI / 180.f), sy = std::sin(gYaw * PI / 180.f);
        float cp = std::cos(gPitch * PI / 180.f), sp = std::sin(gPitch * PI / 180.f);
        Vect3d dir = { cp * sy, sp, cp * cy };
        Vect3d fwd = (dir * -1.f).GetNormalized();
        Vect3d right = (fwd.Cross({ 0.f,1.f,0.f })).GetNormalized();
        Vect3d up = (right.Cross(fwd)).GetNormalized();
        float panScale = gDist * 0.0025f;
        gCamTarget = gCamTarget - right * (io.MouseDelta.x * panScale) + up * (io.MouseDelta.y * panScale);
    }

    glClearColor(0.98f, 0.98f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    int ww = glutGet(GLUT_WINDOW_WIDTH), hh = glutGet(GLUT_WINDOW_HEIGHT);
    float aspect = (hh > 0) ? float(ww) / float(hh) : 1.f;
    gluPerspective(45.0, aspect, 0.01, 200.0);

    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    float cy = std::cos(gYaw * float(M_PI) / 180.f), sy = std::sin(gYaw * float(M_PI) / 180.f);
    float cp = std::cos(gPitch * float(M_PI) / 180.f), sp = std::sin(gPitch * float(M_PI) / 180.f);
    Vect3d eyeOff = { gDist * cp * sy, gDist * sp, gDist * cp * cy };
    Vect3d eye = gCamTarget + eyeOff;
    gluLookAt(eye.x(), eye.y(), eye.z(), gCamTarget.x(), gCamTarget.y(), gCamTarget.z(), 0, 1, 0);

    setupLighting();

    glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1, 1, 1, 1);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glLineWidth(1.0f);
    glDisable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST);

    if (!gLeafTex || !gTexOK) loadLeafTexture(gLeafTexPath);
    drawStemLayers();
    drawLeaves();

    glDisable(GL_LIGHTING);
    drawCtrlViz();
    drawGridXZ();

    // ---- UI ----
    ImGui::Begin("Maize - Concentric Stem & Grid Leaves");

    static bool needReload = false, needRebuild = false;

    // In display() UI, where you have XML load controls:
    ImGui::Text("XML: %s", gXmlPath);
    ImGui::InputText("Path", gXmlPath, IM_ARRAYSIZE(gXmlPath)); ImGui::SameLine();
    if (ImGui::Button("Load XML")) {
        if (loadPlantXML(gXmlPath, gPlant)) { rebuildFlatKeys(); rebuildGeometry(); gLoadOK = true; gLoadFail = false; }
        else { gLoadFail = true; gLoadOK = false; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save XML")) {
        if (savePlantXMLWithEdits(gXmlPath)) { gSaveOK = true; gSaveFail = false; }
        else { gSaveFail = true; gSaveOK = false; }
    }

    // Tiny notifications
    if (gLoadOK) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0, 0.6f, 0, 1), "Loaded."); }
    if (gLoadFail) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.8f, 0.2f, 0, 1), "Load failed."); }
    if (gSaveOK) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0, 0.6f, 0, 1), "Saved."); }
    if (gSaveFail) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.8f, 0.2f, 0, 1), "Save failed."); }

    ImGui::SeparatorText("Leaf (global)");
    needRebuild |= ImGui::SliderFloat("Length (m)", &gLeafLength, 0.2f, 2.0f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Max width (m)", &gLeafWmax, 0.02f, 0.40f, "%.3f");
    needRebuild |= ImGui::SliderInt("Samples (min)", &gSamplesMin, 8, 200);
    ImGui::Checkbox("Droop", &gDroop); ImGui::SameLine(); needRebuild |= ImGui::SliderFloat("Max droop (m)", &gDroopMax, 0.0f, 0.25f, "%.3f");
    ImGui::Checkbox("Twist", &gTwistOn);
    if (gTwistOn) { needRebuild |= ImGui::SliderFloat("Twist max (deg)", &gTwistMaxD, 0.0f, 90.0f, "%.1f"); needRebuild |= ImGui::SliderFloat("Twist power", &gTwistPow, 0.5f, 3.0f, "%.2f"); }
    needRebuild |= ImGui::SliderFloat("Roll offset (deg)", &gRollOffsetD, -180.f, 180.f, "%.1f");

    ImGui::SeparatorText("Waviness");
    needRebuild |= ImGui::SliderFloat("Amplitude (m)", &gWaveAmp, 0.0000f, 0.0200f, "%.4f");
    needRebuild |= ImGui::SliderFloat("Cycles (base→tip)", &gWaveCycles, 0.0f, 20.0f, "%.1f");
    needRebuild |= ImGui::SliderFloat("Across factor", &gWaveAcross, 0.0f, 1.0f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Fade-in frac", &gWaveFadeIn, 0.0f, 0.40f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Fade-out frac", &gWaveFadeOut, 0.0f, 0.40f, "%.2f");

    ImGui::SeparatorText("Width profile");
    needRebuild |= ImGui::SliderFloat("Knee s", &gWidthKnee, 0.40f, 0.90f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Width at knee (x)", &gWidthAtKnee, 0.05f, 0.95f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Early pow", &gEarlyPow, 0.2f, 3.0f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Tip pow", &gTipPow, 0.2f, 3.0f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Tip rel width", &gTipRelWidth, 0.0f, 0.40f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Base ramp frac", &gBaseRampFrac, 0.02f, 0.25f, "%.3f");

    ImGui::SeparatorText("Attachment");
    needRebuild |= ImGui::SliderFloat("Attach frac", &gAttachFrac, 0.0f, 0.40f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Attach epsilon (m)", &gAttachEpsilon, 0.0f, 0.01f, "%.4f");

    ImGui::SeparatorText("Unfold along length");
    needRebuild |= ImGui::SliderFloat("Lip length frac", &gLipLenFrac, 0.02f, 0.20f, "%.3f");
    needRebuild |= ImGui::SliderInt("Leaf columns (odd)", &gLipCols, 7, 65);
    needRebuild |= ImGui::SliderFloat("Arc at base (deg)", &gLipArc0Deg, 90.f, 359.f, "%.0f");
    needRebuild |= ImGui::SliderFloat("Arc at end (deg)", &gLipArc1Deg, 0.f, 180.f, "%.0f");
    needRebuild |= ImGui::SliderFloat("Collar hold frac", &gCollarHoldFrac, 0.0f, 0.9f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Unwrap mid", &gUnwrapMidFrac, 0.05f, 0.95f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Unwrap sharpness", &gUnwrapSharpness, 0.1f, 6.0f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Ring drop (m)", &gRingDrop, 0.0f, 0.05f, "%.3f");  // <— NEW

    ImGui::SeparatorText("Stem layers");
    needRebuild |= ImGui::SliderFloat("Layer shrink (m)", &gStemLayerShrink, 0.0002f, 0.0300f, "%.4f");
    needRebuild |= ImGui::SliderInt("Sides", &gStemSides, 8, 128);
    ImGui::Checkbox("Stem as wireframe", &gStemAsWire);
    ImGui::Checkbox("Override outermost radius", &gOverrideBaseRadius); ImGui::SameLine();
    needRebuild |= ImGui::SliderFloat("Outermost radius (m)", &gOutermostRadius, 0.0010f, 0.1500f, "%.4f");

    ImGui::SeparatorText("Display");
    ImGui::Checkbox("Wireframe (leaves)", &gWireframe); ImGui::SameLine(); ImGui::SliderFloat("Wire width", &gWireWidth, 1.0f, 5.0f, "%.1f");
    ImGui::Checkbox("Use leaf texture", &gUseLeafTex);
    ImGui::Checkbox("Selected leaf solid (override wireframe)", &gSelectedSolid);
    ImGui::InputText("Leaf tex path", gLeafTexPath, IM_ARRAYSIZE(gLeafTexPath));

    ImGui::SeparatorText("Lighting");
    ImGui::Checkbox("Enable lighting", &gLightingOn);
    ImGui::Checkbox("Two-sided lighting", &gTwoSided);
    ImGui::DragFloat4("Light position (x,y,z,w)", gLightPos, 0.05f);
    ImGui::DragFloat("Shininess", &gShininess, 0.5f, 1.0f, 128.0f, "%.1f");
    ImGui::ColorEdit3("Global ambient", gGlobalAmb);
    ImGui::ColorEdit3("Light diffuse", gLightDiff);
    ImGui::ColorEdit3("Light specular", gLightSpec);
    ImGui::ColorEdit3("Material specular", gMatSpec);


    ImGui::SeparatorText("Leaf Editor (per-leaf)");
    if (!gFlatKeys.empty()) {
        ImGui::PushItemWidth(220);
        ImGui::Combo("Leaf", &gSelectedFlat, [](void* data, int idx, const char** out_text) {
            auto& vec = *static_cast<std::vector<std::string>*>(data);
            if (idx < 0 || idx >= (int)vec.size()) return false; *out_text = vec[idx].c_str(); return true; }, &gFlatNames, (int)gFlatNames.size());
        ImGui::PopItemWidth();
        ImGui::Checkbox("Hide selected", &gHideSelected); ImGui::SameLine(); ImGui::Checkbox("Solo selected", &gSoloSelected);

        LeafKey key = gFlatKeys[gSelectedFlat]; auto& edit = gEdits[key];
        if (ImGui::Button(edit.hasCustom ? "Reset to generated" : "Make editable")) {
            if (edit.hasCustom) { edit.hasCustom = false; }
            else {
                if (gSelectedFlat < (int)gLeafGeoms.size()) {
                    edit.customCtrl = gLeafGeoms[gSelectedFlat].ctrl;
                    if (edit.customCtrl.size() < 4) edit.customCtrl = { {0,0,0},{0.3f,0,0},{0.6f,0,0},{1,0,0} };
                    edit.hasCustom = true;
                }
            }
            needRebuild = true;
        }
        ImGui::SameLine(); ImGui::Checkbox("Pin C0 to stem", &edit.pinC0ToStem);
        if (edit.hasCustom) {
            if (ImGui::Button("Add point")) { if (!edit.customCtrl.empty()) { Vect3d last = edit.customCtrl.back(); edit.customCtrl.push_back(last + Vect3d{ 0.2f,0,0 }); } else { edit.customCtrl = { {0,0,0},{0.3f,0,0},{0.6f,0,0},{1,0,0} }; } needRebuild = true; }
            ImGui::SameLine(); if (ImGui::Button("Remove last") && edit.customCtrl.size() > 4) { edit.customCtrl.pop_back(); needRebuild = true; }
            for (size_t i = 0; i < edit.customCtrl.size(); ++i) { float xyz[3] = { edit.customCtrl[i].x(), edit.customCtrl[i].y(), edit.customCtrl[i].z() }; char label[32]; snprintf(label, sizeof(label), "C%zu", i); if (ImGui::DragFloat3(label, xyz, 0.005f)) { edit.customCtrl[i] = { xyz[0],xyz[1],xyz[2] }; needRebuild = true; } }
        }
        else {
            ImGui::TextDisabled("Click Make editable to tweak C0..Cn of this leaf.");
        }
    }
    else { ImGui::TextDisabled("No leaves found in the XML."); }

    if (needRebuild) { rebuildGeometry(); needRebuild = false; }

    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    glutSwapBuffers();
}

static void idle() { glutPostRedisplay(); }

/* ----------------------------- main ------------------------------ */
int main(int argc, char** argv) {
    // initial load if file exists
    loadPlantXML(gXmlPath, gPlant);
    rebuildFlatKeys();
    rebuildGeometry();

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(gWinW, gWinH);
    glutCreateWindow("Maize – Concentric Stem & Grid Leaves");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGLUT_Init();
    ImGui_ImplOpenGL2_Init();
    ImGui_ImplGLUT_InstallFuncs();

    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutMainLoop();

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGLUT_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
