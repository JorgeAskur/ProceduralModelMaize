#pragma once
#include <vector>
#include <random>
#include "Maize.h"

// Statistical Distribution Helper
struct Dist {
    float mean = 0.0f;
    float stdDev = 0.0f;

    // Functions inside struct/class definitions are implicitly inline, 
    // so these won't cause linker errors.
    float get(std::mt19937& gen) const {
        if (stdDev <= 0.0001f) return mean;
        std::normal_distribution<float> d(mean, stdDev);
        return d(gen);
    }
};

// Curve Helper
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
    Dist stemRadius = { 0.025f, 0.002f };
    Dist numLeaves = { 12.0f, 1.0f };

    // Leaf Parameters
    Dist leafLengthBase = { 0.8f, 0.1f };
    Curve leafLengthCurve = { 0.5f, 1.0f };

    Dist leafWidthBase = { 0.1f, 0.01f };
    Curve leafWidthCurve = { 1.0f, 0.7f };

    Dist leafAngleBase = { 45.0f, 5.0f };
    Curve leafAngleCurve = { 1.0f, 0.5f };

    Dist droopinessBase = { 0.5f, 0.1f };

    Dist waveAmp = { 0.01f, 0.005f };
    Dist waveFreq = { 5.0f, 1.0f };

    PlantDesc generate(unsigned int seed);
};