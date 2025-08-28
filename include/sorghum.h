/********************************************************************
 *  sorghum.h  –  Procedural sorghum plant (stem + N individual leaves)
 *                Each leaf owns its own editable parameter set.
 ********************************************************************/
#ifndef SORGHUM_H
#define SORGHUM_H

#include <vector>
#include <cstdint>
#include <GL/gl.h>          // for GLubyte

#include "math/vect3d.h"         // simple 3-float math class  (x(),y(),z())

 /*───────────────────────────────────────────────────────────────────*/
 /*  Parameters that govern the shape of ONE leaf                    */
struct LeafParams
{
    /* envelope ---------------------------------------------------- */
    int   spines = 64;     // cross-section resolution (>2)
    float length = 0.50f;  // metres
    float halfW = 0.10f;  // base half-width
    float angleDeg = -10.0f; // V-angle (positive opens up)
    float dip = 0.0f;   // depth multiplier (negative “scoop”)
    float tipDrop = -0.75f; // parabolic drop
    float tipSharp = 0.97f;  // 0.75 blunt … 1.0 sharp
    float hookAmount = 1.00f;  // basal hook strength
    float hookStart = 0.00f;  // fraction [0–1] where hook begins
    float yWiggle = 1.05f;  // tiny noise in X
    float startW = 0.05f;  // base width at t = 0
    float startAngle = -90.0f; // angle at t = 0 (curling round stem)

    /* curvature --------------------------------------------------- */
    float midArch = 0.05f;  // bow in Z
    float sideRoll = 0.0f;   // transversal roll
    float tipTaperDeg = 0.0f;   // extra taper near tip
};

/*───────────────────────────────────────────────────────────────────*/
class Sorghum
{
public:
    /* construction ------------------------------------------------ */
    Sorghum(uint32_t seed,
        int   leaves = 18,
        float stemR = 0.025f,
        float stemH = 1.5f,
        float spacing = 0.075f,
        float gap = 0.005f);

    /* plant-level structure -------------------------------------- */
    void SetStructure(int leaves = -1,
        float stemR = -1,
        float stemH = -1,
        float spacing = -1,
        float gap = -1);

    /* legacy “all leaves at once” API (still works) -------------- */
    void SetLeafParams(int   sp, float len, float baseW,
        float ang, float dip, float tipDrop,
        float tipSharp, float hookAmp,
        float ywig, float startW, float startAng,
        float midArch, float sideRoll, float tipTaper,
        float hookStart = 0.0f);

    /* NEW granular control --------------------------------------- */
    void SetLeafParams(int idx, const LeafParams& p);  // one leaf
    void SetAllLeaves(const LeafParams& proto);       // clone to all

    /* wrap & noise ----------------------------------------------- */
    void SetWrapParams(float wrapFrac, float fadeFrac, float maxDeg);
    void SetNoiseParams(float ampOff, float freqOff);

    /* transform & color ------------------------------------------ */
    void SetPos(Vect3d p);
    void Move(float dx, float dy, float dz);
    void SetColor(GLubyte r, GLubyte g, GLubyte b);

    /* drawing & export ------------------------------------------- */
    void Render();
    bool SaveOBJ(const char* path) const;

private:
    /* helpers ----------------------------------------------------- */
    void  DrawSorghum();
    void  DrawStem();
    void  DrawLeaf(int idx);

    static float  Bezier(float p0, float p1, float p2, float p3, float t);
    static Vect3d Bezier(const Vect3d& a, const Vect3d& b,
        const Vect3d& c, const Vect3d& d, float t);
    static void   InitLeafTexture();


    /* global plant fields ---------------------------------------- */
    int   numLeaves = 18;
    float stemRadius = 0.025f;
    float stemHeight = 1.5f;
    float leafSpacing = 0.075f;
    float clearance = 0.005f;

    /* per-leaf parameters & jitter ------------------------------- */
    std::vector<LeafParams> leaf;     // N = numLeaves
    std::vector<float>      phaseL;   // random noise phase
    std::vector<float>      phaseR;
    uint32_t                baseSeed;

    /* wrapping & noise ------------------------------------------- */
    float leafWrapFrac = 0.15f;
    float leafFadeFrac = 0.00f;
    float leafMaxWrapDeg = 0.0f;
    float noiseAmpOffset = 0.05f;
    float noiseFreqOffset = 60.0f;

    /* transform, colour ------------------------------------------ */
    Vect3d  pos = { 0,0,0 };
    GLubyte color[3] = { 34,139,34 };
    static GLuint sLeafTex;          // <- 1-pixel-high 64-pixel texture
};

#endif /* SORGHUM_H */
