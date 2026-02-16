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

static bool gShowCtrlPts = false;
static bool gShowCtrlPoly = false;
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
static char gObjPath[256] = "plant_spline.obj";
static char gConfigPath[256] = "maize_generator_config.xml";
static char gBatchOutputDir[256] = "plant_export";
static char gBatchLeafTexPath[256] = "maize_leaf.png";
static char gBatchStemTexPath[256] = "maize_stem_texture.png";
static int gBatchCount = 10;

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
    drawPlant();
    drawNormals();
    drawCtrlViz();

    ImGui::Begin("Maize Generator");

    bool needRegen = false;
    bool needUpdate = false;

    ImGui::Text("Camera: Left Click=Rotate, WASD=Move, Scroll=Zoom");

    if (ImGui::CollapsingHeader("Global Traits", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Edit global traits and defaults for the plant.");

        auto DistUI = [&](const char* label, Dist& d, float v_min, float v_max, float sd_max) {
            ImGui::PushID(label);
            ImGui::Text("%s", label);
            bool c = false;
            c |= ImGui::SliderFloat("Mean", &d.mean, v_min, v_max);
            c |= ImGui::SliderFloat("Std Dev", &d.stdDev, 0.0f, sd_max);
            ImGui::PopID();
            return c;
            };

        auto CurveUI = [&](const char* label, Curve& c, float v_min, float v_max) {
            ImGui::PushID(label);
            ImGui::Text("%s", label);
            bool changed = false;
            changed |= ImGui::SliderFloat("Bottom", &c.bottom, v_min, v_max);
            changed |= ImGui::SliderFloat("Middle", &c.middle, v_min, v_max);
            changed |= ImGui::SliderFloat("Top", &c.top, v_min, v_max);
            ImGui::PopID();
            return changed;
            };

        needRegen |= DistUI("Height (m)", gGen.height, 0.5f, 4.0f, 1.0f);
        needRegen |= DistUI("Stem Radius", gGen.stemRadius, 0.01f, 0.1f, 0.01f);
        needRegen |= DistUI("Leaf Count", gGen.numLeaves, 3.0f, 20.0f, 5.0f);

        ImGui::Separator();
        ImGui::Text("Leaf Defaults");
        needRegen |= DistUI("Length", gGen.leafLengthBase, 0.2f, 1.5f, 0.5f);
        needRegen |= DistUI("Width", gGen.leafWidthBase, 0.02f, 0.2f, 0.05f);
        needRegen |= DistUI("Width Taper", gGen.widthTaper, 0.0f, 10.0f, 2.0f);
        needRegen |= DistUI("Angle", gGen.leafAngleBase, 0.f, 90.f, 20.f);
        needRegen |= DistUI("Droopiness", gGen.droopinessBase, -80.f, 80.f, 10.f);
        needRegen |= DistUI("Stem Inclination", gGen.stemInclination, -90.f, 90.f, 10.f);
        needRegen |= DistUI("Leaf Twist", gGen.leafTwist, -180.f, 180.f, 45.f);

        ImGui::Separator();
        ImGui::Text("Wave Defaults");
        needRegen |= DistUI("Wave Amp", gGen.waveAmp, 0.f, 0.05f, 0.02f);
        needRegen |= DistUI("Wave Freq", gGen.waveFreq, 0.f, 40.f, 10.f);
        needRegen |= DistUI("Wave Phase", gGen.wavePhaseShift, -6.283f, 6.283f, 3.1415f);

        ImGui::Separator();
        ImGui::Text("Leaf Jitter");
        needRegen |= ImGui::SliderFloat("Leaf Jitter", &gGen.leafJitterScale, 0.0f, 1.0f);

        ImGui::Separator();
        ImGui::Text("Leaf Curves (Bottom -> Top)");
        needRegen |= CurveUI("Length Curve", gGen.leafLengthCurve, 0.2f, 2.0f);
        needRegen |= CurveUI("Width Curve", gGen.leafWidthCurve, 0.2f, 2.0f);
        needRegen |= CurveUI("Width Taper Curve", gGen.widthTaperCurve, 0.2f, 2.0f);
        needRegen |= CurveUI("Angle Curve", gGen.leafAngleCurve, 0.2f, 2.0f);
        needRegen |= CurveUI("Droopiness Curve", gGen.droopinessCurve, 0.2f, 2.0f);
        needRegen |= CurveUI("Leaf Twist Curve", gGen.leafTwistCurve, 0.2f, 2.0f);

    }

    if (ImGui::CollapsingHeader("Configuration")) {
        ImGui::InputText("Config Path", gConfigPath, sizeof(gConfigPath));
        if (ImGui::Button("Load Config")) {
            if (gGen.loadConfig(gConfigPath)) {
                RebuildFromGenerator();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Config")) {
            gGen.saveConfig(gConfigPath);
        }
    }

    if (ImGui::CollapsingHeader("Batch Generation")) {
        ImGui::InputText("Output Dir", gBatchOutputDir, sizeof(gBatchOutputDir));
        ImGui::InputText("Leaf Texture", gBatchLeafTexPath, sizeof(gBatchLeafTexPath));
        ImGui::InputText("Stem Texture", gBatchStemTexPath, sizeof(gBatchStemTexPath));
        ImGui::SliderInt("Count", &gBatchCount, 1, 500);
        if (ImGui::Button("Generate Batch")) {
            gGen.generateBatch(gBatchCount, gBatchOutputDir, gBatchLeafTexPath, gBatchStemTexPath);
        }
    }

    if (ImGui::CollapsingHeader("Export")) {
        ImGui::InputText("XML Path", gXmlPath, sizeof(gXmlPath));
        if (ImGui::Button("Load XML")) {
            if (Descriptor::loadPlantXML(gXmlPath)) {
                Maize::rebuildGeometry();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save XML")) {
            Descriptor::savePlantXML(gXmlPath);
        }

        ImGui::InputText("OBJ Path", gObjPath, sizeof(gObjPath));
        if (ImGui::Button("Save OBJ")) {
            Maize::savePlantOBJ(gObjPath, gTexPath, gStemTexPath);
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Visual Settings")) {
        ImGui::Checkbox("Lighting", &gLightingOn);
        ImGui::Checkbox("Show Normals", &gShowNormals);
        ImGui::Checkbox("Show Control Points", &gShowCtrlPts);
        ImGui::Checkbox("Wireframe", &gWireframe);
        needUpdate |= ImGui::SliderFloat("Stem Density", &Maize::gStemDensityScale(), 0.1f, 1.0f, "%.2f");
        int stemRows = Maize::gStemRowOverride();
        if (ImGui::SliderInt("Stem Rows", &stemRows, 4, 400)) {
            Maize::gStemRowOverride() = stemRows;
            needUpdate = true;
        }
        needUpdate |= ImGui::SliderFloat("Stem Ribbon Spacing", &Maize::gStemRibbonSpacing(), 0.05f, 1.0f, "%.2f m");
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