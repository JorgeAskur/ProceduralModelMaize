#include "MaizeGenerator.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <filesystem>

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

    float nLeavesFloat = numLeaves.get(gen, useMeansOnly);
    int nLeaves = (std::max)(3, (int)std::round(nLeavesFloat));

    float totalH = std::max(0.1f, height.get(gen, useMeansOnly));

    float lenBasePlant = leafLengthBase.get(gen, useMeansOnly);
    float widthBasePlant = leafWidthBase.get(gen, useMeansOnly);
    float widthTaperPlant = widthTaper.get(gen, useMeansOnly);
    float angleBasePlant = leafAngleBase.get(gen, useMeansOnly);
    float leafTwistPlant = leafTwist.get(gen, useMeansOnly);
    float droopinessPlant = droopinessBase.get(gen, useMeansOnly);
    float waveAmpPlant = waveAmp.get(gen, useMeansOnly);
    float waveFreqPlant = waveFreq.get(gen, useMeansOnly);
    float sheathEndAnglePlant = sheathEndAngle.get(gen, useMeansOnly);
    float sheathOpeningPlant = sheathOpening.get(gen, useMeansOnly);

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
        L.leafLength = (std::max)(0.1f, lenBase * leafLengthCurve.get(t));

        float widthBase = applyLeafJitter(widthBasePlant, leafWidthBase);
        L.leafWidth = (std::max)(0.01f, widthBase * leafWidthCurve.get(t));

        float widthTaperValue = applyLeafJitter(widthTaperPlant, widthTaper);
        L.widthTaper = (std::max)(0.1f, widthTaperValue);

        float angleBase = applyLeafJitter(angleBasePlant, leafAngleBase);
        L.leafAngle = angleBase * leafAngleCurve.get(t);

        L.leafTwist = applyLeafJitter(leafTwistPlant, leafTwist);

        L.droopiness = applyLeafJitter(droopinessPlant, droopinessBase);

        L.splinePoints = 40;

        float waveAmpValue = applyLeafJitter(waveAmpPlant, waveAmp);
        float waveFreqValue = applyLeafJitter(waveFreqPlant, waveFreq);
        L.waveLAmp = (std::max)(0.f, waveAmpValue);
        L.waveLFreq = (std::max)(1.f, waveFreqValue);
        L.waveRAmp = L.waveLAmp;
        L.waveRFreq = L.waveLFreq;

        L.sheathEndAngle = applyLeafJitter(sheathEndAnglePlant, sheathEndAngle);
        L.sheathOpeningStart = applyLeafJitter(sheathOpeningPlant, sheathOpening);

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
        Maize::GeneratePlant(p, baseName, texPath);
        std::cout << "Generated " << baseName << ".obj" << std::endl;
    }
}