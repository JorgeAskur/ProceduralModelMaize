#pragma once
#include <vector>
#include <map>
#include <string>
#include <math/vect3d.h>

struct Vec2 { float u, v; };

/* ----------------------------- descriptors ----------------------------- */
struct LeafDesc {
    int   id = 0;
    float distance = 0;
    float age = 0.0f;
};

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

struct PlantDesc {
    std::string species = "maize";
    std::vector<TillerDesc> tillers;
};

/* ----------------------------- edits ----------------------------- */
struct LeafKey { int tIdx; int lIdx; bool operator<(const LeafKey& o) const { return (tIdx < o.tIdx) || (tIdx == o.tIdx && lIdx < o.lIdx); } };

struct LeafEdit {
    bool hasCustom = false;
    std::vector<Vect3d> ctrlCenter;
    std::vector<Vect3d> ctrlLeft;
    std::vector<Vect3d> ctrlRight;
    std::vector<Vect3d> ctrlBase; // New: 2D curve for base shape (x=width, y=vertical offset)
};

/* ----------------------------- generated geometry ----------------------------- */
struct LeafGeom {
    int rows = 0, cols = 0;
    std::vector<Vect3d> pos;
    std::vector<Vec2>   uv;
    std::vector<Vect3d> nrm;
    std::vector<Vect3d> ctrlCenter;
    std::vector<Vect3d> ctrlLeft;
    std::vector<Vect3d> ctrlRight;
    std::vector<Vect3d> ctrlBase_world; // New: For visualizing the base curve
};

struct StemLayerGeom {
    float radius, y0, y1;
    int   sides;
    std::vector<Vect3d> band;
    std::vector<Vec2>   uv;
};

/* ----------------------------- API ----------------------------- */
namespace Maize {
    // Global parameters
    extern int gLeafRows;
    extern int gLeafCols;
    extern float gBaseShapeFade;

    // State access
    PlantDesc& plant();
    std::map<LeafKey, LeafEdit>& edits();
    const std::vector<LeafGeom>& leafGeoms();
    const std::vector<StemLayerGeom>& stemLayers();

    // Operations
    void rebuildGeometry();
    int getTriangleCount();

    // Helpers
    Vect3d deBoor(float u, const std::vector<Vect3d>& P);

    void makeDefaultLeafSplines(float L, float W,
        std::vector<Vect3d>& outCenter,
        std::vector<Vect3d>& outLeft,
        std::vector<Vect3d>& outRight,
        std::vector<Vect3d>& outBase);

    // I/O
    bool savePlantOBJ(const char* path, const char* textureFullPath);
    bool saveMTL(const char* mtlPath, const char* textureFileNameOnly);
    std::string dirFromPath(const std::string& p);
}