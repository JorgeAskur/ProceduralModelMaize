#pragma once
#include <string>
#include <map>
#include <vector>
#include "Maize.h"

namespace Descriptor {
	// Default seed parameters
	LeafParams defaultLeafParamsSeed();

    // Loads XML into Maize::plant(), Maize::edits(), and Maize::leafParams().
    bool loadPlantXML(const std::string& path);

    // Saves current state (species, tillers/leaves, per-leaf edits and params) to XML.
    bool savePlantXML(const char* path);
}
