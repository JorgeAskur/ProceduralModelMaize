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

// Curve Helper (Interpolates t from 0..1)
struct Curve {
    float bottom = 1.0f;
    float middle = 1.0f;
    float top = 1.0f;

    float get(float t) const {
        if (t <= 0.5f) {
            float local = t / 0.5f;
            return bottom * (1.f - local) + middle * local;
        }
        float local = (t - 0.5f) / 0.5f;
        return middle * (1.f - local) + top * local;
    }
};

class MaizeGenerator {
public:
    // Tiller Parameters
    Dist height = { 2.0f, 0.2f };
    Dist stemRadius = { 0.02f, 0.002f };
    Dist numLeaves = { 8.0f, 1.0f };

    // Leaf Parameters
    Dist leafLengthBase = { 0.4f, 0.1f };
    bool useLeafLengthCurve = false;
    Curve leafLengthCurve = { 0.4f, 0.4f, 0.4f };

    Dist leafWidthBase = { 0.1f, 0.01f };
    bool useLeafWidthCurve = false;
    Curve leafWidthCurve = { 0.1f, 0.1f, 0.1f };

    Dist widthTaper = { 5.0f, 0.1f };

    Dist leafAngleBase = { 45.0f, 5.0f };
    bool useLeafAngleCurve = false;
    Curve leafAngleCurve = { 45.0f, 45.0f, 45.0f };

    Dist leafTwist = { 10.0f, 10.0f };
    bool useLeafTwistCurve = false;
    Curve leafTwistCurve = { 10.0f, 10.0f, 10.0f };

    Dist droopinessBase = { -20.0f, 5.f };
    bool useDroopinessCurve = false;
    Curve droopinessCurve = { -20.0f, -20.0f, -20.0f };

    Dist stemInclination = { 0.0f, 5.0f };

    // Noise / Organic Feel
    Dist waveAmp = { 0.01f, 0.005f };
    Dist waveFreq = { 8.0f, 2.0f };
    Dist wavePhaseShift = { 0.0f, 1.0f };

    float leafJitterScale = 0.2f;
    float stemRibbonSpacing = 0.3f;

    // Generates a single plant description
    // If useMeansOnly is true, stdDev is ignored (creates the "Average Plant")
    PlantDesc generate(unsigned int seed, bool useMeansOnly = false);

    // Generates N plants and saves them to disk
    void generateBatch(int count, const std::string& outputDir, const std::string& leafTexPath, const std::string& stemTexPath);

    // Updates the generator mean values from a loaded plant description
    void updateFromPlant(const PlantDesc& plant);

    // Save/load generator settings
    bool saveSettings(const char* path) const;
    bool loadSettings(const char* path);
};