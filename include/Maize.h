#pragma once
#include <vector>
#include <map>
#include <string>
#include <math/vect3d.h>

struct Vec2 { float u, v; };

struct LeafDesc {
    int   id = 0;
    float distance = 0;
    float leafLength = 0.7f;
    float leafWidth = 0.08f;
    float leafAngle = 45.0f;
    float droopiness = 0.5f;
    float stemInclinationDeg = 0.0f;
    int   splinePoints = 20;
    float widthTaper = 1.0f;
    float leafTwist = 0.0f;
    float waveLAmp = 0.0f;
    float waveLFreq = 0.0f;
    float waveLPhase = 0.0f;
    float waveRAmp = 0.0f;
    float waveRFreq = 0.0f;
    float waveRPhase = 0.0f;
    float sheathEndAngle = 2.0f;
    float sheathOpeningStart = 0.0f;
};

struct TillerDesc {
    std::string type = "main";
    float radius = 0.02f;
    float alphaDeg = 0.f;
    float stemShrink = 0.001f;
    float azimuthDeg = 180.f;
    float azimuthNoise = 45.f;
    unsigned int randomSeed = 1337;
    std::vector<LeafDesc> leaves;
};

struct PlantDesc {
    std::string species = "maize";
    std::vector<TillerDesc> tillers;
};

struct LeafGeom {
	typedef enum { GEOM_SHEATH, GEOM_BLADE } GeomType;
    GeomType type = GEOM_BLADE;
    int rows = 0, cols = 0;
    int stemRows = 0;
    std::vector<Vect3d> pos;
    std::vector<Vect3d> col;
    std::vector<Vec2>   uv;
    std::vector<Vect3d> nrm;
    std::vector<Vect3d> ctrlCenter;
    std::vector<Vect3d> ctrlLeft;
    std::vector<Vect3d> ctrlRight;
};

class MaizeModel {
private:
    PlantDesc   m_plant;
    std::vector<LeafGeom> m_leafGeoms;

public:
    // Replaced fixed rows/cols with Adaptive Resolution settings
    float m_densityV; // Segments per Meter (Length)
    float m_densityU; // Segments per Meter (Width)
    float m_stemDensityScale; // Stem density multiplier
    int m_stemRowOverride; // 0 = auto
    float m_stemRibbonSpacing; // meters per ribbon repeat

public:
    MaizeModel() : m_densityV(60.0f), m_densityU(120.0f), m_stemDensityScale(0.45f), m_stemRowOverride(0), m_stemRibbonSpacing(0.3f) {}

    PlantDesc& plant() { return m_plant; }
    const std::vector<LeafGeom>& leafGeoms() const { return m_leafGeoms; }

    void rebuildGeometry();
    int getTriangleCount() const;
    bool savePlantOBJ(const char* path, const char* leafTextureFullPath, const char* stemTextureFullPath = nullptr);
    bool saveMTL(const char* mtlPath, const char* leafTextureFileNameOnly, const char* stemTextureFileNameOnly);
};

namespace Maize {
    // Resolution Accessors
    float& gMeshDensityV();
    float& gMeshDensityU();
    float& gStemDensityScale();
    int& gStemRowOverride();
    float& gStemRibbonSpacing();

    PlantDesc& plant();
    const std::vector<LeafGeom>& leafGeoms();
    void rebuildGeometry();
    int getTriangleCount();
    Vect3d deBoor(float u, const std::vector<Vect3d>& P);
    void generateLeafSplines(const LeafDesc& L, float startRadius, std::vector<Vect3d>& outCenter, std::vector<Vect3d>& outLeft, std::vector<Vect3d>& outRight);
    bool savePlantOBJ(const char* path, const char* leafTextureFullPath, const char* stemTextureFullPath = nullptr);
    bool saveMTL(const char* mtlPath, const char* leafTextureFileNameOnly, const char* stemTextureFileNameOnly);
    std::string dirFromPath(const std::string& p);
    void GeneratePlant(const PlantDesc& plantDesc, const std::string& baseFileName, const std::string& leafTexturePath = "", const std::string& stemTexturePath = "");
}