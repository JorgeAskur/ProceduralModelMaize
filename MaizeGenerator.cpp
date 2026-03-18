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
    mainTiller.alphaDeg = 0.0f;
    mainTiller.stemShrink = std::max(stemShrinkMin, mainTiller.radius * stemShrinkFactor);
    mainTiller.azimuthDeg = tillerAzimuthDeg;
    mainTiller.azimuthNoise = tillerAzimuthNoise;
    mainTiller.randomSeed = seed;

    float nLeavesFloat = sampleFromZ(numLeaves);
    int nLeaves = (std::max)(3, (int)std::round(nLeavesFloat));

    float totalH = std::max(0.1f, sampleFromZ(height));

    float lenBasePlant = sampleFromZ(leafLengthBase);
    float widthBasePlant = sampleFromZ(leafWidthBase);
    float widthTaperPlant = sampleFromZ(widthTaper);
    float angleBasePlant = sampleFromZ(leafAngleBase);
    float leafTwistPlant = sampleFromZ(leafTwist);
    float leafCurlPlant = sampleFromZ(leafCurl);
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

    std::vector<float> internodeWeights(nLeaves, 1.0f);
    float internodeWeightSum = 0.0f;
    for (int i = 0; i < nLeaves; ++i) {
        float t = (nLeaves > 1) ? (float)i / (float)(nLeaves - 1) : 0.0f;
        float weight = 1.0f;
        float whorlStartClamped = (std::max)(0.0f, (std::min)(0.95f, whorlStart));
        float whorlSpan = (std::max)(0.01f, 1.0f - whorlStartClamped);
        if (t > whorlStartClamped) {
            float topT = (t - whorlStartClamped) / whorlSpan;
            weight = 1.0f - whorlCompression * topT;
        }
        weight = (std::max)(whorlMinInternodeWeight, weight);
        internodeWeights[i] = weight;
        internodeWeightSum += weight;
    }

    for (int i = 0; i < nLeaves; ++i) {
        float t = (float)i / (float)(nLeaves - 1);

        LeafDesc L;
        L.id = i;
        L.distance = totalH * (internodeWeights[i] / internodeWeightSum);

        float lenBase = applyLeafJitter(lenBasePlant, leafLengthBase);
        L.leafLength = (std::max)(0.1f, lenBase * leafLengthScaleCurve.get(t));

        float widthBase = applyLeafJitter(widthBasePlant, leafWidthBase);
        L.leafWidth = (std::max)(0.01f, widthBase * leafWidthScaleCurve.get(t));

        float widthTaperValue = applyLeafJitter(widthTaperPlant, widthTaper);
        L.widthTaper = (std::max)(0.1f, widthTaperValue * widthTaperScaleCurve.get(t));

        float angleBase = applyLeafJitter(angleBasePlant, leafAngleBase);
        float angleProfile = angleBase + (1.0f - t) * lowerLeafAngleBoost - t * upperLeafAngleDrop;
        L.leafAngle = angleProfile + leafAngleOffsetCurve.get(t);

        L.leafTwist = applyLeafJitter(leafTwistPlant, leafTwist) + leafTwistOffsetCurve.get(t);
        L.leafCurl = applyLeafJitter(leafCurlPlant, leafCurl) + leafCurlOffsetCurve.get(t);

        L.droopiness = applyLeafJitter(droopinessPlant, droopinessBase) + droopinessOffsetCurve.get(t);

        L.stemInclinationDeg = applyLeafJitter(stemInclinationPlant, stemInclination) + stemInclinationOffsetCurve.get(t);

        L.splinePoints = (std::max)(4, splinePointsDefault);

        float waveAmpValue = applyLeafJitter(waveAmpPlant, waveAmp);
        float waveFreqValue = applyLeafJitter(waveFreqPlant, waveFreq);
        float wavePhaseValue = applyLeafJitter(wavePhaseShiftPlant, wavePhaseShift);
        L.waveLAmp = waveAmpValue * waveAmpScaleCurve.get(t);
        L.waveLFreq = waveFreqValue * waveFreqScaleCurve.get(t);
        L.waveLPhase = wavePhaseValue;
        L.waveRAmp = L.waveLAmp;
        L.waveRFreq = L.waveLFreq;
		L.waveRPhase = wavePhaseValue + 3.1415f; // Offset by pi for right side

        L.surfaceNoiseAmp = leafSurfaceNoiseAmp;
        L.surfaceNoiseFreq = leafSurfaceNoiseFreq;
        L.tipWidthFactor = leafTipWidthFactor;
        L.midribTipTaperStart = midribTipTaperStart;
        L.midribTextureStrength = midribTextureStrength;
        L.liguleWrapLengthScale = liguleWrapLengthScale;
        L.liguleUnfoldSharpness = liguleUnfoldSharpness;
        L.sheathOuterScale = sheathOuterScale;

        mainTiller.leaves.push_back(L);
    }
    plant.tillers.push_back(mainTiller);
    return plant;
}

void MaizeGenerator::generateBatch(int count, const std::string& outputDir, const std::string& leafTexPath, const std::string& stemTexPath) {
    std::random_device rd;
    for (int i = 0; i < count; ++i) {
        unsigned int seed = rd();
        PlantDesc p = generate(seed, false);
        std::string baseName = outputDir + "/plant_" + std::to_string(i);
        Maize::GeneratePlant(p, baseName, leafTexPath, stemTexPath);
        std::cout << "Generated " << baseName << ".obj" << std::endl;
    }
}