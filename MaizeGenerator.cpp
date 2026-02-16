#include "MaizeGenerator.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <filesystem>
#include "tinyxml2.h"

using namespace tinyxml2;

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

        float lenBase = applyLeafJitter(useLeafLengthCurve ? leafLengthCurve.get(t) : lenBasePlant, leafLengthBase);
        L.leafLength = (std::max)(0.1f, lenBase);

        float widthBase = applyLeafJitter(useLeafWidthCurve ? leafWidthCurve.get(t) : widthBasePlant, leafWidthBase);
        L.leafWidth = (std::max)(0.01f, widthBase);

        float widthTaperValue = applyLeafJitter(widthTaperPlant, widthTaper);
        L.widthTaper = (std::max)(0.1f, widthTaperValue);

        float angleBase = applyLeafJitter(useLeafAngleCurve ? leafAngleCurve.get(t) : angleBasePlant, leafAngleBase);
        L.leafAngle = angleBase;

        float twistBase = useLeafTwistCurve ? leafTwistCurve.get(t) : leafTwistPlant;
        L.leafTwist = applyLeafJitter(twistBase, leafTwist);

        L.droopiness = applyLeafJitter(useDroopinessCurve ? droopinessCurve.get(t) : droopinessPlant, droopinessBase);

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

void MaizeGenerator::generateBatch(int count, const std::string& outputDir, const std::string& leafTexPath, const std::string& stemTexPath) {
    std::random_device rd;
    for (int i = 0; i < count; ++i) {
        unsigned int seed = rd();
        PlantDesc p = generate(seed, false);
        Maize::gStemRibbonSpacing() = stemRibbonSpacing;
        std::string baseName = outputDir + "/plant_" + std::to_string(i);
        Maize::GeneratePlant(p, baseName, leafTexPath, stemTexPath);
        std::cout << "Generated " << baseName << ".obj" << std::endl;
    }
}

void MaizeGenerator::updateFromPlant(const PlantDesc& plant) {
    if (plant.tillers.empty())
        return;

    const auto& tiller = plant.tillers.front();
    stemRadius.mean = tiller.radius;
    numLeaves.mean = (float)tiller.leaves.size();

    float totalHeight = 0.0f;
    for (const auto& leaf : tiller.leaves)
        totalHeight += std::max(0.0f, leaf.distance);
    height.mean = std::max(0.1f, totalHeight);

    if (tiller.leaves.empty())
        return;

    float leafLengthMean = 0.0f;
    float leafWidthMean = 0.0f;
    float widthTaperMean = 0.0f;
    float leafAngleMean = 0.0f;
    float leafTwistMean = 0.0f;
    float droopinessMean = 0.0f;
    float stemInclinationMean = 0.0f;
    float waveAmpMean = 0.0f;
    float waveFreqMean = 0.0f;
    float wavePhaseMean = 0.0f;

    for (const auto& leaf : tiller.leaves) {
        leafLengthMean += leaf.leafLength;
        leafWidthMean += leaf.leafWidth;
        widthTaperMean += leaf.widthTaper;
        leafAngleMean += leaf.leafAngle;
        leafTwistMean += leaf.leafTwist;
        droopinessMean += leaf.droopiness;
        stemInclinationMean += leaf.stemInclinationDeg;
        waveAmpMean += leaf.waveLAmp;
        waveFreqMean += leaf.waveLFreq;
        wavePhaseMean += leaf.waveLPhase;
    }

    float invLeafCount = 1.0f / std::max(1.0f, (float)tiller.leaves.size());
    leafLengthBase.mean = leafLengthMean * invLeafCount;
    leafWidthBase.mean = leafWidthMean * invLeafCount;
    widthTaper.mean = widthTaperMean * invLeafCount;
    leafAngleBase.mean = leafAngleMean * invLeafCount;
    leafTwist.mean = leafTwistMean * invLeafCount;
    droopinessBase.mean = droopinessMean * invLeafCount;
    stemInclination.mean = stemInclinationMean * invLeafCount;
    waveAmp.mean = waveAmpMean * invLeafCount;
    waveFreq.mean = waveFreqMean * invLeafCount;
    wavePhaseShift.mean = wavePhaseMean * invLeafCount;
}

bool MaizeGenerator::saveSettings(const char* path) const {
    if (!path)
        return false;

    XMLDocument doc;
    doc.InsertFirstChild(doc.NewDeclaration());

    XMLElement* root = doc.NewElement("generator");
    doc.InsertEndChild(root);

    auto saveDist = [&](const char* name, const Dist& dist) {
        XMLElement* elem = doc.NewElement(name);
        elem->SetAttribute("mean", dist.mean);
        elem->SetAttribute("stdDev", dist.stdDev);
        root->InsertEndChild(elem);
    };

    auto saveCurve = [&](const char* name, bool enabled, const Curve& curve) {
        XMLElement* elem = doc.NewElement(name);
        elem->SetAttribute("enabled", enabled ? 1 : 0);
        elem->SetAttribute("bottom", curve.bottom);
        elem->SetAttribute("middle", curve.middle);
        elem->SetAttribute("top", curve.top);
        root->InsertEndChild(elem);
    };

    saveDist("height", height);
    saveDist("stemRadius", stemRadius);
    saveDist("numLeaves", numLeaves);
    saveDist("leafLengthBase", leafLengthBase);
    saveDist("leafWidthBase", leafWidthBase);
    saveDist("widthTaper", widthTaper);
    saveDist("leafAngleBase", leafAngleBase);
    saveDist("leafTwist", leafTwist);
    saveDist("droopinessBase", droopinessBase);
    saveDist("stemInclination", stemInclination);
    saveDist("waveAmp", waveAmp);
    saveDist("waveFreq", waveFreq);
    saveDist("wavePhaseShift", wavePhaseShift);

    saveCurve("leafLengthCurve", useLeafLengthCurve, leafLengthCurve);
    saveCurve("leafWidthCurve", useLeafWidthCurve, leafWidthCurve);
    saveCurve("leafAngleCurve", useLeafAngleCurve, leafAngleCurve);
    saveCurve("droopinessCurve", useDroopinessCurve, droopinessCurve);
    saveCurve("leafTwistCurve", useLeafTwistCurve, leafTwistCurve);

    root->SetAttribute("leafJitterScale", leafJitterScale);
    root->SetAttribute("stemRibbonSpacing", stemRibbonSpacing);

    return doc.SaveFile(path) == XML_SUCCESS;
}

bool MaizeGenerator::loadSettings(const char* path) {
    if (!path)
        return false;

    XMLDocument doc;
    if (doc.LoadFile(path) != XML_SUCCESS)
        return false;

    const XMLElement* root = doc.FirstChildElement("generator");
    if (!root)
        return false;

    auto loadDist = [&](const char* name, Dist& dist) {
        if (const XMLElement* elem = root->FirstChildElement(name)) {
            elem->QueryFloatAttribute("mean", &dist.mean);
            elem->QueryFloatAttribute("stdDev", &dist.stdDev);
        }
    };

    auto loadCurve = [&](const char* name, bool& enabled, Curve& curve) {
        if (const XMLElement* elem = root->FirstChildElement(name)) {
            int enabledInt = enabled ? 1 : 0;
            elem->QueryIntAttribute("enabled", &enabledInt);
            enabled = enabledInt != 0;
            elem->QueryFloatAttribute("bottom", &curve.bottom);
            elem->QueryFloatAttribute("middle", &curve.middle);
            elem->QueryFloatAttribute("top", &curve.top);
        }
    };

    loadDist("height", height);
    loadDist("stemRadius", stemRadius);
    loadDist("numLeaves", numLeaves);
    loadDist("leafLengthBase", leafLengthBase);
    loadDist("leafWidthBase", leafWidthBase);
    loadDist("widthTaper", widthTaper);
    loadDist("leafAngleBase", leafAngleBase);
    loadDist("leafTwist", leafTwist);
    loadDist("droopinessBase", droopinessBase);
    loadDist("stemInclination", stemInclination);
    loadDist("waveAmp", waveAmp);
    loadDist("waveFreq", waveFreq);
    loadDist("wavePhaseShift", wavePhaseShift);

    loadCurve("leafLengthCurve", useLeafLengthCurve, leafLengthCurve);
    loadCurve("leafWidthCurve", useLeafWidthCurve, leafWidthCurve);
    loadCurve("leafAngleCurve", useLeafAngleCurve, leafAngleCurve);
    loadCurve("droopinessCurve", useDroopinessCurve, droopinessCurve);
    loadCurve("leafTwistCurve", useLeafTwistCurve, leafTwistCurve);

    root->QueryFloatAttribute("leafJitterScale", &leafJitterScale);
    root->QueryFloatAttribute("stemRibbonSpacing", &stemRibbonSpacing);

    return true;
}