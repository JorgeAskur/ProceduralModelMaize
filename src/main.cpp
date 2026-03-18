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
#include "MaizeGenerator.h" 

using namespace std;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int   gWinW = 1280, gWinH = 860;
static float gYaw = 30.f, gPitch = 18.f, gDist = 3.5f;
static Vect3d gCamTarget = { 0.0f, 1.0f, 0.0f };
static float gFovDeg = 60.0f;
static float gMoveSpeed = 1.0f;

static int gSelTiller = 0;
static int gSelLeaf = 0;

static bool gShowCtrlPts = false;
static bool gShowCtrlPoly = false;
static bool gShowSkeletonSpline = false;
static bool gSkeletonOnlyView = false;
static bool gWireframe = false;
static bool gShowNormals = false;

static bool gLightingOn = true;
static float gLightPos[4] = { 2.0f, 8.0f, 4.0f, 1.0f };
static float gGlobalAmb[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
static float gLightDiff[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
static float gLightSpec[4] = { 0.6f, 0.6f, 0.6f, 1.0f };
static float gMatSpec[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
static float gShininess = 32.0f;

static char gTexPath[256] = "maize_leaf.png";
static char gStemTexPath[256] = "maize_stem_texture.png";
static char gExportBasePath[256] = "plant_export/plant";
static char gXmlPath[256] = "plant_spline.xml";
static char gSkeletonXmlPath[256] = "plant_skeleton.xml";
static char gObjPath[256] = "plant_spline.obj";
static char gSkeletonObjPath[256] = "plant_skeleton.obj";

static GLuint gTextureID = 0;
static bool   gTextureOK = false;
static GLuint gStemTextureID = 0;
static bool   gStemTextureOK = false;

static MaizeGenerator gGen;

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }

static void loadTexture(const char* path, GLuint* textureId, bool* textureOk) {
    if (*textureId) { glDeleteTextures(1, textureId); *textureId = 0; }
    *textureOk = false;
    int w, h, n;
    unsigned char* data = stbi_load(path, &w, &h, &n, 4);
    if (data) {
        glGenTextures(1, textureId);
        glBindTexture(GL_TEXTURE_2D, *textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        stbi_image_free(data);
        *textureOk = true;
    }
}

static void drawSkeletonViz() {
    if (!gShowSkeletonSpline) return;
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    glColor3f(0.0f, 0.0f, 0.0f);
    for (const auto& G : Maize::leafGeoms()) {
        if (G.ctrlCenter.size() < 2) continue;
        glBegin(GL_LINE_STRIP);
        for (const auto& p : G.ctrlCenter) {
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
    }
    glEnable(GL_DEPTH_TEST);
}

static const LeafGeom* GetStemGeom() {
    for (const auto& G : Maize::leafGeoms()) {
        if (G.type == LeafGeom::GEOM_SHEATH && !G.ctrlCenter.empty()) return &G;
    }
    return nullptr;
}

static void setupLighting() {
    if (!gLightingOn) { glDisable(GL_LIGHTING); return; }
    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
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

static void drawPlant() {
    setupLighting();
    glShadeModel(GL_SMOOTH);

    glColor4f(1.f, 1.f, 1.f, 1.f);
    bool textureEnabled = false;
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);

    for (const auto& G : Maize::leafGeoms()) {
        bool useTexture = (G.type == LeafGeom::GEOM_SHEATH) ? gStemTextureOK : gTextureOK;
        GLuint texId = (G.type == LeafGeom::GEOM_SHEATH) ? gStemTextureID : gTextureID;
        if (useTexture) {
            if (!textureEnabled) { glEnable(GL_TEXTURE_2D); textureEnabled = true; }
            glBindTexture(GL_TEXTURE_2D, texId);
        } else if (textureEnabled) {
            glDisable(GL_TEXTURE_2D);
            textureEnabled = false;
        }
        if (G.rows <= 1 || G.cols <= 1) continue;

        for (int i = 0; i < G.rows - 1; ++i) {
            glBegin(GL_TRIANGLE_STRIP);
            for (int j = 0; j < G.cols; ++j) {
                int a = i * G.cols + j, b = (i + 1) * G.cols + j;
                Vect3d cA = G.col[a];
                Vect3d cB = G.col[b];
                glColor3f(cA.x(), cA.y(), cA.z());
                glNormal3f(G.nrm[a].x(), G.nrm[a].y(), G.nrm[a].z());
                glTexCoord2f(G.uv[a].u, G.uv[a].v);
                glVertex3f(G.pos[a].x(), G.pos[a].y(), G.pos[a].z());

                glColor3f(cB.x(), cB.y(), cB.z());
                glNormal3f(G.nrm[b].x(), G.nrm[b].y(), G.nrm[b].z());
                glTexCoord2f(G.uv[b].u, G.uv[b].v);
                glVertex3f(G.pos[b].x(), G.pos[b].y(), G.pos[b].z());
            }
            glEnd();
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if (textureEnabled) glDisable(GL_TEXTURE_2D);
    if (gLightingOn) glDisable(GL_LIGHTING);
}

static void drawNormals() {
    if (!gShowNormals) return;
    glDisable(GL_LIGHTING);
    glColor3f(0.1f, 0.6f, 0.1f);
    glBegin(GL_LINES);
    for (const auto& G : Maize::leafGeoms()) {
        for (size_t i = 0; i < G.pos.size(); ++i) {
            const auto& p = G.pos[i];
            const auto& n = G.nrm[i];
            Vect3d end = p + n * 0.02f;
            glVertex3f(p.x(), p.y(), p.z());
            glVertex3f(end.x(), end.y(), end.z());
        }
    }
    glEnd();
}

static void drawCtrlViz() {
    if (!gShowCtrlPts && !gShowCtrlPoly) return;
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING);
    auto drawSpline = [&](const std::vector<Vect3d>& ctrl, float r, float g, float b) {
        if (ctrl.empty()) return;
        glColor4f(r, g, b, 1.0f);
        if (gShowCtrlPoly) { glLineWidth(2.f); glBegin(GL_LINE_STRIP); for (const auto& p : ctrl) glVertex3f(p.x(), p.y(), p.z()); glEnd(); }
        if (gShowCtrlPts) { glPointSize(5.0f); glBegin(GL_POINTS); for (const auto& p : ctrl) glVertex3f(p.x(), p.y(), p.z()); glEnd(); }
        };
    for (const auto& G : Maize::leafGeoms()) {
        if (G.type == LeafGeom::GEOM_SHEATH && !G.ctrlCenter.empty()) {
            drawSpline(G.ctrlCenter, 0.2f, 0.8f, 0.2f);
        }
        if (G.type == LeafGeom::GEOM_BLADE) {
            drawSpline(G.ctrlCenter, 1.f, 0.5f, 0.f);
            drawSpline(G.ctrlLeft, 0.2f, 0.6f, 1.f);
            drawSpline(G.ctrlRight, 1.f, 0.f, 1.f);
        }
    }
    glEnable(GL_DEPTH_TEST);
}

static void drawGridAndAxes() {
    glDisable(GL_LIGHTING);
    glLineWidth(1.0f); glColor3f(0.7f, 0.7f, 0.7f);
    glBegin(GL_LINES);
    for (int i = -10; i <= 10; ++i) { glVertex3f((float)i, 0, -10); glVertex3f((float)i, 0, 10); glVertex3f(-10, 0, (float)i); glVertex3f(10, 0, (float)i); }
    glEnd();
    glLineWidth(2.0f); glBegin(GL_LINES);
    glColor3f(1, 0, 0); glVertex3f(0, 0, 0); glVertex3f(1, 0, 0);
    glColor3f(0, 1, 0); glVertex3f(0, 0, 0); glVertex3f(0, 1, 0);
    glColor3f(0, 0, 1); glVertex3f(0, 0, 0); glVertex3f(0, 0, 1);
    glEnd();
}

static void RebuildFromGenerator() {
    unsigned int seed = 1337;
    Maize::plant() = gGen.generate(seed, true);
    Maize::rebuildGeometry();
}

static float hash_to_float_minus1_1(unsigned int seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed = seed + (seed << 3);
    seed = seed ^ (seed >> 4);
    seed = seed * 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return ((float)(int)(seed & 0xFFFFFF) / (float)(0xFFFFFF / 2)) - 1.0f;
}

static const LeafGeom* GetBladeGeomForLeaf(const PlantDesc& plant, int tillerIndex, int leafIndex) {
    int target = 0;
    for (int t = 0; t < tillerIndex; ++t) {
        target += (int)plant.tillers[t].leaves.size();
    }
    target += leafIndex;

    int bladeIndex = 0;
    for (const auto& G : Maize::leafGeoms()) {
        if (G.type == LeafGeom::GEOM_BLADE) {
            if (bladeIndex == target) return &G;
            ++bladeIndex;
        }
    }
    return nullptr;
}

static bool CopyBladeCtrlToLeafOverrides(PlantDesc& plant, int tillerIndex, int leafIndex, LeafDesc& leaf) {
    if (tillerIndex < 0 || tillerIndex >= (int)plant.tillers.size()) return false;
    const auto* bladeGeom = GetBladeGeomForLeaf(plant, tillerIndex, leafIndex);
    if (!bladeGeom) return false;

    const auto& td = plant.tillers[tillerIndex];
    float current_y = 0.f;
    for (int i = 0; i <= leafIndex && i < (int)td.leaves.size(); ++i) {
        current_y += std::max(0.f, td.leaves[i].distance);
    }
    Vect3d sheath_exit_pos = { 0, current_y, 0 };

    unsigned int leaf_seed = td.randomSeed + (unsigned int)leafIndex;
    float noise_val = hash_to_float_minus1_1(leaf_seed);
    float leafAzimuth = td.azimuthDeg * leafIndex + (noise_val * td.azimuthNoise);
    float phi = radiansf(leafAzimuth);
    Vect3d rhat = { std::cos(phi), 0.f, std::sin(phi) };

    Vect3d blade_y_axis = { 0, 1, 0 };
    Vect3d blade_z_axis = rhat;
    Vect3d blade_x_axis = blade_y_axis.Cross(blade_z_axis).GetNormalized();

    float tilt = radiansf(td.alphaDeg);
    float ct = std::cos(tilt), st = std::sin(tilt);
    auto untilt = [&](const Vect3d& p)->Vect3d { return { p.x() * ct + p.y() * st, -p.x() * st + p.y() * ct, p.z() }; };

    auto toLocal = [&](const Vect3d& p_world) {
        Vect3d p = untilt(p_world) - sheath_exit_pos;
        return Vect3d(blade_x_axis.Dot(p), blade_z_axis.Dot(p), blade_y_axis.Dot(p));
    };

    leaf.ctrlCenterOverride.clear();
    leaf.ctrlLeftOverride.clear();
    leaf.ctrlRightOverride.clear();

    for (const auto& p : bladeGeom->ctrlCenter) leaf.ctrlCenterOverride.push_back(toLocal(p));
    for (const auto& p : bladeGeom->ctrlLeft) leaf.ctrlLeftOverride.push_back(toLocal(p));
    for (const auto& p : bladeGeom->ctrlRight) leaf.ctrlRightOverride.push_back(toLocal(p));

    return true;
}

static void display() {
    ImGui_ImplOpenGL2_NewFrame(); ImGui_ImplGLUT_NewFrame(); ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, (GLsizei)io.DisplaySize.x, (GLsizei)io.DisplaySize.y);
    glClearColor(1.f, 1.f, 1.f, 1.f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective((GLdouble)gFovDeg, (GLdouble)io.DisplaySize.x / (GLdouble)io.DisplaySize.y, 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    if (!io.WantCaptureMouse && io.MouseDown[0]) { gYaw += io.MouseDelta.x * 0.3f; gPitch = clampf(gPitch + io.MouseDelta.y * 0.3f, -89.f, 89.f); }
    gDist = clampf(gDist * (1.f - io.MouseWheel * 0.1f), 0.2f, 50.f);

    static int lastTime = 0; int time = glutGet(GLUT_ELAPSED_TIME); float dt = (time - lastTime) / 1000.0f; lastTime = time;
    if (!io.WantCaptureKeyboard) {
        float speed = gMoveSpeed * (io.KeyShift ? 4.f : 1.f);
        float fwd = 0, rt = 0, up = 0;
        if (ImGui::IsKeyDown(ImGuiKey_W)) fwd -= speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_S)) fwd += speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_A)) rt += speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_D)) rt -= speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) up -= speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_E)) up += speed * dt;
        float cy = cos(radiansf(gYaw)), sy = sin(radiansf(gYaw));
        gCamTarget = gCamTarget + Vect3d{ fwd * cy - rt * sy, up, fwd * sy + rt * cy };
    }

    float cy = cos(radiansf(gYaw)), sy = sin(radiansf(gYaw));
    float cp = cos(radiansf(gPitch)), sp = sin(radiansf(gPitch));
    Vect3d eye = gCamTarget + Vect3d{ cp * cy, sp, cp * sy } *gDist;
    gluLookAt(eye.x(), eye.y(), eye.z(), gCamTarget.x(), gCamTarget.y(), gCamTarget.z(), 0, 1, 0);

    drawGridAndAxes();
    if (!gSkeletonOnlyView) {
        drawPlant();
        drawNormals();
        drawCtrlViz();
    }
    drawSkeletonViz();

    ImGui::Begin("Maize Generator");

    bool needRegen = false;
    bool needUpdate = false;

    ImGui::Text("Camera: Left Click=Rotate, WASD=Move, Scroll=Zoom");

    if (ImGui::CollapsingHeader("Global Traits", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Edit global traits and defaults for the plant.");

        auto DistUI = [&](const char* label, Dist& d, float v_min, float v_max) {
            ImGui::PushID(label);
            ImGui::Text("%s", label);
            bool c = false;
            c |= ImGui::SliderFloat("Value", &d.mean, v_min, v_max);
            ImGui::PopID();
            return c;
            };

        auto Curve3UI = [&](const char* label, Curve3& c, float v_min, float v_max) {
            ImGui::PushID(label);
            ImGui::Text("%s", label);
            bool changed = false;
            changed |= ImGui::SliderFloat("Bottom", &c.bottom, v_min, v_max);
            changed |= ImGui::SliderFloat("Middle", &c.middle, v_min, v_max);
            changed |= ImGui::SliderFloat("Top", &c.top, v_min, v_max);
            ImGui::PopID();
            return changed;
            };

        needRegen |= DistUI("Height (m)", gGen.height, 0.5f, 4.0f);
        needRegen |= DistUI("Stem Radius", gGen.stemRadius, 0.01f, 0.1f);
        needRegen |= DistUI("Leaf Count", gGen.numLeaves, 3.0f, 20.0f);
        needRegen |= ImGui::SliderFloat("Tiller Azimuth Base", &gGen.tillerAzimuthDeg, -360.0f, 360.0f);
        needRegen |= ImGui::SliderFloat("Tiller Azimuth Noise", &gGen.tillerAzimuthNoise, 0.0f, 90.0f);
        needRegen |= ImGui::SliderFloat("Stem Shrink Factor", &gGen.stemShrinkFactor, 0.0f, 2.0f);
        needRegen |= ImGui::SliderFloat("Stem Shrink Min", &gGen.stemShrinkMin, 0.0f, 0.02f);

        ImGui::Separator();
        ImGui::Text("Leaf Defaults");
        needRegen |= DistUI("Length", gGen.leafLengthBase, 0.2f, 1.5f);
        needRegen |= DistUI("Width", gGen.leafWidthBase, 0.02f, 0.2f);
        needRegen |= DistUI("Width Taper", gGen.widthTaper, 0.0f, 10.0f);
        needRegen |= DistUI("Angle", gGen.leafAngleBase, 0.f, 90.f);
        needRegen |= DistUI("Droopiness", gGen.droopinessBase, -80.f, 80.f);
        needRegen |= DistUI("Stem Inclination", gGen.stemInclination, -90.f, 90.f);
        needRegen |= DistUI("Leaf Twist", gGen.leafTwist, -180.f, 180.f);
        needRegen |= DistUI("Leaf Curl", gGen.leafCurl, -1.0f, 1.0f);
        needRegen |= ImGui::SliderInt("Spline Points (Default)", &gGen.splinePointsDefault, 4, 80);

        ImGui::Separator();
        ImGui::Text("Wave Defaults");
        needRegen |= DistUI("Wave Amp", gGen.waveAmp, 0.f, 0.05f);
        needRegen |= DistUI("Wave Freq", gGen.waveFreq, 0.f, 40.f);
        needRegen |= DistUI("Wave Phase", gGen.wavePhaseShift, -6.283f, 6.283f);

        ImGui::Separator();
        ImGui::Text("Architecture");
        needRegen |= ImGui::SliderFloat("Whorl Start", &gGen.whorlStart, 0.0f, 0.95f);
        needRegen |= ImGui::SliderFloat("Whorl Compression", &gGen.whorlCompression, 0.0f, 1.5f);
        needRegen |= ImGui::SliderFloat("Whorl Min Internode", &gGen.whorlMinInternodeWeight, 0.01f, 1.0f);
        needRegen |= ImGui::SliderFloat("Lower Leaf Angle Boost", &gGen.lowerLeafAngleBoost, -30.0f, 30.0f);
        needRegen |= ImGui::SliderFloat("Upper Leaf Angle Drop", &gGen.upperLeafAngleDrop, -30.0f, 30.0f);

        ImGui::Separator();
        ImGui::Text("Leaf Visual Shaping Defaults");
        needRegen |= ImGui::SliderFloat("Surface Noise Amp (Default)", &gGen.leafSurfaceNoiseAmp, 0.0f, 0.05f);
        needRegen |= ImGui::SliderFloat("Surface Noise Freq (Default)", &gGen.leafSurfaceNoiseFreq, 0.1f, 24.0f);
        needRegen |= ImGui::SliderFloat("Tip Width (Default)", &gGen.leafTipWidthFactor, 0.0f, 0.12f);
        needRegen |= ImGui::SliderFloat("Midrib Tip Taper (Default)", &gGen.midribTipTaperStart, 0.2f, 0.98f);
        needRegen |= ImGui::SliderFloat("Midrib Texture Strength (Default)", &gGen.midribTextureStrength, 0.0f, 0.95f);
        needRegen |= ImGui::SliderFloat("Ligule Wrap Length (Default)", &gGen.liguleWrapLengthScale, 0.5f, 8.0f);
        needRegen |= ImGui::SliderFloat("Ligule Unfold Sharpness (Default)", &gGen.liguleUnfoldSharpness, 0.2f, 4.0f);
        needRegen |= ImGui::SliderFloat("Sheath Outer Scale (Default)", &gGen.sheathOuterScale, 1.0f, 1.6f);

        ImGui::Separator();
        ImGui::Text("Leaf Curves (Bottom / Middle / Top)");
        needRegen |= Curve3UI("Length Scale Curve", gGen.leafLengthScaleCurve, 0.2f, 2.5f);
        needRegen |= Curve3UI("Width Scale Curve", gGen.leafWidthScaleCurve, 0.2f, 2.5f);
        needRegen |= Curve3UI("Taper Scale Curve", gGen.widthTaperScaleCurve, 0.2f, 2.5f);
        needRegen |= Curve3UI("Wave Amp Scale Curve", gGen.waveAmpScaleCurve, 0.0f, 3.0f);
        needRegen |= Curve3UI("Wave Freq Scale Curve", gGen.waveFreqScaleCurve, 0.2f, 3.0f);
        needRegen |= Curve3UI("Angle Offset Curve", gGen.leafAngleOffsetCurve, -45.0f, 45.0f);
        needRegen |= Curve3UI("Droopiness Offset Curve", gGen.droopinessOffsetCurve, -40.0f, 40.0f);
        needRegen |= Curve3UI("Twist Offset Curve", gGen.leafTwistOffsetCurve, -90.0f, 90.0f);
        needRegen |= Curve3UI("Curl Offset Curve", gGen.leafCurlOffsetCurve, -1.0f, 1.0f);
        needRegen |= Curve3UI("Stem Inclination Offset Curve", gGen.stemInclinationOffsetCurve, -40.0f, 40.0f);
        needRegen |= ImGui::SliderFloat("Leaf Jitter", &gGen.leafJitterScale, 0.0f, 1.0f);

    }

    if (ImGui::CollapsingHeader("Leaf Traits")) {
        ImGui::TextWrapped("Edit individual leaf traits and control points.");
        auto& plant = Maize::plant();
        if (!plant.tillers.empty()) {
            gSelTiller = std::min(gSelTiller, (int)plant.tillers.size() - 1);
            auto& t = plant.tillers[gSelTiller];

            ImGui::Separator(); ImGui::Text("Tiller");
            needUpdate |= ImGui::DragFloat("Radius", &t.radius, 0.001f);
            needUpdate |= ImGui::DragFloat("Stem Shrink", &t.stemShrink, 0.0002f);
            needUpdate |= ImGui::DragFloat("Azimuth Base", &t.azimuthDeg, 1.0f);
            needUpdate |= ImGui::DragFloat("Azimuth Noise", &t.azimuthNoise, 0.5f);

            ImGui::Separator();
            bool useStemOverrides = t.useStemCtrlOverrides;
            if (ImGui::Checkbox("Edit Stem Control Points", &useStemOverrides)) {
                t.useStemCtrlOverrides = useStemOverrides;
                if (!t.useStemCtrlOverrides) {
                    t.stemCtrlOverride.clear();
                } else if (t.stemCtrlOverride.empty()) {
                    if (const auto* stemGeom = GetStemGeom()) {
                        t.stemCtrlOverride = stemGeom->ctrlCenter;
                    }
                }
                needUpdate = true;
            }

            if (t.useStemCtrlOverrides) {
                if (ImGui::Button("Copy Current Stem Control Points")) {
                    if (const auto* stemGeom = GetStemGeom()) {
                        t.stemCtrlOverride = stemGeom->ctrlCenter;
                        needUpdate = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Stem Control Points")) {
                    t.useStemCtrlOverrides = false;
                    t.stemCtrlOverride.clear();
                    needUpdate = true;
                }

                if (ImGui::TreeNode("Stem Control Points")) {
                    for (size_t i = 0; i < t.stemCtrlOverride.size(); ++i) {
                        float v[3] = { t.stemCtrlOverride[i].x(), t.stemCtrlOverride[i].y(), t.stemCtrlOverride[i].z() };
                        ImGui::PushID((int)i);
                        if (ImGui::DragFloat3("##stempt", v, 0.01f)) {
                            t.stemCtrlOverride[i].Set(v[0], v[1], v[2]);
                            needUpdate = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }

            if (!t.leaves.empty()) {
                gSelLeaf = std::min(gSelLeaf, (int)t.leaves.size() - 1);
                ImGui::SliderInt("Select Leaf", &gSelLeaf, 0, (int)t.leaves.size() - 1);
                auto& L = t.leaves[gSelLeaf];

                ImGui::Separator(); ImGui::Text("Leaf #%d Parameters", L.id);
                needUpdate |= ImGui::DragFloat("Distance", &L.distance, 0.01f);
                needUpdate |= ImGui::DragFloat("Length", &L.leafLength, 0.01f);
                needUpdate |= ImGui::DragFloat("Width", &L.leafWidth, 0.005f);
                needUpdate |= ImGui::SliderFloat("Angle", &L.leafAngle, -90.f, 90.f);
                needUpdate |= ImGui::SliderFloat("Leaf Curl", &L.leafCurl, -1.0f, 1.0f);
                needUpdate |= ImGui::SliderFloat("Droopiness", &L.droopiness, -80.f, 80.f);
                needUpdate |= ImGui::SliderFloat("Stem Inclination", &L.stemInclinationDeg, -30.f, 30.f);
                bool splineChanged = ImGui::SliderInt("Spline Pts", &L.splinePoints, 4, 40);
                needUpdate |= splineChanged;
                if (splineChanged && L.useCtrlOverrides) {
                    L.useCtrlOverrides = false;
                    L.ctrlCenterOverride.clear();
                    L.ctrlLeftOverride.clear();
                    L.ctrlRightOverride.clear();
                }
                needUpdate |= ImGui::SliderFloat("Taper", &L.widthTaper, 0.05f, 10.0f);
                needUpdate |= ImGui::SliderFloat("Twist", &L.leafTwist, -180.f, 180.f);
                ImGui::Text("Waves");
                needUpdate |= ImGui::SliderFloat("L Amp", &L.waveLAmp, 0.f, 0.05f);
                needUpdate |= ImGui::SliderFloat("L Freq", &L.waveLFreq, 0.f, 20.f);
                needUpdate |= ImGui::SliderFloat("L Phase", &L.waveLPhase, -6.283f, 6.283f);
                needUpdate |= ImGui::SliderFloat("R Amp", &L.waveRAmp, 0.f, 0.05f);
                needUpdate |= ImGui::SliderFloat("R Freq", &L.waveRFreq, 0.f, 20.f);
                needUpdate |= ImGui::SliderFloat("R Phase", &L.waveRPhase, -6.283f, 6.283f);

                ImGui::Text("Leaf Surface");
                needUpdate |= ImGui::SliderFloat("Surface Noise Amp", &L.surfaceNoiseAmp, 0.0f, 0.05f);
                needUpdate |= ImGui::SliderFloat("Surface Noise Freq", &L.surfaceNoiseFreq, 0.1f, 24.0f);
                needUpdate |= ImGui::SliderFloat("Tip Width", &L.tipWidthFactor, 0.0f, 0.12f);

                ImGui::Text("Midrib");
                needUpdate |= ImGui::SliderFloat("Midrib Tip Taper Start", &L.midribTipTaperStart, 0.2f, 0.98f);
                needUpdate |= ImGui::SliderFloat("Midrib Texture Strength", &L.midribTextureStrength, 0.0f, 0.95f);

                ImGui::Text("Sheath & Unfold");
                needUpdate |= ImGui::SliderFloat("Ligule Wrap Length", &L.liguleWrapLengthScale, 0.5f, 8.0f);
                needUpdate |= ImGui::SliderFloat("Ligule Unfold Sharpness", &L.liguleUnfoldSharpness, 0.2f, 4.0f);
                needUpdate |= ImGui::SliderFloat("Sheath Outer Scale", &L.sheathOuterScale, 1.0f, 1.6f);

                ImGui::Separator();
                bool useOverrides = L.useCtrlOverrides;
                if (ImGui::Checkbox("Edit Control Points", &useOverrides)) {
                    L.useCtrlOverrides = useOverrides;
                    if (!L.useCtrlOverrides) {
                        L.ctrlCenterOverride.clear();
                        L.ctrlLeftOverride.clear();
                        L.ctrlRightOverride.clear();
                    } else if (L.ctrlCenterOverride.empty()) {
                        CopyBladeCtrlToLeafOverrides(plant, gSelTiller, gSelLeaf, L);
                    }
                    needUpdate = true;
                }

                if (L.useCtrlOverrides) {
                    if (ImGui::Button("Copy Current Control Points")) {
                        if (CopyBladeCtrlToLeafOverrides(plant, gSelTiller, gSelLeaf, L)) {
                            needUpdate = true;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset Control Points")) {
                        L.useCtrlOverrides = false;
                        L.ctrlCenterOverride.clear();
                        L.ctrlLeftOverride.clear();
                        L.ctrlRightOverride.clear();
                        needUpdate = true;
                    }

                    auto editCtrl = [&](const char* label, std::vector<Vect3d>& ctrl) {
                        if (ImGui::TreeNode(label)) {
                            for (size_t i = 0; i < ctrl.size(); ++i) {
                                float v[3] = { ctrl[i].x(), ctrl[i].y(), ctrl[i].z() };
                                ImGui::PushID((int)i);
                                if (ImGui::DragFloat3("##pt", v, 0.01f)) {
                                    ctrl[i].Set(v[0], v[1], v[2]);
                                    needUpdate = true;
                                }
                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                    };

                    editCtrl("Center Control Points", L.ctrlCenterOverride);
                    editCtrl("Left Control Points", L.ctrlLeftOverride);
                    editCtrl("Right Control Points", L.ctrlRightOverride);
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Export")) {
        ImGui::InputText("XML Path", gXmlPath, sizeof(gXmlPath));
        if (ImGui::Button("Load XML")) {
            if (Descriptor::loadPlantXML(gXmlPath)) {
                gSkeletonOnlyView = false;
                Maize::rebuildGeometry();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save XML")) {
            Descriptor::savePlantXML(gXmlPath);
        }

        ImGui::InputText("Skeleton XML Path", gSkeletonXmlPath, sizeof(gSkeletonXmlPath));
        if (ImGui::Button("Load Skeleton XML")) {
            if (Descriptor::loadSkeletonXML(gSkeletonXmlPath)) {
                gSkeletonOnlyView = true;
                gShowSkeletonSpline = true;
                Maize::rebuildGeometry();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Skeleton XML")) {
            Descriptor::saveSkeletonXML(gSkeletonXmlPath);
        }

        ImGui::InputText("OBJ Path", gObjPath, sizeof(gObjPath));
        if (ImGui::Button("Save OBJ")) {
            Maize::savePlantOBJ(gObjPath, gTexPath, gStemTexPath);
        }
        ImGui::InputText("Skeleton OBJ Path", gSkeletonObjPath, sizeof(gSkeletonObjPath));
        if (ImGui::Button("Save Skeleton OBJ")) {
            Maize::saveSkeletonOBJ(gSkeletonObjPath);
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Visual Settings")) {
        ImGui::Checkbox("Lighting", &gLightingOn);
        ImGui::Checkbox("Show Normals", &gShowNormals);
        ImGui::Checkbox("Show Control Points", &gShowCtrlPts);
        ImGui::Checkbox("Show Skeleton Spline", &gShowSkeletonSpline);
        ImGui::Checkbox("Skeleton Only View", &gSkeletonOnlyView);
        ImGui::Checkbox("Wireframe", &gWireframe);

        ImGui::Separator();
        ImGui::Text("Stem Mesh");
        needUpdate |= ImGui::SliderFloat("Stem Density", &Maize::gStemDensityScale(), 0.1f, 1.0f, "%.2f");
        int stemRows = Maize::gStemRowOverride();
        if (ImGui::SliderInt("Stem Rows", &stemRows, 4, 400)) {
            Maize::gStemRowOverride() = stemRows;
            needUpdate = true;
        }
        needUpdate |= ImGui::SliderFloat("Stem Ribbon Spacing", &Maize::gStemRibbonSpacing(), 0.05f, 1.0f, "%.2f m");

        ImGui::Separator();
        ImGui::Text("Resolution");
        needUpdate |= ImGui::SliderFloat("Res Vertical", &Maize::gMeshDensityV(), 10.f, 200.f, "%.0f / m");
        needUpdate |= ImGui::SliderFloat("Res Horizontal", &Maize::gMeshDensityU(), 10.f, 200.f, "%.0f / m");
        ImGui::Text("Triangles: %d", Maize::getTriangleCount());
    }

    ImGui::End();

    if (needRegen) RebuildFromGenerator();
    else if (needUpdate) Maize::rebuildGeometry();

    ImGui::Render(); ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    glutSwapBuffers();
}

static void reshape(int w, int h) {
    gWinW = w > 0 ? w : 1; gWinH = h > 0 ? h : 1;
    ImGuiIO& io = ImGui::GetIO(); io.DisplaySize = ImVec2((float)gWinW, (float)gWinH);
}

static void idle() { glutPostRedisplay(); }

int main(int argc, char** argv) {
    srand((unsigned int)time(NULL));
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(gWinW, gWinH);
    glutCreateWindow("Maize Procedural Generator");
    ImGui::CreateContext();
    ImGui_ImplGLUT_Init(); ImGui_ImplGLUT_InstallFuncs(); ImGui_ImplOpenGL2_Init();

    loadTexture(gTexPath, &gTextureID, &gTextureOK);
    loadTexture(gStemTexPath, &gStemTextureID, &gStemTextureOK);
    RebuildFromGenerator();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutMainLoop();
    return 0;
}