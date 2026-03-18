#pragma once
#include <vector>
#include <random>
#include <string>
#include "Maize.h"

// Statistical Distribution Helper
struct Dist {
    float mean = 0.0f;
    float stdDev = 0.0f;

    // Added forceMean to allow generating the "Average Plant"
    float get(std::mt19937& gen, bool forceMean = false) const {
        if (forceMean) return mean;
        if (stdDev <= 0.0001f) return mean;
        std::normal_distribution<float> d(mean, stdDev);
        return d(gen);
    }
};

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
    // Tiller Parameters
    Dist height = { 2.0f, 0.2f };
    Dist stemRadius = { 0.02f, 0.002f };
    Dist numLeaves = { 8.0f, 1.0f };
    float tillerAzimuthDeg = 180.0f;
    float tillerAzimuthNoise = 15.0f;
    float stemShrinkFactor = 0.65f;
    float stemShrinkMin = 0.002f;

    // Leaf Parameters
    Dist leafLengthBase = { 0.4f, 0.1f };

    Dist leafWidthBase = { 0.1f, 0.01f };

    Dist widthTaper = { 5.0f, 0.1f };

    Dist leafAngleBase = { 45.0f, 5.0f };

    Dist leafTwist = { 10.0f, 10.0f };

    Dist leafCurl = { 0.0f, 0.25f };

    Dist droopinessBase = { -20.0f, 5.f };

    Dist stemInclination = { 0.0f, 5.0f };

    // Noise / Organic Feel
    Dist waveAmp = { 0.01f, 0.005f };
    Dist waveFreq = { 8.0f, 2.0f };
    Dist wavePhaseShift = { 0.0f, 1.0f };

    // Architecture shaping
    float whorlStart = 0.65f;
    float whorlCompression = 0.6f;
    float whorlMinInternodeWeight = 0.35f;
    float lowerLeafAngleBoost = 8.0f;
    float upperLeafAngleDrop = 10.0f;

    // Global leaf visual shaping defaults (applied to each generated leaf)
    float leafSurfaceNoiseAmp = 0.01f;
    float leafSurfaceNoiseFreq = 8.0f;
    float leafTipWidthFactor = 0.04f;
    float midribTipTaperStart = 0.75f;
    float midribTextureStrength = 0.35f;
    float liguleWrapLengthScale = 6.8f;
    float liguleUnfoldSharpness = 2.2f;
    float sheathOuterScale = 1.12f;

    // Per-canopy curves (bottom/middle/top)
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

    float leafJitterScale = 0.2f;
    int splinePointsDefault = 40;

    // Generates a single plant description
    // If useMeansOnly is true, stdDev is ignored (creates the "Average Plant")
    PlantDesc generate(unsigned int seed, bool useMeansOnly = false);

    // Generates N plants and saves them to disk
    void generateBatch(int count, const std::string& outputDir, const std::string& texPath);
};