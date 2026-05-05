#pragma once
#include <string>

struct PlantDesc;

namespace Descriptor {
    bool loadPlantXML(const std::string& path, std::string* outError = nullptr);
    bool savePlantXML(const char* path);
    // Thread-safe overload that serializes an explicit PlantDesc without touching Maize::plant()
    bool savePlantXML(const PlantDesc& plant, const char* path);
    bool loadSkeletonXML(const std::string& path, std::string* outError = nullptr);
    bool saveSkeletonXML(const char* path);
    bool saveSkeletonXML(const PlantDesc& plant, const char* path);
}
