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

#define M_PI 3.14159265358979323846f

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

/* ------------------------------ math ------------------------------ */
struct Vec3 { float x, y, z; };
static inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x,a.y + b.y,a.z + b.z }; }
static inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x,a.y - b.y,a.z - b.z }; }
static inline Vec3 operator*(Vec3 a, float s) { return { a.x * s,a.y * s,a.z * s }; }
static inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static inline float len(Vec3 a) { return std::sqrt(std::max(0.f, dot(a, a))); }
static inline Vec3  norm(Vec3 a) { float L = len(a); return (L > 1e-8f) ? a * (1.f / L) : Vec3{ 1,0,0 }; }
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

static inline Vec3 projectOntoPlane(Vec3 v, Vec3 n_unit) { return v - n_unit * dot(v, n_unit); }

// Find θ such that rotateAroundAxis(B, T, θ) aligns with target (radial r).
static float autoRollTheta(Vec3 B, Vec3 T, Vec3 target) { float x = dot(B, target); float y = dot(cross(T, B), target); return std::atan2(y, x); }
static inline float radiansf(float d) { return d * 3.14159265358979323846f / 180.f; }
static inline float smooth01(float x) {x = clampf(x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline Vec3  lerp3(const Vec3& a, const Vec3& b, float t) { return { lerpf(a.x,b.x,t), lerpf(a.y,b.y,t), lerpf(a.z,b.z,t) }; }
static inline Vec3  add3(const Vec3& a, const Vec3& b) { return { a.x + b.x,a.y + b.y,a.z + b.z }; }
static inline Vec3  sub3(const Vec3& a, const Vec3& b) { return { a.x - b.x,a.y - b.y,a.z - b.z }; }
static inline Vec3  mul3(const Vec3& a, float s) { return { a.x * s,a.y * s,a.z * s }; }
static inline Vec3  cross3(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static inline float dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3  norm3(const Vec3& v) { float L = sqrtf(dot3(v, v)) + 1e-20f; return { v.x / L,v.y / L,v.z / L }; }

// --- Texture state ---
static GLuint gLeafTex = 0;
static bool   gUseLeafTex = true;
static char   gLeafTexPath[256] = "maize_leaf.png";
static float  gTexURepeat = 1.0f;
static float  gTexVRepeat = 1.0f;
static bool   gTexOK = false, gTexFail = false;

// ImGui Combo getter for std::vector<std::string>
static bool VectorGetter(void* data, int idx, const char** out_text) {
    auto& vec = *static_cast<std::vector<std::string>*>(data);
    if (idx < 0 || idx >= (int)vec.size()) return false;
    *out_text = vec[idx].c_str();
    return true;
}

static Vec3 rotateAroundAxis(Vec3 v, Vec3 axis_unit, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis_unit, v) * s + axis_unit * (dot(axis_unit, v) * (1.f - c));
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
static Vec3 deBoor(float u, const std::vector<Vec3>& P, const std::vector<float>& U, int p = 3) {
    int n = (int)P.size() - 1; if (n < p) return P.empty() ? Vec3{ 0,0,0 } : P.back();
    int i = findSpan(n, p, u, U);
    Vec3 d[4]; for (int j = 0; j <= p; ++j) d[j] = P[i - p + j];
    for (int r = 1; r <= p; ++r)
        for (int j = p; j >= r; --j) {
            float denom = U[i + 1 + j - r] - U[i - p + j];
            float a = (denom > 1e-12f) ? (u - U[i - p + j]) / denom : 0.f;
            d[j] = d[j - 1] * (1.f - a) + d[j] * a;
        }
    return d[p];
}

// arc-length resample curve defined by ctrl (world space)
static std::vector<Vec3> resampleMidrib(const std::vector<Vec3>& ctrl, int N) {
    const int p = 3; auto U = openUniformKnots((int)ctrl.size(), p);
    const int dense = std::max(512, 4 * N);
    std::vector<Vec3> pts; pts.reserve(dense);
    float total = 0.f; Vec3 prev{};
    for (int k = 0; k < dense; ++k) {
        float u = (float)k / (dense - 1);
        Vec3 C = deBoor(u, ctrl, U, p);
        if (k > 0) total += len(C - prev);
        pts.push_back(C); prev = C;
    }
    auto pointAtS = [&](float s)->Vec3 {
        float target = s * total, acc = 0.f;
        for (int k = 1; k < dense; ++k) {
            Vec3 d = pts[k] - pts[k - 1]; float seg = len(d);
            if (acc + seg >= target) { float a = (seg > 1e-8f) ? (target - acc) / seg : 0.f; return pts[k - 1] * (1 - a) + pts[k] * a; }
            acc += seg;
        }
        return pts.back();
        };
    std::vector<Vec3> mid(N);
    for (int i = 0; i < N; ++i) { float s = (N == 1) ? 0.f : float(i) / (N - 1); mid[i] = pointAtS(s); }
    return mid;
}

// Sample midrib & frames at a fractional arc-length position s in [0,1]
static inline void sampleMidFrame(float s,
    const std::vector<Vec3>& mid,
    const std::vector<Vec3>& T,
    const std::vector<Vec3>& N2,
    const std::vector<Vec3>& B2,
    Vec3& C, Vec3& Tout, Vec3& Nout, Vec3& Bout)
{
    int n = (int)mid.size();
    if (n <= 1) { C = mid.empty() ? Vec3{ 0,0,0 } : mid[0]; Tout = { 1,0,0 }; Nout = { 0,1,0 }; Bout = { 0,0,1 }; return; }

    s = clampf(s, 0.f, 1.f);
    float f = s * (float)(n - 1);
    int   i0 = (int)std::floor(f);
    int   i1 = std::min(i0 + 1, n - 1);
    float a = f - (float)i0;

    C = lerp3(mid[i0], mid[i1], a);
    Tout = norm(lerp3(T[i0], T[i1], a));
    Nout = norm(lerp3(N2[i0], N2[i1], a));
    Bout = norm(lerp3(B2[i0], B2[i1], a));
}


// parallel-transport frames along polyline mid[]
static void buildPTFrames(const std::vector<Vec3>& mid,
    std::vector<Vec3>& T, std::vector<Vec3>& N, std::vector<Vec3>& B) {
    int n = (int)mid.size(); T.resize(n); N.resize(n); B.resize(n);
    for (int i = 0; i < n; ++i) {
        Vec3 Tl = (i > 0) ? mid[i] - mid[i - 1] : mid[i + 1] - mid[i];
        Vec3 Tr = (i < n - 1) ? mid[i + 1] - mid[i] : mid[i] - mid[i - 1];
        T[i] = norm(Tl + Tr);
    }
    Vec3 up = { 0,1,0 }; if (len(cross(T[0], up)) < 1e-4f) up = { 1,0,0 };
    B[0] = norm(cross(T[0], up)); N[0] = norm(cross(B[0], T[0]));
    for (int i = 1; i < n; ++i) {
        Vec3 a = cross(T[i - 1], T[i]); float L = len(a);
        if (L > 1e-8f) {
            a = a * (1.f / L); float c = clampf(dot(T[i - 1], T[i]), -1.f, 1.f);
            float ang = std::acos(c);
            N[i] = rotateAroundAxis(N[i - 1], a, ang);
            B[i] = rotateAroundAxis(B[i - 1], a, ang);
        }
        else { N[i] = N[i - 1]; B[i] = B[i - 1]; }
    }
    for (int i = 0; i < n; ++i) { B[i] = norm(cross(T[i], N[i])); N[i] = norm(cross(B[i], T[i])); }
}

// Single, shared surface radius for ring + lip + blade attach
static inline float sheathSurfaceRadius(float stemR,
    float attachEps) {
    float pad = attachEps;
    pad = std::max(pad, 0.0005f);   // keep off the stem to avoid z-fighting
    return stemR + pad;
}

static inline float widthParabola(float s, float wMax) { return wMax * 4.f * s * (1.f - s); }

static inline float hash01(int x) {
    // Thomas Wang integer hash -> [0,1)
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    unsigned int u = static_cast<unsigned int>(x);
    return (u & 0x7fffffff) / float(0x7fffffff);
}

static inline float smoothstepf(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

/* ----------------------------- data ------------------------------ */
struct LeafDesc {
    int   id = 0;        // id from XML
    float distance = 0;  // internode length (used cumulatively)
    float alphaDeg = 30; // pitch
    int   spines = 10;  // suggested samples
    float azimuthDeg = 0;// computed from tiller.alpha + id * tiller.beta
};
struct TillerDesc {
    std::string type = "main";
    float radius = 0.02f;
    float alphaDeg = 0.f; // base azimuth
    float betaDeg = 180.f;
    std::vector<LeafDesc> leaves;
};
struct PlantDesc {
    std::string species = "maize";
    std::vector<TillerDesc> tillers;
};
struct Vec2 { float u, v; };

// --- Sheath (leaf collar) ---
struct SheathGeom {
    std::vector<Vec3> band;   // cylinder band (one triangle strip)
    std::vector<Vec3> blend;  // transition from cylinder band to leaf cross-section (one strip)
};
static std::vector<SheathGeom> gSheathGeoms;

// ---- Curved sheath lip (grid) ----
struct SheathLipGeom {
    int cols = 0, rows = 0;                // grid size
    std::vector<Vec3> pos;                 // rows*cols
    std::vector<Vec2> uv;                  // rows*cols
};
static std::vector<SheathLipGeom> gSheathLips;

// Controls for the curved lip
static float gLipLenFrac = 0.08f;    // fraction of leaf length for the curved lip (0.03–0.12 works well)
static int   gLipCols = 10;       // columns across width (>=6)
static float gLipArc0Deg = 340.f;    // arc at the very base (near ring, <360 to avoid seam)
static float gLipArc1Deg = 60.f;     // arc at the end of the lip section
static float gLipLift = 0.0015f;  // push off stem to avoid z-fighting
static float gLipFeather = 1.0f;     // 1=linear unfold; >1 slower start; <1 faster start

// --- Sheath base ring (thin cylinder at leaf base) ---
static float gSheathRingHeight = 0.015f;  // meters tall
static int   gSheathRingSides = 48;      // tessellation (>=8)


// forward decls so loadParamsXML can call them
static void rebuildGeometry();
static void rebuildFlatKeys();

/* -------------- globals (camera, UI, generation) ----------------- */
static int   gWinW = 1280, gWinH = 860;
static float gYaw = 30.f, gPitch = 18.f, gDist = 3.5f;
static Vec3 gCamTarget = { 0.5f, 0.25f, 0.0f }; // center of interest

static char gXmlPath[256] = "maizeDebug.xml";
static PlantDesc   gPlant;

static float gLeafLength = 1.00f;
static float gLeafWmax = 0.18f;
static int   gSamplesMin = 3;
static bool  gDroop = true;
static float gDroopMax = 0.06f;
static bool  gTwistOn = true;
static float gTwistMaxD = 20.f;
static float gTwistPow = 1.4f;
static float gRollOffsetD = 180.0f; // rotate blade +90° about tangent to face "up"

static bool  gShowCtrlPts = true;
static bool  gShowCtrlPoly = true;
static bool  gShowCtrlLabel = true;
static float gCtrlPtSize = 10.f;
static bool  gWireframe = false;
static float gWireWidth = 1.5f;
static bool  gWiggleOn = true;       // enable/disable animated wiggle
static float gWiggleAmt = 0.002f; // random noise to add to ctrl pts
static float gWiggleWaves = 2.5f;    // number of ripples along the leaf length
static bool gShowSelectedLeaf = false;

static float gAttachFrac = 0.10f;  // 0..1 length of leaf to conform to stem
static float gAttachFeather = 0.04f;  // softness of the blend
static float gAttachEpsilon = 0.0015f;// push outward to avoid z-fighting
static bool  gBaseTangentProject = true; // see section C, optional

static char gParamPath[256] = "leaf_params.xml";
static bool gSaveOK = false, gSaveFail = false, gLoadOK = false, gLoadFail = false;

static bool  gAutoRollTangency = true;


// --- texture + stem color matching ---
static GLuint gLeafTexID = 0;
static int    gLeafTexW = 0, gLeafTexH = 0;
static std::vector<unsigned char> gLeafTexRGBA; // keep a CPU copy (RGBA8)

static bool  gStemMatchTexture = true;
static float gStemBrightness = 0.95f; // 0.0..2.0 scale
static Vec3  gStemColor = { 0.15f, 0.45f, 0.15f }; // fallback if no texture

/* --------- editable control points (persistent per leaf) --------- */
struct LeafKey {
    int tIdx; int lIdx; bool operator<(const LeafKey& o) const {
        return (tIdx < o.tIdx) || (tIdx == o.tIdx && lIdx < o.lIdx);
    }
};
struct LeafEdit {
    bool hasCustom = false;
    bool pinC0ToStem = true;          // keep C0 stuck to stem base position
    std::vector<Vec3> customCtrl;   // world-space control points
};
static std::map<LeafKey, LeafEdit> gEdits;

// flat list for UI selection
static std::vector<LeafKey> gFlatKeys;
static std::vector<std::string> gFlatNames;
static int gSelectedFlat = 0;

static void rebuildFlatKeys() {
    gFlatKeys.clear(); gFlatNames.clear();
    for (size_t ti = 0; ti < gPlant.tillers.size(); ++ti) {
        const auto& td = gPlant.tillers[ti];
        for (size_t li = 0; li < td.leaves.size(); ++li) {
            gFlatKeys.push_back({ (int)ti,(int)li });
            char buf[64]; std::snprintf(buf, sizeof(buf), "T%d leaf id=%d", int(ti), td.leaves[li].id);
            gFlatNames.emplace_back(buf);
        }
    }
    gSelectedFlat = std::min(gSelectedFlat, (int)gFlatKeys.size() - 1);
}

static const char* flatNameCStr(int i) { return gFlatNames[i].c_str(); }

/* --------------------- generated geometry store ------------------- */
struct LeafGeom {
    std::vector<Vec3> strip; // [L0,R0,L1,R1,...]
	std::vector<Vec2> uv;  // per-vertex normals
    std::vector<Vec3> ctrl;  // ctrl used for this leaf
};
static std::vector<LeafGeom> gLeafGeoms;

/* -------- default generator for ctrl (seed for editing) ---------- */
static std::vector<Vec3> makeLeafCtrl(Vec3 base, Vec3 stemUp, Vec3 dir, float L) {
    Vec3 p0 = base;
    Vec3 p1 = base + dir * (0.30f * L) + stemUp * (0.10f * L);
    Vec3 p2 = base + dir * (0.65f * L) + stemUp * (0.05f * L);
    Vec3 p3 = base + dir * (0.95f * L) + stemUp * (-0.05f * L);
    Vec3 p4 = base + dir * (1.10f * L) + stemUp * (-0.12f * L);
    return { p0,p1,p2,p3,p4 };
}


// when params are loaded, refresh lists + meshes
#define CALL_REBUILD() do { rebuildFlatKeys(); rebuildGeometry(); } while(0)

static bool saveParamsXML(const char* path) {
    XMLDocument doc;
    XMLElement* root = doc.NewElement("leaf_params");
	root->SetAttribute("source_xml", gXmlPath);
    doc.InsertFirstChild(root);

    // Globals
    XMLElement* globals = doc.NewElement("globals");
    globals->SetAttribute("length_m", gLeafLength);
    globals->SetAttribute("max_width_m", gLeafWmax);
    globals->SetAttribute("samples_min", gSamplesMin);
    globals->SetAttribute("droop_on", (int)gDroop);
    globals->SetAttribute("droop_max_m", gDroopMax);
    globals->SetAttribute("twist_on", (int)gTwistOn);
    globals->SetAttribute("twist_max_deg", gTwistMaxD);
    globals->SetAttribute("twist_pow", gTwistPow);
    globals->SetAttribute("roll_deg", gRollOffsetD);
    globals->SetAttribute("wiggle_on", (int)gWiggleOn);
    globals->SetAttribute("wiggle_amount_m", gWiggleAmt);
	globals->SetAttribute("wiggle_waves", gWiggleWaves);
    root->InsertEndChild(globals);

    // Per-leaf edits
    XMLElement* edits = doc.NewElement("edits");
    for (const auto& kv : gEdits) {
        const LeafKey& key = kv.first;
        const LeafEdit& ed = kv.second;
        if (!ed.hasCustom || ed.customCtrl.size() < 4) continue;

        XMLElement* leaf = doc.NewElement("leaf");
        leaf->SetAttribute("tIdx", key.tIdx);
        leaf->SetAttribute("lIdx", key.lIdx);
        leaf->SetAttribute("pinC0ToStem", (int)ed.pinC0ToStem);

        for (const Vec3& p : ed.customCtrl) {
            XMLElement* cp = doc.NewElement("cp");
            cp->SetAttribute("x", p.x);
            cp->SetAttribute("y", p.y);
            cp->SetAttribute("z", p.z);
            leaf->InsertEndChild(cp);
        }
        edits->InsertEndChild(leaf);
    }
    root->InsertEndChild(edits);

    return (doc.SaveFile(path) == XML_SUCCESS);
}

static bool loadParamsXML(const char* path) {
    XMLDocument doc;
    if (doc.LoadFile(path) != XML_SUCCESS) return false;

    const XMLElement* root = doc.FirstChildElement("leaf_params");
    if (!root) return false;

    // Globals
    if (const XMLElement* g = root->FirstChildElement("globals")) {
        g->QueryFloatAttribute("length_m", &gLeafLength);
        g->QueryFloatAttribute("max_width_m", &gLeafWmax);
        g->QueryIntAttribute("samples_min", &gSamplesMin);
        { int v = 0; if (g->QueryIntAttribute("droop_on", &v) == XML_SUCCESS) gDroop = (v != 0); }
        g->QueryFloatAttribute("droop_max_m", &gDroopMax);
        { int v = 0; if (g->QueryIntAttribute("twist_on", &v) == XML_SUCCESS) gTwistOn = (v != 0); }
        g->QueryFloatAttribute("twist_max_deg", &gTwistMaxD);
        g->QueryFloatAttribute("twist_pow", &gTwistPow);
        g->QueryFloatAttribute("roll_deg", &gRollOffsetD);
        { int v = 0; if (g->QueryIntAttribute("wiggle_on", &v) == XML_SUCCESS) gWiggleOn = (v != 0); }
        g->QueryFloatAttribute("wiggle_amount_m", &gWiggleAmt);
		g->QueryFloatAttribute("wiggle_waves", &gWiggleWaves);
    }

    // Per-leaf edits
    gEdits.clear();
    if (const XMLElement* edits = root->FirstChildElement("edits")) {
        for (const XMLElement* leaf = edits->FirstChildElement("leaf");
            leaf; leaf = leaf->NextSiblingElement("leaf")) {
            int tIdx = 0, lIdx = 0, pin = 1;
            leaf->QueryIntAttribute("tIdx", &tIdx);
            leaf->QueryIntAttribute("lIdx", &lIdx);
            leaf->QueryIntAttribute("pinC0ToStem", &pin);

            LeafEdit ed;
            ed.hasCustom = true;
            ed.pinC0ToStem = (pin != 0);
            ed.customCtrl.clear();

            for (const XMLElement* cp = leaf->FirstChildElement("cp");
                cp; cp = cp->NextSiblingElement("cp")) {
                Vec3 p{};
                cp->QueryFloatAttribute("x", &p.x);
                cp->QueryFloatAttribute("y", &p.y);
                cp->QueryFloatAttribute("z", &p.z);
                ed.customCtrl.push_back(p);
            }
            if (ed.customCtrl.size() >= 4) {
                gEdits[{tIdx, lIdx}] = std::move(ed);
            }
        }
    }

    // Let your app rebuild meshes/UI after loading
    CALL_REBUILD();
    return true;
}
static bool loadPlantXML(const std::string& path, PlantDesc& out) {
    XMLDocument doc; if (doc.LoadFile(path.c_str()) != XML_SUCCESS) return false;
    const XMLElement* species = doc.FirstChildElement("plant")->FirstChildElement("species");
    if (!species) return false;
    if (const char* sp = species->Attribute("name")) out.species = sp; else out.species = "unknown";
    out.tillers.clear();
    for (const XMLElement* t = species->FirstChildElement("tiller"); t; t = t->NextSiblingElement("tiller")) {
        TillerDesc td;
        if (const char* tt = t->Attribute("type")) td.type = tt;
        t->QueryFloatAttribute("radius", &td.radius);
        t->QueryFloatAttribute("alpha", &td.alphaDeg);
        t->QueryFloatAttribute("beta", &td.betaDeg);
        if (const XMLElement* leaves = t->FirstChildElement("leaves")) {
            for (const XMLElement* lf = leaves->FirstChildElement("leaf"); lf; lf = lf->NextSiblingElement("leaf")) {
                LeafDesc L; lf->QueryIntAttribute("id", &L.id);
                lf->QueryFloatAttribute("distance", &L.distance);
                lf->QueryFloatAttribute("alpha", &L.alphaDeg);
                lf->QueryIntAttribute("spines", &L.spines);
                L.azimuthDeg = td.alphaDeg + L.id * td.betaDeg;
                td.leaves.push_back(L);
            }
        }
        out.tillers.push_back(td);
    }
    return true;
}

static Vec3 computeStemColorFromTexture(const unsigned char* rgba, int w, int h) {
    // sample a central strip to avoid dark edges/alpha; ignore very transparent pixels
    const float u0 = 0.20f, u1 = 0.80f;   // 40–60% across width
    const float v0 = 0.08f, v1 = 0.80f;   // avoid very base/tip
    int x0 = int(u0 * w), x1 = int(u1 * w);
    int y0 = int(v0 * h), y1 = int(v1 * h);

    double r = 0, g = 0, b = 0; int cnt = 0;
    for (int y = y0; y < y1; ++y) {
        const unsigned char* row = rgba + (size_t)y * w * 4;
        for (int x = x0; x < x1; ++x) {
            const unsigned char* p = row + x * 4;
            if (p[3] < 16) continue; // skip near-transparent
            r += p[0]; g += p[1]; b += p[2]; ++cnt;
        }
    }
    if (cnt == 0) return { 0.15f, 0.45f, 0.15f };
    float rf = float(r / cnt) / 255.f;
    float gf = float(g / cnt) / 255.f;
    float bf = float(b / cnt) / 255.f;
    return { rf, gf, bf };
}

static bool loadLeafTexture(const char* path) {
    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load(path, &w, &h, &n, 4); // force RGBA
    if (!data) return false;
    gLeafTexW = w; gLeafTexH = h;
    gLeafTexRGBA.assign(data, data + size_t(w * h * 4));

    if (gLeafTex == 0) glGenTextures(1, &gLeafTex);
    glBindTexture(GL_TEXTURE_2D, gLeafTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Build mipmaps (keeps things simple for legacy fixed-function)
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
	gStemColor = computeStemColorFromTexture(gLeafTexRGBA.data(), w, h);
    
    return true;
}

/* ----------------------- mesh rebuild (core) ---------------------- */
static void rebuildGeometry() {
    gLeafGeoms.clear();
    gSheathGeoms.clear();   // rings
    gSheathLips.clear();    // curved lips

    const Vec3 stemAxis = { 0,1,0 };
    const bool doWiggle = gWiggleOn && gWiggleAmt > 0.f;
    const float twoPi = 2.f * float(M_PI);

    for (size_t ti = 0; ti < gPlant.tillers.size(); ++ti) {
        const auto& td = gPlant.tillers[ti];

        // cumulative heights
        std::vector<float> yAt; yAt.reserve(td.leaves.size());
        float accY = 0.f;
        for (const auto& L : td.leaves) { accY += std::max(0.f, L.distance); yAt.push_back(accY); }

        for (size_t li = 0; li < td.leaves.size(); ++li) {
            const auto& L = td.leaves[li];
            float phiDeg = L.azimuthDeg;
            float phi = phiDeg * float(M_PI / 180.0f);
            float pitch = L.alphaDeg * float(M_PI / 180.0f);

            Vec3 r = { std::cos(phi), 0.f, std::sin(phi) };
            Vec3 base = r * td.radius + Vec3{ 0.f, yAt[li], 0.f };
            Vec3 dir = norm(r * std::cos(pitch) + stemAxis * std::sin(pitch));

            // ctrl points
            LeafKey key{ (int)ti,(int)li };
            auto it = gEdits.find(key);
            std::vector<Vec3> ctrl =
                (it != gEdits.end() && it->second.hasCustom && it->second.customCtrl.size() >= 4)
                ? it->second.customCtrl : makeLeafCtrl(base, stemAxis, dir, gLeafLength);
            if (it != gEdits.end() && it->second.hasCustom && it->second.pinC0ToStem) ctrl[0] = base;

            int samples = std::max(L.spines, gSamplesMin);

            // midrib & frames
            auto mid = resampleMidrib(ctrl, samples);
            std::vector<Vec3> T, N, B; buildPTFrames(mid, T, N, B);

            // roll + twist
            std::vector<Vec3> N2 = N, B2 = B;
            float baseRoll = gRollOffsetD * (float)M_PI / 180.f;
            if (gAutoRollTangency) {
                float theta0 = autoRollTheta(B[0], T[0], r);
                baseRoll += theta0;
            }
            for (int i = 0; i < samples; ++i) {
                float s = (samples == 1) ? 0.f : (float)i / (samples - 1);
                float twist = gTwistOn ? (gTwistMaxD * (float)M_PI / 180.f) * std::pow(s, gTwistPow) : 0.f;
                float th = baseRoll + twist;
                N2[i] = rotateAroundAxis(N[i], T[i], th);
                B2[i] = rotateAroundAxis(B[i], T[i], th);
            }

            // ---------------- base ring (visual) ----------------
            SheathGeom ring;
            const int   sides = std::max(8, gSheathRingSides);
            const float y0 = base.y - gLipLift;            // ↓ extend down by LipLift
            const float y1 = base.y + gSheathRingHeight;   // top stays the seam
            float Rsurf = sheathSurfaceRadius(td.radius, gAttachEpsilon);
            for (int i = 0; i <= sides; ++i) {
                float a = (float)i / sides * 2.f * (float)M_PI;
                float cx = std::cos(a) * Rsurf, cz = std::sin(a) * Rsurf;
                ring.band.push_back({ cx, y0, cz });
                ring.band.push_back({ cx, y1, cz });
            }
            gSheathGeoms.push_back(std::move(ring));

            // ---------------- curved lip (grid) ----------------
            bool  haveLipSeam = false;
            Vec3  lipL_end{}, lipR_end{};
            float dphiLipEndRad = radiansf(gLipArc1Deg);

            int iStart = 0;
            if (gLipLenFrac > 0.f) {
                iStart = std::max(1, (int)std::ceil(gLipLenFrac * (samples - 1)));
                int lipRows = iStart + 1;

                const int cols = std::max(6, gLipCols);
                SheathLipGeom M; M.cols = cols; M.rows = lipRows;
                M.pos.resize((size_t)lipRows * cols);
                M.uv.resize((size_t)lipRows * cols);
                auto idxAt = [&](int i, int j) { return i * cols + j; };

                const float Rsurf = sheathSurfaceRadius(td.radius, gAttachEpsilon);
                const float yRingTop = base.y + gSheathRingHeight;
                const float sLipEnd = (samples == 1) ? 0.f : (float)iStart / (samples - 1); // end s of the lip along midrib

                for (int k = 0; k < lipRows; ++k) {
                    // ---- feathered row s along the midrib (you already have this) ----
                    float tRow = (lipRows == 1) ? 1.f : (float)k / (lipRows - 1);      // 0..1
                    float tFeather = std::pow(tRow, gLipFeather);
                    tFeather = clampf(tFeather, 0.f, 1.f);
                    float s01 = tFeather * sLipEnd;

                    // Sample mid/frames at fractional s01
                    Vec3 C, Trow, Nrow, Brow;
                    sampleMidFrame(s01, mid, T, N2, B2, C, Trow, Nrow, Brow);

                    // First lip row sits right on ring top
                    if (k == 0) C.y = yRingTop;

                    // Conform centerline toward cylinder near base
                    float tAttach = 0.f;
                    if (gAttachFrac > 0.f) {
                        float x = clampf(s01 / std::max(1e-4f, gAttachFrac), 0.f, 1.f);
                        tAttach = 1.f - (x * x * (3.f - 2.f * x));
                    }
                    Vec3 Ccyl = r * Rsurf + Vec3{ 0.f, C.y, 0.f };
                    C = C * (1.f - tAttach) + Ccyl * tAttach;

                    Vec3 azim = norm(cross(Vec3{ 0,1,0 }, r));
                    Vec3 Nblend = norm(Nrow * (1.f - tAttach) + azim * tAttach);

                    if (gDroop) C.y -= gDroopMax * s01 * s01;

                    float wLeaf = widthParabola(s01, gLeafWmax);
                    float baseRamp = smooth01(s01 / std::max(1e-4f, 0.08f));
                    float halfWLeaf = 0.5f * wLeaf * baseRamp;

                    // Unfold amount across rows
                    float b = tRow;                    // 0..1
                    b = b * b * (3.f - 2.f * b);      // smoothstep

                    // Arc this row uses while still hugging the cylinder
                    float dphi = radiansf(mixf(gLipArc0Deg, gLipArc1Deg, b));

                    // *** Anti-hourglass: blend width from cylinder chord → leaf width ***
                    float halfChord = Rsurf * std::sinf(0.5f * dphi);
                    float halfW = mixf(halfChord, halfWLeaf, b);

                    for (int j = 0; j < cols; ++j) {
                        float u = (cols == 1) ? 0.f : (float)j / (cols - 1);
                        float phj = phi + (u - 0.5f) * dphi;

                        // Cylinder point (keeps the nice wrap at b≈0)
                        Vec3 Pcyl = { std::cos(phj) * Rsurf, C.y, std::sin(phj) * Rsurf };

                        // Leaf point using the blended, non-pinching width
                        Vec3 Pleaf = C + Nblend * ((u - 0.5f) * 2.f * halfW);

                        // Interpolate cylinder → leaf
                        Vec3 P = lerp3(Pcyl, Pleaf, b);

                        int id = idxAt(k, j);
                        M.pos[id] = P;
                        M.uv[id] = { u * gTexURepeat, s01 * gTexVRepeat };
                    }
                }

                // capture seam endpoints from last row (match blade L/R)
                haveLipSeam = true;
                int last = (lipRows - 1) * M.cols;
                lipL_end = M.pos[last + (M.cols - 1)]; // u=1 -> +halfW -> blade's LEFT
                lipR_end = M.pos[last + 0];            // u=0 -> -halfW -> blade's RIGHT
                dphiLipEndRad = radiansf(gLipArc1Deg);

                gSheathLips.push_back(std::move(M));
            }

            // ---------------- leaf strip (trimmed) ----------------
            LeafGeom G; G.ctrl = ctrl;

            const int   startIdx = iStart;
            const float s0 = (samples == 1) ? 0.f : (float)startIdx / (samples - 1);

            G.strip.reserve(2 * (samples - startIdx));
            G.uv.reserve(2 * (samples - startIdx));

            Vec3 azim = norm(cross(Vec3{ 0,1,0 }, r));
            //float Rsurf = sheathSurfaceRadius(td.radius);

            for (int i = startIdx; i < samples; ++i) {
                float s01 = (samples == 1) ? 0.f : (float)i / (samples - 1);
                float w = widthParabola(s01, gLeafWmax);
                Vec3  C = mid[i];

                float tAttach = 0.f;
                if (gAttachFrac > 0.f) {
                    float x = clampf(s01 / std::max(1e-4f, gAttachFrac), 0.f, 1.f);
                    tAttach = 1.f - (x * x * (3.f - 2.f * x));
                }
                Vec3 Ccyl = r * (Rsurf)+Vec3{ 0.f, C.y, 0.f };
                C = C * (1.f - tAttach) + Ccyl * tAttach;

                Vec3 Nblend = norm(N2[i] * (1.f - tAttach) + azim * tAttach);
                Vec3 Bblend = norm(cross(T[i], Nblend));

                if (gDroop) C.y -= gDroopMax * s01 * s01;

                float baseRamp = smooth01(s01 / std::max(1e-4f, 0.08f));
                float halfW = 0.5f * w * baseRamp;

                Vec3 Lleaf = C + Nblend * halfW;
                Vec3 Rleaf = C - Nblend * halfW;

                // start with blade edges
                Vec3 Lp = Lleaf, Rp = Rleaf;

                // (A) pin first row to lip seam, if present
                if (haveLipSeam && i == startIdx) {
                    Lp = lipL_end; Rp = lipR_end;
                }

                // wiggle (skip the seam row so it stays sealed to the lip)
                if (doWiggle && i > startIdx) {
                    float leafPhase = twoPi * hash01((int)ti * 131071 + (int)li * 31337);
                    float widthFrac = (gLeafWmax > 1e-6f) ? (w / gLeafWmax) : 0.f;
                    float fadeIn = smoothstepf(0.10f, 0.25f, s01);
                    float fadeOut = 1.f - smoothstepf(0.88f, 0.98f, s01);
                    float env = widthFrac * fadeIn * fadeOut;
                    float phase = leafPhase + twoPi * gWiggleWaves * s01;
                    float dB = gWiggleAmt * env * std::sinf(phase);
                    if (i == samples - 1) dB = 0.f;
                    Lp = Lp + Bblend * (+dB);
                    Rp = Rp + Bblend * (-dB);
                }


                G.strip.push_back(Lp);  G.uv.push_back({ 0.0f * gTexURepeat, s01 * gTexVRepeat });
                G.strip.push_back(Rp);  G.uv.push_back({ 1.0f * gTexURepeat, s01 * gTexVRepeat });
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
static void drawStem(float radius, float yTop) {
    const int sides = 32;

    // Isolate state so leaves can't affect the stem.
    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT);

    glDisable(GL_TEXTURE_2D);          // <- critical
    glDisable(GL_BLEND);               // PNG alpha shouldn't affect the stem
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    Vec3 c = gStemMatchTexture ? gStemColor : Vec3{ 0.15f, 0.45f, 0.15f };
    c = c * gStemBrightness;
    glColor3f(clampf(c.x, 0, 1), clampf(c.y, 0, 1), clampf(c.z, 0, 1));

    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= sides; ++i) {
        float a = (float)i / sides * 2.f * (float)M_PI;
        float cx = std::cos(a) * radius, cz = std::sin(a) * radius;
        glVertex3f(cx, 0.f, cz);
        glVertex3f(cx, yTop, cz);
    }
    glEnd();

    glPopAttrib();
}

static void drawSheathRings() {
    if (gSheathGeoms.empty()) return;

    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    Vec3 c = gStemMatchTexture ? gStemColor : Vec3{ 0.15f, 0.45f, 0.15f };
    c = c * gStemBrightness;
    glColor3f(clampf(c.x, 0, 1), clampf(c.y, 0, 1), clampf(c.z, 0, 1));

    for (const auto& S : gSheathGeoms) {
        glBegin(GL_TRIANGLE_STRIP);
        for (const Vec3& v : S.band) glVertex3f(v.x, v.y, v.z);
        glEnd();
    }
    glPopAttrib();
}

static void drawSheathLips() {
    if (gSheathLips.empty()) return;

    if (gWireframe) {
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
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glColor3f(0.04f, 0.55f, 0.06f);
        }
    }

    for (const auto& M : gSheathLips) {
        const int rows = M.rows, cols = M.cols;
        for (int i = 0; i < rows - 1; ++i) {
            glBegin(GL_TRIANGLE_STRIP);
            for (int j = 0; j < cols; ++j) {
                int i0 = i * cols + j;
                int i1 = (i + 1) * cols + j;

                if (!gWireframe && gUseLeafTex && gLeafTex) glTexCoord2f(M.uv[i0].u, M.uv[i0].v);
                glVertex3f(M.pos[i0].x, M.pos[i0].y, M.pos[i0].z);

                if (!gWireframe && gUseLeafTex && gLeafTex) glTexCoord2f(M.uv[i1].u, M.uv[i1].v);
                glVertex3f(M.pos[i1].x, M.pos[i1].y, M.pos[i1].z);
            }
            glEnd();
        }
    }

    if (gWireframe) { glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glLineWidth(1.0f); }
}

static void drawPlant() {
    // stems: cumulative heights
    //for (const auto& td : gPlant.tillers) {
    //    float stemHeight = 0.f; for (const auto& L : td.leaves) stemHeight += std::max(0.f, L.distance);
    //    drawStem(td.radius, stemHeight);
    //}


    // 2) draw the thin rings
    drawSheathRings();

    // 3) draw the curved lips
    drawSheathLips();

    // leaves
    if (gWireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(gWireWidth);
        glColor3f(0.0f, 0.35f, 0.0f);   // darker green for wires
    }
    else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (gUseLeafTex && gLeafTex) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, gLeafTex);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor3f(1.0f, 1.0f, 1.0f); // don't tint texture
        }
        else {
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glColor3f(0.04f, 0.55f, 0.06f); // solid green
        }
    }

    for (const auto& G : gLeafGeoms) {
        glBegin(GL_TRIANGLE_STRIP);
        for (size_t i = 0; i < G.strip.size(); ++i) {
            if (!gWireframe && gUseLeafTex && gLeafTex) {
                glColor3f(1.0f, 1.0f, 1.0f);
                glTexCoord2f(G.uv[i].u, G.uv[i].v);
            }
            else {
				glColor3f(0.04f, 0.55f, 0.06f);
            }
            const Vec3& v = G.strip[i];
            glVertex3f(v.x, v.y, v.z);
        }
        glEnd();
    }
    if (!gFlatKeys.empty() && !gShowSelectedLeaf) {
        const int sel = gSelectedFlat;
        if (sel >= 0 && sel < (int)gLeafGeoms.size()) {
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(gWireWidth + 1.5f);
            glColor3f(0.95f, 0.95f, 0.15f);
            glBegin(GL_TRIANGLE_STRIP);
            for (const auto& v : gLeafGeoms[sel].strip) glVertex3f(v.x, v.y, v.z);
            glEnd();
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glLineWidth(1.0f);
        }
    }

    // restore defaults
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(1.0f);
    // ctrl viz on top
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND); // bitmap labels look crisper without blending tint

    if (gShowCtrlPoly) {
        glLineWidth(2.f); glColor3f(1.f, 0.45f, 0.05f);
        for (const auto& G : gLeafGeoms) {
            glBegin(GL_LINE_STRIP);
            for (const auto& p : G.ctrl) glVertex3f(p.x, p.y, p.z);
            glEnd();
        }
    }
    if (gShowCtrlPts) {
        glPointSize(gCtrlPtSize); glColor3f(0.95f, 0.2f, 0.1f);
        glBegin(GL_POINTS);
        for (const auto& G : gLeafGeoms)
            for (const auto& p : G.ctrl) glVertex3f(p.x, p.y, p.z);
        glEnd();
    }
    if (gShowCtrlLabel) {
        glColor3f(0.05f, 0.05f, 0.05f);
        for (const auto& G : gLeafGeoms) {
            for (size_t i = 0; i < G.ctrl.size(); ++i) {
                const Vec3& p = G.ctrl[i]; glRasterPos3f(p.x, p.y, p.z);
                char buf[8]; std::snprintf(buf, sizeof(buf), "C%zu", i);
                for (const char* c = buf; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
            }
        }
    }
    glEnable(GL_DEPTH_TEST);
}

/* ------------------------- GLUT / ImGui -------------------------- */
static void display() {
    // ---- ImGui ----
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGLUT_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse && io.MouseDown[0]) {
        gYaw += io.MouseDelta.x * 0.3f; gPitch += io.MouseDelta.y * 0.3f;
        gPitch = clampf(gPitch, -89.f, 89.f);
    }
    if (!io.WantCaptureMouse && io.MouseWheel != 0.f) {
        gDist = clampf(gDist * (io.MouseWheel > 0.f ? 0.90f : 1.10f), 0.3f, 50.f);
    }
    if (!io.WantCaptureMouse && io.MouseDown[1]) { // 0=LMB, 1=RMB, 2=MMB
        const float PI = 3.1415926535f;
        float cy = std::cos(gYaw * PI / 180.f), sy = std::sin(gYaw * PI / 180.f);
        float cp = std::cos(gPitch * PI / 180.f), sp = std::sin(gPitch * PI / 180.f);

        // camera direction (from target → eye)
        Vec3 dir = { cp * sy, sp, cp * cy };
        Vec3 fwd = norm(dir * -1.f);                  // eye→target
        Vec3 right = norm(cross(fwd, { 0.f,1.f,0.f }));
        Vec3 up = norm(cross(right, fwd));

        float panScale = gDist * 0.0025f;               // tweak pan speed
        gCamTarget = gCamTarget - right * (io.MouseDelta.x * panScale)
            + up * (io.MouseDelta.y * panScale);
    }

    glClearColor(0.98f, 0.98f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    int ww = glutGet(GLUT_WINDOW_WIDTH), hh = glutGet(GLUT_WINDOW_HEIGHT);
    float aspect = (hh > 0) ? float(ww) / float(hh) : 1.f;
    gluPerspective(45.0, aspect, 0.01, 200.0);

    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    float cy = std::cos(gYaw * M_PI / 180.f), sy = std::sin(gYaw * M_PI / 180.f);
    float cp = std::cos(gPitch * M_PI / 180.f), sp = std::sin(gPitch * M_PI / 180.f);

    // eye offset around the target using yaw/pitch
    Vec3 eyeOff = { gDist * cp * sy, gDist * sp, gDist * cp * cy };
    Vec3 eye = gCamTarget + eyeOff;

    gluLookAt(eye.x, eye.y, eye.z,
        gCamTarget.x, gCamTarget.y, gCamTarget.z,
        0, 1, 0);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // colors affect vertices again
    glColor4f(1, 1, 1, 1);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(1.0f);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    drawGridXZ();
    drawPlant();

	if (gLeafTexW == 0 && gLeafTexH == 0) {
        loadLeafTexture(gLeafTexPath);
	}

    ImGui::Begin("Plant Viewer - Editable Leaves");
    static bool needReload = false, needRebuild = false;

    ImGui::Text("XML: %s", gXmlPath);
    if (ImGui::Button("Load XML")) needReload = true;
    ImGui::SameLine();
	ImGui::InputText("Path", gXmlPath, IM_ARRAYSIZE(gXmlPath));




    if (needReload) {
        PlantDesc tmp;
        if (loadPlantXML(gXmlPath, tmp)) {
            gPlant = std::move(tmp);
            rebuildFlatKeys();     // rebuild list for editor
            needRebuild = true;
        }
        else {
            ImGui::OpenPopup("Load error");
        }
        needReload = false;
    }
    if (ImGui::BeginPopup("Load error")) {
        ImGui::Text("Failed to load XML at:\n%s", gXmlPath);
        ImGui::EndPopup();
    }

    ImGui::SeparatorText("Save / Load Parameters");
    ImGui::InputText("Params file", gParamPath, IM_ARRAYSIZE(gParamPath));
    if (ImGui::Button("Save params")) {
        if (saveParamsXML(gParamPath)) { gSaveOK = true; gSaveFail = false; }
        else { gSaveFail = true; gSaveOK = false; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load params")) {
        if (loadParamsXML(gParamPath)) { gLoadOK = true; gLoadFail = false; }
        else { gLoadFail = true; gLoadOK = false; }
    }
    // tiny notifications
    if (gSaveOK) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0, 0.6f, 0, 1), "Saved."); }
    if (gSaveFail) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.8f, 0.2f, 0, 1), "Save failed."); }
    if (gLoadOK) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0, 0.6f, 0, 1), "Loaded."); }
    if (gLoadFail) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.8f, 0.2f, 0, 1), "Load failed."); }

    ImGui::SeparatorText("Leaf parameters (global)");
    needRebuild |= ImGui::SliderFloat("Length (m)", &gLeafLength, 0.2f, 2.0f, "%.2f");
    needRebuild |= ImGui::SliderFloat("Max width (m)", &gLeafWmax, 0.02f, 0.40f, "%.3f");
    needRebuild |= ImGui::SliderInt("Samples (min)", &gSamplesMin, 5, 100);

    ImGui::Checkbox("Droop", &gDroop); ImGui::SameLine();
    needRebuild |= ImGui::SliderFloat("Max droop (m)", &gDroopMax, 0.0f, 0.25f, "%.3f");

    ImGui::Checkbox("Twist", &gTwistOn);
    if (gTwistOn) {
        needRebuild |= ImGui::SliderFloat("Twist max (deg)", &gTwistMaxD, 0.0f, 90.0f, "%.1f");
        needRebuild |= ImGui::SliderFloat("Twist power", &gTwistPow, 0.5f, 3.0f, "%.2f");
    }
    needRebuild |= ImGui::SliderFloat("Roll offset (deg)", &gRollOffsetD, -180.f, 180.f, "%.1f");

    ImGui::SeparatorText("Control points (view)");
    ImGui::Checkbox("Show points", &gShowCtrlPts); ImGui::SameLine();
    ImGui::Checkbox("Show polygon", &gShowCtrlPoly); ImGui::SameLine();
    ImGui::Checkbox("Labels", &gShowCtrlLabel);
    ImGui::SliderFloat("Point size", &gCtrlPtSize, 3.f, 20.f, "%.1f");

    ImGui::SeparatorText("Display");
    ImGui::Checkbox("Wireframe (leaves)", &gWireframe); ImGui::SameLine();
    ImGui::SliderFloat("Wire width", &gWireWidth, 1.0f, 5.0f, "%.1f");

    ImGui::SeparatorText("Wiggle");
    if (gWiggleOn) {
        needRebuild |= ImGui::SliderFloat("Amount (m)", &gWiggleAmt, 0.0f, 0.05f, "%.3f");
        // Period (seconds per cycle)
        needRebuild |= ImGui::SliderFloat("Frequency (#)", &gWiggleWaves, 0.0f, 20.0f, "%.1f");
    }

    ImGui::SeparatorText("Sheath ring");
    needRebuild |= ImGui::SliderFloat("Ring height (m)", &gSheathRingHeight, 0.001f, 0.05f, "%.3f");
    ImGui::SliderInt("Ring sides", &gSheathRingSides, 8, 128);

    ImGui::SeparatorText("Curved Sheath Lip");
    needRebuild |= ImGui::SliderFloat("Lip length frac", &gLipLenFrac, 0.02f, 0.20f, "%.3f");
    needRebuild |= ImGui::SliderInt("Lip columns", &gLipCols, 6, 24);
    needRebuild |= ImGui::SliderFloat("Arc at base Lip (deg)", &gLipArc0Deg, 200.f, 359.f, "%.0f");
    needRebuild |= ImGui::SliderFloat("Arc at lip end (deg)", &gLipArc1Deg, 10.f, 200.f, "%.0f");
    needRebuild |= ImGui::SliderFloat("Lip drop (m)", &gLipLift, 0.0000f, 1.00f, "%.2f");
    ImGui::SliderFloat("Unfold feather", &gLipFeather, 0.4f, 3.0f, "%.2f");


    ImGui::SeparatorText("Leaf Editor (per-leaf)");
	ImGui::Checkbox("Hide selected leaf", &gShowSelectedLeaf);
    if (!gFlatKeys.empty()) {
        ImGui::PushItemWidth(220);
        ImGui::Combo("Leaf", &gSelectedFlat, VectorGetter, &gFlatNames, (int)gFlatNames.size());
        ImGui::PopItemWidth();

        LeafKey key = gFlatKeys[gSelectedFlat];
        auto& edit = gEdits[key];           // creates default if missing

        // seed editable ctrl from current generated ctrl
        if (ImGui::Button(edit.hasCustom ? "Reset to generated" : "Make editable")) {
            if (edit.hasCustom) {
                edit.hasCustom = false;   // will use generated next rebuild
            }
            else {
                // seed from geometry (ensure geometry exists)
                if (gSelectedFlat < (int)gLeafGeoms.size()) {
                    edit.customCtrl = gLeafGeoms[gSelectedFlat].ctrl;
                    if (edit.customCtrl.size() < 4) edit.customCtrl = { {0,0,0},{0.3f,0,0},{0.6f,0,0},{1,0,0} };
                    edit.hasCustom = true;
                }
            }
            needRebuild = true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Pin C0 to stem", &edit.pinC0ToStem); // only affects custom

        if (edit.hasCustom) {
            // add/remove points
            if (ImGui::Button("Add point")) {
                if (!edit.customCtrl.empty()) {
                    Vec3 last = edit.customCtrl.back();
                    edit.customCtrl.push_back(last + Vec3{ 0.2f,0,0 });
                }
                else {
                    edit.customCtrl = { {0,0,0},{0.3f,0,0},{0.6f,0,0},{1,0,0} };
                }
                needRebuild = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove last") && edit.customCtrl.size() > 4) {
                edit.customCtrl.pop_back(); needRebuild = true;
            }
            // edit points
            for (size_t i = 0; i < edit.customCtrl.size(); ++i) {
                float xyz[3] = { edit.customCtrl[i].x, edit.customCtrl[i].y, edit.customCtrl[i].z };
                char label[32]; std::snprintf(label, sizeof(label), "C%zu", i);
                if (ImGui::DragFloat3(label, xyz, 0.005f)) {
                    edit.customCtrl[i] = { xyz[0],xyz[1],xyz[2] };
                    needRebuild = true;
                }
            }
        }
        else {
            ImGui::TextDisabled("Click \"Make editable\" to tweak C0..Cn of this leaf.");
        }
    }
    else {
        ImGui::TextDisabled("No leaves found in the XML.");
    }

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
    glutCreateWindow("Plant Viewer – Editable de Boor Leaves");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGLUT_Init();
    ImGui_ImplOpenGL2_Init();
    ImGui_ImplGLUT_InstallFuncs();

    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutMainLoop();

    // (usually unreachable)
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGLUT_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
