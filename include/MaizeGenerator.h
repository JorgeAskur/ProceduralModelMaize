#pragma once
#include <array>
#include <atomic>
#include <vector>
#include <random>
#include <string>
#include "Maize.h"

// Gaussian parameter helper used by the procedural generator
// Every distribution-backed trait in the UI is expressed as mean/std-dev. The
// generator can either sample from the distribution or force the mean to build
// the deterministic "average plant" preview
struct Dist {
    float mean = 0.0f;
    float stdDev = 0.0f;

    float get(std::mt19937& gen, bool forceMean = false) const {
        if (forceMean) return mean;
        if (stdDev <= 0.0001f) return mean;
        std::normal_distribution<float> d(mean, stdDev);
        return d(gen);
    }
};

// Simple bottom/middle/top canopy profile curve
// Many generator controls vary by canopy height This compact curve lets the UI
// expose a three-handle profile without requiring a full spline editor
struct Curve3 {
    float bottom = 1.0f;
    float middle = 1.0f;
    float top = 1.0f;

    float get(float t) const {
        t = (std::max)(0.0f, (std::min)(1.0f, t));
        if (t <= 0.5f) {
            float u = t * 2.0f;
            return bottom * (1.0f - u) + middle * u;
        }
        float u = (t - 0.5f) * 2.0f;
        return middle * (1.0f - u) + top * u;
    }
};

class MaizeGenerator {
public:
    // Per-parameter loading vector for the latent trait model
    // Each correlated parameter can respond to four hidden whole-plant traits
    // For example, height can load on vigor/slenderness, while waves can load
    // primarily on texture
    struct TraitLoadings {
        float vigor = 0.0f;
        float slenderness = 0.0f;
        float posture = 0.0f;
        float texture = 0.0f;
    };

    // The sampled hidden factors for one plant draw. These are exposed so
    // callers can inspect or reuse the exact latent state behind a generated
    // plant instead of only seeing the final PlantDesc
    struct LatentTraits {
        float vigor = 0.0f;
        float slenderness = 0.0f;
        float posture = 0.0f;
        float texture = 0.0f;
    };

    // Plant-level correlated base traits sampled once per plant before canopy
    // curves and per-leaf jitter are applied
    struct PlantLevelTraits {
        float leafLengthBase = 0.0f;
        float leafWidthBase = 0.0f;
        float widthTaper = 0.0f;
        float leafAngleBase = 0.0f;
        float leafTwist = 0.0f;
        float leafCurl = 0.0f;
        float droopinessBase = 0.0f;
        float stemInclination = 0.0f;
        float waveAmp = 0.0f;
        float waveFreq = 0.0f;
        float wavePhaseShift = 0.0f;
    };

    // Fully resolved leaf traits after canopy-curve modulation, jitter, and
    // generator-default visual fields have all been applied
    struct ComputedLeafTraits {
        int id = 0;
        float canopyT = 0.0f;
        float distance = 0.0f;
        float leafLength = 0.0f;
        float leafWidth = 0.0f;
        float azimuthDeg = 0.0f;
        float widthTaper = 0.0f;
        float leafAngle = 0.0f;
        float leafTwist = 0.0f;
        float leafCurl = 0.0f;
        float droopiness = 0.0f;
        float stemInclinationDeg = 0.0f;
        int splinePoints = 0;
        float waveLAmp = 0.0f;
        float waveLFreq = 0.0f;
        float waveLPhase = 0.0f;
        float waveRAmp = 0.0f;
        float waveRFreq = 0.0f;
        float waveRPhase = 0.0f;
        float surfaceNoiseAmp = 0.0f;
        float surfaceNoiseFreq = 0.0f;
        float midribTipTaperStart = 0.0f;
        float midribTextureStrength = 0.0f;
        float midribWidth = 0.0f;
        float liguleWrapLengthScale = 0.0f;
        float liguleUnfoldSharpness = 0.0f;
        float sheathOuterScale = 0.0f;
    };

    // Snapshot of the full trait computation for one plant draw
    struct TraitComputation {
        std::string species = "Maize_Procedural";
        std::string phenotypeId;
        unsigned int seed = 0;
        bool useMeansOnly = false;
        float correlationStrength = 0.0f;
        std::string tillerType = "main";
        float tillerAlphaDeg = 0.0f;
        float stemRadius = 0.0f;
        float totalHeight = 0.0f;
        float stemShrink = 0.0f;
        float tasselDroop = 0.0f;   // correlated tassel droop (degrees), latent-driven
        LatentTraits latent;
        PlantLevelTraits plantLevel;
        std::vector<ComputedLeafTraits> leaves;
    };

    enum CorrelatedParameter {
        CorrelatedParameter_StemRadius = 0,
        CorrelatedParameter_NumLeaves,
        CorrelatedParameter_Height,
        CorrelatedParameter_LeafLengthBase,
        CorrelatedParameter_LeafWidthBase,
        CorrelatedParameter_WidthTaper,
        CorrelatedParameter_LeafAngleBase,
        CorrelatedParameter_LeafTwist,
        CorrelatedParameter_LeafCurl,
        CorrelatedParameter_DroopinessBase,
        CorrelatedParameter_StemInclination,
        CorrelatedParameter_WaveAmp,
        CorrelatedParameter_WaveFreq,
        CorrelatedParameter_WavePhaseShift,
        CorrelatedParameter_TasselDroop,
        CorrelatedParameter_Count
    };

    MaizeGenerator();

    // Global identity parameters applied to every generated plant
    std::string speciesName = "Maize_Procedural";
    std::string phenotypeId;

    // Whole-plant / tiller distributions
    Dist height = { 1.2f, 0.2f };
    Dist stemRadius = { 0.02f, 0.002f };
    Dist numLeaves = { 8.0f, 1.0f };
    float tillerAzimuthDeg = 180.0f;
    float tillerAzimuthNoise = 20.0f;
    float stemShrinkFactor = 0.65f;
    float stemShrinkMin = 0.002f;

    // Base leaf trait distributions sampled once per plant, then optionally
    // perturbed per leaf by leafJitterScale
    Dist leafLengthBase = { 0.4f, 0.1f };
    Dist leafWidthBase = { 0.1f, 0.01f };
    Dist widthTaper = { 5.0f, 0.1f };
    Dist leafAngleBase = { 45.0f, 5.0f };
    Dist leafTwist = { 10.0f, 10.0f };
    Dist leafCurl = { 0.0f, 0.25f };
    Dist droopinessBase = { -20.0f, 5.f };
    Dist stemInclination = { 0.0f, 1.0f };

    // Edge-wave parameters used to add serration-like undulation
    Dist waveAmp = { 0.01f, 0.005f };
    Dist waveFreq = { 8.0f, 2.0f };
    Dist wavePhaseShift = { 0.0f, 1.0f };

    // Vertical architecture controls
    float whorlStart = 0.65f;
    float whorlCompression = 0.6f;
    float whorlMinInternodeWeight = 0.35f;

    // Visual shaping defaults copied into every generated LeafDesc
    float leafSurfaceNoiseAmp = 0.01f;
    float leafSurfaceNoiseFreq = 8.0f;
    float midribTipTaperStart = 0.75f;
    float midribTextureStrength = 0.35f;
    float midribWidth = 0.075f;         // Gaussian half-width of the midrib ridge (UV space); varies by genotype
    float liguleWrapLengthScale = 6.8f;
    float liguleUnfoldSharpness = 2.2f;
    float sheathOuterScale = 1.12f;

    // Per-canopy modifiers. Multiplicative curves scale base values, while
    // offset curves add canopy-position-specific bias
    Curve3 leafLengthScaleCurve = { 1.0f, 1.0f, 1.0f };
    Curve3 leafWidthScaleCurve = { 1.0f, 1.0f, 1.0f };
    Curve3 widthTaperScaleCurve = { 1.0f, 1.0f, 1.0f };
    Curve3 waveAmpScaleCurve = { 1.0f, 1.0f, 1.0f };
    Curve3 waveFreqScaleCurve = { 1.0f, 1.0f, 1.0f };

    Curve3 leafAngleOffsetCurve = { 0.0f, 0.0f, 0.0f };
    Curve3 droopinessOffsetCurve = { 0.0f, 0.0f, 0.0f };
    Curve3 leafTwistOffsetCurve = { 0.0f, 0.0f, 0.0f };
    Curve3 leafCurlOffsetCurve = { 0.0f, 0.0f, 0.0f };
    Curve3 stemInclinationOffsetCurve = { 0.0f, 0.0f, 0.0f };

    float leafJitterScale = 0.2f; // Fraction of std-dev reused as leaf-to-leaf jitter inside one plant
    float traitCorrelationStrength = 0.7f; // Global blend from independent draws to the full latent model
    std::array<TraitLoadings, CorrelatedParameter_Count> traitLoadings = {}; // Editable loading matrix shown in the UI
    int splinePointsDefault = 40; // Default spline resolution assigned to newly generated leaves

    // Tassel template applied to every generated plant when tasselEnabled is set.
    // Each plant's tassel is seeded from its own plant seed so a set varies, and
    // its uprightness is modulated by the latent vigor trait (see buildPlantFromTraits).
    bool tasselEnabled = false;
    TasselDesc tasselParams;                  // base genotype knobs (also the per-plant defaults)

    // Tassel droop is a correlated trait (see CorrelatedParameter_TasselDroop):
    // its mean is the baseline droop and the latent loadings in the trait matrix
    // decide how vigor/slenderness/posture/texture shift it. Vigor has a strong
    // negative loading so vigorous plants hold the tassel upright.
    Dist tasselDroop = { 0.0f, 12.0f };

    // Exposes the current internal trait sampler as a reusable public API
    TraitComputation computeTraits(unsigned int seed, bool useMeansOnly = false) const;
    PlantDesc buildPlantFromTraits(const TraitComputation& traits) const;

    // Generates a single procedural PlantDesc from the current settings
    // If useMeansOnly is true, all distribution sampling is collapsed to means
    PlantDesc generate(unsigned int seed, bool useMeansOnly = false);

    // Generates and exports a numbered set of plants. Plants are indexed
    // [startIndex, startIndex + count); the seed and filename suffix both use
    // the plant index so separate batches from different configs can populate
    // non-overlapping index ranges into the same output directory.
    // If outProgress is non-null, it is incremented once per completed plant
    // so a UI thread can display progress while the batch runs on a worker
    // thread.
    void generateBatch(int count, const std::string& basePathPrefix, const std::string& leafTexPath, const std::string& stemTexPath = "", bool useMeansOnly = false, bool exportSkeleton = false, std::atomic<int>* outProgress = nullptr, int startIndex = 0, std::atomic<bool>* cancelFlag = nullptr, bool skeletonIncludeTassel = true);

    // Persistence for the generator UI state
    bool saveConfig(const std::string& path) const;
    bool loadConfig(const std::string& path, std::string* outError = nullptr);

    static const char* getCorrelatedParameterName(int param);
    static TraitLoadings getDefaultTraitLoadings(int param);
    const TraitLoadings& getTraitLoadings(int param) const;
    TraitLoadings& getTraitLoadings(int param);
    void resetTraitLoadings();
};
