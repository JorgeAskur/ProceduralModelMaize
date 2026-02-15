#include "Descriptor.h"
#include "Maize.h"
#include "tinyxml2.h"
#include <string>
#include <vector>

using namespace tinyxml2;

namespace Descriptor {

    // Save splines as XML attributes
    static void saveSplineAsAttributes(XMLElement* parent, const char* name, const std::vector<Vect3d>& ctrl) {
        XMLElement* spline_elem = parent->GetDocument()->NewElement(name);
        spline_elem->SetAttribute("count", (int)ctrl.size());
        for (size_t i = 0; i < ctrl.size(); ++i) {
            std::string px = "p" + std::to_string(i) + "x";
            std::string py = "p" + std::to_string(i) + "y";
            std::string pz = "p" + std::to_string(i) + "z";
            spline_elem->SetAttribute(px.c_str(), ctrl[i].x());
            spline_elem->SetAttribute(py.c_str(), ctrl[i].y());
            spline_elem->SetAttribute(pz.c_str(), ctrl[i].z());
        }
        parent->InsertEndChild(spline_elem);
    }

    // Load spline control points from XML attributes
    static void loadSplineFromAttributes(const XMLElement* parent, const char* name, std::vector<Vect3d>& out_ctrl) {
        out_ctrl.clear();
        const XMLElement* spline_elem = parent->FirstChildElement(name);
        if (spline_elem) {
            int count = 0;
            spline_elem->QueryIntAttribute("count", &count);
            out_ctrl.reserve(count);
            for (int i = 0; i < count; ++i) {
                std::string px = "p" + std::to_string(i) + "x";
                std::string py = "p" + std::to_string(i) + "y";
                std::string pz = "p" + std::to_string(i) + "z";

                float x, y, z;
                spline_elem->QueryFloatAttribute(px.c_str(), &x);
                spline_elem->QueryFloatAttribute(py.c_str(), &y);
                spline_elem->QueryFloatAttribute(pz.c_str(), &z);

                Vect3d p{ x, y, z };
                out_ctrl.push_back(p);
            }
        }
    }

    // Save the plant description and edits to an XML file
    bool savePlantXML(const char* path) {
        XMLDocument doc;
        doc.InsertFirstChild(doc.NewDeclaration());

        XMLElement* root = doc.NewElement("plant");
        doc.InsertEndChild(root);

        XMLElement* species = doc.NewElement("species");
        species->SetAttribute("name", Maize::plant().species.c_str());
        root->InsertEndChild(species);

        for (size_t ti = 0; ti < Maize::plant().tillers.size(); ++ti) {
            const auto& td = Maize::plant().tillers[ti];
            XMLElement* tiller_elem = doc.NewElement("Tiller");
            tiller_elem->SetAttribute("type", td.type.c_str());
            tiller_elem->SetAttribute("radius", td.radius);
            tiller_elem->SetAttribute("alpha", td.alphaDeg);
            tiller_elem->SetAttribute("beta", td.azimuthDeg);
            tiller_elem->SetAttribute("randomSeed", td.randomSeed);
            tiller_elem->SetAttribute("stemShrink", td.stemShrink);
            if (td.useStemCtrlOverrides && !td.stemCtrlOverride.empty()) {
                tiller_elem->SetAttribute("useStemCtrlOverrides", 1);
                saveSplineAsAttributes(tiller_elem, "stemCtrl", td.stemCtrlOverride);
            }

            XMLElement* leaves_elem = doc.NewElement("leaves");
            leaves_elem->SetAttribute("number", (int)td.leaves.size());

            for (size_t li = 0; li < td.leaves.size(); ++li) {
                const auto& L = td.leaves[li];
                XMLElement* leaf_elem = doc.NewElement("leaf");
                leaf_elem->SetAttribute("id", L.id);
                leaf_elem->SetAttribute("distance", L.distance);

                // Save new high-level parameters
                leaf_elem->SetAttribute("leafLength", L.leafLength);
                leaf_elem->SetAttribute("leafWidth", L.leafWidth);
                leaf_elem->SetAttribute("leafAngle", L.leafAngle);
                leaf_elem->SetAttribute("droopiness", L.droopiness);
                leaf_elem->SetAttribute("stemInclinationDeg", L.stemInclinationDeg);
                leaf_elem->SetAttribute("splinePoints", L.splinePoints);
                leaf_elem->SetAttribute("widthTaper", L.widthTaper);
                leaf_elem->SetAttribute("leafTwist", L.leafTwist);

                leaf_elem->SetAttribute("waveLAmp", L.waveLAmp);
                leaf_elem->SetAttribute("waveLFreq", L.waveLFreq);
                leaf_elem->SetAttribute("waveLPhase", L.waveLPhase);
                leaf_elem->SetAttribute("waveRAmp", L.waveRAmp);
                leaf_elem->SetAttribute("waveRFreq", L.waveRFreq);
                leaf_elem->SetAttribute("waveRPhase", L.waveRPhase);

                if (L.useCtrlOverrides && !L.ctrlCenterOverride.empty()) {
                    leaf_elem->SetAttribute("useCtrlOverrides", 1);
                    saveSplineAsAttributes(leaf_elem, "ctrlCenter", L.ctrlCenterOverride);
                    saveSplineAsAttributes(leaf_elem, "ctrlLeft", L.ctrlLeftOverride);
                    saveSplineAsAttributes(leaf_elem, "ctrlRight", L.ctrlRightOverride);
                }

                leaves_elem->InsertEndChild(leaf_elem);
            }
            tiller_elem->InsertEndChild(leaves_elem);
            species->InsertEndChild(tiller_elem);
        }
        return (doc.SaveFile(path) == XML_SUCCESS);
    }

    // Load the plant description and edits from an XML file
    bool loadPlantXML(const std::string& path) {
        XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != XML_SUCCESS) return false;

        Maize::plant() = PlantDesc{};

        const XMLElement* species = doc.FirstChildElement("plant")->FirstChildElement("species");
        if (!species) return false;
        Maize::plant().species = species->Attribute("name");

        for (const XMLElement* t = species->FirstChildElement("Tiller"); t; t = t->NextSiblingElement("Tiller")) {
            TillerDesc td;
            td.type = t->Attribute("type");
            t->QueryFloatAttribute("radius", &td.radius);
            t->QueryFloatAttribute("alpha", &td.alphaDeg);
            t->QueryFloatAttribute("beta", &td.azimuthDeg);
            t->QueryUnsignedAttribute("randomSeed", &td.randomSeed);
            t->QueryFloatAttribute("stemShrink", &td.stemShrink);
            int useStemOverrides = 0;
            t->QueryIntAttribute("useStemCtrlOverrides", &useStemOverrides);
            if (useStemOverrides) {
                loadSplineFromAttributes(t, "stemCtrl", td.stemCtrlOverride);
                td.useStemCtrlOverrides = !td.stemCtrlOverride.empty();
            }

            if (const XMLElement* leaves = t->FirstChildElement("leaves")) {
                for (const XMLElement* lf = leaves->FirstChildElement("leaf"); lf; lf = lf->NextSiblingElement("leaf")) {
                    LeafDesc L;
                    lf->QueryIntAttribute("id", &L.id);
                    lf->QueryFloatAttribute("distance", &L.distance);

                    // Set defaults first, in case they aren't in the XML
                    L.leafLength = 0.7f;
                    L.leafWidth = 0.08f;
                    L.leafAngle = 45.0f;
                    L.droopiness = 0.5f;
                    L.stemInclinationDeg = 0.0f;
                    L.splinePoints = 4; 
                    L.widthTaper = 1.0f;
                    L.leafTwist = 0.0f;

                    L.waveLAmp = 0.0f;
                    L.waveLFreq = 0.0f;
                    L.waveLPhase = 0.0f;
                    L.waveRAmp = 0.0f;
                    L.waveRFreq = 0.0f;
                    L.waveRPhase = 0.0f;

                    // Now try to load them from XML
                    lf->QueryFloatAttribute("leafLength", &L.leafLength);
                    lf->QueryFloatAttribute("leafWidth", &L.leafWidth);
                    lf->QueryFloatAttribute("leafAngle", &L.leafAngle);
                    lf->QueryFloatAttribute("droopiness", &L.droopiness);
                    lf->QueryFloatAttribute("stemInclinationDeg", &L.stemInclinationDeg);
                    lf->QueryIntAttribute("splinePoints", &L.splinePoints);
                    lf->QueryFloatAttribute("widthTaper", &L.widthTaper);
                    lf->QueryFloatAttribute("leafTwist", &L.leafTwist);

                    lf->QueryFloatAttribute("waveLAmp", &L.waveLAmp);
                    lf->QueryFloatAttribute("waveLFreq", &L.waveLFreq);
                    lf->QueryFloatAttribute("waveLPhase", &L.waveLPhase);
                    lf->QueryFloatAttribute("waveRAmp", &L.waveRAmp);
                    lf->QueryFloatAttribute("waveRFreq", &L.waveRFreq);
                    lf->QueryFloatAttribute("waveRPhase", &L.waveRPhase);

                    int useOverrides = 0;
                    lf->QueryIntAttribute("useCtrlOverrides", &useOverrides);
                    if (useOverrides) {
                        loadSplineFromAttributes(lf, "ctrlCenter", L.ctrlCenterOverride);
                        loadSplineFromAttributes(lf, "ctrlLeft", L.ctrlLeftOverride);
                        loadSplineFromAttributes(lf, "ctrlRight", L.ctrlRightOverride);
                        L.useCtrlOverrides = !L.ctrlCenterOverride.empty();
                    }

                    // Clamp it just in case
                    L.splinePoints = std::max(4, std::min(L.splinePoints, 40));

                    td.leaves.push_back(L);
                }
            }
            Maize::plant().tillers.push_back(td);
        }
        return true;
    }
}