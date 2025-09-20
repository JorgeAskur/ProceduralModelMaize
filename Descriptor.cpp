#include "Descriptor.h"
#include "Maize.h"
#include "tinyxml2.h"
using namespace tinyxml2;

namespace Descriptor {

	// Global defaults
    static float gLeafLength = 0.40f, gLeafWmax = 0.1f;
    static int   gSamplesMin = 16;
    static float gDroopMax = 0.0f;
    static float gTwistMaxD = 20.f, gTwistPow = 1.4f;
    static float gWaveAmp = 0.0030f, gWaveCycles = 6.0f, gWaveAcross = 0.5f, gWaveFadeIn = 0.4f, gWaveFadeOut = 0.4f;
    static float gWidthKnee = 0.66f, gWidthAtKnee = 0.45f, gEarlyPow = 0.70f, gTipPow = 1.80f, gBaseRampFrac = 0.08f;
    static float gAttachFrac = 0.10f, gAttachEpsilon = 0.0015f;
    static float gLipLenFrac = 0.08f; static int gLipCols = 11; static float gLipArc0Deg = 179.f, gLipArc1Deg = 40.f;
    static float gUnwrapMidFrac = 0.55f, gUnwrapSharpness = 2.50f, gRingDrop = 0.010f;
	static float gOpenStartFrac = 0.95f, gSeamMaxDeg = 179.f, gOpenSharpness = 3.0f, gSeamOffsetDeg = 180.f;

	// Return a LeafParams struct with the current global defaults
    static LeafParams defaultLeafParamsSeed() {
        LeafParams P; P.length = gLeafLength; P.wmax = gLeafWmax; P.samplesMin = gSamplesMin;
        P.droopMax = gDroopMax; P.twistMaxD = gTwistMaxD; P.twistPow = gTwistPow;
        P.waveAmp = gWaveAmp; P.waveCycles = gWaveCycles; P.waveAcross = gWaveAcross;
        P.waveFadeIn = gWaveFadeIn; P.waveFadeOut = gWaveFadeOut; P.widthKnee = gWidthKnee; P.widthAtKnee = gWidthAtKnee;
        P.earlyPow = gEarlyPow; P.tipPow = gTipPow; P.baseRampFrac = gBaseRampFrac; P.attachFrac = gAttachFrac; P.attachEps = gAttachEpsilon;
        P.lipLenFrac = gLipLenFrac; P.lipCols = gLipCols; P.lipArc0Deg = gLipArc0Deg; P.lipArc1Deg = gLipArc1Deg; P.unwrapMidFrac = gUnwrapMidFrac; P.unwrapSharpness = gUnwrapSharpness; 
		P.openStartFrac = gOpenStartFrac; P.seamMaxDeg = gSeamMaxDeg; P.openSharpness = gOpenSharpness; P.seamOffsetDeg = gSeamOffsetDeg;
        P.ringDrop = gRingDrop;
        return P;
    }

    bool savePlantXML(const char* path) {
        XMLDocument doc; auto* root = doc.NewElement("plant"); doc.InsertFirstChild(root);
        auto* species = doc.NewElement("species"); species->SetAttribute("name", Maize::plant().species.c_str()); root->InsertEndChild(species);

        // Global defaults block
        auto* P = doc.NewElement("params");
        P->SetAttribute("length_m", gLeafLength); P->SetAttribute("max_width_m", gLeafWmax); P->SetAttribute("samples_min", gSamplesMin);
        P->SetAttribute("base_ramp_frac", gBaseRampFrac);
        P->SetAttribute("attach_frac", gAttachFrac); P->SetAttribute("attach_epsilon", gAttachEpsilon);
        P->SetAttribute("lip_len_frac", gLipLenFrac); P->SetAttribute("lip_cols", gLipCols); P->SetAttribute("lip_arc0_deg", gLipArc0Deg);
        P->SetAttribute("lip_arc1_deg", gLipArc1Deg); P->SetAttribute("unwrap_mid", gUnwrapMidFrac); P->SetAttribute("unwrap_sharpness", gUnwrapSharpness);
        P->SetAttribute("ring_drop_m", gRingDrop);
        species->InsertEndChild(P);

        // Tillers
        for (size_t ti = 0; ti < Maize::plant().tillers.size(); ++ti) {
            const auto& td = Maize::plant().tillers[ti];
            auto* t = doc.NewElement("tiller"); t->SetAttribute("type", td.type.c_str()); t->SetAttribute("radius", td.radius);
            t->SetAttribute("alpha", td.alphaDeg); t->SetAttribute("beta", td.betaDeg); t->SetAttribute("stem_shrink", td.stemShrink);
            auto* leaves = doc.NewElement("leaves"); leaves->SetAttribute("number", (int)td.leaves.size()); 

			// Leaves
            for (size_t li = 0; li < td.leaves.size(); ++li) {
                const auto& L = td.leaves[li];
                auto* lf = doc.NewElement("leaf"); lf->SetAttribute("id", L.id); lf->SetAttribute("distance", L.distance);
				lf->SetAttribute("alpha", L.alphaDeg); lf->SetAttribute("spines", L.spines);

                // Per-leaf edit
                LeafKey key{ (int)ti,(int)li }; auto& edits = Maize::edits(); auto itE = edits.find(key);
                if (itE != edits.end() && itE->second.hasCustom && itE->second.customCtrl.size() >= 4) {
                    const LeafEdit& E = itE->second; auto* ed = doc.NewElement("edit"); ed->SetAttribute("pinC0ToStem", (int)E.pinC0ToStem);
                    for (const Vect3d& p : E.customCtrl) { auto* cp = doc.NewElement("cp"); cp->SetAttribute("x", p.x()); cp->SetAttribute("y", p.y()); cp->SetAttribute("z", p.z()); ed->InsertEndChild(cp); }
                    lf->InsertEndChild(ed);
                }

                // Per-leaf params
                auto itP = Maize::leafParams().find(key);
                if (itP != Maize::leafParams().end()) {
                    const LeafParams& LP = itP->second; auto* lp = doc.NewElement("lparams");
                    lp->SetAttribute("length_m", LP.length); lp->SetAttribute("max_width_m", LP.wmax); lp->SetAttribute("samples_min", LP.samplesMin);
                    lp->SetAttribute("droop_max_m", LP.droopMax); lp->SetAttribute("twist_max_deg", LP.twistMaxD); lp->SetAttribute("twist_pow", LP.twistPow);
                    lp->SetAttribute("wave_amp_m", LP.waveAmp); lp->SetAttribute("wave_cycles", LP.waveCycles); lp->SetAttribute("wave_across", LP.waveAcross);
                    lp->SetAttribute("wave_fade_in", LP.waveFadeIn); lp->SetAttribute("wave_fade_out", LP.waveFadeOut);
                    lp->SetAttribute("width_knee", LP.widthKnee); lp->SetAttribute("width_at_knee", LP.widthAtKnee);
                    lp->SetAttribute("early_pow", LP.earlyPow); lp->SetAttribute("tip_pow", LP.tipPow); lp->SetAttribute("base_ramp_frac", LP.baseRampFrac);
                    lp->SetAttribute("attach_frac", LP.attachFrac); lp->SetAttribute("attach_eps", LP.attachEps);
                    lp->SetAttribute("lip_len_frac", LP.lipLenFrac); lp->SetAttribute("lip_cols", LP.lipCols);
                    lp->SetAttribute("lip_arc0_deg", LP.lipArc0Deg); lp->SetAttribute("lip_arc1_deg", LP.lipArc1Deg);
                    lp->SetAttribute("unwrap_mid", LP.unwrapMidFrac); lp->SetAttribute("unwrap_sharpness", LP.unwrapSharpness);
                    lp->SetAttribute("open_start_frac", LP.openStartFrac); lp->SetAttribute("open_sharpness", LP.openSharpness); 
                    lp->SetAttribute("seam_max_deg", LP.seamMaxDeg); lp->SetAttribute("seam_offset_deg", LP.seamOffsetDeg);
                    lp->SetAttribute("ring_drop_m", LP.ringDrop);
                    lf->InsertEndChild(lp);
                }
                leaves->InsertEndChild(lf);
            }
            t->InsertEndChild(leaves);
            species->InsertEndChild(t);
        }
        return (doc.SaveFile(path) == XML_SUCCESS);
    }

	// Load from XML, replacing current plant, edits, and leafParams
    bool loadPlantXML(const std::string& path) {
        XMLDocument doc; if (doc.LoadFile(path.c_str()) != XML_SUCCESS) return false;
        Maize::plant() = PlantDesc{}; Maize::edits().clear(); Maize::leafParams().clear();

        const XMLElement* species = doc.FirstChildElement("plant")->FirstChildElement("species"); if (!species) return false;
        if (const char* sp = species->Attribute("name")) Maize::plant().species = sp; else Maize::plant().species = "unknown";

        // Optional defaults
        if (const XMLElement* P = species->FirstChildElement("params")) {
            P->QueryFloatAttribute("length_m", &gLeafLength); P->QueryFloatAttribute("max_width_m", &gLeafWmax); P->QueryIntAttribute("samples_min", &gSamplesMin);
            P->QueryFloatAttribute("droop_max_m", &gDroopMax);
            P->QueryFloatAttribute("twist_max_deg", &gTwistMaxD); P->QueryFloatAttribute("twist_pow", &gTwistPow);
            P->QueryFloatAttribute("base_ramp_frac", &gBaseRampFrac);
            P->QueryFloatAttribute("attach_frac", &gAttachFrac); P->QueryFloatAttribute("attach_epsilon", &gAttachEpsilon);
            P->QueryFloatAttribute("lip_len_frac", &gLipLenFrac); P->QueryIntAttribute("lip_cols", &gLipCols);
            P->QueryFloatAttribute("lip_arc0_deg", &gLipArc0Deg); P->QueryFloatAttribute("lip_arc1_deg", &gLipArc1Deg);
            P->QueryFloatAttribute("unwrap_mid", &gUnwrapMidFrac); P->QueryFloatAttribute("unwrap_sharpness", &gUnwrapSharpness);
			P->QueryFloatAttribute("open_start_frac", &gOpenStartFrac); P->QueryFloatAttribute("seam_max_deg", &gSeamMaxDeg);
			P->QueryFloatAttribute("open_sharpness", &gOpenSharpness); P->QueryFloatAttribute("seam_offset_deg", &gSeamOffsetDeg);
            P->QueryFloatAttribute("ring_drop_m", &gRingDrop);
        }

        size_t ti = 0;
		// Tillers
        for (const XMLElement* t = species->FirstChildElement("tiller"); t; t = t->NextSiblingElement("tiller"), ++ti) {
            TillerDesc td; if (const char* tt = t->Attribute("type")) td.type = tt;
			t->QueryFloatAttribute("radius", &td.radius); t->QueryFloatAttribute("alpha", &td.alphaDeg); t->QueryFloatAttribute("beta", &td.betaDeg); t->QueryFloatAttribute("stem_shrink", &td.stemShrink);
            if (const XMLElement* leaves = t->FirstChildElement("leaves")) {
                size_t li = 0;
				// Leaves
                for (const XMLElement* lf = leaves->FirstChildElement("leaf"); lf; lf = lf->NextSiblingElement("leaf"), ++li) {
                    LeafDesc L; lf->QueryIntAttribute("id", &L.id); lf->QueryFloatAttribute("distance", &L.distance);
                    lf->QueryFloatAttribute("alpha", &L.alphaDeg); lf->QueryIntAttribute("spines", &L.spines);
                    L.azimuthDeg = td.alphaDeg + L.id * td.betaDeg; td.leaves.push_back(L);

                    LeafKey key{ (int)ti, (int)li };
                    if (const XMLElement* ed = lf->FirstChildElement("edit")) {
                        LeafEdit E; E.hasCustom = true; int pin = 1; ed->QueryIntAttribute("pinC0ToStem", &pin); E.pinC0ToStem = (pin != 0);
                        for (const XMLElement* cp = ed->FirstChildElement("cp"); cp; cp = cp->NextSiblingElement("cp")) {
                            float x = 0, y = 0, z = 0; cp->QueryFloatAttribute("x", &x); cp->QueryFloatAttribute("y", &y); cp->QueryFloatAttribute("z", &z);
                            E.customCtrl.emplace_back(x, y, z);
                        }
                        if (E.customCtrl.size() >= 4) { Maize::edits()[key] = std::move(E); }
                    }
                    LeafParams LP = defaultLeafParamsSeed();
                    if (const XMLElement* lp = lf->FirstChildElement("lparams")) {
                        lp->QueryFloatAttribute("length_m", &LP.length); lp->QueryFloatAttribute("max_width_m", &LP.wmax); lp->QueryIntAttribute("samples_min", &LP.samplesMin);
                        lp->QueryFloatAttribute("droop_max_m", &LP.droopMax);
                        lp->QueryFloatAttribute("twist_max_deg", &LP.twistMaxD); lp->QueryFloatAttribute("twist_pow", &LP.twistPow);
                        lp->QueryFloatAttribute("wave_amp_m", &LP.waveAmp); lp->QueryFloatAttribute("wave_cycles", &LP.waveCycles);
                        lp->QueryFloatAttribute("wave_across", &LP.waveAcross); lp->QueryFloatAttribute("wave_fade_in", &LP.waveFadeIn);
                        lp->QueryFloatAttribute("wave_fade_out", &LP.waveFadeOut);
                        lp->QueryFloatAttribute("width_knee", &LP.widthKnee); lp->QueryFloatAttribute("width_at_knee", &LP.widthAtKnee);
                        lp->QueryFloatAttribute("early_pow", &LP.earlyPow); lp->QueryFloatAttribute("tip_pow", &LP.tipPow);
                        lp->QueryFloatAttribute("base_ramp_frac", &LP.baseRampFrac);
                        lp->QueryFloatAttribute("attach_frac", &LP.attachFrac); lp->QueryFloatAttribute("attach_eps", &LP.attachEps);
                        lp->QueryFloatAttribute("lip_len_frac", &LP.lipLenFrac); lp->QueryIntAttribute("lip_cols", &LP.lipCols);
                        lp->QueryFloatAttribute("lip_arc0_deg", &LP.lipArc0Deg); lp->QueryFloatAttribute("lip_arc1_deg", &LP.lipArc1Deg);
                        lp->QueryFloatAttribute("unwrap_mid", &LP.unwrapMidFrac); lp->QueryFloatAttribute("unwrap_sharpness", &LP.unwrapSharpness);
                        lp->QueryFloatAttribute("ring_drop_m", &LP.ringDrop); lp->QueryFloatAttribute("open_start_frac", &LP.openStartFrac);
                        lp->QueryFloatAttribute("open_sharpness", &LP.openSharpness);
                    }
                    Maize::leafParams()[key] = LP;
                }
            }
            Maize::plant().tillers.push_back(td);
        }
        return true;
    }

}