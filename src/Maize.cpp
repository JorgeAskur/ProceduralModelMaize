/*────────────────────────── Maize.cpp ──────────────────────────*/
#define _USE_MATH_DEFINES
#include <cmath>
#include <vector>
#include <random>
#include <GL/freeglut.h>
#include "imgui.h"
#include "Maize.h"

/* ── helpers: Bézier & rotations ───────────────────────────────*/
static inline float Bezier(float p0, float p1, float p2, float p3, float t) {
    float u = 1.f - t; return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}
static inline Vect3d RotY(const Vect3d& p, float c, float s) {
    return { c * p.x() + s * p.z(),p.y(),-s * p.x() + c * p.z() };
}
static inline Vect3d RotZ(const Vect3d& p, float c, float s) {
    return { c * p.x() - s * p.y(),s * p.x() + c * p.y(),p.z() };
}

/* 0 ─ helper (put at top of the file once) */
static inline float Saturate(float x) { return (x < 0.f) ? 0.f : (x > 1.f ? 1.f : x); }

GLuint Maize::sLeafTex = 0;

/*────────────────── ctor & default params ─────────────────────*/
Maize::Maize(uint32_t seed, int leaves, float r, float h, float spacing, float gap)
    : numLeaves(std::max(1, leaves)),
    stemRadius(std::max(0.005f, r)),
    stemHeight(std::max(0.10f, h)),
    leafSpacing(std::max(0.03f, spacing)),
    clearance(std::max(0.0f, gap)),
    phaseL(numLeaves), phaseR(numLeaves),
    baseSeed(seed ? seed : std::random_device{}()),
    leaf(numLeaves)                         // default-constructed params
{
    std::mt19937 rng(baseSeed);
    std::uniform_real_distribution<float>d(0.f, 10.f * M_PI);
    for (int i = 0; i < numLeaves; ++i) { phaseL[i] = d(rng); phaseR[i] = d(rng); }
}

/*────────────────── structure resize ──────────────────────────*/
void Maize::SetStructure(int leaves, float r, float h, float spacing, float gap)
{
    bool changed = (leaves >= 0 && leaves != numLeaves);
    if (leaves >= 0) numLeaves = std::max(1, leaves);
    if (r >= 0) stemRadius = std::max(0.005f, r);
    if (h >= 0) stemHeight = std::max(0.10f, h);
    if (spacing >= 0) leafSpacing = std::max(0.03f, spacing);
    clearance = gap;

    if (changed) {
        phaseL.resize(numLeaves); phaseR.resize(numLeaves); leaf.resize(numLeaves);
        std::mt19937 rng(baseSeed);
        std::uniform_real_distribution<float>d(0.f, 10.f * M_PI);
        for (int i = 0; i < numLeaves; ++i) { phaseL[i] = d(rng); phaseR[i] = d(rng); }
    }
}

/*────────────────── per-leaf setters (same as sorghum) ─────────*/
void Maize::SetLeafParams(int idx, const LeafParams& p) { if (idx >= 0 && idx < numLeaves)leaf[idx] = p; }
void Maize::SetAllLeaves(const LeafParams& proto) { for (auto& L : leaf)L = proto; }
void Maize::SetLeafParams(int sp, float len, float baseW, float ang, float dip,
    float tipDrop, float tipSharp, float hookAmp, float ywig, float startW, float startAng,
    float midArch, float sideRoll, float tipTaper, float hookStart)
{
    LeafParams P; P.spines = sp; P.length = len; P.halfW = baseW; P.angleDeg = ang; P.dip = dip;
    P.tipDrop = tipDrop; P.tipSharp = tipSharp; P.hookAmount = hookAmp; P.yWiggle = ywig;
    P.startW = startW; P.startAngle = startAng; P.midArch = midArch; P.sideRoll = sideRoll;
    P.tipTaperDeg = tipTaper; P.hookStart = hookStart;
    SetAllLeaves(P);
}

/*────────────────── global settings ───────────────────────────*/
void Maize::SetWrapParams(float wf, float ff, float md, float pow, float sFrac, float pw) {
    leafWrapFrac = (wf < 0.01f ? 0.01f : (wf > 0.95f ? 0.95f : wf));
    leafFadeFrac = (ff < 0.f ? 0.f : (ff > 0.5f ? 0.5f : ff));
    leafMaxWrapDeg = (md < 0.f ? 0.f : (md > 360.f ? 360.f : md));
    leafWrapPow = pow;
    if (sFrac < 0.001f)      sFrac = 0.001f;
    else if (sFrac > 0.10f) sFrac = 0.10f;
    sheathFrac = sFrac;
    leafCurvePow = (pw < 0.25f ? 0.25f : (pw > 3.f ? 3.f : pw));
}
void Maize::SetNoiseParams(float a, float f) { noiseAmpOffset = a; noiseFreqOffset = f; }
void Maize::SetPos(Vect3d p) { pos = p; } void Maize::Move(Vect3d d) { pos += d; }
void Maize::SetColor(GLubyte r, GLubyte g, GLubyte b) { color[0] = r; color[1] = g; color[2] = b; }

/*──────── build 1×64 mid-rib texture (once per app) ───────────*/
static void InitLeafTexture()
{
    if (Maize::sLeafTex) return;
    constexpr int W = 64; unsigned char tex[W * 3];
    for (int i = 0; i < W; ++i) {
        float s = i / float(W - 1), d = fabsf(s - 0.5f) * 2.f, hi = (d < 0.4f) ? 1.f - d / 0.4f : 0.f;
        float r = 0.30f * (1 - hi) + 0.93f * hi, g = 0.80f * (1 - hi) + 0.94f * hi, b = 0.30f * (1 - hi) + 0.25f * hi;
        tex[i * 3 + 0] = (unsigned char)(255 * r + 0.5f);
        tex[i * 3 + 1] = (unsigned char)(255 * g + 0.5f);
        tex[i * 3 + 2] = (unsigned char)(255 * b + 0.5f);
    }
    glGenTextures(1, &Maize::sLeafTex);
    glBindTexture(GL_TEXTURE_2D, Maize::sLeafTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/*────────────────── stem (simple cylinder) ─────────────────────*/
void Maize::DrawStem() const
{
    glPushAttrib(GL_POLYGON_BIT | GL_CURRENT_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_LIGHTING); glColor3ub(225, 245, 225);
    glPushMatrix(); glRotatef(-90, 1, 0, 0);
    glutSolidCylinder(stemRadius, stemHeight, 20, 4);
    glPopMatrix(); glEnable(GL_LIGHTING); glPopAttrib();
}

/*────────────────── draw one leaf ribbon ───────────────────────*/
void Maize::DrawLeaf(int idx) const
{
    /* ── 0. placement on the stem ────────────────────────────── */
    const float baseY = std::min(clearance + idx * leafSpacing, stemHeight);
    const float phiDeg = 180.0f * idx;                 // golden angle
    glPushMatrix();
    glRotatef(phiDeg, 0.f, 1.f, 0.f);                // azimuth
    glTranslatef(stemRadius + clearance, baseY, 0.02);/* radial + vertical shift */

    const LeafParams& P = leaf[idx];
    auto DegToRad = [](float d) { return d * (float)M_PI / 180.f; };

    /* ── 1. constants ────────────────────────────────────── */
    const float sheathFrac = this->sheathFrac;   // user-controlled
    const float sheathFade = 0.05f;           // blend into blade
    const int   SEG = P.spines;
    const Vect3d refX(1, 0, 0);
    const float taperRad = DegToRad(P.tipTaperDeg);

    const float w0 = P.startW,
        w1 = w0 * 0.9f,
        w2 = P.halfW * 0.2f,
        w3 = P.halfW * (1 - P.tipSharp);

    auto Cubic = [](float a, float b, float c, float d, float t) {
        float u = 1.f - t; return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d; };

    /* noise phase */
    const float noiseAmp = noiseAmpOffset - phaseL[idx] / 150.f;
    const float noiseFreq = noiseFreqOffset - phaseR[idx];

    /* ── 2. mid-rib parametric curve ─────────────────────── */
    auto Mid = [&](float t)->Vect3d
    {
        /* sheathing segment – hug the stem, straight up */
        if (t < sheathFrac)
            return { 0.f, t / sheathFrac * (sheathFrac * P.length), 0.f };

        /* blend segment (fade from sheath to blade) */
        float u = (t - sheathFrac) / sheathFade;  // 0→1 over fade zone
        u = Saturate(u);
        float bladeT = (t - sheathFrac) / (1.f - sheathFrac); // 0→1 on blade

        /* hook & droop (scaled by blade length) */
        float h = Saturate((bladeT - P.hookStart) / (1.f - P.hookStart));
        float hookFac = h * h * (3 - 2 * h);            // smoothstep

        /* >>> shift from X to Z <<< */
        float zHook = P.hookAmount * P.length * hookFac * (1.f - bladeT);
        float zDrop = P.tipDrop * P.length * bladeT * bladeT;   // keep tipDrop negative

        /* centre line ------------------------------------------------ */
        float y = sheathFrac * P.length
            + bladeT * ((1.f - sheathFrac) * P.length);

        float zMidArch = P.midArch * std::sinf(bladeT * M_PI) * P.length;

        return {                             /* X stays 0 - radial */
                 0.f,
                 y,
                 zHook + zDrop + zMidArch    /* Z carries hook + droop */
        };
        /*return { (1 - u) * 0.f + u * (xHook + xDrop),
                 (1 - u) * (t * P.length) + u * y,
                 (1 - u) * 0.f + u * z };*/
    };

    /* ── 3. collar pitch only after sheath ───────────────── */
    auto CollarPitchDeg = [&](float t)
    {
        if (t < sheathFrac) return P.startAngle;           // still vertical
        float u = (t - sheathFrac) / (leafWrapFrac - sheathFrac);
        u = std::pow(Saturate(u), leafCurvePow);           // ← NEW
        return (1.f - u) * P.startAngle + u * P.angleDeg;
    };

    /* ── 4. wrap exponent (same idea, but respect sheath) ── */
    auto WrapWt = [&](float t)->float
    {
        if (t < sheathFrac)              return 1.f;   // full wrap
        if (t < leafWrapFrac)
        {
            float u = (t - sheathFrac) / (leafWrapFrac - sheathFrac);
            return 1.f - std::pow(u, leafWrapPow);
        }
        if (t < leafWrapFrac + leafFadeFrac)
        {
            float u = (t - leafWrapFrac) / leafFadeFrac;
            return std::pow(1.f - u, leafWrapPow);
        }
        return 0.f;
    };

    /* ── 5. generate the two strips ──────────────────────── */
    auto EmitStrip = [&](bool left)
    {
        Vect3d prevB(0, 0, 0);
        glBegin(GL_TRIANGLE_STRIP);

        for (int i = 0; i <= SEG; ++i)
        {
            float t = float(i) / SEG,
                dt = 1.f / SEG;

            /* Frenet-ish frame */
            Vect3d C = Mid(t),
                C2 = Mid(Saturate(t + dt));
            Vect3d T = (C2 - C); T.Normalize();
            Vect3d B = (refX.Cross(T)); B.Normalize();
            if (i && B.Dot(prevB) < 0) B = -B;
            Vect3d U = T.Cross(B); U.Normalize();
            prevB = B;

            /* width + taper */
            float w = Cubic(w0, w1, w2, w3, t);
            if (t > 0.70f) {
                float v = (t - 0.70f) / 0.30f;
                w *= 1.f - v * std::sinf(taperRad);
            }

            /* jitter */
            float wig = noiseAmp * w *
                std::sinf(t * noiseFreq + (left ? phaseL[idx] : phaseR[idx]));

            /* pitch & side-roll */
            float ang = DegToRad(CollarPitchDeg(t));
            float cA = std::cosf(ang), sA = std::sinf(ang);
            Vect3d D = cA * ((left ? -1.f : +1.f) * U)     // +/-U term
                + sA * B;                              // tangent term
            float roll = P.sideRoll * std::sinf(M_PI * t);
            D = (D + (left ? +1 : -1) * roll * U); D.Normalize();

            /* dip */
            Vect3d Cmid = C - U * (P.dip * sA * w);

            /* wrap */
            float ww = WrapWt(t);
            if (ww > 0.f) {
                float arc = ww * DegToRad(leafMaxWrapDeg);
                Cmid += { -stemRadius * (1 - std::cosf(arc)), 0,
                    -stemRadius * std::sinf(arc) };
                D = RotY(D, std::cosf(arc), std::sinf(arc));
            }

            /* final rim point */
            Vect3d Rim = Cmid + D * (w + wig);
            Rim.SetX(Rim.x() * P.yWiggle);

            /* crude normal (rib→rim) */
            Vect3d N = (left ? (Cmid - Rim) : (Rim - Cmid));
            N.SetY(0);
            Vect3d NN(N.z(), 0, -N.x());

            if (!left) glNormal3f(NN.x(), NN.y(), NN.z());
            glTexCoord2f(left ? 1.f : 0.f, t);
            glVertex3fv((left ? Rim : Rim));        // only position matters
            if (left) { glTexCoord2f(0.5f, t); glVertex3fv(Cmid); }
            else { glTexCoord2f(0.5f, t); glVertex3fv(Cmid); }
        }
        glEnd();
    };

    /* right then left */
    EmitStrip(false);
    EmitStrip(true);

    glPopMatrix();
}


/*────────────────── whole plant ───────────────────────────────*/
void Maize::DrawMaize() { DrawStem(); for (int i = 0; i < numLeaves; ++i)DrawLeaf(i); }

/*────────────────── render entry point ─────────────────────────*/
void Maize::Render()
{
    InitLeafTexture();
    glPushMatrix(); glTranslatef(pos.x(), pos.y(), pos.z());
    glColor3ub(color[0], color[1], color[2]);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, sLeafTex);
    DrawMaize();
    glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
    glPopMatrix();
}