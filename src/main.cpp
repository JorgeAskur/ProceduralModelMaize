#include <GL/freeglut.h>
#include <GL/glu.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <math/vect3d.h>

#include "imgui.h"
#include "imgui_impl_glut.h"
#include "imgui_impl_opengl2.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Maize.h"
#include "Descriptor.h"
using namespace std;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Window and Camera globals
static int   gWinW = 1280, gWinH = 860;
static float gYaw = 30.f, gPitch = 18.f, gDist = 3.5f;
static Vect3d gCamTarget = { 0.0f, 0.0f, 0.0f };
static float gFovDeg = 60.0f;
static float gMoveSpeed = 1.0f;
static float gBoost = 4.0f;
static float gSlow = 0.25f;

// Selection globals
static int gSelTiller = 0;
static int gSelLeaf = 0;
// --- REMOVED ---
// static int gSelSpline = 3; 

// Overlay globals
static bool gShowCtrlPts = true;
static bool gShowCtrlPoly = true;
static bool gWireframe = false;

// Lighting globals
static bool gLightingOn = true;
static float gLightPos[4] = { 2.0f, 3.0f, 4.0f, 1.0f };
static float gGlobalAmb[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
static float gLightDiff[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
static float gLightSpec[4] = { 0.6f, 0.6f, 0.6f, 1.0f };
static float gMatSpec[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
static float gShininess = 32.0f;

// File paths
static char gXmlPath[256] = "plant_spline.xml";
static char gObjPath[256] = "plant_spline.obj";
static char gTexPath[256] = "maize_leaf.png";

// Texture globals
static GLuint gTextureID = 0;
static bool   gTextureOK = false;

// Helper functions
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }
static int leafGeomIndex(int tIdx, int lIdx);

// Texture loading
static void loadTexture(const char* path) {
    if (gTextureID) {
        glDeleteTextures(1, &gTextureID);
        gTextureID = 0;
    }
    gTextureOK = false;
    int w, h, n;
    unsigned char* data = stbi_load(path, &w, &h, &n, 4);
    if (data) {
        glGenTextures(1, &gTextureID);
        glBindTexture(GL_TEXTURE_2D, gTextureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
        gTextureOK = true;
    }
}

// Lighting setup
static void setupLighting() {
    if (!gLightingOn) {
        glDisable(GL_LIGHTING);
        return;
    }
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, gLightPos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, gLightDiff);
    glLightfv(GL_LIGHT0, GL_SPECULAR, gLightSpec);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gGlobalAmb);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, gMatSpec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, gShininess);
    glEnable(GL_NORMALIZE);
}

// Plant drawing
static void drawPlant() {
    setupLighting();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    if (gTextureOK) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, gTextureID);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }

    glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);

    // Define colors
    const Vect3d stemColor(0.8f, 0.8f, 0.5f); // Yellowish stem
    const Vect3d leafColor(0.4f, 0.8f, 0.4f); // Green leaf

    // Helper function to interpolate color
    auto lerp_color = [&](const Vect3d& a, const Vect3d& b, float t) {
        t = clampf(t, 0.f, 1.f);
        return a * (1.f - t) + b * t;
        };

    auto getColor = [&](int row, int stemRows) {
        const int transitionRows = 15; // Number of rows over which to transition
        // Start transitioning a bit before the sheath ends
        const int transitionStartRow = std::max(0, stemRows - (transitionRows / 2));

        float t = 0.f;
        if (row <= transitionStartRow) {
            t = 0.f;
        }
        else if (row >= transitionStartRow + transitionRows) {
            t = 1.f;
        }
        else {
            // Smoothly interpolate from 0 to 1 over the transitionRows
            t = (float)(row - transitionStartRow) / (float)transitionRows;
        }
        return lerp_color(stemColor, leafColor, t);
        };


    for (const auto& G : Maize::leafGeoms()) {
        if (G.rows <= 1 || G.cols <= 1) continue;

        // Draw the entire leaf in one go, interpolating color
        for (int i = 0; i < G.rows - 1; ++i) {
            glBegin(GL_TRIANGLE_STRIP);

            Vect3d colorA = getColor(i, G.stemRows);
            Vect3d colorB = getColor(i + 1, G.stemRows);

            for (int j = 0; j < G.cols; ++j) {
                int a = i * G.cols + j;
                int b = (i + 1) * G.cols + j;

                // Current row
                glColor3f(colorA.x(), colorA.y(), colorA.z());
                glNormal3f(G.nrm[a].x(), G.nrm[a].y(), G.nrm[a].z());
                glTexCoord2f(G.uv[a].u, G.uv[a].v);
                glVertex3f(G.pos[a].x(), G.pos[a].y(), G.pos[a].z());

                // Next row
                glColor3f(colorB.x(), colorB.y(), colorB.z());
                glNormal3f(G.nrm[b].x(), G.nrm[b].y(), G.nrm[b].z());
                glTexCoord2f(G.uv[b].u, G.uv[b].v);
                glVertex3f(G.pos[b].x(), G.pos[b].y(), G.pos[b].z());
            }
            glEnd();
        }
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if (gTextureOK) {
        glDisable(GL_TEXTURE_2D);
    }
    if (gLightingOn) {
        glDisable(GL_LIGHTING);
    }
}

// Control point visualization
static void drawCtrlViz() {
    if (!gShowCtrlPts && !gShowCtrlPoly) return;
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING);

    auto drawSpline = [&](const std::vector<Vect3d>& ctrl, float r, float g, float b, int leafIdx) {
        if (ctrl.empty()) return;

        // Highlight selected leaf
        float alpha = (leafIdx == leafGeomIndex(gSelTiller, gSelLeaf)) ? 1.0f : 0.25f;
        glColor4f(r, g, b, alpha);

        if (gShowCtrlPoly) {
            glLineWidth(2.f);
            glBegin(GL_LINE_STRIP);
            for (const auto& p : ctrl) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
        }
        if (gShowCtrlPts) {
            glPointSize(7.0f);
            glBegin(GL_POINTS);
            for (const auto& p : ctrl) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
        }
        };

    for (size_t i = 0; i < Maize::leafGeoms().size(); ++i) {
        const auto& G = Maize::leafGeoms()[i];
        drawSpline(G.ctrlCenter, 1.f, 0.5f, 0.f, (int)i);
        drawSpline(G.ctrlLeft, 0.2f, 0.6f, 1.f, (int)i);
        drawSpline(G.ctrlRight, 1.f, 0.f, 1.f, (int)i);
        // --- REMOVED ---
        // Also draw the opening spline
        // drawSpline(G.ctrlOpening_world, 0.f, 1.f, 1.f, (int)i);
    }
    glEnable(GL_DEPTH_TEST);
}

// Grid and axes drawing
static void drawGridAndAxes() {
    glDisable(GL_LIGHTING);

    glLineWidth(1.0f);
    glColor3f(0.7f, 0.7f, 0.7f);
    glBegin(GL_LINES);
    for (int i = -10; i <= 10; ++i) {
        glVertex3f((float)i, 0, -10); glVertex3f((float)i, 0, 10);
        glVertex3f(-10, 0, (float)i); glVertex3f(10, 0, (float)i);
    }
    glEnd();

    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3f(1.f, 0.f, 0.f);
    glVertex3f(gCamTarget.x(), gCamTarget.y(), gCamTarget.z());
    glVertex3f(gCamTarget.x() + 0.2f, gCamTarget.y(), gCamTarget.z());
    glColor3f(0.f, 1.f, 0.f);
    glVertex3f(gCamTarget.x(), gCamTarget.y(), gCamTarget.z());
    glVertex3f(gCamTarget.x(), gCamTarget.y() + 0.2f, gCamTarget.z());
    glColor3f(0.f, 0.f, 1.f);
    glVertex3f(gCamTarget.x(), gCamTarget.y(), gCamTarget.z());
    glVertex3f(gCamTarget.x(), gCamTarget.y(), gCamTarget.z() + 0.2f);
    glEnd();
}

// Helper to get leaf geometry index from tiller and leaf indices
static int leafGeomIndex(int tIdx, int lIdx) {
    const auto& plant = Maize::plant();
    if (tIdx < 0 || tIdx >= (int)plant.tillers.size()) return -1;
    int base = 0;
    for (int t = 0; t < tIdx; ++t) base += (int)plant.tillers[t].leaves.size();
    if (lIdx < 0 || lIdx >= (int)plant.tillers[tIdx].leaves.size()) return -1;
    return base + lIdx;
}

// Draw text helper
static void drawText(float x, float y, const char* text, ImU32 color) {
    ImGui::GetBackgroundDrawList()->AddText(ImVec2(x, y), color, text);
}

// Draw spline control point overlays
static void drawSplineOverlays() {

    // This function is now empty as the only spline it drew (ctrlOpening)
    // is no longer visualized or directly edited.
    return;

    // --- REMOVED ALL PREVIOUS CODE ---
}

// Main display function
static void display() {
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGLUT_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();

    glViewport(0, 0, (GLsizei)io.DisplaySize.x, (GLsizei)io.DisplaySize.y);
    glClearColor(1.f, 1.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();

    if (io.DisplaySize.y == 0)
        gluPerspective((GLdouble)gFovDeg, (GLdouble)io.DisplaySize.x / 1.0, 0.1, 100.0);
    else
        gluPerspective((GLdouble)gFovDeg, (GLdouble)io.DisplaySize.x / (GLdouble)io.DisplaySize.y, 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    // Control camera with mouse and keyboard
    if (!io.WantCaptureMouse && io.MouseDown[0]) {
        gYaw += io.MouseDelta.x * 0.3f;
        gPitch = clampf(gPitch + io.MouseDelta.y * 0.3f, -89.f, 89.f);
    }
    gDist = clampf(gDist * (1.f - io.MouseWheel * 0.1f), 0.2f, 50.f);

    static int lastTime = 0;
    int time = glutGet(GLUT_ELAPSED_TIME);
    float dt = (time - lastTime) / 1000.0f;
    lastTime = time;

    if (!io.WantCaptureKeyboard) {
        float speed = gMoveSpeed * (io.KeyShift ? gBoost : 1.f) * (io.KeyCtrl ? gSlow : 1.f);
        float forward_move = 0.0f;
        float right_move = 0.0f;
        float up_move = 0.0f;

        if (ImGui::IsKeyDown(ImGuiKey_W)) forward_move -= speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_S)) forward_move += speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_A)) right_move += speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_D)) right_move -= speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) up_move -= speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_E)) up_move += speed * dt;

        float cy = cos(radiansf(gYaw));
        float sy = sin(radiansf(gYaw));

        gCamTarget.SetX(gCamTarget.x() + forward_move * cy - right_move * sy);
        gCamTarget.SetY(gCamTarget.y() + up_move);
        gCamTarget.SetZ(gCamTarget.z() + forward_move * sy + right_move * cy);
    }

    float cy = cos(radiansf(gYaw));
    float sy = sin(radiansf(gYaw));
    float cp = cos(radiansf(gPitch));
    float sp = sin(radiansf(gPitch));

    Vect3d eye = gCamTarget + Vect3d{ cp * cy, sp, cp * sy } *gDist;
    gluLookAt(eye.x(), eye.y(), eye.z(), gCamTarget.x(), gCamTarget.y(), gCamTarget.z(), 0, 1, 0);

    drawGridAndAxes();
    drawPlant();
    drawCtrlViz();
    drawSplineOverlays();

    ImGui::Begin("Maize Spline Editor");
    bool needRebuild = false;

    ImGui::SeparatorText("File I/O");
    if (ImGui::Button("New Plant")) {
        Maize::plant() = PlantDesc{};
        TillerDesc t; t.type = "main";

        // Create a leaf and define its parameters
        LeafDesc leaf1;
        leaf1.id = 0;
        leaf1.distance = 0.10f;
        leaf1.sheathLength = 0.15f;
        leaf1.leafLength = 0.4f; // short
        leaf1.leafWidth = 0.1f; // wide
        leaf1.leafAngle = 45.0f;
        leaf1.droopiness = 0.2f;
        leaf1.baseWidthPct = 0.8f;
        leaf1.splinePoints = 4;
        leaf1.widthTaper = 1.0f;
        leaf1.baseNudge = 1.1f;
        leaf1.leafTwist = 0.0f;
        leaf1.tipWidth = 0.0f;

        // --- ADDED ---
        leaf1.waveLAmp = 0.0f;
        leaf1.waveLFreq = 0.0f;
        leaf1.waveRAmp = 0.0f;
        leaf1.waveRFreq = 0.0f;

        // --- NEW ---
        leaf1.sheathEndAngle = 2.0f;
        leaf1.sheathOpeningStart = 0.0f;

        // --- REMOVED ---
        // Define default opening spline
        // std::vector<Vect3d> d1, d2, d3; // Dummies
        // Maize::makeDefaultLeafSplines(0.7f, 0.08f, d1, d2, d3, leaf1.ctrlOpening);

        // Add the custom leaf
        t.leaves.push_back(leaf1);

        Maize::plant().tillers.push_back(t);
        gSelTiller = 0;
        gSelLeaf = 0;
        needRebuild = true;
    }
    ImGui::SameLine();
    ImGui::InputText("XML Path", gXmlPath, sizeof(gXmlPath));
    if (ImGui::Button("Load XML")) {
        if (Descriptor::loadPlantXML(gXmlPath)) {
            needRebuild = true;
            gSelTiller = 0;
            gSelLeaf = 0;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save XML")) { Descriptor::savePlantXML(gXmlPath); }
    ImGui::InputText("OBJ Path", gObjPath, sizeof(gObjPath));
    if (ImGui::Button("Export OBJ")) { Maize::savePlantOBJ(gObjPath, gTexPath); }

    ImGui::SeparatorText("Texture");
    ImGui::InputText("Texture Path", gTexPath, sizeof(gTexPath));
    if (ImGui::Button("Load Texture")) {
        loadTexture(gTexPath);
    }

    ImGui::SeparatorText("Lighting & View");
    ImGui::Checkbox("Enable Lighting", &gLightingOn);
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &gWireframe);
    ImGui::DragFloat3("Light Position", gLightPos, 0.1f);
    ImGui::SliderFloat("Shininess", &gShininess, 1.0f, 128.0f);


    ImGui::SeparatorText("View Options");
    ImGui::Checkbox("Show Control Points", &gShowCtrlPts);
    ImGui::SameLine();
    ImGui::Checkbox("Show Control Polygon", &gShowCtrlPoly);

    needRebuild |= ImGui::SliderInt("Leaf Rows", &Maize::gLeafRows(), 3, 200);
    needRebuild |= ImGui::SliderInt("Leaf Columns", &Maize::gLeafCols(), 3, 51);

    ImGui::SeparatorText("Scene Statistics");
    ImGui::Text("Total Triangles: %d", Maize::getTriangleCount());

    auto& plant = Maize::plant();
    if (!plant.tillers.empty()) {
        gSelTiller = std::min(gSelTiller, (int)plant.tillers.size() - 1);
        auto& t = plant.tillers[gSelTiller];

        ImGui::SeparatorText("Tiller Parameters");
        needRebuild |= ImGui::DragFloat("Radius", &t.radius, 0.001f, 0.01f, 0.1f, "%.3f m");
        needRebuild |= ImGui::DragFloat("Tilt", &t.alphaDeg, 0.1f, -20.f, 20.f, "%.1f deg");
        needRebuild |= ImGui::DragFloat("Shrink per Leaf", &t.stemShrink, 0.0001f, 0.f, 0.01f, "%.4f m");
        needRebuild |= ImGui::DragFloat("Azimuth Base", &t.azimuthDeg, 1.0f, -360.f, 360.f, "%.1f deg");
        needRebuild |= ImGui::DragFloat("Azimuth Noise", &t.azimuthNoise, 0.1f, 0.f, 90.f, "%.1f deg");

        ImGui::InputInt("Random Seed", (int*)&t.randomSeed);
        if (ImGui::IsItemEdited()) needRebuild = true;
        ImGui::SameLine();
        if (ImGui::Button("Randomize")) {
            t.randomSeed = (unsigned int)rand();
            needRebuild = true;
        }

        ImGui::SeparatorText("Leaf Management");
        if (ImGui::Button("Add New Leaf")) {
            LeafDesc newLeaf;

            // Check if a previous leaf exists to copy from
            if (!t.leaves.empty()) {
                newLeaf = t.leaves.back(); // This now copies all fields, including new params
            }

            // Set a new unique ID
            newLeaf.id = t.leaves.empty() ? 0 : t.leaves.back().id + 1;

            t.leaves.push_back(newLeaf);
            gSelLeaf = (int)t.leaves.size() - 1;

            needRebuild = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Selected Leaf")) {
            if (!t.leaves.empty()) {
                t.leaves.erase(t.leaves.begin() + gSelLeaf);
                if (gSelLeaf >= (int)t.leaves.size()) {
                    gSelLeaf = (int)t.leaves.size() - 1;
                }
                needRebuild = true;
            }
        }


        if (!t.leaves.empty()) {
            gSelLeaf = std::min(gSelLeaf, (int)t.leaves.size() - 1);

            ImGui::SeparatorText("Leaf Selection");
            if (ImGui::BeginCombo("Leaf", std::to_string(gSelLeaf).c_str())) {
                for (int i = 0; i < (int)t.leaves.size(); ++i) {
                    if (ImGui::Selectable(std::to_string(i).c_str(), gSelLeaf == i)) gSelLeaf = i;
                }
                ImGui::EndCombo();
            }

            auto& L = t.leaves[gSelLeaf];
            ImGui::SeparatorText("Leaf Parameters");
            needRebuild |= ImGui::DragFloat("Internode Dist.", &L.distance, 0.01f, 0.0f, 1.0f, "%.2f m");
            needRebuild |= ImGui::SliderFloat("Sheath Length", &L.sheathLength, 0.f, 1.f, "%.2f");

            // --- MODIFIED LEAF BLADE EDITOR ---
            ImGui::SeparatorText("Leaf Blade Parameters");
            needRebuild |= ImGui::DragFloat("Length", &L.leafLength, 0.01f, 0.1f, 2.0f, "%.2f m");
            needRebuild |= ImGui::DragFloat("Width", &L.leafWidth, 0.005f, 0.01f, 0.3f, "%.3f m");
            needRebuild |= ImGui::SliderFloat("Leaf Angle", &L.leafAngle, -90.f, 90.f, "%.1f deg");
            needRebuild |= ImGui::SliderFloat("Droopiness", &L.droopiness, 0.f, 2.f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Base Width %", &L.baseWidthPct, 0.f, 1.f, "%.2f");
            needRebuild |= ImGui::SliderInt("Spline Points", &L.splinePoints, 4, 10);
            needRebuild |= ImGui::SliderFloat("Width Taper", &L.widthTaper, 0.2f, 3.0f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Base Nudge", &L.baseNudge, 0.5f, 2.0f, "%.2f");
            needRebuild |= ImGui::SliderFloat("Leaf Twist", &L.leafTwist, -180.f, 180.f, "%.1f deg");
            needRebuild |= ImGui::SliderFloat("Tip Width", &L.tipWidth, 0.f, L.leafWidth * 0.5f, "%.4f m");

            // --- ADDED ---
            ImGui::SeparatorText("Leaf Blade Waves");
            needRebuild |= ImGui::SliderFloat("L. Wave Amp", &L.waveLAmp, 0.f, 0.05f, "%.3f m");
            needRebuild |= ImGui::SliderFloat("L. Wave Freq", &L.waveLFreq, 0.f, 10.f, "%.1f");
            needRebuild |= ImGui::SliderFloat("R. Wave Amp", &L.waveRAmp, 0.f, 0.05f, "%.3f m");
            needRebuild |= ImGui::SliderFloat("R. Wave Freq", &L.waveRFreq, 0.f, 10.f, "%.1f");


            // --- MODIFIED SHEATH EDITOR ---
            ImGui::SeparatorText("Sheath Parameters");
            needRebuild |= ImGui::SliderFloat("Sheath End Angle", &L.sheathEndAngle, 0.f, 2.f, "%.2f PI");
            needRebuild |= ImGui::SliderFloat("Sheath Opening Start", &L.sheathOpeningStart, 0.f, 1.f, "%.2f");


            // --- REMOVED SPLINE EDITOR ---

        }
    }
    ImGui::End();

    if (needRebuild) {
        Maize::rebuildGeometry();
    }

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    glutSwapBuffers();

} // This was the original end of display()

// Reshape function
static void reshape(int w, int h) {
    gWinW = w > 0 ? w : 1;
    gWinH = h > 0 ? h : 1;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)gWinW, (float)gWinH);
}

// Idle function
static void idle() { glutPostRedisplay(); }

// Main function
int main(int argc, char** argv) {
    srand((unsigned int)time(NULL));

    // --- Example of generation from code (now updated) ---
    //// Create the plant description
    //PlantDesc myPlant;
    //myPlant.species = "maize";
    //TillerDesc mainTiller;
    //mainTiller.type = "main";
    //mainTiller.radius = 0.025f;
    //mainTiller.azimuthDeg = 180.f; // Leaves alternate
    //mainTiller.randomSeed = 2024;
    //
    //// Create a leaf and define its parameters
    //LeafDesc leaf1;
    //leaf1.id = 0;
    //leaf1.distance = 0.30f;
    //leaf1.sheathLength = 0.15f;
    //leaf1.leafLength = 0.45f;
    //leaf1.leafWidth = 0.08f;
    //leaf1.leafAngle = 45.0f;
    //leaf1.droopiness = 1.2f; // Extra droopy
    //leaf1.baseWidthPct = 0.9f;
    //leaf1.splinePoints = 4;
    //leaf1.widthTaper = 1.0f;
    //leaf1.baseNudge = 1.1f;
    //leaf1.leafTwist = 0.0f;
    //leaf1.tipWidth = 0.0f;
    //leaf1.waveLAmp = 0.01f;
    //leaf1.waveLFreq = 5.0f;
    //leaf1.waveRAmp = 0.01f;
    //leaf1.waveRFreq = 5.0f;
    //leaf1.sheathEndAngle = 2.0f;
    //leaf1.sheathOpeningStart = 0.0f;
    //mainTiller.leaves.push_back(leaf1);

    //LeafDesc leaf2;
    //leaf2.id = 1;
    //leaf2.distance = 0.25f;
    //leaf2.sheathLength = 0.18f;
    //leaf2.leafLength = 0.30f;
    //leaf2.leafWidth = 0.08f;
    //leaf2.leafAngle = 30.0f;
    //leaf2.droopiness = 0.5f;
    //leaf2.baseWidthPct = 0.8f;
    //leaf2.splinePoints = 4;
    //leaf2.widthTaper = 1.0f;
    //leaf2.baseNudge = 1.1f;
    //leaf2.leafTwist = 0.0f;
    //leaf2.tipWidth = 0.0f;
    //leaf2.waveLAmp = 0.0f;
    //leaf2.waveLFreq = 0.0f;
    //leaf2.waveRAmp = 0.0f;
    //leaf2.waveRFreq = 0.0f;
    //leaf2.sheathEndAngle = 2.0f;
    //leaf2.sheathOpeningStart = 0.0f;
    //mainTiller.leaves.push_back(leaf2);
    //
    //myPlant.tillers.push_back(mainTiller);
    //Maize::GeneratePlant(myPlant, "my_plant", "maize_leaf.png");
    // --- End of example ---

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(gWinW, gWinH);
    glutCreateWindow("Maize Spline Editor");
    ImGui::CreateContext();
    ImGui_ImplGLUT_Init();
    ImGui_ImplGLUT_InstallFuncs();
    ImGui_ImplOpenGL2_Init();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)gWinW, (float)gWinH);

    // Try to load existing file first
    bool loaded = Descriptor::loadPlantXML(gXmlPath);

    if (!loaded || Maize::plant().tillers.empty()) {
        // If load failed or plant is empty, create a default
        Maize::plant() = PlantDesc{};
        TillerDesc t; t.type = "main";

        // Create a leaf and define its parameters
        LeafDesc leaf1;
        leaf1.id = 0;
        leaf1.distance = 0.30f;
        leaf1.sheathLength = 0.15f;
        leaf1.leafLength = 0.45f;
        leaf1.leafWidth = 0.08f;
        leaf1.leafAngle = 45.0f;
        leaf1.droopiness = 1.0f;
        leaf1.baseWidthPct = 0.9f;
        leaf1.splinePoints = 4;
        leaf1.widthTaper = 1.0f;
        leaf1.baseNudge = 1.1f;
        leaf1.leafTwist = 0.0f;
        leaf1.tipWidth = 0.0f;
        // --- ADDED ---
        leaf1.waveLAmp = 0.0f;
        leaf1.waveLFreq = 0.0f;
        leaf1.waveRAmp = 0.0f;
        leaf1.waveRFreq = 0.0f;
        leaf1.sheathEndAngle = 2.0f;
        leaf1.sheathOpeningStart = 0.0f;
        t.leaves.push_back(leaf1);

        LeafDesc leaf2;
        leaf2.id = 1;
        leaf2.distance = 0.25f;
        leaf2.sheathLength = 0.18f;
        leaf2.leafLength = 0.30f;
        leaf2.leafWidth = 0.08f;
        leaf2.leafAngle = 30.0f;
        leaf2.droopiness = 0.5f;
        leaf2.baseWidthPct = 0.8f;
        leaf2.splinePoints = 4;
        leaf2.widthTaper = 1.0f;
        leaf2.baseNudge = 1.1f;
        leaf2.leafTwist = 0.0f;
        leaf2.tipWidth = 0.0f;
        // --- ADDED ---
        leaf2.waveLAmp = 0.0f;
        leaf2.waveLFreq = 0.0f;
        leaf2.waveRAmp = 0.0f;
        leaf2.waveRFreq = 0.0f;
        leaf2.sheathEndAngle = 2.0f;
        leaf2.sheathOpeningStart = 0.0f;
        t.leaves.push_back(leaf2);

        Maize::plant().tillers.push_back(t);
    }

    loadTexture(gTexPath);
    Maize::rebuildGeometry();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutMainLoop();
    return 0;
}