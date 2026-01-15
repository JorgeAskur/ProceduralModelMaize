#include "MaizeGenerator.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <filesystem> // C++17 for directory creation

// Helper to clamp values
template <typename T>
T clampVal(T val, T minVal, T maxVal) {
    return (val < minVal) ? minVal : ((val > maxVal) ? maxVal : val);
}

PlantDesc MaizeGenerator::generate(unsigned int seed, bool useMeansOnly) {
    std::mt19937 gen(seed);
    PlantDesc plant;
    plant.species = "Maize_Procedural";

    TillerDesc mainTiller;
    mainTiller.type = "main";
    mainTiller.radius = std::max(0.005f, stemRadius.get(gen, useMeansOnly));
    mainTiller.azimuthDeg = 180.0f;
    mainTiller.azimuthNoise = 15.0f;
    mainTiller.randomSeed = seed;

    // Integer counts need rounding
    float nLeavesFloat = numLeaves.get(gen, useMeansOnly);
    int nLeaves = (std::max)(3, (int)std::round(nLeavesFloat));

    float totalH = std::max(0.1f, height.get(gen, useMeansOnly));

    for (int i = 0; i < nLeaves; ++i) {
        float t = (float)i / (float)(nLeaves - 1); // 0..1 height factor

        LeafDesc L;
        L.id = i;
        L.distance = totalH / (float)nLeaves; // Even spacing

        // Apply Stats
        float lenBase = leafLengthBase.get(gen, useMeansOnly);
        L.leafLength = (std::max)(0.1f, lenBase * leafLengthCurve.get(t));

        float widthBase = leafWidthBase.get(gen, useMeansOnly);
        L.leafWidth = (std::max)(0.01f, widthBase * leafWidthCurve.get(t));

        // New Params
        L.widthTaper = (std::max)(0.1f, widthTaper.get(gen, useMeansOnly));

        float angleBase = leafAngleBase.get(gen, useMeansOnly);
        L.leafAngle = angleBase * leafAngleCurve.get(t);

        L.leafTwist = leafTwist.get(gen, useMeansOnly);

        L.droopiness = droopinessBase.get(gen, useMeansOnly);

        // Defaults
        L.splinePoints = 40;

        // Noise
        L.waveLAmp = (std::max)(0.f, waveAmp.get(gen, useMeansOnly));
        L.waveLFreq = (std::max)(1.f, waveFreq.get(gen, useMeansOnly));
        L.waveRAmp = L.waveLAmp;
        L.waveRFreq = L.waveLFreq;

        L.sheathEndAngle = sheathEndAngle.get(gen, useMeansOnly);
        L.sheathOpeningStart = sheathOpening.get(gen, useMeansOnly);

        mainTiller.leaves.push_back(L);
    }
    plant.tillers.push_back(mainTiller);
    return plant;
}

void MaizeGenerator::generateBatch(int count, const std::string& outputDir, const std::string& texPath) {
    // std::filesystem::create_directories(outputDir); 
    std::random_device rd;
    for (int i = 0; i < count; ++i) {
        unsigned int seed = rd();
        PlantDesc p = generate(seed, false); // Use Variance
        std::string baseName = outputDir + "/plant_" + std::to_string(i);
        Maize::GeneratePlant(p, baseName, texPath);
        std::cout << "Generated " << baseName << ".obj" << std::endl;
    }
}