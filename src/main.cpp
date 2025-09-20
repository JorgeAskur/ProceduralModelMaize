#include <GL/freeglut.h>
#include <GL/glu.h>
#include <vector>
#include <string>
#include <cstdio>
#include <math/vect3d.h>

#include "imgui.h"
#include "imgui_impl_glut.h"
#include "imgui_impl_opengl2.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Maize.h"
#include "Descriptor.h"

#define M_PI 3.14159265358979323846

// Window and camera
static int   gWinW = 1280, gWinH = 860;
static float gYaw = 30.f, gPitch = 18.f, gDist = 3.5f;
static Vect3d gCamTarget = { 0.0f, 0.0f, 0.0f };

// Camera controls
static float gFovDeg = 60.0f;
static float gMoveSpeed = 1.0f;
static float gBoost = 4.0f;
static float gSlow = 0.25f;

static bool  gKeyDown[256] = { false };
static bool  gMouseRot = false;
static int   gLastMouseX = 0, gLastMouseY = 0;

// For frame time
static int   gLastTimeMs = 0;

// Mouse sensitivity
static float gMouseYawSens = 0.25f;
static float gMousePitchSens = 0.25f;

// Orbit pivot
static Vect3d gAxisPos = { 0.0, 0.0, 0.0 };


// Display / render options
static bool  gWireframe = false;
static bool  gLightingOn = true;
static float gLightPos[4] = { 2.0f, 3.0f, 2.0f, 1.0f };
static float gGlobalAmb[4] = { 0.15f, 0.15f, 0.15f, 1.0f };
static float gLightDiff[4] = { 0.95f, 0.95f, 0.95f, 1.0f };
static float gLightSpec[4] = { 0.20f, 0.20f, 0.20f, 1.0f };
static float gMatSpec[4] = { 0.12f, 0.12f, 0.12f, 1.0f };
static float gShininess = 18.0f;

// Control point display
static bool gShowCtrlPts = true; static bool gShowCtrlPoly = true; static bool gShowCtrlLabel = true;
static float gCtrlPtSize = 5.0f;

static int gSelTiller = 0;
static int gSelLeaf = 0;

// Texture
static GLuint gLeafTex = 0; static bool gTexOK = false;
static char   gLeafTexPath[256] = "maize_leaf.png";
static char   gXmlPath[256] = "plant.xml";
static char   gObjPath[256] = "plant.obj";

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

// Load a texture from file, create GL texture object
static bool loadLeafTexture(const char* path) {
    int w = 0, h = 0, n = 0; stbi_uc* data = stbi_load(path, &w, &h, &n, 4);
    if (!data) return false;
    if (!gLeafTex) glGenTextures(1, &gLeafTex);
    glBindTexture(GL_TEXTURE_2D, gLeafTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    gTexOK = true; return true;
}

// Setup OpenGL lighting state
static void setupLighting() {
    if (!gLightingOn) { glDisable(GL_LIGHTING); return; }

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, gLightPos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, gLightDiff);
    glLightfv(GL_LIGHT0, GL_SPECULAR, gLightSpec);

    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gGlobalAmb);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);

    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, gMatSpec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, gShininess);

    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
}

// Draw the plant geometry
static void drawPlant() {
    setupLighting();
    glColor4f(1.f, 1.f, 1.f, 1.f); 
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    if (gTexOK) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, gLeafTex); }
    if (gWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); else glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Stem
    for (const auto& S : Maize::stemLayers()) {
        if (S.band.empty()) continue;

        glBegin(GL_TRIANGLE_STRIP);

        const float uShift = S.uPhase;

        for (const auto& v : S.band) {
            Vect3d n = Vect3d(v.x(), 0.0, v.z());
            double L = n.Length();
            if (L > 1e-12) n = n * (1.0 / L);
            else           n = Vect3d{ 1,0,0 };

            // Base U from angle
            float u = (std::atan2((float)v.z(), (float)v.x()) + (float)M_PI) / (2.0f * (float)M_PI);

            // Apply layer phase
            u += uShift;
            u -= std::floor(u);

            glNormal3f((float)n.x(), (float)n.y(), (float)n.z());
            glTexCoord2f(u, (float)v.y());
            glVertex3f((float)v.x(), (float)v.y(), (float)v.z());
        }
        glEnd();
    }

    // Leaves
    for (const auto& G : Maize::leafGeoms()) {
        if (G.rows <= 1 || G.cols <= 1) continue;
        for (int i = 0; i < G.rows - 1; ++i) {
            glBegin(GL_TRIANGLE_STRIP);
            for (int j = 0; j < G.cols; ++j) {
                int a = i * G.cols + j, b = (i + 1) * G.cols + j;
                const auto& pa = G.pos[a], & pb = G.pos[b]; const auto& ua = G.uv[a], & ub = G.uv[b]; const auto& na = G.nrm[a], & nb = G.nrm[b];
                glNormal3f((float)na.x(), (float)na.y(), (float)na.z()); glTexCoord2f(ua.u, ua.v); glVertex3f((float)pa.x(), (float)pa.y(), (float)pa.z());
                glNormal3f((float)nb.x(), (float)nb.y(), (float)nb.z()); glTexCoord2f(ub.u, ub.v); glVertex3f((float)pb.x(), (float)pb.y(), (float)pb.z());
            }
            glEnd();
        }
    }

    if (gTexOK) { glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D); }
}

// Draw control points and polygons
static void drawCtrlViz() {
    glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);

    if (gShowCtrlPoly) {
        glLineWidth(2.f); glColor3f(1.f, 0.45f, 0.05f);
        for (const auto& G : Maize::leafGeoms()) {
            glBegin(GL_LINE_STRIP);
            for (const auto& p : G.ctrl) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
        }
    }
    if (gShowCtrlPts) {
        glPointSize(gCtrlPtSize); glColor3f(0.95f, 0.2f, 0.1f);
        glBegin(GL_POINTS);
        for (const auto& G : Maize::leafGeoms())
            for (const auto& p : G.ctrl) glVertex3f(p.x(), p.y(), p.z());
        glEnd();
    }
    if (gShowCtrlLabel) {
        glColor3f(0.05f, 0.05f, 0.05f);
        for (const auto& G : Maize::leafGeoms()) {
            for (size_t i = 0; i < G.ctrl.size(); ++i) {
                const Vect3d& p = G.ctrl[i]; glRasterPos3f(p.x(), p.y(), p.z());
                char buf[8]; std::snprintf(buf, sizeof(buf), "C%zu", i);
                for (const char* c = buf; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
            }
        }
    }
    glEnable(GL_DEPTH_TEST);
}

// Draw a ground grid in XZ plane with axis
static void drawGridXZ(float size = 2.5f, int steps = 36) {
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_LIGHTING_BIT | GL_TEXTURE_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    const float yLift = 0.0002f;

    glLineWidth(1.0f);
    glColor3f(0.7f, 0.7f, 0.7f);
    glBegin(GL_LINES);
    for (int i = 0; i <= steps; ++i) {
        float t = float(i) / steps, x = -size + 2 * size * t;
        glVertex3f(x, yLift, -size); glVertex3f(x, yLift, size);
        glVertex3f(-size, yLift, x); glVertex3f(size, yLift, x);
    }
    glEnd();

	// Axis at cam target
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3f(0.9f, 0.1f, 0.1f); // X
    glVertex3f((float)gAxisPos.x(), (float)gAxisPos.y(), (float)gAxisPos.z());
    glVertex3f((float)(gAxisPos.x() + 0.1f), (float)gAxisPos.y(), (float)gAxisPos.z());

    glColor3f(0.1f, 0.6f, 0.1f); // Y
    glVertex3f((float)gAxisPos.x(), (float)gAxisPos.y(), (float)gAxisPos.z());
    glVertex3f((float)gAxisPos.x(), (float)(gAxisPos.y() + 0.1f), (float)gAxisPos.z());

    glColor3f(0.1f, 0.3f, 0.9f); // Z
    glVertex3f((float)gAxisPos.x(), (float)gAxisPos.y(), (float)gAxisPos.z());
    glVertex3f((float)gAxisPos.x(), (float)gAxisPos.y(), (float)(gAxisPos.z() + 0.1f));
    glEnd();

    glPopAttrib();
}


// Main display function
static void display() {
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGLUT_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, gWinW, gWinH);
    glClearColor(1.f, 1.f, 1.f, 1); 
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(gFovDeg, (double)gWinW / (double)gWinH, 0.01, 100.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

	// Set camera
    float cy = gYaw * (float)M_PI / 180.f, cp = gPitch * (float)M_PI / 180.f;
    Vect3d eye = gCamTarget + Vect3d{ std::cos(cp) * std::cos(cy),
                                      std::sin(cp),
                                      std::cos(cp) * std::sin(cy) } *gDist;
    gluLookAt(eye.x(), eye.y(), eye.z(), gCamTarget.x(), gCamTarget.y(), gCamTarget.z(), 0, 1, 0);

    drawGridXZ(2.5f, 36);

    drawPlant();
    drawCtrlViz();
    io.DisplaySize = ImVec2((float)gWinW, (float)gWinH);

    // Delta time
    static int gLastTimeMs = 0;
    int now = glutGet(GLUT_ELAPSED_TIME);
    if (gLastTimeMs == 0) gLastTimeMs = now;
    float dt = (now - gLastTimeMs) * 0.001f;
    gLastTimeMs = now;

    // Mouse rotate, wheel zoom
    if (!io.WantCaptureMouse && io.MouseDown[0]) {
        gYaw += io.MouseDelta.x * 0.3f;
        gPitch += io.MouseDelta.y * 0.3f;
        gPitch = clampf(gPitch, -89.f, 89.f);
    }
    if (!io.WantCaptureMouse && io.MouseWheel != 0.f) {
        gDist = clampf(gDist * (io.MouseWheel > 0.f ? 0.90f : 1.10f), 0.2f, 50.f);
    }

    // Keyboard translate
    if (!io.WantCaptureKeyboard) {
        auto down = [](ImGuiKey k) -> int { return ImGui::IsKeyDown(k) ? 1 : 0; };

        int w = down(ImGuiKey_W);
        int s = down(ImGuiKey_S);
        int a = down(ImGuiKey_A);
        int d = down(ImGuiKey_D);
        int q = down(ImGuiKey_Q);
        int e = down(ImGuiKey_E);

        bool shift = ImGui::IsKeyDown(ImGuiMod_Shift);
        bool ctrl = ImGui::IsKeyDown(ImGuiMod_Ctrl);

        float mult = gMoveSpeed * (shift ? gBoost : 1.f) * (ctrl ? gSlow : 1.f);

        // Cam direction 
        const float PI = 3.1415926535f;
        float cy = std::cos(gYaw * PI / 180.f), sy = std::sin(gYaw * PI / 180.f);
        float cp = std::cos(gPitch * PI / 180.f), sp = std::sin(gPitch * PI / 180.f);
        Vect3d dir = { cp * cy, sp, cp * sy };
        Vect3d fwd = dir.GetNormalized();
        Vect3d right = (fwd.Cross({ 0.f,1.f,0.f })).GetNormalized();
        Vect3d up = (right.Cross(fwd)).GetNormalized();

        Vect3d vel = fwd * float(s - w) + right * float(a - d) + up * float(q - e);
        double L = vel.Length();
        if (L > 1e-6) {
            vel = vel * (1.0 / L);
            Vect3d delta = vel * (mult * dt);
            gCamTarget = gCamTarget + delta;
			gAxisPos = gAxisPos + delta;
        }
    }

    ImGui::Begin("Maize");

    bool needRebuild = false;
     
	// XML read/write
    ImGui::SeparatorText("XML");
    ImGui::InputText("XML path", gXmlPath, IM_ARRAYSIZE(gXmlPath));
    ImGui::SameLine();
    if (ImGui::Button("Load XML")) {
        if (Descriptor::loadPlantXML(gXmlPath)) { Maize::rebuildGeometry(); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save XML")) {
        Descriptor::savePlantXML(gXmlPath);
    }

    // Export
    ImGui::SeparatorText("Export");
    ImGui::InputText("OBJ path", gObjPath, IM_ARRAYSIZE(gObjPath));
    if (ImGui::Button("Export OBJ + MTL")) {
        Maize::savePlantOBJ(gObjPath, gLeafTexPath);
    }

    // Lighting
    ImGui::SeparatorText("Lighting");
    ImGui::Checkbox("Enable lighting", &gLightingOn);
    ImGui::SameLine();
    ImGui::DragFloat4("Light position", gLightPos, 0.05f);
    ImGui::DragFloat("Shininess", &gShininess, 0.5f, 1.0f, 128.0f, "%.1f");

    // Overlays
    ImGui::SeparatorText("Overlays");
    ImGui::Checkbox("Ctrl pts", &gShowCtrlPts); ImGui::SameLine();
    ImGui::Checkbox("Ctrl poly", &gShowCtrlPoly); ImGui::SameLine();
    ImGui::Checkbox("Ctrl labels", &gShowCtrlLabel);
    ImGui::SliderFloat("Ctrl pt size", &gCtrlPtSize, 2.f, 30.f);
    
    // Wireframe
    ImGui::Checkbox("Wireframe (leaves)", &gWireframe);

    // Tiller params
    ImGui::SeparatorText("Tiller params");
    auto& plantForT = Maize::plant();
    if (plantForT.tillers.empty()) {
        ImGui::TextDisabled("No tillers loaded.");
    }
    else {
        // Make sure indices are in range
        gSelTiller = std::min(gSelTiller, (int)plantForT.tillers.size() - 1);

        // Pick which tiller to edit
        if (ImGui::BeginCombo("Edit tiller", std::to_string(gSelTiller).c_str())) {
            for (int i = 0; i < (int)plantForT.tillers.size(); ++i) {
                bool sel = (i == gSelTiller);
                std::string label = "Tiller " + std::to_string(i) + " (" + plantForT.tillers[i].type + ")";
                if (ImGui::Selectable(label.c_str(), sel)) gSelTiller = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        auto& T = plantForT.tillers[gSelTiller];

        // Base radius controls the outermost stem radius for this tiller
        needRebuild |= ImGui::SliderFloat("Base radius (m)", &T.radius, 0.001f, 0.06f, "%.4f");

        // Azimuth of the first leaf
        needRebuild |= ImGui::SliderFloat("Alpha (deg)", &T.alphaDeg, -1.f, 1.f, "%.2f");

        // Phyllotaxy step (degrees between leaves around the stem)
        needRebuild |= ImGui::SliderFloat("Beta (deg)", &T.betaDeg, -360.f, 360.f, "%.1f");

        // Per-leaf stem radius reduction
        needRebuild |= ImGui::SliderFloat("Stem shrink per leaf (m)", &T.stemShrink, 0.0000f, 0.0150f, "%.4f");

        if (ImGui::Button("Recompute leaf azimuths from alpha/beta")) {
            for (auto& L : T.leaves) {
                L.azimuthDeg = L.id * T.betaDeg;
            }
            needRebuild = true;
        }

        ImGui::TextDisabled("Leaves: %d", (int)T.leaves.size());
    }


    // Per-leaf editor
    ImGui::SeparatorText("Leaf Editor");

    // Clamp selections to available tillers/leaves
    auto& plant = Maize::plant();
    if (!plant.tillers.empty()) {
        gSelTiller = std::min(gSelTiller, (int)plant.tillers.size() - 1);
        auto& t = plant.tillers[gSelTiller];
        int leafCount = (int)t.leaves.size();
        if (leafCount > 0) {
            gSelLeaf = std::min(gSelLeaf, leafCount - 1);
        }
        else {
            gSelLeaf = 0;
        }

        // Tiller picker
        if (ImGui::BeginCombo("Tiller", std::to_string(gSelTiller).c_str())) {
            for (int i = 0; i < (int)plant.tillers.size(); ++i) {
                bool sel = (i == gSelTiller);
                std::string label = "Tiller " + std::to_string(i) + " (" + plant.tillers[i].type + ")";
                if (ImGui::Selectable(label.c_str(), sel)) { gSelTiller = i; gSelLeaf = 0; }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Leaf picker
        if (!t.leaves.empty()) {
            int curId = t.leaves[gSelLeaf].id;
            if (ImGui::BeginCombo("Leaf", ("id=" + std::to_string(curId)).c_str())) {
                for (int li = 0; li < (int)t.leaves.size(); ++li) {
                    bool sel = (li == gSelLeaf);
                    std::string label = "Leaf " + std::to_string(li) + " (id=" + std::to_string(t.leaves[li].id) + ")";
                    if (ImGui::Selectable(label.c_str(), sel)) { gSelLeaf = li; }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Pull editable bundles
            LeafKey key{ gSelTiller, gSelLeaf };                      
            auto& allEdits = Maize::edits();                          
            auto  itEdit = allEdits.find(key);
            bool  hasEdit = (itEdit != allEdits.end() && itEdit->second.hasCustom);

            // Controls to create/reset custom control points
            if (ImGui::Button(hasEdit ? "Reset to generated" : "Make editable")) {
                if (hasEdit) {
                    allEdits.erase(key);
                }
                else {
                    // Seed editable curve with the generated midrib ctrl from geometry if present,
                    LeafEdit E;
                    E.hasCustom = true; E.pinC0ToStem = true;
                    // Try to grab from current leaf geometry
                    if (gSelLeaf < (int)Maize::leafGeoms().size() && !Maize::leafGeoms()[gSelLeaf].ctrl.empty()) {
                        E.customCtrl = Maize::leafGeoms()[gSelLeaf].ctrl;
                    }
                    else {
                        // Fallback
                        E.customCtrl = { {0,0,0},{0.3,0,0},{0.6,0,0},{1.0,0,0} };
                    }
                    allEdits[key] = std::move(E);
                }
                needRebuild = true;
            }

            if (hasEdit) {
                ImGui::SameLine();
                ImGui::Checkbox("Pin C0 to stem", &allEdits[key].pinC0ToStem);

                // Editable control points
                auto& ctrl = allEdits[key].customCtrl;
                if (ImGui::Button("Add point")) {
                    if (!ctrl.empty()) ctrl.push_back(ctrl.back() + Vect3d{ 0.2,0,0 });
                    else ctrl = { {0,0,0},{0.3,0,0},{0.6,0,0},{1.0,0,0} };
                    needRebuild = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove last") && ctrl.size() > 4) { ctrl.pop_back(); needRebuild = true; }

                for (size_t i = 0; i < ctrl.size(); ++i) {
                    float v[3] = { (float)ctrl[i].x(), (float)ctrl[i].y(), (float)ctrl[i].z() };
                    std::string label = "C" + std::to_string(i);
                    if (ImGui::DragFloat3(label.c_str(), v, 0.005f)) {
                        ctrl[i] = { v[0], v[1], v[2] };
                        needRebuild = true;
                    }
                }
            }

            // Per-leaf numeric parameters
            ImGui::SeparatorText("Parameters (selected leaf)");
            LeafParams& LP = Maize::paramsFor(key);

            needRebuild |= ImGui::SliderFloat("Length (m)", &LP.length, 0.10f, 1.00f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Max width (m)", &LP.wmax, 0.02f, 0.150f, "%.3f");
            needRebuild |= ImGui::SliderInt("Samples (min)", &LP.samplesMin, 8, 200);

            needRebuild |= ImGui::SliderFloat("Droop max (m)", &LP.droopMax, 0.0f, 0.25f, "%.3f");
            needRebuild |= ImGui::SliderFloat("Twist max (deg)", &LP.twistMaxD, 0.0f, 90.0f, "%.1f");
            needRebuild |= ImGui::SliderFloat("Twist power", &LP.twistPow, 0.5f, 3.0f, "%.2f");

            ImGui::SeparatorText("Waviness");
            needRebuild |= ImGui::SliderFloat("Amplitude (m)", &LP.waveAmp, 0.0000f, 0.0050f, "%.4f");
            needRebuild |= ImGui::SliderFloat("Cycles", &LP.waveCycles, 0.0f, 8.0f, "%.1f");
            needRebuild |= ImGui::SliderFloat("Across factor", &LP.waveAcross, 0.0f, 2.0f, "%.2f");

            ImGui::SeparatorText("Width profile");
            needRebuild |= ImGui::SliderFloat("Knee s", &LP.widthKnee, 0.40f, 0.90f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Width at knee", &LP.widthAtKnee, 0.05f, 0.95f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Early pow", &LP.earlyPow, 0.2f, 3.0f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Tip pow", &LP.tipPow, 1.0f, 3.0f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Base ramp frac", &LP.baseRampFrac, 0.001f, 0.05f, "%.4f");

            ImGui::SeparatorText("Attachment / Unwrap");
            needRebuild |= ImGui::SliderFloat("Attach frac", &LP.attachFrac, 0.0f, 0.40f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Attach epsilon", &LP.attachEps, 0.0f, 0.01f, "%.4f");
            needRebuild |= ImGui::SliderFloat("Lip length frac", &LP.lipLenFrac, 0.02f, 0.20f, "%.3f");
            needRebuild |= ImGui::SliderInt("Leaf columns (odd)", &LP.lipCols, 7, 65);
            needRebuild |= ImGui::SliderFloat("Arc at base (deg)", &LP.lipArc0Deg, 0.0f, 270.f, "%.0f");
            needRebuild |= ImGui::SliderFloat("Arc at end (deg)", &LP.lipArc1Deg, 0.0f, 359.f, "%.0f");
            needRebuild |= ImGui::SliderFloat("Unwrap mid", &LP.unwrapMidFrac, 0.05f, 0.95f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Unwrap sharpness", &LP.unwrapSharpness, 0.1f, 10.0f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Ring drop (m)", &LP.ringDrop, 0.0f, 0.05f, "%.3f");

            ImGui::SeparatorText("Seam opening");
            needRebuild |= ImGui::SliderFloat("Open start (s)", &LP.openStartFrac, 0.50f, 0.95f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Open sharpness", &LP.openSharpness, 0.5f, 6.0f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Seam max (deg)", &LP.seamMaxDeg, 0.0f, 179.0f, "%.1f");
            needRebuild |= ImGui::SliderFloat("Seam offset (deg)", &LP.seamOffsetDeg, 0.0f, 359.0f, "%.1f");
        }
        else {
            ImGui::TextDisabled("Selected tiller has no leaves.");
        }
    }
    else {
        ImGui::TextDisabled("No tillers loaded.");
    }

    ImGui::Separator();
    if (needRebuild) { Maize::rebuildGeometry(); }

    ImGui::End();

    ImGui::Render(); ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    glutSwapBuffers();
}

static void camBasis(Vect3d& fwd, Vect3d& right, Vect3d& up) {
    const float PI = 3.1415926535f;
    float cy = std::cos(gYaw * PI / 180.f), sy = std::sin(gYaw * PI / 180.f);
    float cp = std::cos(gPitch * PI / 180.f), sp = std::sin(gPitch * PI / 180.f);

    // Look direction from yaw/pitch
    Vect3d dir = { cp * std::cos(gYaw * PI / 180.f),
                   sp,
                   cp * std::sin(gYaw * PI / 180.f) };
    fwd = dir.GetNormalized();
    right = (fwd.Cross({ 0.f,1.f,0.f })).GetNormalized();
    up = (right.Cross(fwd)).GetNormalized();
}

static void idle() {
    glutPostRedisplay();
}

static void reshape(int w, int h) {
    gWinW = std::max(1, w);
    gWinH = std::max(1, h);
    ImGui::GetIO().DisplaySize = ImVec2((float)gWinW, (float)gWinH);
    glutPostRedisplay();
}

int main(int argc, char** argv) {
    glutInit(&argc, argv); glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH); 
    if (gWinW <= 0 || gWinH <= 0) { glutSwapBuffers(); return 0; }

    glutInitWindowSize(gWinW, gWinH); 
    glutCreateWindow("Maize");
    ImGui::CreateContext(); ImGui_ImplGLUT_Init(); ImGui_ImplGLUT_InstallFuncs(); ImGui_ImplOpenGL2_Init();

    // Minimal seed plant so there is something to draw
    if (Maize::plant().tillers.empty()) {
        TillerDesc t; t.radius = 0.02f; t.alphaDeg = 0.f; t.betaDeg = 180.f;
        for (int i = 0; i < 6; ++i) { LeafDesc L; L.id = i; L.distance = 0.12f; L.alphaDeg = 30.f; L.spines = 14; L.azimuthDeg = t.alphaDeg + L.id * t.betaDeg; t.leaves.push_back(L); }
        Maize::plant().tillers.push_back(t);
    }
    Descriptor::loadPlantXML(gXmlPath);
    Maize::rebuildGeometry();
	loadLeafTexture(gLeafTexPath);

    glutDisplayFunc(display); 
    glutReshapeFunc(reshape); 
    glutIdleFunc(idle);

    glutMainLoop();
    return 0;
}