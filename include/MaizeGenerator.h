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
    float top = 1.0f;

    float get(float t) const {
        return bottom * (1.f - t) + top * t;
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

    Dist leafWidthBase = { 0.1f, 0.01f };

    Dist widthTaper = { 5.0f, 0.1f }; // NEW

    Dist leafAngleBase = { 45.0f, 5.0f };

    Dist leafTwist = { 10.0f, 10.0f }; // NEW (Degrees)

    Dist droopinessBase = { -20.0f, 5.f };

    Dist stemInclination = { 0.0f, 5.0f };

    // Noise / Organic Feel
    Dist waveAmp = { 0.01f, 0.005f };
    Dist waveFreq = { 8.0f, 2.0f };
    Dist wavePhaseShift = { 0.0f, 1.0f };

    // Sheath
    Dist sheathEndAngle = { 1.50f, 0.1f };
    Dist sheathOpening = { 0.90f, 0.1f };

    float leafJitterScale = 0.2f;

    // Generates a single plant description
    // If useMeansOnly is true, stdDev is ignored (creates the "Average Plant")
    PlantDesc generate(unsigned int seed, bool useMeansOnly = false);

    // Generates N plants and saves them to disk
    void generateBatch(int count, const std::string& outputDir, const std::string& texPath);
};