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

static inline Vec3 projectOntoPlane(Vec3 v, Vec3 n_unit) {
    return v - n_unit * dot(v, n_unit);
}

// Find θ such that rotateAroundAxis(B, T, θ) aligns with target (radial r).
static float autoRollTheta(Vec3 B, Vec3 T, Vec3 target) {
    // Maximize dot(B', target) over θ where B' = B cosθ + (T×B) sinθ + T (T·B)(1−cosθ).
    // Because target ⟂ T for a cylinder normal, the last term vanishes. The optimum is:
    float x = dot(B, target);
    float y = dot(cross(T, B), target);
    return std::atan2(y, x); // rotate B about T by θ
}

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


// forward decls so loadParamsXML can call them
static void rebuildGeometry();
static void rebuildFlatKeys();

/* -------------- globals (camera, UI, generation) ----------------- */
static int   gWinW = 1280, gWinH = 860;
static float gYaw = 30.f, gPitch = 18.f, gDist = 3.5f;

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
static float gRollOffsetD = 90.0f; // rotate blade +90° about tangent to face "up"

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

static bool saveParamsXML(const char* path);
static bool loadParamsXML(const char* path);

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
            float phi = L.azimuthDeg * float(M_PI / 180.0f);
            float pitch = L.alphaDeg * float(M_PI / 180.0f);

            Vec3 r = { std::cos(phi), 0.f, std::sin(phi) };
            Vec3 base = r * td.radius + Vec3{ 0.f, yAt[li], 0.f };
            Vec3 dir = norm(r * std::cos(pitch) + stemAxis * std::sin(pitch));

            // choose control points: custom (if set) or generated
            LeafKey key{ (int)ti,(int)li };
            auto it = gEdits.find(key);
            std::vector<Vec3> ctrl =
                (it != gEdits.end() && it->second.hasCustom && it->second.customCtrl.size() >= 4)
                ? it->second.customCtrl : makeLeafCtrl(base, stemAxis, dir, gLeafLength);

            // pin base if requested
            if (it != gEdits.end() && it->second.hasCustom && it->second.pinC0ToStem) {
                ctrl[0] = base;
            }

            int samples = std::max(L.spines, gSamplesMin);

            // sample midrib & frames
            auto mid = resampleMidrib(ctrl, samples);
            std::vector<Vec3> T, N, B; buildPTFrames(mid, T, N, B);

            // roll (+ twist) about tangent
            std::vector<Vec3> N2 = N, B2 = B;
            float baseRoll = gRollOffsetD * (float)M_PI / 180.f;

            // --- auto-roll to match cylinder tangency at s=0 ---
            if (gAutoRollTangency) {
                // 'r' is already computed earlier for this leaf:
                float theta0 = autoRollTheta(B[0], T[0], r); // radians
                baseRoll += theta0;
            }

            for (int i = 0; i < samples; ++i) {
                float s = (samples == 1) ? 0.f : (float)i / (samples - 1);
                float twist = gTwistOn ? (gTwistMaxD * (float)M_PI / 180.f) * std::pow(s, gTwistPow) : 0.f;
                float theta = baseRoll + twist;
                N2[i] = rotateAroundAxis(N[i], T[i], theta);
                B2[i] = rotateAroundAxis(B[i], T[i], theta);
            }


            // build strip
            LeafGeom G; G.ctrl = ctrl;
            G.strip.reserve(2 * samples);
            G.uv.reserve(2 * samples); // if using UVs

            // Precompute azimuthal direction around the cylinder:
            Vec3 azim = norm(cross(Vec3{ 0,1,0 }, r)); // tangent around stem

            for (int i = 0; i < samples; ++i) {
                float s01 = (samples == 1) ? 0.f : (float)i / (samples - 1);
                float w = widthParabola(s01, gLeafWmax);
                Vec3  C = mid[i];

                // --- base conformity blend ---
                float tAttach = 0.f;
                if (gAttachFrac > 0.f) {
                    // Smoothly go from 1 at the base to 0 at s = gAttachFrac
                    float a0 = 0.f, a1 = std::max(1e-4f, gAttachFrac);
                    float x = clampf((s01 - a0) / (a1 - a0), 0.f, 1.f);
                    // smoothstep then invert (1 - smoothstep)
                    tAttach = 1.f - (x * x * (3.f - 2.f * x));
                }

                // Target centerline point on cylinder at same height (push outward a hair)
                Vec3 Ccyl = r * (td.radius + gAttachEpsilon) + Vec3{ 0.f, C.y, 0.f };
                // Blend midrib toward cylinder near base
                C = C * (1.f - tAttach) + Ccyl * tAttach;

                // Blend width direction toward azimuthal (so edges wrap around stem)
                Vec3 Nblend = norm(N2[i] * (1.f - tAttach) + azim * tAttach);
                Vec3 Bblend = norm(cross(T[i], Nblend));

                // --- droop (keep your current axis: y) ---
                if (gDroop) C.y -= gDroopMax * s01 * s01;

                // --- ramp leaf width near base to avoid exploding from stem ---
                float baseRamp = 1.f;
                {
                    float bw0 = 0.f, bw1 = std::max(1e-4f, 0.08f); // ~first 8% of the leaf
                    float xb = clampf((s01 - bw0) / (bw1 - bw0), 0.f, 1.f);
                    baseRamp = xb * xb * (3.f - 2.f * xb); // smoothstep up from 0 to 1
                }
                float halfW = 0.5f * w * baseRamp;

                Vec3 Lp = C + Nblend * halfW;
                Vec3 Rp = C - Nblend * halfW;

                // --- wiggle corrugation like before (but use Bblend) ---
                if (doWiggle) {
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

                G.strip.push_back(Lp); G.strip.push_back(Rp);

                // If you’re texturing:
                float v = s01;
                G.uv.push_back({ 0.0f, v }); // left edge
                G.uv.push_back({ 1.0f, v }); // right edge
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

static void drawPlant() {
    // stems: cumulative heights
    for (const auto& td : gPlant.tillers) {
        float stemHeight = 0.f; for (const auto& L : td.leaves) stemHeight += std::max(0.f, L.distance);
        drawStem(td.radius, stemHeight);
    }
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
                glTexCoord2f(G.uv[i].u, G.uv[i].v);
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

    glClearColor(0.98f, 0.98f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    int ww = glutGet(GLUT_WINDOW_WIDTH), hh = glutGet(GLUT_WINDOW_HEIGHT);
    float aspect = (hh > 0) ? float(ww) / float(hh) : 1.f;
    gluPerspective(45.0, aspect, 0.01, 200.0);

    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    const float PI = 3.1415926535f;
    float cy = std::cos(gYaw * PI / 180.f), sy = std::sin(gYaw * PI / 180.f);
    float cp = std::cos(gPitch * PI / 180.f), sp = std::sin(gPitch * PI / 180.f);
    Vec3 eye = { gDist * cp * sy, gDist * sp, gDist * cp * cy };
    gluLookAt(eye.x, eye.y, eye.z, 0.5, 0.25, 0.0, 0, 1, 0);

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
