#include "MaizeGenerator.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <filesystem>

PlantDesc MaizeGenerator::generate(unsigned int seed, bool useMeansOnly) {
    std::mt19937 gen(seed);
    PlantDesc plant;
    plant.species = "Maize_Procedural";

    float plantZ = 0.0f;
    if (!useMeansOnly) {
        std::normal_distribution<float> zDist(0.0f, 1.0f);
        plantZ = zDist(gen);
    }

    auto sampleFromZ = [&](const Dist& dist) {
        if (useMeansOnly || dist.stdDev <= 0.0001f) return dist.mean;
        return dist.mean + dist.stdDev * plantZ;
        };

    TillerDesc mainTiller;
    mainTiller.type = "main";
    mainTiller.radius = std::max(0.005f, sampleFromZ(stemRadius));
    mainTiller.azimuthDeg = 180.0f;
    mainTiller.azimuthNoise = 15.0f;
    mainTiller.randomSeed = seed;

    float nLeavesFloat = sampleFromZ(numLeaves);
    int nLeaves = (std::max)(3, (int)std::round(nLeavesFloat));

    float totalH = std::max(0.1f, sampleFromZ(height));

    float lenBasePlant = sampleFromZ(leafLengthBase);
    float widthBasePlant = sampleFromZ(leafWidthBase);
    float widthTaperPlant = sampleFromZ(widthTaper);
    float angleBasePlant = sampleFromZ(leafAngleBase);
    float leafTwistPlant = sampleFromZ(leafTwist);
    float droopinessPlant = sampleFromZ(droopinessBase);
    float stemInclinationPlant = sampleFromZ(stemInclination);
    float waveAmpPlant = sampleFromZ(waveAmp);
    float waveFreqPlant = sampleFromZ(waveFreq);
	float wavePhaseShiftPlant = sampleFromZ(wavePhaseShift);

    auto applyLeafJitter = [&](float base, const Dist& dist) {
        if (useMeansOnly || dist.stdDev <= 0.0001f || leafJitterScale <= 0.0f) return base;
        std::normal_distribution<float> jitter(0.0f, dist.stdDev * leafJitterScale);
        return base + jitter(gen);
        };

    for (int i = 0; i < nLeaves; ++i) {
        float t = (float)i / (float)(nLeaves - 1);

        LeafDesc L;
        L.id = i;
        L.distance = totalH / (float)nLeaves;

        float lenBase = applyLeafJitter(lenBasePlant, leafLengthBase);
        L.leafLength = (std::max)(0.1f, lenBase);

        float widthBase = applyLeafJitter(widthBasePlant, leafWidthBase);
        L.leafWidth = (std::max)(0.01f, widthBase);

        float widthTaperValue = applyLeafJitter(widthTaperPlant, widthTaper);
        L.widthTaper = (std::max)(0.1f, widthTaperValue);

        float angleBase = applyLeafJitter(angleBasePlant, leafAngleBase);
        L.leafAngle = angleBase;

        L.leafTwist = applyLeafJitter(leafTwistPlant, leafTwist);

        L.droopiness = applyLeafJitter(droopinessPlant, droopinessBase);

        L.stemInclinationDeg = applyLeafJitter(stemInclinationPlant, stemInclination);

        L.splinePoints = 40;

        float waveAmpValue = applyLeafJitter(waveAmpPlant, waveAmp);
        float waveFreqValue = applyLeafJitter(waveFreqPlant, waveFreq);
        float wavePhaseValue = applyLeafJitter(wavePhaseShiftPlant, wavePhaseShift);
        L.waveLAmp = (std::max)(0.f, waveAmpValue);
        L.waveLFreq = (std::max)(1.f, waveFreqValue);
        L.waveLPhase = wavePhaseValue;
        L.waveRAmp = L.waveLAmp;
        L.waveRFreq = L.waveLFreq;
		L.waveRPhase = wavePhaseValue + 3.1415f; // Offset by pi for right side

        mainTiller.leaves.push_back(L);
    }
    plant.tillers.push_back(mainTiller);
    return plant;
}

void MaizeGenerator::generateBatch(int count, const std::string& outputDir, const std::string& texPath) {
    std::random_device rd;
    for (int i = 0; i < count; ++i) {
        unsigned int seed = rd();
        PlantDesc p = generate(seed, false);
        std::string baseName = outputDir + "/plant_" + std::to_string(i);
        Maize::GeneratePlant(p, baseName, texPath, "");
        std::cout << "Generated " << baseName << ".obj" << std::endl;
    }
}