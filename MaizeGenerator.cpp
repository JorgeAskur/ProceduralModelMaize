#include "MaizeGenerator.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>
#include <vector>
#include "tinyxml2.h"

namespace {
    using TraitLoadings = MaizeGenerator::TraitLoadings;
    using LatentTraits = MaizeGenerator::LatentTraits;
    using PlantLevelTraits = MaizeGenerator::PlantLevelTraits;
    using ComputedLeafTraits = MaizeGenerator::ComputedLeafTraits;
    using TraitComputation = MaizeGenerator::TraitComputation;

    float hash_to_float_minus1_1(unsigned int seed) {
        seed = (seed ^ 61) ^ (seed >> 16);
        seed = seed + (seed << 3);
        seed = seed ^ (seed >> 4);
        seed = seed * 0x27d4eb2d;
        seed = seed ^ (seed >> 15);
        return ((float)(int)(seed & 0xFFFFFF) / (float)(0xFFFFFF / 2)) - 1.0f;
    }

    TraitLoadings makeLoadings(float vigor = 0.0f, float slenderness = 0.0f, float posture = 0.0f, float texture = 0.0f) {
        TraitLoadings loadings;
        loadings.vigor = vigor;
        loadings.slenderness = slenderness;
        loadings.posture = posture;
        loadings.texture = texture;
        return loadings;
    }

    struct CorrelatedParameterInfo {
        const char* key;
        const char* displayName;
        TraitLoadings defaultLoadings;
    };

    struct DistField {
        const char* name;
        Dist MaizeGenerator::* member;
    };

    struct FloatField {
        const char* name;
        float MaizeGenerator::* member;
    };

    struct IntField {
        const char* name;
        int MaizeGenerator::* member;
    };

    struct Curve3Field {
        const char* name;
        Curve3 MaizeGenerator::* member;
    };

    const CorrelatedParameterInfo kCorrelatedParameterInfos[] = {
        { "stemRadius", "Stem Radius", makeLoadings(0.65f, -0.40f, 0.0f, 0.0f) },
        { "numLeaves", "Leaf Count", makeLoadings(0.55f, 0.10f, 0.0f, 0.0f) },
        { "height", "Height", makeLoadings(0.60f, 0.40f, 0.0f, 0.0f) },
        { "leafLengthBase", "Leaf Length", makeLoadings(0.60f, 0.25f, 0.0f, 0.0f) },
        { "leafWidthBase", "Leaf Width", makeLoadings(0.50f, -0.15f, 0.0f, 0.0f) },
        { "widthTaper", "Width Taper", makeLoadings(-0.10f, 0.10f, 0.15f, 0.0f) },
        { "leafAngleBase", "Leaf Angle", makeLoadings(0.10f, 0.0f, 0.65f, 0.0f) },
        { "leafTwist", "Leaf Twist", makeLoadings(0.0f, 0.0f, 0.10f, 0.45f) },
        { "leafCurl", "Leaf Curl", makeLoadings(0.0f, 0.0f, 0.10f, 0.55f) },
        { "droopinessBase", "Droopiness", makeLoadings(-0.55f, 0.0f, 0.30f, 0.0f) },
        { "stemInclination", "Stem Inclination", makeLoadings(0.0f, 0.15f, 0.45f, 0.0f) },
        { "waveAmp", "Wave Amp", makeLoadings(0.0f, 0.0f, 0.0f, 0.60f) },
        { "waveFreq", "Wave Freq", makeLoadings(0.0f, 0.0f, 0.10f, 0.35f) },
        { "wavePhaseShift", "Wave Phase", makeLoadings(0.0f, 0.0f, 0.0f, 0.30f) },
        // Strong negative vigor loading -> high vigor drives the tassel upright.
        { "tasselDroop", "Tassel Droop", makeLoadings(-1.0f, 0.0f, -0.25f, 0.0f) },
    };

    const DistField kDistFields[] = {
        { "height", &MaizeGenerator::height },
        { "stemRadius", &MaizeGenerator::stemRadius },
        { "numLeaves", &MaizeGenerator::numLeaves },
        { "leafLengthBase", &MaizeGenerator::leafLengthBase },
        { "leafWidthBase", &MaizeGenerator::leafWidthBase },
        { "widthTaper", &MaizeGenerator::widthTaper },
        { "leafAngleBase", &MaizeGenerator::leafAngleBase },
        { "leafTwist", &MaizeGenerator::leafTwist },
        { "leafCurl", &MaizeGenerator::leafCurl },
        { "droopinessBase", &MaizeGenerator::droopinessBase },
        { "stemInclination", &MaizeGenerator::stemInclination },
        { "waveAmp", &MaizeGenerator::waveAmp },
        { "waveFreq", &MaizeGenerator::waveFreq },
        { "wavePhaseShift", &MaizeGenerator::wavePhaseShift },
        { "tasselDroop", &MaizeGenerator::tasselDroop },
    };

    const FloatField kFloatFields[] = {
        { "tillerAzimuthDeg", &MaizeGenerator::tillerAzimuthDeg },
        { "tillerAzimuthNoise", &MaizeGenerator::tillerAzimuthNoise },
        { "stemShrinkFactor", &MaizeGenerator::stemShrinkFactor },
        { "stemShrinkMin", &MaizeGenerator::stemShrinkMin },
        { "whorlStart", &MaizeGenerator::whorlStart },
        { "whorlCompression", &MaizeGenerator::whorlCompression },
        { "whorlMinInternodeWeight", &MaizeGenerator::whorlMinInternodeWeight },
        { "leafSurfaceNoiseAmp", &MaizeGenerator::leafSurfaceNoiseAmp },
        { "leafSurfaceNoiseFreq", &MaizeGenerator::leafSurfaceNoiseFreq },
        { "midribTipTaperStart", &MaizeGenerator::midribTipTaperStart },
        { "midribTextureStrength", &MaizeGenerator::midribTextureStrength },
        { "midribWidth", &MaizeGenerator::midribWidth },
        { "liguleWrapLengthScale", &MaizeGenerator::liguleWrapLengthScale },
        { "liguleUnfoldSharpness", &MaizeGenerator::liguleUnfoldSharpness },
        { "sheathOuterScale", &MaizeGenerator::sheathOuterScale },
        { "leafJitterScale", &MaizeGenerator::leafJitterScale },
        { "traitCorrelationStrength", &MaizeGenerator::traitCorrelationStrength },
    };

    const IntField kIntFields[] = {
        { "splinePointsDefault", &MaizeGenerator::splinePointsDefault },
    };

    const Curve3Field kCurve3Fields[] = {
        { "leafLengthScaleCurve", &MaizeGenerator::leafLengthScaleCurve },
        { "leafWidthScaleCurve", &MaizeGenerator::leafWidthScaleCurve },
        { "widthTaperScaleCurve", &MaizeGenerator::widthTaperScaleCurve },
        { "waveAmpScaleCurve", &MaizeGenerator::waveAmpScaleCurve },
        { "waveFreqScaleCurve", &MaizeGenerator::waveFreqScaleCurve },
        { "leafAngleOffsetCurve", &MaizeGenerator::leafAngleOffsetCurve },
        { "droopinessOffsetCurve", &MaizeGenerator::droopinessOffsetCurve },
        { "leafTwistOffsetCurve", &MaizeGenerator::leafTwistOffsetCurve },
        { "leafCurlOffsetCurve", &MaizeGenerator::leafCurlOffsetCurve },
        { "stemInclinationOffsetCurve", &MaizeGenerator::stemInclinationOffsetCurve },
    };

    struct InternodeWeights {
        std::vector<float> values;
        float sum = 0.0f;
    };

    int sanitizeCorrelatedParameter(int param) {
        if (param < 0 || param >= MaizeGenerator::CorrelatedParameter_Count) {
            return MaizeGenerator::CorrelatedParameter_StemRadius;
        }
        return param;
    }

    TraitLoadings defaultLoadingsFor(int param) {
        return kCorrelatedParameterInfos[sanitizeCorrelatedParameter(param)].defaultLoadings;
    }

    const char* getCorrelatedParameterKey(int param) {
        return kCorrelatedParameterInfos[sanitizeCorrelatedParameter(param)].key;
    }

    int findCorrelatedParameterByKey(const char* key) {
        if (!key || !key[0]) return -1;
        for (int param = 0; param < MaizeGenerator::CorrelatedParameter_Count; ++param) {
            if (std::string(key) == getCorrelatedParameterKey(param)) {
                return param;
            }
        }
        return -1;
    }

    LatentTraits sampleLatentTraits(std::mt19937& gen, bool useMeansOnly) {

        LatentTraits latent;
        if (useMeansOnly) return latent;

        std::normal_distribution<float> standardNormal(0.0f, 1.0f);
        latent.vigor = standardNormal(gen);
        latent.slenderness = standardNormal(gen);
        latent.posture = standardNormal(gen);
        latent.texture = standardNormal(gen);
        return latent;
    }

    float sampleCorrelatedValue(const Dist& dist, const TraitLoadings& loadings, const LatentTraits& latent, float corrStrength, std::mt19937& gen, bool useMeansOnly) {
        if (useMeansOnly || dist.stdDev <= 0.0001f) return dist.mean;

        float lv = loadings.vigor * corrStrength;
        float ls = loadings.slenderness * corrStrength;
        float lp = loadings.posture * corrStrength;
        float lt = loadings.texture * corrStrength;
        float used = lv * lv + ls * ls + lp * lp + lt * lt;
        if (used > 1.0f) {
            float invNorm = 1.0f / std::sqrt(used);
            lv *= invNorm;
            ls *= invNorm;
            lp *= invNorm;
            lt *= invNorm;
            used = 1.0f;
        }

        std::normal_distribution<float> standardNormal(0.0f, 1.0f);
        float residual = std::sqrt((std::max)(0.0f, 1.0f - used));
        float z = lv * latent.vigor + ls * latent.slenderness + lp * latent.posture + lt * latent.texture + residual * standardNormal(gen);
        return dist.mean + dist.stdDev * z;
    }

    float jitterLeafTrait(std::mt19937& gen, float base, const Dist& dist, bool useMeansOnly, float jitterScale) {
        if (useMeansOnly || dist.stdDev <= 0.0001f || jitterScale <= 0.0f) return base;
        std::normal_distribution<float> jitter(0.0f, dist.stdDev * jitterScale);
        return base + jitter(gen);
    }
    InternodeWeights buildInternodeWeights(int nLeaves, float whorlStart, float whorlCompression, float whorlMinInternodeWeight) {
        InternodeWeights result;
        result.values.assign(nLeaves, 1.0f);
        if (nLeaves <= 0) return result;

        float whorlStartClamped = (std::max)(0.0f, (std::min)(0.95f, whorlStart));
        float whorlSpan = (std::max)(0.01f, 1.0f - whorlStartClamped);
        for (int i = 0; i < nLeaves; ++i) {
            float t = (nLeaves > 1) ? (float)i / (float)(nLeaves - 1) : 0.0f;
            float weight = 1.0f;
            if (t > whorlStartClamped) {
                float topT = (t - whorlStartClamped) / whorlSpan;
                weight = 1.0f - whorlCompression * topT;
            }
            weight = (std::max)(whorlMinInternodeWeight, weight);
            result.values[i] = weight;
            result.sum += weight;
        }
        return result;
    }

    PlantLevelTraits samplePlantLevelTraits(const MaizeGenerator& generator, const LatentTraits& latent, float corrStrength, std::mt19937& gen, bool useMeansOnly) {
        PlantLevelTraits plantTraits;
        plantTraits.leafLengthBase = sampleCorrelatedValue(generator.leafLengthBase, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_LeafLengthBase), latent, corrStrength, gen, useMeansOnly);
        plantTraits.leafWidthBase = sampleCorrelatedValue(generator.leafWidthBase, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_LeafWidthBase), latent, corrStrength, gen, useMeansOnly);
        plantTraits.widthTaper = sampleCorrelatedValue(generator.widthTaper, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_WidthTaper), latent, corrStrength, gen, useMeansOnly);
        plantTraits.leafAngleBase = sampleCorrelatedValue(generator.leafAngleBase, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_LeafAngleBase), latent, corrStrength, gen, useMeansOnly);
        plantTraits.leafTwist = sampleCorrelatedValue(generator.leafTwist, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_LeafTwist), latent, corrStrength, gen, useMeansOnly);
        plantTraits.leafCurl = sampleCorrelatedValue(generator.leafCurl, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_LeafCurl), latent, corrStrength, gen, useMeansOnly);
        plantTraits.droopinessBase = sampleCorrelatedValue(generator.droopinessBase, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_DroopinessBase), latent, corrStrength, gen, useMeansOnly);
        plantTraits.stemInclination = sampleCorrelatedValue(generator.stemInclination, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_StemInclination), latent, corrStrength, gen, useMeansOnly);
        plantTraits.waveAmp = sampleCorrelatedValue(generator.waveAmp, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_WaveAmp), latent, corrStrength, gen, useMeansOnly);
        plantTraits.waveFreq = sampleCorrelatedValue(generator.waveFreq, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_WaveFreq), latent, corrStrength, gen, useMeansOnly);
        plantTraits.wavePhaseShift = sampleCorrelatedValue(generator.wavePhaseShift, generator.getTraitLoadings(MaizeGenerator::CorrelatedParameter_WavePhaseShift), latent, corrStrength, gen, useMeansOnly);
        return plantTraits;
    }

    template <typename LeafLike>
    void applyLeafVisualDefaults(const MaizeGenerator& generator, LeafLike& leaf) {
        leaf.surfaceNoiseAmp = generator.leafSurfaceNoiseAmp;
        leaf.surfaceNoiseFreq = generator.leafSurfaceNoiseFreq;
        leaf.midribTipTaperStart = generator.midribTipTaperStart;
        leaf.midribTextureStrength = generator.midribTextureStrength;
        leaf.midribWidth = generator.midribWidth;
        leaf.liguleWrapLengthScale = generator.liguleWrapLengthScale;
        leaf.liguleUnfoldSharpness = generator.liguleUnfoldSharpness;
        leaf.sheathOuterScale = generator.sheathOuterScale;
    }
}

MaizeGenerator::MaizeGenerator() {
    resetTraitLoadings();
}

const char* MaizeGenerator::getCorrelatedParameterName(int param) {
    return kCorrelatedParameterInfos[sanitizeCorrelatedParameter(param)].displayName;
}

MaizeGenerator::TraitLoadings MaizeGenerator::getDefaultTraitLoadings(int param) {
    return defaultLoadingsFor(param);
}

const MaizeGenerator::TraitLoadings& MaizeGenerator::getTraitLoadings(int param) const {
    return traitLoadings[sanitizeCorrelatedParameter(param)];
}

MaizeGenerator::TraitLoadings& MaizeGenerator::getTraitLoadings(int param) {
    return traitLoadings[sanitizeCorrelatedParameter(param)];
}

void MaizeGenerator::resetTraitLoadings() {
    for (int param = 0; param < CorrelatedParameter_Count; ++param) {
        traitLoadings[param] = defaultLoadingsFor(param);
    }
}

MaizeGenerator::TraitComputation MaizeGenerator::computeTraits(unsigned int seed, bool useMeansOnly) const {
    std::mt19937 gen(seed);
    TraitComputation traits;
    traits.species = speciesName.empty() ? std::string("Maize_Procedural") : speciesName;
    traits.phenotypeId = phenotypeId;
    traits.seed = seed;
    traits.useMeansOnly = useMeansOnly;
    traits.tillerType = "main";
    traits.tillerAlphaDeg = 0.0f;

    traits.latent = sampleLatentTraits(gen, useMeansOnly);
    traits.correlationStrength = std::clamp(traitCorrelationStrength, 0.0f, 1.0f);
    traits.stemRadius = std::max(0.005f, sampleCorrelatedValue(stemRadius, getTraitLoadings(CorrelatedParameter_StemRadius), traits.latent, traits.correlationStrength, gen, useMeansOnly));
    traits.stemShrink = std::max(stemShrinkMin, traits.stemRadius * stemShrinkFactor);

    float nLeavesFloat = sampleCorrelatedValue(numLeaves, getTraitLoadings(CorrelatedParameter_NumLeaves), traits.latent, traits.correlationStrength, gen, useMeansOnly);
    int nLeaves = (std::max)(3, (int)std::round(nLeavesFloat));

    traits.totalHeight = std::max(0.1f, sampleCorrelatedValue(height, getTraitLoadings(CorrelatedParameter_Height), traits.latent, traits.correlationStrength, gen, useMeansOnly));

    // Tassel droop is correlated like the other traits; latent loadings decide
    // how vigor/posture/etc. shift it (negative vigor loading -> upright when vigorous).
    traits.tasselDroop = std::max(0.0f, sampleCorrelatedValue(tasselDroop, getTraitLoadings(CorrelatedParameter_TasselDroop), traits.latent, traits.correlationStrength, gen, useMeansOnly));

    traits.plantLevel = samplePlantLevelTraits(*this, traits.latent, traits.correlationStrength, gen, useMeansOnly);

    const InternodeWeights internodeWeights = buildInternodeWeights(nLeaves, whorlStart, whorlCompression, whorlMinInternodeWeight);
    traits.leaves.reserve(nLeaves);

    for (int i = 0; i < nLeaves; ++i) {
        float t = (float)i / (float)(nLeaves - 1);

        ComputedLeafTraits leaf;
        leaf.id = i;
        leaf.canopyT = t;
        leaf.distance = traits.totalHeight * (internodeWeights.values[i] / internodeWeights.sum);
        leaf.azimuthDeg = Maize::normalizeAzimuthDeg(tillerAzimuthDeg * (float)i + hash_to_float_minus1_1(seed + (unsigned int)i) * tillerAzimuthNoise);

        float lenBase = jitterLeafTrait(gen, traits.plantLevel.leafLengthBase, leafLengthBase, useMeansOnly, leafJitterScale);
        leaf.leafLength = (std::max)(0.1f, lenBase * leafLengthScaleCurve.get(t));

        float widthBase = jitterLeafTrait(gen, traits.plantLevel.leafWidthBase, leafWidthBase, useMeansOnly, leafJitterScale);
        leaf.leafWidth = (std::max)(0.01f, widthBase * leafWidthScaleCurve.get(t));

        float widthTaperValue = jitterLeafTrait(gen, traits.plantLevel.widthTaper, widthTaper, useMeansOnly, leafJitterScale);
        leaf.widthTaper = (std::max)(0.1f, widthTaperValue * widthTaperScaleCurve.get(t));

        float angleBase = jitterLeafTrait(gen, traits.plantLevel.leafAngleBase, leafAngleBase, useMeansOnly, leafJitterScale);
        leaf.leafAngle = angleBase + leafAngleOffsetCurve.get(t);

        leaf.leafTwist = jitterLeafTrait(gen, traits.plantLevel.leafTwist, leafTwist, useMeansOnly, leafJitterScale) + leafTwistOffsetCurve.get(t);
        leaf.leafCurl = jitterLeafTrait(gen, traits.plantLevel.leafCurl, leafCurl, useMeansOnly, leafJitterScale) + leafCurlOffsetCurve.get(t);

        leaf.droopiness = jitterLeafTrait(gen, traits.plantLevel.droopinessBase, droopinessBase, useMeansOnly, leafJitterScale) + droopinessOffsetCurve.get(t);

        leaf.stemInclinationDeg = jitterLeafTrait(gen, traits.plantLevel.stemInclination, stemInclination, useMeansOnly, leafJitterScale) + stemInclinationOffsetCurve.get(t);

        leaf.splinePoints = (std::max)(4, splinePointsDefault);

        float waveAmpValue = jitterLeafTrait(gen, traits.plantLevel.waveAmp, waveAmp, useMeansOnly, leafJitterScale);
        float waveFreqValue = jitterLeafTrait(gen, traits.plantLevel.waveFreq, waveFreq, useMeansOnly, leafJitterScale);
        float wavePhaseValue = jitterLeafTrait(gen, traits.plantLevel.wavePhaseShift, wavePhaseShift, useMeansOnly, leafJitterScale);
        leaf.waveLAmp = waveAmpValue * waveAmpScaleCurve.get(t);
        leaf.waveLFreq = waveFreqValue * waveFreqScaleCurve.get(t);
        leaf.waveLPhase = wavePhaseValue;
        leaf.waveRAmp = leaf.waveLAmp;
        leaf.waveRFreq = leaf.waveLFreq;
        leaf.waveRPhase = wavePhaseValue + 1.57f; // phase shift between left and right edges

        applyLeafVisualDefaults(*this, leaf);
        traits.leaves.push_back(leaf);
    }

    return traits;
}

PlantDesc MaizeGenerator::buildPlantFromTraits(const TraitComputation& traits) const {
    PlantDesc plant;
    plant.species = traits.species;
    plant.phenotypeId = traits.phenotypeId;

    TillerDesc mainTiller;
    mainTiller.type = traits.tillerType;
    mainTiller.radius = traits.stemRadius;
    mainTiller.alphaDeg = traits.tillerAlphaDeg;
    mainTiller.stemShrink = traits.stemShrink;

    for (size_t i = 0; i < traits.leaves.size(); ++i) {
        const ComputedLeafTraits& computed = traits.leaves[i];
        LeafDesc L;
        L.id = computed.id;
        L.distance = computed.distance;
        L.leafLength = computed.leafLength;
        L.leafWidth = computed.leafWidth;
        L.azimuthDeg = computed.azimuthDeg;
        L.widthTaper = computed.widthTaper;
        L.leafAngle = computed.leafAngle;
        L.leafTwist = computed.leafTwist;
        L.leafCurl = computed.leafCurl;
        L.droopiness = computed.droopiness;
        L.stemInclinationDeg = computed.stemInclinationDeg;
        L.splinePoints = computed.splinePoints;
        L.waveLAmp = computed.waveLAmp;
        L.waveLFreq = computed.waveLFreq;
        L.waveLPhase = computed.waveLPhase;
        L.waveRAmp = computed.waveRAmp;
        L.waveRFreq = computed.waveRFreq;
        L.waveRPhase = computed.waveRPhase;
        L.surfaceNoiseAmp = computed.surfaceNoiseAmp;
        L.surfaceNoiseFreq = computed.surfaceNoiseFreq;
        L.midribTipTaperStart = computed.midribTipTaperStart;
        L.midribTextureStrength = computed.midribTextureStrength;
        L.midribWidth = computed.midribWidth;
        L.liguleWrapLengthScale = computed.liguleWrapLengthScale;
        L.liguleUnfoldSharpness = computed.liguleUnfoldSharpness;
        L.sheathOuterScale = computed.sheathOuterScale;
        mainTiller.leaves.push_back(L);
    }

    plant.tillers.push_back(mainTiller);

    // Tassel: applied per generated plant when enabled. The plant's own seed
    // drives the tassel genotype draw (so a set varies), and the latent vigor
    // trait modulates uprightness — vigorous plants hold the tassel upright,
    // weaker plants droop more.
    if (tasselEnabled) {
        plant.tassel = tasselParams;
        plant.tassel.enabled = true;
        plant.tassel.seed = traits.seed;
        // Droop comes from the correlated trait (latent-driven via the matrix).
        plant.tassel.matureDroopStrength = std::max(0.0f, traits.tasselDroop);
    }

    return plant;
}

PlantDesc MaizeGenerator::generate(unsigned int seed, bool useMeansOnly) {
    return buildPlantFromTraits(computeTraits(seed, useMeansOnly));
}

void MaizeGenerator::generateBatch(int count, const std::string& basePathPrefix, const std::string& leafTexPath, const std::string& stemTexPath, bool useMeansOnly, bool exportSkeleton, std::atomic<int>* outProgress, int startIndex, std::atomic<bool>* cancelFlag, bool skeletonIncludeTassel) {
    if (count <= 0) return;
    if (startIndex < 0) startIndex = 0;
    const int endIndex = startIndex + count;

    namespace fs = std::filesystem;

    fs::path prefixPath = basePathPrefix.empty() ? (fs::path("plant_export") / "plant") : fs::path(basePathPrefix);
    fs::path outputDir = prefixPath.parent_path();
    fs::path fileStem = prefixPath.filename();
    if (fileStem.empty()) {
        fileStem = "plant";
    }

    std::error_code ec;
    if (!outputDir.empty()) {
        fs::create_directories(outputDir, ec);
        if (ec) {
            std::cerr << "Failed to create output directory: " << outputDir.string() << std::endl;
            return;
        }
    }

    auto copyTextureToOutput = [&](const std::string& texPath) {
        // Copy textures once beside the exports
        if (texPath.empty() || outputDir.empty()) return;
        fs::path src(texPath);
        if (!fs::exists(src, ec)) return;
        fs::path dst = outputDir / src.filename();
        ec.clear();
        if (fs::exists(dst, ec)) {
            ec.clear();
            if (fs::equivalent(src, dst, ec)) return;
        }
        ec.clear();
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    };

    copyTextureToOutput(leafTexPath);
    if (!stemTexPath.empty() && stemTexPath != leafTexPath) {
        copyTextureToOutput(stemTexPath);
    }

    std::string leafTexFileOnly = leafTexPath.empty() ? "" : fs::path(leafTexPath).filename().string();
    std::string stemTexFileOnly = stemTexPath.empty() ? "" : fs::path(stemTexPath).filename().string();
    std::string leafTexFull = leafTexFileOnly.empty() ? "" : (outputDir / leafTexFileOnly).string();
    std::string stemTexFull = stemTexFileOnly.empty() ? "" : (outputDir / stemTexFileOnly).string();

    // Write the shared maize.mtl once upfront. All OBJs in the batch reference
    // the same mtllib, and concurrent writes from worker threads would race on
    // the same file handle, so this must be done serially before fanning out.
    {
        const std::string mtlFileName = "maize.mtl";
        fs::path mtlPath = outputDir.empty() ? fs::path(mtlFileName) : (outputDir / mtlFileName);
        const char* leafTexNameForMtl = leafTexFileOnly.empty() ? nullptr : leafTexFileOnly.c_str();
        const char* stemTexNameForMtl = stemTexFileOnly.empty()
            ? leafTexNameForMtl
            : stemTexFileOnly.c_str();
        MaizeModel mtlWriter;
        mtlWriter.saveMTL(mtlPath.string().c_str(), leafTexNameForMtl, stemTexNameForMtl);
    }

    // Decide worker count. Each worker owns a private MaizeModel and a private
    // slice of plant indices, so there is no shared mutable state across
    // threads beyond the atomic progress counter.
    const unsigned hw = std::thread::hardware_concurrency();
    int workerCount = (int)((hw == 0) ? 2u : hw);
    if (workerCount > count) workerCount = count;
    if (workerCount < 1) workerCount = 1;

    auto workerFn = [&](int workerIndex) {
        MaizeModel localModel;
        for (int i = startIndex + workerIndex; i < endIndex; i += workerCount) {
            if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) break;
            unsigned int seed = (unsigned int)i;
            PlantDesc plant = generate(seed, useMeansOnly);

            std::ostringstream suffix;
            suffix << "_" << std::setw(4) << std::setfill('0') << i;
            fs::path plantPath = outputDir / (fileStem.string() + suffix.str());

            Maize::GeneratePlantWith(localModel, plant, plantPath.string(), leafTexFull, stemTexFull, exportSkeleton, /*writeMTL=*/false, skeletonIncludeTassel);

            if (outProgress) outProgress->fetch_add(1, std::memory_order_relaxed);
        }
    };

    if (workerCount == 1) {
        workerFn(0);
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (int w = 0; w < workerCount; ++w) {
        workers.emplace_back(workerFn, w);
    }
    for (auto& t : workers) t.join();
}

bool MaizeGenerator::saveConfig(const std::string& path) const {
    using namespace tinyxml2;

    XMLDocument doc;
    doc.InsertFirstChild(doc.NewDeclaration());

    XMLElement* root = doc.NewElement("MaizeGeneratorConfig");
    root->SetAttribute("version", 4);
    root->SetAttribute("speciesName", speciesName.c_str());
    root->SetAttribute("phenotypeId", phenotypeId.c_str());
    doc.InsertEndChild(root);

    auto saveDist = [&](const char* name, const Dist& d) {
        XMLElement* elem = doc.NewElement(name);
        elem->SetAttribute("mean", d.mean);
        elem->SetAttribute("stdDev", d.stdDev);
        root->InsertEndChild(elem);
    };

    auto saveCurve3 = [&](const char* name, const Curve3& c) {
        XMLElement* elem = doc.NewElement(name);
        elem->SetAttribute("bottom", c.bottom);
        elem->SetAttribute("middle", c.middle);
        elem->SetAttribute("top", c.top);
        root->InsertEndChild(elem);
    };

    auto saveFloat = [&](const char* name, float value) {
        XMLElement* elem = doc.NewElement(name);
        elem->SetAttribute("value", value);
        root->InsertEndChild(elem);
    };

    auto saveInt = [&](const char* name, int value) {
        XMLElement* elem = doc.NewElement(name);
        elem->SetAttribute("value", value);
        root->InsertEndChild(elem);
    };

    auto saveTraitLoadings = [&]() {
        XMLElement* matrixElem = doc.NewElement("traitLoadingsMatrix");
        for (int param = 0; param < CorrelatedParameter_Count; ++param) {
            const TraitLoadings& loadings = getTraitLoadings(param);
            XMLElement* rowElem = doc.NewElement("parameter");
            rowElem->SetAttribute("key", getCorrelatedParameterKey(param));
            rowElem->SetAttribute("vigor", loadings.vigor);
            rowElem->SetAttribute("slenderness", loadings.slenderness);
            rowElem->SetAttribute("posture", loadings.posture);
            rowElem->SetAttribute("texture", loadings.texture);
            matrixElem->InsertEndChild(rowElem);
        }
        root->InsertEndChild(matrixElem);
    };

    for (const auto& field : kDistFields) {
        saveDist(field.name, this->*(field.member));
    }
    for (const auto& field : kFloatFields) {
        saveFloat(field.name, this->*(field.member));
    }
    for (const auto& field : kCurve3Fields) {
        saveCurve3(field.name, this->*(field.member));
    }
    saveTraitLoadings();
    for (const auto& field : kIntFields) {
        saveInt(field.name, this->*(field.member));
    }

    // Tassel template (omits the per-plant seed, which is derived at generation).
    {
        XMLElement* t = doc.NewElement("Tassel");
        t->SetAttribute("enabled", tasselEnabled ? 1 : 0);
        t->SetAttribute("scale", tasselParams.scale);
        t->SetAttribute("branchNodeCount", tasselParams.branchNodeCount);
        t->SetAttribute("spikeNodeCount", tasselParams.spikeNodeCount);
        t->SetAttribute("secondaryNodeCount", tasselParams.secondaryNodeCount);
        t->SetAttribute("phyllotaxisAngle", tasselParams.phyllotaxisAngle);
        t->SetAttribute("lateralThicknessRatio", tasselParams.lateralThicknessRatio);
        t->SetAttribute("secondaryInsertionAngle", tasselParams.secondaryInsertionAngle);
        t->SetAttribute("plastochronGdd", tasselParams.plastochronGdd);
        t->SetAttribute("maturityGdd", tasselParams.maturityGdd);
        t->SetAttribute("branchAngleRelaxation", tasselParams.branchAngleRelaxation);
        t->SetAttribute("matureDroopStrength", tasselParams.matureDroopStrength);
        t->SetAttribute("matureDroopStartT", tasselParams.matureDroopStartT);
        t->SetAttribute("gravitropism", tasselParams.gravitropism);
        root->InsertEndChild(t);
    }

    return doc.SaveFile(path.c_str()) == XML_SUCCESS;
}

bool MaizeGenerator::loadConfig(const std::string& path, std::string* outError) {
    static const int kCurrentVersion = 4;
    using namespace tinyxml2;

    XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != XML_SUCCESS) {
        if (outError) *outError = std::string("Could not open '") + path + "': " + doc.ErrorStr();
        return false;
    }

    const XMLElement* root = doc.FirstChildElement("MaizeGeneratorConfig");
    if (!root) {
        if (outError) *outError = "No <MaizeGeneratorConfig> root element in '" + path + "'";
        return false;
    }

    int version = 0;
    root->QueryIntAttribute("version", &version);
    if (version > kCurrentVersion && outError) {
        *outError = "Config version " + std::to_string(version) + " is newer than supported ("
                    + std::to_string(kCurrentVersion) + "); some settings may not load correctly.";
    }

    if (const char* s = root->Attribute("speciesName")) speciesName = s;
    if (const char* p = root->Attribute("phenotypeId")) phenotypeId = p;

    auto loadDist = [&](const char* name, Dist& d) {
        if (const XMLElement* elem = root->FirstChildElement(name)) {
            elem->QueryFloatAttribute("mean", &d.mean);
            elem->QueryFloatAttribute("stdDev", &d.stdDev);
        }
    };

    auto loadCurve3 = [&](const char* name, Curve3& c) {
        if (const XMLElement* elem = root->FirstChildElement(name)) {
            elem->QueryFloatAttribute("bottom", &c.bottom);
            elem->QueryFloatAttribute("middle", &c.middle);
            elem->QueryFloatAttribute("top", &c.top);
        }
    };

    auto loadFloat = [&](const char* name, float& value) {
        if (const XMLElement* elem = root->FirstChildElement(name)) {
            elem->QueryFloatAttribute("value", &value);
        }
    };

    auto loadInt = [&](const char* name, int& value) {
        if (const XMLElement* elem = root->FirstChildElement(name)) {
            elem->QueryIntAttribute("value", &value);
        }
    };

    auto loadTraitLoadings = [&]() {
		// Start from defaults so partial config files can override just a few rows without needing to specify them all
        resetTraitLoadings();
        if (const XMLElement* matrixElem = root->FirstChildElement("traitLoadingsMatrix")) {
            for (const XMLElement* rowElem = matrixElem->FirstChildElement("parameter"); rowElem; rowElem = rowElem->NextSiblingElement("parameter")) {
                int param = findCorrelatedParameterByKey(rowElem->Attribute("key"));
                if (param < 0) continue;
                TraitLoadings& loadings = getTraitLoadings(param);
                rowElem->QueryFloatAttribute("vigor", &loadings.vigor);
                rowElem->QueryFloatAttribute("slenderness", &loadings.slenderness);
                rowElem->QueryFloatAttribute("posture", &loadings.posture);
                rowElem->QueryFloatAttribute("texture", &loadings.texture);
            }
        }
    };

    for (const auto& field : kDistFields) {
        loadDist(field.name, this->*(field.member));
    }
    for (const auto& field : kFloatFields) {
        loadFloat(field.name, this->*(field.member));
    }
    for (const auto& field : kCurve3Fields) {
        loadCurve3(field.name, this->*(field.member));
    }
    loadTraitLoadings();
    for (const auto& field : kIntFields) {
        loadInt(field.name, this->*(field.member));
    }

    // Tassel template (absent in older configs -> current defaults retained).
    if (const XMLElement* t = root->FirstChildElement("Tassel")) {
        int en = tasselEnabled ? 1 : 0;
        t->QueryIntAttribute("enabled", &en);
        tasselEnabled = (en != 0);
        t->QueryFloatAttribute("scale", &tasselParams.scale);
        t->QueryIntAttribute("branchNodeCount", &tasselParams.branchNodeCount);
        t->QueryIntAttribute("spikeNodeCount", &tasselParams.spikeNodeCount);
        t->QueryIntAttribute("secondaryNodeCount", &tasselParams.secondaryNodeCount);
        t->QueryFloatAttribute("phyllotaxisAngle", &tasselParams.phyllotaxisAngle);
        t->QueryFloatAttribute("lateralThicknessRatio", &tasselParams.lateralThicknessRatio);
        t->QueryFloatAttribute("secondaryInsertionAngle", &tasselParams.secondaryInsertionAngle);
        t->QueryFloatAttribute("plastochronGdd", &tasselParams.plastochronGdd);
        t->QueryFloatAttribute("maturityGdd", &tasselParams.maturityGdd);
        t->QueryFloatAttribute("branchAngleRelaxation", &tasselParams.branchAngleRelaxation);
        t->QueryFloatAttribute("matureDroopStrength", &tasselParams.matureDroopStrength);
        t->QueryFloatAttribute("matureDroopStartT", &tasselParams.matureDroopStartT);
        t->QueryFloatAttribute("gravitropism", &tasselParams.gravitropism);
    }

    traitCorrelationStrength = std::clamp(traitCorrelationStrength, 0.0f, 1.0f);
    splinePointsDefault = (std::max)(4, splinePointsDefault);

    return true;
}
