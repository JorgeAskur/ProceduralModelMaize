#include "MaizeGenerator.h"
#include <algorithm>
#include <cmath>
#include <iostream>

// Helper to clamp values
template <typename T>
T clampVal(T val, T minVal, T maxVal) {
    return (val < minVal) ? minVal : ((val > maxVal) ? maxVal : val);
}

PlantDesc MaizeGenerator::generate(unsigned int seed) {
    std::mt19937 gen(seed);
    PlantDesc plant;
    plant.species = "Maize_Procedural";

    TillerDesc mainTiller;
    mainTiller.type = "main";
    mainTiller.radius = stemRadius.get(gen);
    mainTiller.azimuthDeg = 180.0f;
    mainTiller.azimuthNoise = 15.0f;
    mainTiller.randomSeed = seed;

    // Use (std::max) with parens to avoid Windows macro conflict
    int nLeaves = (std::max)(3, (int)std::round(numLeaves.get(gen)));
    float totalH = height.get(gen);

    for (int i = 0; i < nLeaves; ++i) {
        float t = (float)i / (float)(nLeaves - 1); // 0..1 height

        LeafDesc L;
        L.id = i;
        L.distance = totalH / (float)nLeaves;

        // Apply Stats
        L.leafLength = (std::max)(0.1f, leafLengthBase.get(gen) * leafLengthCurve.get(t));
        L.leafWidth = (std::max)(0.01f, leafWidthBase.get(gen) * leafWidthCurve.get(t));
        L.leafAngle = leafAngleBase.get(gen) * leafAngleCurve.get(t);
        L.droopiness = (std::max)(0.0f, droopinessBase.get(gen));

        // Defaults
        L.splinePoints = 8;
        L.widthTaper = 1.0f;
        L.leafTwist = 0.0f;

        // Noise
        L.waveLAmp = (std::max)(0.f, waveAmp.get(gen));
        L.waveLFreq = (std::max)(1.f, waveFreq.get(gen));
        L.waveRAmp = (std::max)(0.f, waveAmp.get(gen));
        L.waveRFreq = (std::max)(1.f, waveFreq.get(gen));

        L.sheathEndAngle = 2.0f;

        mainTiller.leaves.push_back(L);
    }
    plant.tillers.push_back(mainTiller);
    return plant;
}