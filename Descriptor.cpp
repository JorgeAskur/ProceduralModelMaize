#include "Descriptor.h"
#include "Maize.h"
#include "tinyxml2.h"
#include <string>
#include <vector>

using namespace tinyxml2;

namespace Descriptor {

    // Spline points are stored as flat XML attributes because the existing file format is intentionally simple and hand-inspectable
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

    // Counterpart to saveSplineAsAttributes()
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

    // Skeleton XML stores only center splines
    static void buildLeafSidesFromCenter(LeafDesc& L);

    // Full plant XML stores both procedural parameters and any explicit editor control-point overrides
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
            tiller_elem->SetAttribute("azimuthNoise", td.azimuthNoise);
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
                leaf_elem->SetAttribute("leafAzimuthDeg", L.azimuthDeg);
                leaf_elem->SetAttribute("leafAngle", L.leafAngle);
                leaf_elem->SetAttribute("droopiness", L.droopiness);
                leaf_elem->SetAttribute("stemInclinationDeg", L.stemInclinationDeg);
                leaf_elem->SetAttribute("splinePoints", L.splinePoints);
                leaf_elem->SetAttribute("widthTaper", L.widthTaper);
                leaf_elem->SetAttribute("leafTwist", L.leafTwist);
                leaf_elem->SetAttribute("leafCurl", L.leafCurl);

                leaf_elem->SetAttribute("waveLAmp", L.waveLAmp);
                leaf_elem->SetAttribute("waveLFreq", L.waveLFreq);
                leaf_elem->SetAttribute("waveLPhase", L.waveLPhase);
                leaf_elem->SetAttribute("waveRAmp", L.waveRAmp);
                leaf_elem->SetAttribute("waveRFreq", L.waveRFreq);
                leaf_elem->SetAttribute("waveRPhase", L.waveRPhase);
                leaf_elem->SetAttribute("surfaceNoiseAmp", L.surfaceNoiseAmp);
                leaf_elem->SetAttribute("surfaceNoiseFreq", L.surfaceNoiseFreq);
                leaf_elem->SetAttribute("midribTipTaperStart", L.midribTipTaperStart);
                leaf_elem->SetAttribute("midribTextureStrength", L.midribTextureStrength);
                leaf_elem->SetAttribute("liguleWrapLengthScale", L.liguleWrapLengthScale);
                leaf_elem->SetAttribute("liguleUnfoldSharpness", L.liguleUnfoldSharpness);
                leaf_elem->SetAttribute("sheathOuterScale", L.sheathOuterScale);

                if (L.useCtrlOverrides && !L.ctrlCenterOverride.empty()) {
                    leaf_elem->SetAttribute("useCtrlOverrides", 1);
                    saveSplineAsAttributes(leaf_elem, "ctrlCenter", L.ctrlCenterOverride);
                    if (!L.ctrlLeftOverride.empty() && L.ctrlLeftOverride.size() == L.ctrlCenterOverride.size()) {
                        saveSplineAsAttributes(leaf_elem, "ctrlLeft", L.ctrlLeftOverride);
                    }
                    if (!L.ctrlRightOverride.empty() && L.ctrlRightOverride.size() == L.ctrlCenterOverride.size()) {
                        saveSplineAsAttributes(leaf_elem, "ctrlRight", L.ctrlRightOverride);
                    }
                }

                leaves_elem->InsertEndChild(leaf_elem);
            }
            tiller_elem->InsertEndChild(leaves_elem);
            species->InsertEndChild(tiller_elem);
        }
        return (doc.SaveFile(path) == XML_SUCCESS);
    }

    // Full plant XML load path
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
            t->QueryFloatAttribute("azimuthNoise", &td.azimuthNoise);
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

                    // Apply code defaults first so older XML files that predate
                    // some attributes still load into a usable plant
                    L.leafLength = 0.7f;
                    L.leafWidth = 0.08f;
                    L.azimuthDeg = Maize::computeLeafAzimuthDeg(td, (int)td.leaves.size());
                    L.leafAngle = 45.0f;
                    L.droopiness = 0.5f;
                    L.stemInclinationDeg = 0.0f;
                    L.splinePoints = 4; 
                    L.widthTaper = 1.0f;
                    L.leafTwist = 0.0f;
                    L.leafCurl = 0.0f;

                    L.waveLAmp = 0.0f;
                    L.waveLFreq = 0.0f;
                    L.waveLPhase = 0.0f;
                    L.waveRAmp = 0.0f;
                    L.waveRFreq = 0.0f;
                    L.waveRPhase = 0.0f;
                    L.surfaceNoiseAmp = 0.01f;
                    L.surfaceNoiseFreq = 8.0f;
                    L.midribTipTaperStart = 0.75f;
                    L.midribTextureStrength = 0.35f;
                    L.liguleWrapLengthScale = 6.8f;
                    L.liguleUnfoldSharpness = 2.2f;
                    L.sheathOuterScale = 1.12f;

                    // Then override any defaults that are actually present
                    lf->QueryFloatAttribute("leafLength", &L.leafLength);
                    lf->QueryFloatAttribute("leafWidth", &L.leafWidth);
                    lf->QueryFloatAttribute("leafAzimuthDeg", &L.azimuthDeg);
                    lf->QueryFloatAttribute("leafAngle", &L.leafAngle);
                    lf->QueryFloatAttribute("droopiness", &L.droopiness);
                    lf->QueryFloatAttribute("stemInclinationDeg", &L.stemInclinationDeg);
                    lf->QueryIntAttribute("splinePoints", &L.splinePoints);
                    lf->QueryFloatAttribute("widthTaper", &L.widthTaper);
                    lf->QueryFloatAttribute("leafTwist", &L.leafTwist);
                    lf->QueryFloatAttribute("leafCurl", &L.leafCurl);

                    lf->QueryFloatAttribute("waveLAmp", &L.waveLAmp);
                    lf->QueryFloatAttribute("waveLFreq", &L.waveLFreq);
                    lf->QueryFloatAttribute("waveLPhase", &L.waveLPhase);
                    lf->QueryFloatAttribute("waveRAmp", &L.waveRAmp);
                    lf->QueryFloatAttribute("waveRFreq", &L.waveRFreq);
                    lf->QueryFloatAttribute("waveRPhase", &L.waveRPhase);
                    lf->QueryFloatAttribute("surfaceNoiseAmp", &L.surfaceNoiseAmp);
                    lf->QueryFloatAttribute("surfaceNoiseFreq", &L.surfaceNoiseFreq);
                    lf->QueryFloatAttribute("midribTipTaperStart", &L.midribTipTaperStart);
                    lf->QueryFloatAttribute("midribTextureStrength", &L.midribTextureStrength);
                    lf->QueryFloatAttribute("liguleWrapLengthScale", &L.liguleWrapLengthScale);
                    lf->QueryFloatAttribute("liguleUnfoldSharpness", &L.liguleUnfoldSharpness);
                    lf->QueryFloatAttribute("sheathOuterScale", &L.sheathOuterScale);

                    int useOverrides = 0;
                    lf->QueryIntAttribute("useCtrlOverrides", &useOverrides);
                    if (useOverrides) {
                        loadSplineFromAttributes(lf, "ctrlCenter", L.ctrlCenterOverride);
                        loadSplineFromAttributes(lf, "ctrlLeft", L.ctrlLeftOverride);
                        loadSplineFromAttributes(lf, "ctrlRight", L.ctrlRightOverride);
                        if (!L.ctrlCenterOverride.empty()) {
                            if (L.ctrlLeftOverride.size() != L.ctrlCenterOverride.size() || L.ctrlRightOverride.size() != L.ctrlCenterOverride.size()) {
                                buildLeafSidesFromCenter(L);
                            }
                            L.useCtrlOverrides = true;
                        }
                    }

                    // Keep editor-driven files from requesting large
                    // spline densities
                    L.splinePoints = std::max(4, std::min(L.splinePoints, 40));

                    td.leaves.push_back(L);
                }
            }
            Maize::plant().tillers.push_back(td);
        }
        return true;
    }

    static void buildLeafSidesFromCenter(LeafDesc& L) {
        // This is only an approximation of the procedural blade cross-section
        const size_t n = L.ctrlCenterOverride.size();
        L.ctrlLeftOverride.resize(n);
        L.ctrlRightOverride.resize(n);
        if (n == 0) return;

        float halfBase = std::max(0.0f, L.leafWidth * 0.5f);
        float taperPower = std::max(0.05f, L.widthTaper);

        for (size_t i = 0; i < n; ++i) {
            float t = (n > 1) ? (float)i / (float)(n - 1) : 1.0f;
            float widthScale = std::max(0.0f, 1.0f - std::pow(t, taperPower));
            float halfW = halfBase * widthScale;

            Vect3d c = L.ctrlCenterOverride[i];
            Vect3d l = c;
            Vect3d r = c;
            l.SetX(c.x() - halfW);
            r.SetX(c.x() + halfW);
            L.ctrlLeftOverride[i] = l;
            L.ctrlRightOverride[i] = r;
        }

        L.ctrlLeftOverride.back() = L.ctrlCenterOverride.back();
        L.ctrlRightOverride.back() = L.ctrlCenterOverride.back();
    }

    bool saveSkeletonXML(const char* path) {
        // Skeleton XML is intentionally lighter than full plant XML, it records
        // just the stem center spline and the leaf center splines
        XMLDocument doc;
        doc.InsertFirstChild(doc.NewDeclaration());

        XMLElement* root = doc.NewElement("plantSkeleton");
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

            // Skeleton export always emits an explicit stem spline, even if the
            // current plant is being driven procedurally
            std::vector<Vect3d> stemCtrl = td.stemCtrlOverride;
            if (stemCtrl.size() < 2) {
                stemCtrl.clear();
                Vect3d stemPos(0.0f, 0.0f, 0.0f);
                stemCtrl.push_back(stemPos);
                float cumulativeInclinationDeg = 0.0f;
                for (const auto& leaf : td.leaves) {
                    float dist = std::max(0.f, leaf.distance);
                    cumulativeInclinationDeg += leaf.stemInclinationDeg;
                    float incRad = 3.14159265358979323846f * cumulativeInclinationDeg / 180.0f;
                    Vect3d dir(std::sin(incRad), std::cos(incRad), 0.0f);
                    stemPos = stemPos + dir * dist;
                    stemCtrl.push_back(stemPos);
                }
            }
            if (stemCtrl.size() >= 2) {
                tiller_elem->SetAttribute("useStemCtrlOverrides", 1);
                saveSplineAsAttributes(tiller_elem, "stemCtrl", stemCtrl);
            }

            XMLElement* leaves_elem = doc.NewElement("leaves");
            leaves_elem->SetAttribute("number", (int)td.leaves.size());

            for (size_t li = 0; li < td.leaves.size(); ++li) {
                const auto& L = td.leaves[li];
                XMLElement* leaf_elem = doc.NewElement("leaf");
                leaf_elem->SetAttribute("id", L.id);
                leaf_elem->SetAttribute("leafAzimuthDeg", L.azimuthDeg);

                std::vector<Vect3d> center;
                if (L.useCtrlOverrides && !L.ctrlCenterOverride.empty()) {
                    center = L.ctrlCenterOverride;
                }
                else {
                    std::vector<Vect3d> left, right;
                    Maize::generateLeafSplines(L, td.radius, center, left, right);
                }

                if (!center.empty()) {
                    saveSplineAsAttributes(leaf_elem, "ctrlCenter", center);
                }

                leaves_elem->InsertEndChild(leaf_elem);
            }

            tiller_elem->InsertEndChild(leaves_elem);
            species->InsertEndChild(tiller_elem);
        }

        return (doc.SaveFile(path) == XML_SUCCESS);
    }

    bool loadSkeletonXML(const std::string& path) {
        // Skeleton import reconstructs a minimally valid editable plant from the center splines alone
        XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != XML_SUCCESS) return false;

        Maize::plant() = PlantDesc{};

        const XMLElement* root = doc.FirstChildElement("plantSkeleton");
        if (!root) root = doc.FirstChildElement("plant");
        if (!root) return false;

        const XMLElement* species = root->FirstChildElement("species");
        if (!species) return false;
        Maize::plant().species = species->Attribute("name") ? species->Attribute("name") : "Maize_Procedural";

        for (const XMLElement* t = species->FirstChildElement("Tiller"); t; t = t->NextSiblingElement("Tiller")) {
            TillerDesc td;
            td.type = t->Attribute("type") ? t->Attribute("type") : "main";
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

            bool hasPrevLeafBase = false;
            Vect3d prevLeafBase(0.0f, 0.0f, 0.0f);
            Vect3d stemBase(0.0f, 0.0f, 0.0f);
            if (td.useStemCtrlOverrides && !td.stemCtrlOverride.empty()) {
                stemBase = td.stemCtrlOverride.front();
            }

            if (const XMLElement* leaves = t->FirstChildElement("leaves")) {
                for (const XMLElement* lf = leaves->FirstChildElement("leaf"); lf; lf = lf->NextSiblingElement("leaf")) {
                    LeafDesc L;
                    lf->QueryIntAttribute("id", &L.id);
                    L.azimuthDeg = Maize::computeLeafAzimuthDeg(td, (int)td.leaves.size());
                    lf->QueryFloatAttribute("leafAzimuthDeg", &L.azimuthDeg);

                    loadSplineFromAttributes(lf, "ctrlCenter", L.ctrlCenterOverride);
                    if (!L.ctrlCenterOverride.empty()) {
                        const Vect3d& thisLeafBase = L.ctrlCenterOverride.front();
                        // Leaf distances are re-estimated from successive center
                        // spline bases because the skeleton format stores no
                        // explicit internode distances
                        if (hasPrevLeafBase) {
                            L.distance = (thisLeafBase - prevLeafBase).Length();
                        }
                        else {
                            L.distance = (thisLeafBase - stemBase).Length();
                        }
                        prevLeafBase = thisLeafBase;
                        hasPrevLeafBase = true;

                        buildLeafSidesFromCenter(L);
                        L.useCtrlOverrides = true;
                    }

                    L.splinePoints = std::max(4, std::min(L.splinePoints, 80));
                    td.leaves.push_back(L);
                }
            }

            Maize::plant().tillers.push_back(td);
        }

        return true;
    }
}
