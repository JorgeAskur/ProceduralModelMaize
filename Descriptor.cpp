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

            XMLElement* leaves_elem = doc.NewElement("leaves");
            leaves_elem->SetAttribute("number", (int)td.leaves.size());

            for (size_t li = 0; li < td.leaves.size(); ++li) {
                const auto& L = td.leaves[li];
                // const LeafKey key{ (int)ti, (int)li }; // No longer needed
                XMLElement* leaf_elem = doc.NewElement("leaf");
                leaf_elem->SetAttribute("id", L.id);
                leaf_elem->SetAttribute("distance", L.distance);
                leaf_elem->SetAttribute("sheathLength", L.sheathLength);

                saveSplineAsAttributes(leaf_elem, "control_points_central", L.ctrlCenter);
                saveSplineAsAttributes(leaf_elem, "control_points_left", L.ctrlLeft);
                saveSplineAsAttributes(leaf_elem, "control_points_right", L.ctrlRight);
                saveSplineAsAttributes(leaf_elem, "control_points_opening", L.ctrlOpening);

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

            if (const XMLElement* leaves = t->FirstChildElement("leaves")) {
                for (const XMLElement* lf = leaves->FirstChildElement("leaf"); lf; lf = lf->NextSiblingElement("leaf")) {
                    LeafDesc L;
                    lf->QueryIntAttribute("id", &L.id);
                    lf->QueryFloatAttribute("distance", &L.distance);

                    lf->QueryFloatAttribute("sheathLength", &L.sheathLength);

                    loadSplineFromAttributes(lf, "control_points_central", L.ctrlCenter);
                    loadSplineFromAttributes(lf, "control_points_left", L.ctrlLeft);
                    loadSplineFromAttributes(lf, "control_points_right", L.ctrlRight);
                    loadSplineFromAttributes(lf, "control_points_opening", L.ctrlOpening);

                    // If any spline is missing generate default
                    if (L.ctrlOpening.empty()) {
                        std::vector<Vect3d> d1, d2, d3;
                        Maize::makeDefaultLeafSplines(0.7f, 0.08f, d1, d2, d3, L.ctrlOpening);
                    }

                    td.leaves.push_back(L);
                }
            }
            Maize::plant().tillers.push_back(td);
        }
        return true;
    }
}

