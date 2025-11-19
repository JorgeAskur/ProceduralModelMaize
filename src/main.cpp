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

static int   gWinW = 1280, gWinH = 860;
static float gYaw = 30.f, gPitch = 18.f, gDist = 3.5f;
static Vect3d gCamTarget = { 0.0f, 1.0f, 0.0f };
static float gFovDeg = 60.0f;
static float gMoveSpeed = 1.0f;

static int gSelTiller = 0;
static int gSelLeaf = 0;

static bool gShowCtrlPts = true;
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

static char gXmlPath[256] = "plant_spline.xml";
static char gObjPath[256] = "plant_spline.obj";
static char gTexPath[256] = "maize_leaf.png";

static GLuint gTextureID = 0;
static bool   gTextureOK = false;

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }

static void loadTexture(const char* path) {
    if (gTextureID) { glDeleteTextures(1, &gTextureID); gTextureID = 0; }
    gTextureOK = false;
    int w, h, n;
    unsigned char* data = stbi_load(path, &w, &h, &n, 4);
    if (data) {
        glGenTextures(1, &gTextureID);
        glBindTexture(GL_TEXTURE_2D, gTextureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        #ifndef GL_CLAMP_TO_EDGE
        #define GL_CLAMP_TO_EDGE 0x812F
        #endif
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        stbi_image_free(data);
        gTextureOK = true;
    }
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

    // --- IMPORTANT FIX ---
    // Force Smooth Shading (Gouraud) to blend lighting across vertices.
    // This removes the "faceted" look.
    glShadeModel(GL_SMOOTH);
    // ---------------------

    glColor4f(1.f, 1.f, 1.f, 1.f);
    if (gTextureOK) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, gTextureID); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); }
    glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);

    for (const auto& G : Maize::leafGeoms()) {
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

        if (gShowNormals) {
            glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
            glColor3f(1.0f, 1.0f, 0.0f);
            glBegin(GL_LINES);
            for (size_t i = 0; i < G.pos.size(); ++i) {
                glVertex3f(G.pos[i].x(), G.pos[i].y(), G.pos[i].z());
                Vect3d np = G.pos[i] + G.nrm[i] * 0.02f;
                glVertex3f(np.x(), np.y(), np.z());
            }
            glEnd();
            if (gLightingOn) glEnable(GL_LIGHTING);
            if (gTextureOK) glEnable(GL_TEXTURE_2D);
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if (gTextureOK) glDisable(GL_TEXTURE_2D);
    if (gLightingOn) glDisable(GL_LIGHTING);
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
        // Only visualize control points for Blades
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
    drawCtrlViz();

    ImGui::Begin("Maize Spline Editor");
    bool needRebuild = false;

    ImGui::Separator(); ImGui::Text("File I/O");
    if (ImGui::Button("New Plant")) {
        Maize::plant() = PlantDesc{};
        TillerDesc t; t.type = "main";
        LeafDesc leaf1;
        leaf1.id = 0; leaf1.distance = 0.10f;
        leaf1.leafLength = 0.4f; leaf1.leafWidth = 0.1f;
        t.leaves.push_back(leaf1);
        Maize::plant().tillers.push_back(t);
        gSelTiller = 0; gSelLeaf = 0;
        needRebuild = true;
    }
    ImGui::SameLine();
    ImGui::InputText("XML", gXmlPath, sizeof(gXmlPath));
    if (ImGui::Button("Load XML")) { if (Descriptor::loadPlantXML(gXmlPath)) { needRebuild = true; gSelTiller = 0; gSelLeaf = 0; } }
    ImGui::SameLine();
    if (ImGui::Button("Save XML")) { Descriptor::savePlantXML(gXmlPath); }
    if (ImGui::Button("Export OBJ")) { Maize::savePlantOBJ(gObjPath, gTexPath); }

    ImGui::Separator(); ImGui::Text("Lighting & View");
    ImGui::Checkbox("Enable Lighting", &gLightingOn);
    ImGui::Checkbox("Show Normals", &gShowNormals);
    ImGui::Checkbox("Show Control Points", &gShowCtrlPts);
    ImGui::Checkbox("Wireframe", &gWireframe);

    ImGui::Separator(); ImGui::Text("Adaptive Mesh Resolution");
    needRebuild |= ImGui::SliderFloat("Res Vertical", &Maize::gMeshDensityV(), 10.f, 200.f, "%.0f / m");
    needRebuild |= ImGui::SliderFloat("Res Horizontal", &Maize::gMeshDensityU(), 10.f, 200.f, "%.0f / m");
    ImGui::Text("Total Triangles: %d", Maize::getTriangleCount());

    auto& plant = Maize::plant();
    if (!plant.tillers.empty()) {
        gSelTiller = std::min(gSelTiller, (int)plant.tillers.size() - 1);
        auto& t = plant.tillers[gSelTiller];

        ImGui::Separator(); ImGui::Text("Tiller");
        needRebuild |= ImGui::DragFloat("Radius", &t.radius, 0.001f);
        needRebuild |= ImGui::DragFloat("Azimuth Base", &t.azimuthDeg, 1.0f);

        if (ImGui::Button("Add Leaf")) {
            LeafDesc newLeaf = t.leaves.empty() ? LeafDesc() : t.leaves.back();
            newLeaf.id = t.leaves.empty() ? 0 : t.leaves.back().id + 1;
            t.leaves.push_back(newLeaf);
            gSelLeaf = (int)t.leaves.size() - 1;
            needRebuild = true;
        }

        if (!t.leaves.empty()) {
            gSelLeaf = std::min(gSelLeaf, (int)t.leaves.size() - 1);
            ImGui::SliderInt("Select Leaf", &gSelLeaf, 0, (int)t.leaves.size() - 1);
            auto& L = t.leaves[gSelLeaf];

            ImGui::Separator(); ImGui::Text("Leaf Parameters");
            needRebuild |= ImGui::DragFloat("Distance", &L.distance, 0.01f);
            needRebuild |= ImGui::DragFloat("Length", &L.leafLength, 0.01f);
            needRebuild |= ImGui::DragFloat("Width", &L.leafWidth, 0.005f);
            needRebuild |= ImGui::SliderFloat("Angle", &L.leafAngle, -90.f, 90.f);
            needRebuild |= ImGui::SliderFloat("Droopiness", &L.droopiness, -80.f, 80.f);
            needRebuild |= ImGui::SliderInt("Spline Pts", &L.splinePoints, 4, 40);
            needRebuild |= ImGui::SliderFloat("Taper", &L.widthTaper, 0.05f, 10.0f);
            needRebuild |= ImGui::SliderFloat("Twist", &L.leafTwist, -180.f, 180.f);
            needRebuild |= ImGui::SliderFloat("Sheath Angle", &L.sheathEndAngle, 1.f, 1.5f);
            needRebuild |= ImGui::SliderFloat("Sheath Open", &L.sheathOpeningStart, 0.9f, 1.f);

            ImGui::Separator(); ImGui::Text("Leaf Waves");
            needRebuild |= ImGui::SliderFloat("L Amp", &L.waveLAmp, 0.f, 0.05f);
            needRebuild |= ImGui::SliderFloat("L Freq", &L.waveLFreq, 0.f, 1.f);
            needRebuild |= ImGui::SliderFloat("R Amp", &L.waveRAmp, 0.f, 0.05f);
            needRebuild |= ImGui::SliderFloat("R Freq", &L.waveRFreq, 0.f, 1.f);
        }
    }
    ImGui::End();

    if (needRebuild) Maize::rebuildGeometry();

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
    glutCreateWindow("Maize Spline Editor");
    ImGui::CreateContext();
    ImGui_ImplGLUT_Init(); ImGui_ImplGLUT_InstallFuncs(); ImGui_ImplOpenGL2_Init();

    bool loaded = Descriptor::loadPlantXML(gXmlPath);
    if (!loaded || Maize::plant().tillers.empty()) {
        Maize::plant() = PlantDesc{};
        TillerDesc t; t.type = "main";
        LeafDesc leaf1;
        leaf1.id = 0; leaf1.distance = 0.10f;
        t.leaves.push_back(leaf1);
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