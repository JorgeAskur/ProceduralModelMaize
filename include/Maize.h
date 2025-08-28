/*────────────────────────── Maize.h ───────────────────────────*/
#ifndef MAIZE_H
#define MAIZE_H

#include <vector>
#include <cstdint>
#include <GL/freeglut.h>
#include "math/vect3d.h"

/*──────────────────────── leaf parameters ─────────────────────*/
struct LeafParams
{
    /* geometry */
    int   spines = 32;     // # ribs
    float length = 1.0f;   // blade length (m)
    float halfW = 0.06f;  // half width at ligule
    float tipSharp = 1.0f;   // 1 = linear, >1 = pointier

    /* pose */
    float angleDeg = -35.f;  // collar pitch (deg)
    float sideRoll = 0.0f;   // twist around mid-rib (0-1)
    float dip = 0.0f;   // downward bend at mid-rib
    float midArch = 0.10f;  // left–right arch (+Z)

    /* hook & droop */
    float hookAmount = 0.20f;  // upward “kick”
    float hookStart = 0.00f;  // 0 = base, 1 = tip
    float tipDrop = 0.40f;  // downward quadratic droop

    /* base flare */
    float startW = 0.05f;  // sheath width (m)
    float startAngle = -90.f;  // roll at the very base (deg)

    /* profile */
    float tipTaperDeg = 1.0f;   // extra taper at the last 30 %

    /* jitter */
    float yWiggle = 1.0f;   // per-leaf random Y scale
};

/*────────────────────────── Maize class ───────────────────────*/
class Maize
{
public:
    static GLuint sLeafTex;                       // shared 1×64 texture

    /* construction */
    Maize(uint32_t seed = 0, int leaves = 16,
        float r = 0.022f, float h = 2.0f,
        float spacing = 0.12f, float gap = 0.0f);

    /* ── frame-to-frame setters ─────────────────────────────── */
    void SetStructure(int leaves, float r, float h,
        float spacing, float gap);

    /* per-leaf control (same API as Sorghum) */
    void SetLeafParams(int idx, const LeafParams& p); // one leaf
    void SetAllLeaves(const LeafParams& proto);       // broadcast
    void SetLeafParams(int sp, float len, float baseW, float ang,
        float dip, float tipDrop, float tipSharp,
        float hookAmp, float ywig, float startW,
        float startAng, float midArch, float sideRoll,
        float tipTaper, float hookStart);

    /* wrap, noise, placement */
    void SetWrapParams(float frac, float fade, float deg, float pow, float sheathFrac, float pw);
    void SetNoiseParams(float amp, float freq);
    void SetPos(Vect3d p);   void Move(Vect3d d);
    void SetColor(GLubyte r, GLubyte g, GLubyte b);

    /* drawing */
    void Render();            // call every frame

private:
    /* helpers */
    void DrawMaize();
    void DrawStem()  const;
    void DrawLeaf(int idx) const;

    /* geometry state ────────────────────────────────────────── */
    int   numLeaves;
    float stemRadius, stemHeight;
    float leafSpacing, clearance;

    /* per-leaf parameters */
    std::vector<LeafParams> leaf;

    /* sheath wrap (global) */
    float leafWrapFrac = 0.25f;     // % of blade hugging stem
    float leafFadeFrac = 0.05f;
    float leafMaxWrapDeg = 25.f;
    float leafWrapPow = 1.f;
    float sheathFrac = 0.15f;
    float leafCurvePow = 1.f;

    /* noise / jitter (global) */
    float noiseAmpOffset = 0.05f;
    float noiseFreqOffset = 30.f;

    /* misc */
    std::vector<float> phaseL, phaseR; // independent noise phase
    Vect3d  pos{ 0,0,0 };
    GLubyte color[3]{ 255,255,255 };
    uint32_t baseSeed;
};

/*──────────────────────────────────────────────────────────────*/
#endif // MAIZE_H
