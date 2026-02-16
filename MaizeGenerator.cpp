#include "MaizeGenerator.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <filesystem>
#include "tinyxml2.h"

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
        L.widthTaper = (std::max)(0.1f, widthTaperValue * widthTaperCurve.get(t));

        float angleBase = applyLeafJitter(useLeafAngleCurve ? leafAngleCurve.get(t) : angleBasePlant, leafAngleBase);
        L.leafAngle = angleBase;

        float twistBase = useLeafTwistCurve ? leafTwistCurve.get(t) : leafTwistPlant;
        L.leafTwist = applyLeafJitter(twistBase, leafTwist);

        L.leafTwist = applyLeafJitter(leafTwistPlant, leafTwist) * leafTwistCurve.get(t);

        L.stemInclinationDeg = applyLeafJitter(stemInclinationPlant, stemInclination);

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

bool MaizeGenerator::saveConfig(const std::string& path) const {
    using namespace tinyxml2;
    XMLDocument doc;
    doc.InsertFirstChild(doc.NewDeclaration());
    XMLElement* root = doc.NewElement("MaizeGeneratorConfig");
    doc.InsertEndChild(root);

    auto saveDist = [&](const char* name, const Dist& d) {
        XMLElement* elem = doc.NewElement(name);
        elem->SetAttribute("mean", d.mean);
        elem->SetAttribute("stdDev", d.stdDev);
        root->InsertEndChild(elem);
    };

    auto saveCurve = [&](const char* name, const Curve& c) {
        XMLElement* elem = doc.NewElement(name);
        elem->SetAttribute("bottom", c.bottom);
        elem->SetAttribute("middle", c.middle);
        elem->SetAttribute("top", c.top);
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

    saveCurve("leafLengthCurve", leafLengthCurve);
    saveCurve("leafWidthCurve", leafWidthCurve);
    saveCurve("widthTaperCurve", widthTaperCurve);
    saveCurve("leafAngleCurve", leafAngleCurve);
    saveCurve("leafTwistCurve", leafTwistCurve);
    saveCurve("droopinessCurve", droopinessCurve);

    root->SetAttribute("leafJitterScale", leafJitterScale);

    return doc.SaveFile(path.c_str()) == XML_SUCCESS;
}

bool MaizeGenerator::loadConfig(const std::string& path) {
    using namespace tinyxml2;
    XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != XML_SUCCESS) return false;
    const XMLElement* root = doc.FirstChildElement("MaizeGeneratorConfig");
    if (!root) return false;

    auto loadDist = [&](const char* name, Dist& d) {
        if (const XMLElement* elem = root->FirstChildElement(name)) {
            elem->QueryFloatAttribute("mean", &d.mean);
            elem->QueryFloatAttribute("stdDev", &d.stdDev);
        }
    };

    auto loadCurve = [&](const char* name, Curve& c) {
        if (const XMLElement* elem = root->FirstChildElement(name)) {
            elem->QueryFloatAttribute("bottom", &c.bottom);
            elem->QueryFloatAttribute("middle", &c.middle);
            elem->QueryFloatAttribute("top", &c.top);
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

    loadCurve("leafLengthCurve", leafLengthCurve);
    loadCurve("leafWidthCurve", leafWidthCurve);
    loadCurve("widthTaperCurve", widthTaperCurve);
    loadCurve("leafAngleCurve", leafAngleCurve);
    loadCurve("leafTwistCurve", leafTwistCurve);
    loadCurve("droopinessCurve", droopinessCurve);

    root->QueryFloatAttribute("leafJitterScale", &leafJitterScale);
    return true;
}
