#pragma once
#include <string>

namespace Descriptor {
    bool loadPlantXML(const std::string& path);
    bool savePlantXML(const char* path);
    bool loadSkeletonXML(const std::string& path);
    bool saveSkeletonXML(const char* path);
}
