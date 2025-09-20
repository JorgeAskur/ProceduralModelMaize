#pragma once
#include <vector>
#include <map>
#include <string>
#include <math/vect3d.h>

struct Vec2 { float u, v; };

// Plant descriptors
struct LeafDesc {
    int   id = 0;           // id from XML
    float distance = 0;     // internode length
    float alphaDeg = 30;    // vertical tilt
    int   spines = 10;      // sampling suggestion
    float azimuthDeg = 0;   // computed from id * tiller.beta
};
struct TillerDesc {
    std::string type = "main";
    float radius = 0.02f;     // base stem radius
    float alphaDeg = 0.f;     // base azimuth
    float betaDeg = 180.f;    // phyllotaxy step
	float stemShrink = 0.002f; // per-leaf radius reduction
    std::vector<LeafDesc> leaves;
};
struct PlantDesc {
    std::string species = "maize";
    std::vector<TillerDesc> tillers;
};

// Editable leaf controls
struct LeafKey { int tIdx; int lIdx; bool operator<(const LeafKey& o) const { return (tIdx < o.tIdx) || (tIdx == o.tIdx && lIdx < o.lIdx); } };
struct LeafEdit {
    bool hasCustom = false;
    bool pinC0ToStem = true;
    std::vector<Vect3d> customCtrl;   // control points
};

// Per-leaf parameter bundle
struct LeafParams {
    // Shape / sampling
    float length = 0.40f;
    float wmax = 0.1f;
    int   samplesMin = 16;
    // Droop / twist / roll
    float droopMax = 0.0f;
    float twistMaxD = 20.f;
    float twistPow = 1.4f;

    // Waviness
    float waveAmp = 0.0030f;
    float waveCycles = 6.0f;
    float waveAcross = 0.5f;
    float waveFadeIn = 0.4f;
    float waveFadeOut = 0.4f;
    // Width profile
    float widthKnee = 0.66f;
    float widthAtKnee = 0.45f;
    float earlyPow = 0.70f;
    float tipPow = 1.80f;
    float baseRampFrac = 0.08f;
    // Attach + unwrap
    float attachFrac = 0.10f;
    float attachEps = 0.0015f;
    float lipLenFrac = 0.08f;
    int   lipCols = 11;
    float lipArc0Deg = 179.f;
    float lipArc1Deg = 40.f;
    float unwrapMidFrac = 0.55f;
    float unwrapSharpness = 2.50f;
    // Seam opening controls
    float openStartFrac = 0.95f;
    float seamMaxDeg = 179.f;
    float openSharpness = 3.0f;
    float seamOffsetDeg = 180.f;
    // Seam ring drop
    float ringDrop = 0.00f;
};

// Generated geometry
struct LeafGeom {
    int rows = 0, cols = 0;
    std::vector<Vect3d> pos;
    std::vector<Vec2>   uv;
    std::vector<Vect3d> ctrl;
    std::vector<Vect3d> nrm;
};
struct StemLayerGeom {
    float radius = 0.f; float y0 = 0.f, y1 = 0.f; int sides = 32;
    std::vector<Vect3d> band; // triangle strip vertices
    float uPhase = 0.f;       // 0.0 for even, 0.5 for odd (180° in U)
};

// Maize API
namespace Maize {
    // State access
    PlantDesc& plant();
    std::map<LeafKey, LeafEdit>& edits();
    std::map<LeafKey, LeafParams>& leafParams();

    // Geometry outputs
    const std::vector<LeafGeom>& leafGeoms();
    const std::vector<StemLayerGeom>& stemLayers();

    // Parameter helpers
    LeafParams defaultLeafParams();
    LeafParams& paramsFor(const LeafKey& key);

    // Rebuild all geometry from current state
    void rebuildGeometry();

	// Geometry helpers
	std::vector<float> openUniformKnots(int nCtrl, int p);
	int findSpan(int n, int p, float u, const std::vector<float>& U);
	Vect3d deBoor(float u, const std::vector<Vect3d>& P, const std::vector<Vect3d>& U, int p);
	std::vector<Vect3d> resampleMidrib(const std::vector<Vect3d>& ctrl, int N);
	std::vector<int> pickRowsByCurvature(const std::vector<Vect3d>& mid, float angTol, float minStep);
	void buildPTFrames(const std::vector<Vect3d>& mid, std::vector<Vect3d>& T, std::vector<Vect3d>& N, std::vector<Vect3d>& B);
	static inline float widthBaseMax(float s01, float wmax, float knee, float atKnee, float earlyPow, float tipPow);
    static inline float unwrapAmountAtS(float s01, const LeafParams& P);
	static inline float seamAngleRadAtFrac(float frac, const LeafParams& P);
	static std::vector<Vect3d> makeLeafCtrl(Vect3d base, Vect3d stemUp, Vect3d dir, float L);

    // OBJ/MTL export
    bool savePlantOBJ(const char* path, const char* textureFullPath);
	static bool saveMTL(const char* mtlPath, const char* textureFileNameOnly);
	static std::string dirFromPath(const std::string& p);
    static std::string fileFromPath(const std::string& p);
	static std::string replaceExt(const std::string& p, const char* newExt);
	static bool samePath(const std::string& a, const std::string& b);
	static std::string normPath(std::string p);
	static bool copyFile(const char* src, const char* dst);

}