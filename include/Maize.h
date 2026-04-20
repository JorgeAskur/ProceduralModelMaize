#pragma once
#include <vector>
#include <map>
#include <string>
#include <math/vect3d.h>

struct Vec2 { float u, v; };

// High-level procedural description of a single maize leaf
//
// The model is spline-driven: these parameters define the centerline, left edge,
// and right edge splines that are later sampled into a mesh. A leaf can also
// switch into "override" mode, where the editor stores explicit control points
// instead of regenerating them from the scalar parameters
struct LeafDesc {
    int   id = 0;
    float distance = 0;                 // stem length
    float leafLength = 0.7f;            // leaf length
    float leafWidth = 0.08f;            // blade width near the base
    float azimuthDeg = 0.0f;            // absolute leaf azimuth around the stem
    float leafAngle = 45.0f;            // leaf angle 
    float droopiness = 0.5f;            // bending strength along the blade
    float stemInclinationDeg = 0.0f;    // stem bend 
    int   splinePoints = 20;            // number of control points
    float widthTaper = 1.0f;            // narrowing of blade towards the tip
    float leafTwist = 0.0f;             // twist in degrees from base to tip
    float leafCurl = 0.0f;              // curl amount
    float waveLAmp = 0.0f;              // left waviness amplitude
    float waveLFreq = 0.0f;             // left waviness frequency
    float waveLPhase = 0.0f;            // left waviness phase
    float waveRAmp = 0.0f;              // right waviness amplitude
    float waveRFreq = 0.0f;             // right waviness frequency
    float waveRPhase = 0.0f;            // right waviness phase
    float surfaceNoiseAmp = 0.01f;      // surface noise amplitude
    float surfaceNoiseFreq = 8.0f;      // surface noise frequency
    float midribTipTaperStart = 0.75f;  // fraction where the midrib starts fading out
    float midribTextureStrength = 0.35f;// uv compression around the midrib strip
    float liguleWrapLengthScale = 6.8f; // length of the wrapped part of the sheath 
    float liguleUnfoldSharpness = 2.2f; // sheath unwrapping into the blade
    float sheathOuterScale = 1.12f;     // Radius expansion around the collar cylinder
    bool useCtrlOverrides = false;      // scalar parameters are ignored for spline generation
    std::vector<Vect3d> ctrlCenterOverride; // center control points 
    std::vector<Vect3d> ctrlLeftOverride;   // left control points
    std::vector<Vect3d> ctrlRightOverride;  // right control points
};

// Description of one tiller / stem
struct TillerDesc {
    std::string type = "main";
    float radius = 0.02f;                    // stem radius
    float alphaDeg = 0.f;                    // tiller tilt
    float stemShrink = 0.001f;               // radius taper from base to top
    bool useStemCtrlOverrides = false;       // use explicit stem control points
    std::vector<Vect3d> stemCtrlOverride;    // stem control points
    std::vector<LeafDesc> leaves;            // leaves attached to this tiller in bottom-to-top order
};

// Full editable plant description
struct PlantDesc {
    std::string species = "maize";           // species name stored at the plant level
    std::string phenotypeId;                 // phenotype identifier stored at the plant level
    std::vector<TillerDesc> tillers;
};

struct LeafGeom {
	typedef enum { GEOM_SHEATH, GEOM_BLADE } GeomType;
    GeomType type = GEOM_BLADE;
    int rows = 0, cols = 0;                 
    int stemRows = 0;                       
    std::vector<Vect3d> pos;                // vertices
    std::vector<Vect3d> col;                // colors
    std::vector<Vec2>   uv;                 // texture coordinates
    std::vector<Vect3d> nrm;                // normals
    std::vector<Vect3d> ctrlCenter;         // center control spline
    std::vector<Vect3d> ctrlLeft;           // left control spline
    std::vector<Vect3d> ctrlRight;          // right control spline
    std::vector<Vect3d> stemCurve;          // smooth sampled stem B-spline for skeleton
};

// Model object shared by the UI, exporter, and procedural generator
class MaizeModel {
private:
    PlantDesc   m_plant;
    std::vector<LeafGeom> m_leafGeoms;

public:
    float m_densityV;         // Mesh density along the length direction
    float m_densityU;         // Mesh density across width/circumference
    float m_stemDensityScale; // Stem uses a scaled-down density so it stays cheaper than the blade

public:
    MaizeModel() : m_densityV(200.0f), m_densityU(200.0f), m_stemDensityScale(0.1f) {}

    PlantDesc& plant() { return m_plant; }
    const std::vector<LeafGeom>& leafGeoms() const { return m_leafGeoms; }

    void rebuildGeometry();
    int getTriangleCount() const;
    bool savePlantOBJ(const char* path, const char* leafTextureFullPath, const char* stemTextureFullPath = nullptr, bool writeMTL = true);
    bool saveSkeletonOBJ(const char* path) const;
    bool saveMTL(const char* mtlPath, const char* leafTextureFileNameOnly, const char* stemTextureFileNameOnly);
};

namespace Maize {
    float& gMeshDensityV();
    float& gMeshDensityU();
    float& gStemDensityScale();

    PlantDesc& plant();
    const std::vector<LeafGeom>& leafGeoms();
    void rebuildGeometry();
    int getTriangleCount();
    // Cubic B-spline evaluation used for both stems and leaf control splines
    Vect3d deBoor(float u, const std::vector<Vect3d>& P);

    // Builds center/left/right control splines for a leaf
    void generateLeafSplines(const LeafDesc& L, float startRadius, std::vector<Vect3d>& outCenter, std::vector<Vect3d>& outLeft, std::vector<Vect3d>& outRight);
    // Wraps an azimuth angle (degrees) into [0, 360)
    float normalizeAzimuthDeg(float deg);
    bool savePlantOBJ(const char* path, const char* leafTextureFullPath, const char* stemTextureFullPath = nullptr);
    bool saveSkeletonOBJ(const char* path);
    bool saveMTL(const char* mtlPath, const char* leafTextureFileNameOnly, const char* stemTextureFileNameOnly);
    std::string dirFromPath(const std::string& p);
    void GeneratePlant(const PlantDesc& plantDesc, const std::string& baseFileName, const std::string& leafTexturePath = "", const std::string& stemTexturePath = "", bool exportSkeleton = false);
    // Thread-safe export: operates on a caller-owned MaizeModel instead of the
    // global singleton so multiple worker threads can generate plants in parallel.
    // writeMTL should be false for workers (MTL is written once upfront by the
    // batch driver to avoid concurrent writes to maize.mtl).
    void GeneratePlantWith(MaizeModel& model, const PlantDesc& plantDesc, const std::string& baseFileName, const std::string& leafTexturePath = "", const std::string& stemTexturePath = "", bool exportSkeleton = false, bool writeMTL = false);
}
