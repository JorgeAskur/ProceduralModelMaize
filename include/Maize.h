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

    // Control points are now part of the leaf description
    std::vector<Vect3d> ctrlCenter;
    std::vector<Vect3d> ctrlLeft;
    std::vector<Vect3d> ctrlRight;
    std::vector<Vect3d> ctrlOpening;
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
    std::vector<Vect3d> ctrlOpening_world;
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
        std::vector<Vect3d>& outRight,
        std::vector<Vect3d>& outOpening);

    // I/O
    bool savePlantOBJ(const char* path, const char* textureFullPath);
    bool saveMTL(const char* mtlPath, const char* textureFileNameOnly);
    std::string dirFromPath(const std::string& p);

    // Plant generation
    void GeneratePlant(const PlantDesc& plantDesc, const std::string& baseFileName, const std::string& texturePath = "");

}

