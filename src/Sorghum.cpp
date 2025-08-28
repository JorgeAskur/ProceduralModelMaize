/********************************************************************
 *  sorghum.cpp  –  Procedural sorghum plant (stem + N leaves)
 *                  • Per-leaf parameters
 *                  • Hook-start fraction
 *                  • Linear gradients for hookAmount & tipDrop
 *                  • NO std::clamp (manual bounding only)
 ********************************************************************/
#define _USE_MATH_DEFINES
#include <cmath>
#include <random>
#include <algorithm>      // for std::max / std::min
#include <fstream>
#include <array>

#include <GL/freeglut.h>
#include "sorghum.h"

GLuint Sorghum::sLeafTex = 0;

 /*───────────────── helpers: always-filled polygons for the stem ───*/
static void PushFillMode()
{
    glPushAttrib(GL_POLYGON_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}
static void PopFillMode() { glPopAttrib(); }

/*───────────────── constructor ───────────────────────────────────*/
Sorghum::Sorghum(uint32_t seed, int leaves, float r, float h,
    float sp, float gap)
    : numLeaves(std::max(1, leaves)),
    stemRadius(std::max(0.005f, r)),
    stemHeight(std::max(0.10f, h)),
    leafSpacing(std::max(0.03f, sp)),
    clearance(std::max(0.0f, gap)),
    phaseL(numLeaves),
    phaseR(numLeaves),
    baseSeed(seed ? seed : std::random_device{}()),
    leaf(numLeaves)
{
    std::mt19937 rng(baseSeed);
    std::uniform_real_distribution<float> d(0.f, 10.f * (float)M_PI);
    for (int i = 0; i < numLeaves; ++i) { phaseL[i] = d(rng); phaseR[i] = d(rng); }
}

/*───────────────── resize stem / leaf count … ────────────────────*/
void Sorghum::SetStructure(int leaves, float r, float h, float sp, float gap)
{
    bool changed = (leaves >= 0 && leaves != numLeaves);
    if (leaves >= 0) numLeaves = std::max(1, leaves);
    if (r >= 0) stemRadius = std::max(0.005f, r);
    if (h >= 0) stemHeight = std::max(0.10f, h);
    if (sp >= 0) leafSpacing = std::max(0.03f, sp);
    if (gap >= 0) clearance = std::max(0.0f, gap);

    if (changed) {
        phaseL.resize(numLeaves);
        phaseR.resize(numLeaves);
        leaf.resize(numLeaves);
        std::mt19937 rng(baseSeed);
        std::uniform_real_distribution<float> d(0.f, 10.f * (float)M_PI);
        for (int i = 0; i < numLeaves; ++i) { phaseL[i] = d(rng); phaseR[i] = d(rng); }
    }
}

/*──────── (setters unchanged …) ──────────────────────────────────*/
void Sorghum::SetLeafParams(int idx, const LeafParams& p)
{
    if (idx >= 0 && idx < numLeaves) leaf[idx] = p;
}
void Sorghum::SetAllLeaves(const LeafParams& proto)
{
    for (auto& L : leaf)L = proto;
}

void Sorghum::SetLeafParams(int sp, float len, float baseW, float ang,
    float dip, float tipDrop, float tipSharp, float hookAmp, float ywig,
    float startW, float startAng, float midArch, float sideRoll,
    float tipTaper, float hookStart)
{
    LeafParams P;
    P.spines = sp; P.length = len; P.halfW = baseW; P.angleDeg = ang; P.dip = dip;
    P.tipDrop = tipDrop; P.tipSharp = tipSharp; P.hookAmount = hookAmp;
    P.hookStart = hookStart; P.yWiggle = ywig; P.startW = startW;
    P.startAngle = startAng; P.midArch = midArch; P.sideRoll = sideRoll;
    P.tipTaperDeg = tipTaper;
    SetAllLeaves(P);
}

/*───────────────── wrap & noise (manual bounds) ──────────────────*/
void Sorghum::SetWrapParams(float wf, float ff, float md) {
    leafWrapFrac = (wf < 0.05f ? 0.05f : (wf > 0.95f ? 0.95f : wf));
    leafFadeFrac = (ff < 0.0f ? 0.0f : (ff > 0.5f ? 0.5f : ff));
    leafMaxWrapDeg = (md < 0.0f ? 0.0f : (md > 90.f ? 90.f : md));
}
void Sorghum::SetNoiseParams(float a, float f) { noiseAmpOffset = a; noiseFreqOffset = f; }
void Sorghum::SetPos(Vect3d p) { pos = p; }
void Sorghum::Move(float dx, float dy, float dz) { pos += Vect3d(dx, dy, dz); }
void Sorghum::SetColor(GLubyte r, GLubyte g, GLubyte b) { color[0] = r; color[1] = g; color[2] = b; }

/*───────────────── rendering entry ───────────────────────────────*/
void Sorghum::Render()
{
    InitLeafTexture();                 // ← add

    glPushMatrix();
    glTranslatef(pos.x(), pos.y(), pos.z());

    glColor3ub(color[0], color[1], color[2]);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, sLeafTex);

    DrawSorghum();

    glDisable(GL_TEXTURE_2D);          // keep other code unaffected
    glPopMatrix();
}

/*───────────────── stem ──────────────────────────────────────────*/
void Sorghum::DrawStem()
{
    PushFillMode();
    glPushAttrib(GL_CURRENT_BIT);

    glDisable(GL_LIGHTING);                  // ← ignore lights
    glColor3ub(225, 245, 225);               // pastel green
    glPushMatrix();
    glRotatef(-90.f, 1, 0, 0);
    glutSolidCylinder(stemRadius, stemHeight, 20, 4);
    glPopMatrix();
    glEnable(GL_LIGHTING);                   //  restore lighting

    glPopAttrib();
    PopFillMode();
}

/*───────────────── whole plant ───────────────────────────────────*/
void Sorghum::DrawSorghum() {
    DrawStem();
    const float TWIST_MAX = 15.f, JITTER = 45.f;
    for (int i = 0; i < numLeaves; ++i) {
        float az = (i & 1) ? 180.f : 0.f;
        float u = (numLeaves > 1) ? float(i) / (numLeaves - 1) : 0.f;
        az += TWIST_MAX * (u * u * (3 - 2 * u));
        az += JITTER * std::sinf((phaseL[i] - phaseR[i]) * 0.15915f);
        float y = leafSpacing * (i + 1);
        glPushMatrix();
        glRotatef(az, 0, 1, 0); glTranslatef(stemRadius + clearance, y, 0);
        glRotatef(90.f, 0, 0, 1); DrawLeaf(i); glPopMatrix();
    }
}

/*───────────────── draw ONE leaf ─────────────────────────────────*/
void Sorghum::DrawLeaf(int idx)
{
    const LeafParams& P = leaf[idx];

    /* ═══ NEW gradient for hookAmount & tipDrop ══════════════════ */
    float alpha = (numLeaves > 1) ? float(idx) / (numLeaves - 1) : 0.f; // 0 = bottom, 1 = top
    float leafHookAmount = 0.50f - alpha * 0.15f;   // 0.50 → 0.35
    float leafTipDrop = -0.25f * alpha;          // 0 → -0.25
    float leafLength = P.length * (0.25 + (1 - 0.25) * alpha);


    /* ════════════════════════════════════════════════════════════ */

    const int   leafSpines = P.spines;
    const float leafBaseW = P.halfW;
    const float leafAngleDeg = P.angleDeg;
    const float leafDip = P.dip;
    const float leafTipSharp = P.tipSharp;
    const float leafHookStart = P.hookStart;
    const float leafYwiggle = P.yWiggle;
    const float leafStartW = P.startW;
    const float leafStartAngleDeg = P.startAngle;
    const float leafMidArch = P.midArch;
    const float leafSideRoll = P.sideRoll;
    const float leafTipTaperDeg = P.tipTaperDeg;

    float noiseAmp = noiseAmpOffset - (phaseL[idx] / 150.f);
    float noiseFreq = noiseFreqOffset - phaseR[idx];

    float w0 = leafStartW, w1 = w0 * 0.9f,
        w2 = leafBaseW * 0.2f, w3 = leafBaseW * (1 - leafTipSharp);

    auto AngleDegAt = [&](float t) {
        float b = t / leafWrapFrac; if (b < 0)b = 0; else if (b > 1)b = 1;
        return (1 - b) * leafStartAngleDeg + b * leafAngleDeg; };

    struct Rib { Vect3d L, C, R; };
    std::vector<Rib> ribs(leafSpines + 1);

    const Vect3d refX(1, 0, 0); Vect3d prevB(0, 0, 0);

    auto mid = [&](float t) {
        float y = t * leafLength;
        float h = (t - leafHookStart) / (1.f - leafHookStart);
        if (h < 0)h = 0; else if (h > 1)h = 1;
        float hookEase = h * h * (3 - 2 * h);
        float x = -leafHookAmount * hookEase - leafTipDrop * t * t;
        float z = leafMidArch * std::sin((float)M_PI * t) * leafLength;
        return Vect3d(x, y, z); };

    for (int i = 0; i <= leafSpines; ++i) {
        float t = float(i) / leafSpines, dt = 1.f / leafSpines;
        Vect3d C = mid(t), C2 = mid(std::min(1.f, t + dt));
        Vect3d T = C2 - C; T.Normalize();
        Vect3d B = refX.Cross(T); if (i > 0 && B.Dot(prevB) < 0)B = -B; B.Normalize();
        Vect3d U = T.Cross(B); U.Normalize(); prevB = B;

        float w = Bezier(w0, w1, w2, w3, t);
        if (t > 0.70f) {
            float u = (t - 0.70f) / 0.30f;
            w *= 1.f - u * std::sin(leafTipTaperDeg * (float)M_PI / 180.f);
        }
        float wigL = noiseAmp * w * std::sinf(t * noiseFreq + phaseL[idx]);
        float wigR = noiseAmp * w * std::sinf(t * noiseFreq + phaseR[idx]);

        float ang = AngleDegAt(t) * (float)M_PI / 180.f;
        float c = std::cosf(ang), s = std::sinf(ang);
        Vect3d Ld = c * B + s * U;
        Vect3d Rd = -c * B + s * U;

        float roll = leafSideRoll * std::sin((float)M_PI * t);
        Ld += roll * U; Ld.Normalize();
        Rd -= roll * U; Rd.Normalize();

        Vect3d Cmid = C - U * (leafDip * s * w);

        float ww;
        if (t < leafWrapFrac) ww = 1.f;
        else if (t < leafWrapFrac + leafFadeFrac)
            ww = 1.f - (t - leafWrapFrac) / leafFadeFrac;
        else ww = 0.f;

        if (ww > 0) {
            float arc = ww * (leafMaxWrapDeg * (float)M_PI / 180.f);
            float dx = -stemRadius * (1 - std::cosf(arc));
            float dz = -stemRadius * std::sinf(arc);
            Cmid += Vect3d(dx, 0, dz);
            Ld = Ld.GetRotatedY(arc); Rd = Rd.GetRotatedY(arc);
        }

        ribs[i].L = Cmid + Ld * (w + wigL);
        ribs[i].R = Cmid + Rd * (w + wigR);
        ribs[i].C = Cmid;
        ribs[i].L.SetX(ribs[i].L.x() * leafYwiggle);
        ribs[i].R.SetX(ribs[i].R.x() * leafYwiggle);
    }

    glBegin(GL_TRIANGLE_STRIP);
    for (auto& r : ribs) {
        glTexCoord2f(0.0f, r.C.y() / leafLength);   // L  (rim)
        glVertex3fv(r.L);
        glTexCoord2f(0.5f, r.C.y() / leafLength);   // C  (mid-rib)
        glVertex3fv(r.C);
    }
    glEnd();

    glBegin(GL_TRIANGLE_STRIP);
    for (auto& r : ribs) {
        glTexCoord2f(0.5f, r.C.y() / leafLength);   // C  (mid-rib)
        glVertex3fv(r.C);
        glTexCoord2f(1.0f, r.C.y() / leafLength);   // R  (rim)
        glVertex3fv(r.R);
    }
    glEnd();
}

/* ── helper: write tiny gradient texture once per session ─────── */
static void WriteLeafTexture(const char* texName = "leafTex.ppm")
{
    static bool done = false; if (done) return; done = true;

    constexpr int W = 64;
    std::ofstream ppm(texName, std::ios::out | std::ios::trunc);
    if (!ppm) return;
    ppm << "P3\n" << W << " 1\n255\n";
    for (int i = 0; i < W; ++i) {
        float s = i / float(W - 1);
        float d = std::fabs(s - 0.5f) * 2.f;          // 0 … 1
        float w = (d < 0.4f) ? 1.f - d / 0.4f : 0.f;  // highlight weight

        float r = 0.30f * (1 - w) + 0.93f * w;
        float g = 0.80f * (1 - w) + 0.94f * w;
        float b = 0.30f * (1 - w) + 0.25f * w;

        ppm << int(r * 255 + .5f) << ' '
            << int(g * 255 + .5f) << ' '
            << int(b * 255 + .5f) << '\n';
    }
}

/* ── helper: write MTL file next to OBJ ────────────────────────── */
static void WriteMTL(const char* mtlName, const char* texName = "leafTex.ppm")
{
    std::ofstream mtl(mtlName, std::ios::out | std::ios::trunc);
    if (!mtl) return;
    mtl << "newmtl leafMat\n"
        << "Ka 1 1 1\nKd 1 1 1\nKs 0 0 0\n"
        << "map_Kd " << texName << "\n";
}

/* ───────── SaveOBJ with UVs + MTL + texture ‐‐ replace entirely ─ */
bool Sorghum::SaveOBJ(const char* path) const
{
    /* ------------------------------------------------ write OBJ -- */
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) return false;

    /* derive names */
    std::string objName(path);
    std::string base = objName.substr(0, objName.find_last_of('.'));
    std::string mtlName = base + ".mtl";
    std::string texName = "leafTex.ppm";          // flat name; safe

    /* mtllib header */
    out << "mtllib " << mtlName.substr(mtlName.find_last_of("/\\") + 1) << "\n"
        << "usemtl leafMat\n";

    /* containers */
    struct V { float x, y, z; };
    struct VT { float u, v; };
    std::vector<V>  verts;
    std::vector<VT> vts;
    std::vector<std::array<int, 3>> tris;   // indices are 1-based!

    auto addV = [&](const Vect3d& p, float u, float v)->int {
        verts.push_back({ p.x(),p.y(),p.z() });
        vts.push_back({ u,v });
        return (int)verts.size();          // 1-based
        };

    /* =============== 1 ▸ closed stem (with dummy UVs) ============ */
    const int slices = 20;
    std::vector<int> rimB, rimT;
    for (int i = 0; i < slices; ++i) {
        float a = 2.f * M_PI * i / slices, x = stemRadius * std::cosf(a), z = stemRadius * std::sinf(a);
        rimB.push_back(addV({ x,0,z }, 0, 0));
        rimT.push_back(addV({ x,stemHeight,z }, 0, 1));
    }
    for (int i = 0; i < slices; ++i) {
        int n = (i + 1) % slices, b0 = rimB[i], b1 = rimB[n], t0 = rimT[i], t1 = rimT[n];
        tris.push_back({ b0,t0,b1 }); tris.push_back({ b1,t0,t1 });
    }
    int vBC = addV({ 0,0,0 }, 0, 0), vTC = addV({ 0,stemHeight,0 }, 0, 1);
    for (int i = 0; i < slices; ++i) {
        int n = (i + 1) % slices; tris.push_back({ rimB[n],rimB[i],vBC });
        tris.push_back({ vTC,rimT[i],rimT[n] });
    }

    /* =============== 2 ▸ leaves with real UVs ==================== */
    constexpr float TWIST_MAX = 15.f, JITTER_A = 45.f;
    for (int li = 0; li < numLeaves; ++li) {
        const LeafParams& P = leaf[li];
        float az = (li & 1) ? 180.f : 0.f, u = (numLeaves > 1) ? float(li) / (numLeaves - 1) : 0.f;
        az += TWIST_MAX * (u * u * (3 - 2 * u));
        az += JITTER_A * std::sinf((phaseL[li] - phaseR[li]) * 0.15915f);
        float ca = std::cosf(az * (float)M_PI / 180.f);
        float sa = std::sinf(az * (float)M_PI / 180.f);
        auto Ry = [&](const Vect3d& p) {return Vect3d(ca * p.x() + sa * p.z(), p.y(), -sa * p.x() + ca * p.z()); };
        auto Rz90 = [&](const Vect3d& p) {return Vect3d(-p.y(), p.x(), p.z()); };
        Vect3d base(stemRadius + clearance, leafSpacing * (li + 1), 0);

        float alpha = (numLeaves > 1) ? u : 0.f;
        float hookAmt = 0.50f - 0.15f * alpha, tipDrop = -0.25f * alpha;
        float leafLength = P.length;             // here you used full length

        float noiseAmp = noiseAmpOffset - (phaseL[li] / 150.f);
        float noiseFreq = noiseFreqOffset - phaseR[li];

        float w0 = P.startW, w1 = w0 * 0.9f,
            w2 = P.halfW * 0.2f, w3 = P.halfW * (1 - P.tipSharp);

        auto AngleDegAt = [&](float t) {float b = t / leafWrapFrac;
        if (b < 0)b = 0; else if (b > 1)b = 1;
        return (1 - b) * P.startAngle + b * P.angleDeg; };

        auto mid = [&](float t) {float y = t * leafLength;
        float h = (t - P.hookStart) / (1.f - P.hookStart);
        h = (h < 0) ? 0 : ((h > 1) ? 1 : h);
        float x = -hookAmt * (h * h * (3 - 2 * h)) - tipDrop * t * t;
        float z = P.midArch * std::sin((float)M_PI * t) * leafLength;
        return Vect3d(x, y, z); };

        const int N = P.spines; Vect3d refX(1, 0, 0), prevB; std::vector<int> vL, vC, vR;
        for (int i = 0; i <= N; ++i) {
            float t = float(i) / N, dt = 1.f / N;
            Vect3d C = mid(t), C2 = mid(std::min(1.f, t + dt));
            Vect3d T = C2 - C; T.Normalize();
            Vect3d B = refX.Cross(T); if (i && B.Dot(prevB) < 0)B = -B; B.Normalize();
            Vect3d U = T.Cross(B); U.Normalize(); prevB = B;

            float w = Bezier(w0, w1, w2, w3, t);
            if (t > 0.70f) { float u = (t - 0.70f) / 0.30f; w *= 1.f - u * std::sin(P.tipTaperDeg * (float)M_PI / 180.f); }
            float wigL = noiseAmp * w * std::sinf(t * noiseFreq + phaseL[li]);
            float wigR = noiseAmp * w * std::sinf(t * noiseFreq + phaseR[li]);

            float ang = AngleDegAt(t) * (float)M_PI / 180.f;
            float c = std::cosf(ang), s = std::sinf(ang);
            Vect3d Ld = c * B + s * U, Rd = -c * B + s * U;
            float roll = P.sideRoll * std::sin((float)M_PI * t);
            Ld += roll * U; Ld.Normalize(); Rd -= roll * U; Rd.Normalize();
            Vect3d Cmid = C - U * (P.dip * s * w);

            float ww = (t < leafWrapFrac) ? 1.f :
                (t < leafWrapFrac + leafFadeFrac) ? 1.f - (t - leafWrapFrac) / leafFadeFrac : 0.f;
            if (ww > 0) {
                float arc = ww * (leafMaxWrapDeg * (float)M_PI / 180.f);
                Cmid += Vect3d(-stemRadius * (1 - std::cosf(arc)), 0, -stemRadius * std::sinf(arc));
                Ld = Ld.GetRotatedY(arc); Rd = Rd.GetRotatedY(arc);
            }

            Vect3d L = Cmid + Ld * (w + wigL), R = Cmid + Rd * (w + wigR);
            L.SetX(L.x() * P.yWiggle); R.SetX(R.x() * P.yWiggle);

            float vCoord = t;                 // simple V
            vL.push_back(addV(Ry(Rz90(L)) + base, 0.f, vCoord));
            vC.push_back(addV(Ry(Rz90(Cmid)) + base, 0.5f, vCoord));
            vR.push_back(addV(Ry(Rz90(R)) + base, 1.f, vCoord));
        }
        for (int i = 0; i < N; ++i) {
            tris.push_back({ vL[i], vC[i], vL[i + 1] });
            tris.push_back({ vC[i], vC[i + 1], vL[i + 1] });
            tris.push_back({ vC[i], vR[i], vC[i + 1] });
            tris.push_back({ vR[i], vR[i + 1], vC[i + 1] });
        }
    }

    /* ------ write vertices, UVs, faces -------------------------- */
    for (auto& v : verts) out << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
    for (auto& t : vts) out << "vt " << t.u << ' ' << t.v << '\n';
    for (auto& f : tris) out << "f "
        << f[0] << '/' << f[0] << ' ' << f[1] << '/' << f[1] << ' ' << f[2] << '/' << f[2] << '\n';

    out.close();

    /* write texture + mtl next to OBJ ---------------------------- */
    WriteLeafTexture(texName.c_str());
    WriteMTL(mtlName.c_str(), texName.c_str());
    return true;
}
void Sorghum::InitLeafTexture()
{
    if (sLeafTex) return;                          // already built

    constexpr int W = 64;                          // 64 × 1 RGB
    unsigned char tex[W * 3];

    /*   s = 0 … 1  (left→right)
     *   d = distance from centre, 0 at s=½
     *   highlightWeight  ≈ 1  in central 40 % of leaf,
     *                     fades to 0 toward the rims            */
    for (int i = 0; i < W; ++i)
    {
        float  s = i / float(W - 1);
        float  d = std::fabs(s - 0.5f) * 2.0f;          // 0 … 1
        float  highlightWeight = (d < 0.2f)
            ? (1.f - d / 0.2f)      // linear core
            : 0.f;                  // rims

        /* blade colour (light fresh green) */
        float bladeR = 0.30f, bladeG = 0.80f, bladeB = 0.30f;

        /* mid-rib highlight (yellow-green) */
        float hiR = 0.93f, hiG = 0.94f, hiB = 0.25f;

        float r = bladeR * (1 - highlightWeight) + hiR * highlightWeight;
        float g = bladeG * (1 - highlightWeight) + hiG * highlightWeight;
        float b = bladeB * (1 - highlightWeight) + hiB * highlightWeight;

        tex[i * 3 + 0] = (unsigned char)(255 * r + 0.5f);
        tex[i * 3 + 1] = (unsigned char)(255 * g + 0.5f);
        tex[i * 3 + 2] = (unsigned char)(255 * b + 0.5f);
    }

    glGenTextures(1, &sLeafTex);
    glBindTexture(GL_TEXTURE_2D, sLeafTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, 1, 0,
        GL_RGB, GL_UNSIGNED_BYTE, tex);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/*───────────────── cubic Bézier helpers ──────────────────────────*/
float Sorghum::Bezier(float p0, float p1, float p2, float p3, float t) {
    float u = 1.f - t; return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}
Vect3d Sorghum::Bezier(const Vect3d& a, const Vect3d& b,
    const Vect3d& c, const Vect3d& d, float t) {
    return { Bezier(a.x(),b.x(),c.x(),d.x(),t),
            Bezier(a.y(),b.y(),c.y(),d.y(),t),
            Bezier(a.z(),b.z(),c.z(),d.z(),t) };
}
