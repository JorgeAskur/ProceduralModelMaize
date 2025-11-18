#pragma once
#include <vector>
#include <map>
#include <string>
#include <math/vect3d.h>

// Vector2 structure for UV coordinates
struct Vec2 { float u, v; };

// Leaf
struct LeafDesc {
    int   id = 0;
    float distance = 0;
    float sheathLength = 0.15f;

    // --- NEW High-Level Parameters ---
    float leafLength = 0.7f;
    float leafWidth = 0.08f;
    float leafAngle = 45.0f; // --- ADDED --- Angle of leaf from stem
    float droopiness = 0.5f; // How much the tip droops
    float baseWidthPct = 0.8f; // Percentage of leafWidth at the base
    int   splinePoints = 4; // Number of control points for the blade
    float widthTaper = 1.0f; // Easing for width taper (1=linear, >1=wider longer)
    float baseNudge = 1.1f; // How much to "nudge" the base of the blade outwards
    float leafTwist = 0.0f; // Twist angle in degrees
    float tipWidth = 0.0f; // Width of the leaf tip (0=pointy)

    // --- ADDED: Wave Parameters ---
    float waveLAmp = 0.0f;
    float waveLFreq = 0.0f;
    float waveRAmp = 0.0f;
    float waveRFreq = 0.0f;

    // --- NEW Sheath Parameters ---
    float sheathEndAngle = 2.0f;   // Angle (in PI radians) at the top
    float sheathOpeningStart = 0.0f; // Point along sheath (0-1) where opening begins

    // --- REMOVED ---
    // float sheathStartAngle = 0.5f;
    // std::vector<Vect3d> ctrlOpening;
};

// Tiller
struct TillerDesc {
    std::string type = "main";
    float radius = 0.02f;
    float alphaDeg = 0.f;
    float stemShrink = 0.002f;
    float azimuthDeg = 180.f;
    float azimuthNoise = 5.f;
    unsigned int randomSeed = 1337;
    std::vector<LeafDesc> leaves;
};

// Plant
struct PlantDesc {
    std::string species = "maize";
    std::vector<TillerDesc> tillers;
};

// Leaf geometry data
struct LeafGeom {
    int rows = 0, cols = 0;
    int stemRows = 0;
    std::vector<Vect3d> pos;
    std::vector<Vect3d> col;
    std::vector<Vec2>   uv;
    std::vector<Vect3d> nrm;
    std::vector<Vect3d> ctrlCenter;
    std::vector<Vect3d> ctrlLeft;
    std::vector<Vect3d> ctrlRight;

    // --- REMOVED ---
    // std::vector<Vect3d> ctrlOpening_world;
};

// Maize model class
class MaizeModel {
private:
    // State
    PlantDesc   m_plant;
    std::vector<LeafGeom>      m_leafGeoms;

public:
    // Global parameters
    int m_leafRows;
    int m_leafCols;

public:
    MaizeModel() : m_leafRows(50), m_leafCols(21) {}

    // State access
    PlantDesc& plant() { return m_plant; }
    const std::vector<LeafGeom>& leafGeoms() const { return m_leafGeoms; }

    // Operations
    void rebuildGeometry();
    int getTriangleCount() const;

    // I/O
    bool savePlantOBJ(const char* path, const char* textureFullPath);
    bool saveMTL(const char* mtlPath, const char* textureFileNameOnly);
};


// Public API namespace
namespace Maize {

    // Global parameters access
    int& gLeafRows();
    int& gLeafCols();

    // State access
    PlantDesc& plant();
    const std::vector<LeafGeom>& leafGeoms();

    // Operations
    void rebuildGeometry();
    int getTriangleCount();

    // Helpers
    Vect3d deBoor(float u, const std::vector<Vect3d>& P);

    // Create default leaf splines
    void makeDefaultLeafSplines(float L, float W,
        std::vector<Vect3d>& outCenter,
        std::vector<Vect3d>& outLeft,
        std::vector<Vect3d>& outRight);

// --- NEW Function ---
// Generates sheath opening spline from high-level parameters
    void generateOpeningSpline(
        const LeafDesc& L,
        std::vector<Vect3d>& outOpening
    );

    // --- NEW Function ---
    // Generates blade splines from high-level parameters
    void generateLeafSplines(
        const LeafDesc& L,
        float startRadius, // <-- Added this
        std::vector<Vect3d>& outCenter,
        std::vector<Vect3d>& outLeft,
        std::vector<Vect3d>& outRight
    );

    // I/O
    bool savePlantOBJ(const char* path, const char* textureFullPath);
    bool saveMTL(const char* mtlPath, const char* textureFileNameOnly);
    std::string dirFromPath(const std::string& p);

    // Plant generation
    void GeneratePlant(const PlantDesc& plantDesc, const std::string& baseFileName, const std::string& texturePath = "");

}