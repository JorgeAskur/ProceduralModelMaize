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

#define M_PI 3.14159265358979323846

/* ---------------- Window + camera ---------------- */
static int   gWinW = 1280, gWinH = 860;
static float gYaw = 30.f, gPitch = 18.f, gDist = 3.5f;
static Vect3d gCamTarget = { 0.0f, 0.0f, 0.0f };
static float gFovDeg = 60.0f;
static float gMoveSpeed = 1.0f;
static float gBoost = 4.0f;
static float gSlow = 0.25f;

/* ---------------- Selection & UI State ---------------- */
static int gSelTiller = 0;
static int gSelLeaf = 0;
static int gSelSpline = 0; // 0=Center, 1=Left, 2=Right, 3=Base

/* ---------------- Overlays & View Options ---------------- */
static bool gShowCtrlPts = true;
static bool gShowCtrlPoly = true;
static bool gWireframe = false;

/* ---------------- Lighting ---------------- */
static bool gLightingOn = true;
static float gLightPos[4] = { 2.0f, 3.0f, 4.0f, 1.0f };
static float gGlobalAmb[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
static float gLightDiff[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
static float gLightSpec[4] = { 0.6f, 0.6f, 0.6f, 1.0f };
static float gMatSpec[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
static float gShininess = 32.0f;


/* ---------------- Paths ---------------- */
static char gXmlPath[256] = "plant_spline.xml";
static char gObjPath[256] = "plant_spline.obj";
static char gTexPath[256] = "maize_leaf.png";

/* ---------------- Globals ---------------- */
static GLuint gTextureID = 0;
static bool   gTextureOK = false;

/* ---------------- Helpers ---------------- */
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }

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

/* ---------------- Lighting Setup ---------------- */
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


/* ---------------- Drawing ---------------- */
static void drawPlant() {
    setupLighting();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    if (gTextureOK) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, gTextureID);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }

    glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);

    for (const auto& S : Maize::stemLayers()) {
        if (S.band.empty()) continue;
        glColor3f(0.8f, 0.8f, 0.8f);
        glBegin(GL_TRIANGLE_STRIP);
        for (size_t i = 0; i < S.band.size(); ++i) {
            Vect3d n = Vect3d(S.band[i].x(), 0, S.band[i].z()).GetNormalized();
            glNormal3f(n.x(), n.y(), n.z());
            glTexCoord2f(S.uv[i].u, S.uv[i].v);
            glVertex3f(S.band[i].x(), S.band[i].y(), S.band[i].z());
        }
        glEnd();
    }

    for (const auto& G : Maize::leafGeoms()) {
        if (G.rows <= 1 || G.cols <= 1) continue;
        glColor3f(1.0f, 1.0f, 1.0f);
        for (int i = 0; i < G.rows - 1; ++i) {
            glBegin(GL_TRIANGLE_STRIP);
            for (int j = 0; j < G.cols; ++j) {
                int a = i * G.cols + j;
                int b = (i + 1) * G.cols + j;

                glNormal3f(G.nrm[a].x(), G.nrm[a].y(), G.nrm[a].z());
                glTexCoord2f(G.uv[a].u, G.uv[a].v);
                glVertex3f(G.pos[a].x(), G.pos[a].y(), G.pos[a].z());

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

static void drawCtrlViz() {
    if (!gShowCtrlPts && !gShowCtrlPoly) return;
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING);

    auto drawSpline = [&](const std::vector<Vect3d>& ctrl, float r, float g, float b) {
        if (ctrl.empty()) return;
        glColor3f(r, g, b);
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

    for (const auto& G : Maize::leafGeoms()) {
        drawSpline(G.ctrlCenter, 1.f, 0.5f, 0.f);
        drawSpline(G.ctrlLeft, 0.2f, 0.6f, 1.f);   // Left is Blue
        drawSpline(G.ctrlRight, 1.f, 0.f, 1.f);  // Right is Magenta
        drawSpline(G.ctrlBase_world, 0.f, 1.f, 0.f);
    }
    glEnable(GL_DEPTH_TEST);
}

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

static int leafGeomIndex(int tIdx, int lIdx) {
    const auto& plant = Maize::plant();
    if (tIdx < 0 || tIdx >= (int)plant.tillers.size()) return -1;
    int base = 0;
    for (int t = 0; t < tIdx; ++t) base += (int)plant.tillers[t].leaves.size();
    if (lIdx < 0 || lIdx >= (int)plant.tillers[tIdx].leaves.size()) return -1;
    return base + lIdx;
}

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
    gluPerspective(gFovDeg, io.DisplaySize.x / std::max(1.f, io.DisplaySize.y), 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

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

    ImGui::Begin("Maize Spline Editor");
    bool needRebuild = false;

    ImGui::SeparatorText("File I/O");
    if (ImGui::Button("New Plant")) {
        Maize::plant() = PlantDesc{};
        Maize::edits().clear();
        TillerDesc t; t.type = "main";
        t.leaves.push_back({ 0, 0.2f });
        Maize::plant().tillers.push_back(t);
        gSelTiller = 0;
        gSelLeaf = 0;
        needRebuild = true;
    }
    ImGui::SameLine();
    ImGui::InputText("XML Path", gXmlPath, sizeof(gXmlPath));
    if (ImGui::Button("Load XML")) { if (Descriptor::loadPlantXML(gXmlPath)) needRebuild = true; }
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

    needRebuild |= ImGui::SliderInt("Leaf Rows", &Maize::gLeafRows, 3, 200);
    needRebuild |= ImGui::SliderInt("Leaf Columns", &Maize::gLeafCols, 3, 51);
    needRebuild |= ImGui::SliderFloat("Base Shape Fade", &Maize::gBaseShapeFade, 0.0f, 0.5f, "%.2f");


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
            newLeaf.id = t.leaves.empty() ? 0 : t.leaves.back().id + 1;
            newLeaf.distance = 0.2f;
            newLeaf.age = 0.0f;
            t.leaves.push_back(newLeaf);
            gSelLeaf = (int)t.leaves.size() - 1;
            needRebuild = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Selected Leaf")) {
            if (!t.leaves.empty()) {
                Maize::edits().erase({ gSelTiller, gSelLeaf });
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
            needRebuild |= ImGui::SliderFloat("Age", &L.age, 0.f, 1.f, "%.2f");

            ImGui::SeparatorText("Spline Editor");
            const LeafKey key{ gSelTiller, gSelLeaf };
            auto& edits = Maize::edits();
            bool hasEdit = edits.count(key);

            if (ImGui::Button(hasEdit ? "Reset Splines" : "Make Editable")) {
                if (hasEdit) {
                    edits.erase(key);
                    hasEdit = false;
                }
                else {
                    const auto& geoms = Maize::leafGeoms();
                    int idx = leafGeomIndex(gSelTiller, gSelLeaf);
                    if (idx >= 0 && idx < (int)geoms.size()) {
                        const auto& geom = geoms[idx];
                        const Vect3d globalUp = { 0,1,0 };

                        float current_y = 0.f;
                        for (int i = 0; i < gSelLeaf; ++i) current_y += t.leaves[i].distance;
                        current_y += L.distance;

                        float rLeaf_here = std::max(0.001f, t.radius - t.stemShrink * gSelLeaf);
                        float leafAzimuth = t.azimuthDeg * (gSelLeaf)+0;
                        float phi = radiansf(leafAzimuth);
                        Vect3d rhat = { std::cos(phi), 0.f, std::sin(phi) };
                        Vect3d base = Vect3d{ 0.f, current_y, 0.f } + rhat * rLeaf_here;

                        LeafEdit tempE;
                        Maize::makeDefaultLeafSplines(0.7f, 0.08f, tempE.ctrlCenter, tempE.ctrlLeft, tempE.ctrlRight, tempE.ctrlBase);
                        Vect3d local_dir = (tempE.ctrlCenter[1] - tempE.ctrlCenter[0]).GetNormalized();

                        Vect3d z_axis = (rhat * local_dir.z() + globalUp * local_dir.y()).GetNormalized();
                        Vect3d x_axis = globalUp.Cross(z_axis).GetNormalized();
                        Vect3d y_axis = z_axis.Cross(x_axis).GetNormalized();

                        const float tt = t.alphaDeg * (float)M_PI / 180.f;
                        const float ct = std::cos(tt), st = std::sin(tt);
                        auto invTiltP = [&](const Vect3d& p)->Vect3d {
                            return { p.x() * ct + p.y() * st, -p.x() * st + p.y() * ct, p.z() };
                            };

                        auto worldToLocal = [&](const Vect3d& p_world) {
                            Vect3d rel_p = invTiltP(p_world) - base;
                            return Vect3d(rel_p.Dot(x_axis), rel_p.Dot(y_axis), rel_p.Dot(z_axis));
                            };

                        auto transformToLocal = [&](const std::vector<Vect3d>& geom_ctrl) {
                            std::vector<Vect3d> local_ctrl;
                            for (const auto& p_tilted : geom_ctrl) {
                                local_ctrl.push_back(worldToLocal(p_tilted));
                            }
                            return local_ctrl;
                            };

                        LeafEdit E; E.hasCustom = true;
                        E.ctrlCenter = transformToLocal(geom.ctrlCenter);
                        E.ctrlLeft = transformToLocal(geom.ctrlLeft);
                        E.ctrlRight = transformToLocal(geom.ctrlRight);

                        Maize::makeDefaultLeafSplines(0.7f, 0.08f, tempE.ctrlCenter, tempE.ctrlLeft, tempE.ctrlRight, E.ctrlBase);

                        edits[key] = E;
                        hasEdit = true;
                    }
                }
                needRebuild = true;
            }

            if (hasEdit) {
                ImGui::RadioButton("Center", &gSelSpline, 0); ImGui::SameLine();
                ImGui::RadioButton("Left", &gSelSpline, 1); ImGui::SameLine();
                ImGui::RadioButton("Right", &gSelSpline, 2); ImGui::SameLine();
                ImGui::RadioButton("Base Shape", &gSelSpline, 3);

                auto it = edits.find(key);
                if (it != edits.end()) {
                    auto& E = it->second;
                    std::vector<Vect3d>* activeSpline = nullptr;
                    // **FIX**: The UI's "Left" should edit the geometric Left, and "Right" the geometric Right.
                    if (gSelSpline == 0)      activeSpline = &E.ctrlCenter;
                    else if (gSelSpline == 1) activeSpline = &E.ctrlLeft;
                    else if (gSelSpline == 2) activeSpline = &E.ctrlRight;
                    else if (gSelSpline == 3) activeSpline = &E.ctrlBase;

                    int insert_after_idx = -1;
                    int remove_at_idx = -1;

                    for (size_t i = 0; i < activeSpline->size(); ++i) {
                        ImGui::PushID((int)i);

                        bool is_endpoint = (i == 0 || i == activeSpline->size() - 1);
                        bool is_base_spline_endpoint = (gSelSpline == 3 && is_endpoint);

                        if (is_base_spline_endpoint) ImGui::BeginDisabled();

                        float v[3] = { (*activeSpline)[i].x(), (*activeSpline)[i].y(), (*activeSpline)[i].z() };
                        if (ImGui::DragFloat3("P", v, 0.01f)) {
                            (*activeSpline)[i] = Vect3d(v[0], v[1], v[2]);
                            needRebuild = true;
                        }

                        if (is_base_spline_endpoint) ImGui::EndDisabled();

                        ImGui::SameLine(); if (ImGui::Button("+")) insert_after_idx = (int)i;
                        ImGui::SameLine(); if (ImGui::Button("-")) remove_at_idx = (int)i;

                        if (gSelSpline == 1 || gSelSpline == 2) {
                            ImGui::SameLine();
                            if (ImGui::Button("M")) {
                                // **FIX**: Mirror logic now correctly swaps between Left and Right.
                                std::vector<Vect3d>* dstSpline = (gSelSpline == 1) ? &E.ctrlRight : &E.ctrlLeft;
                                if (i < dstSpline->size()) {
                                    const Vect3d& srcPoint = (*activeSpline)[i];
                                    (*dstSpline)[i] = Vect3d(-srcPoint.x(), srcPoint.y(), srcPoint.z());
                                    needRebuild = true;
                                }
                            }
                        }

                        ImGui::PopID();
                    }

                    if (insert_after_idx != -1) {
                        int i = insert_after_idx;
                        if (i == activeSpline->size() - 1) activeSpline->push_back((*activeSpline)[i] + Vect3d(0, 0, 0.1));
                        else activeSpline->insert(activeSpline->begin() + i + 1, ((*activeSpline)[i] + (*activeSpline)[i + 1]) * 0.5f);
                        needRebuild = true;
                    }
                    if (remove_at_idx != -1) {
                        // **FIX**: Allow removing points from base curve down to 3 points
                        size_t min_points = (gSelSpline == 3) ? 3 : 4;
                        if (activeSpline->size() > min_points) {
                            activeSpline->erase(activeSpline->begin() + remove_at_idx);
                            needRebuild = true;
                        }
                    }
                }
            }
        }
    }
    ImGui::End();

    if (needRebuild) {
        Maize::rebuildGeometry();
    }

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    glutSwapBuffers();
}

static void reshape(int w, int h) {
    gWinW = w > 0 ? w : 1;
    gWinH = h > 0 ? h : 1;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)gWinW, (float)gWinH);
}

static void idle() { glutPostRedisplay(); }

int main(int argc, char** argv) {
    srand((unsigned int)time(NULL));

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

    if (Maize::plant().tillers.empty()) {
        TillerDesc t; t.type = "main";
        t.leaves.push_back({ 0, 0.2f });
        t.leaves.push_back({ 1, 0.2f });
        t.leaves.push_back({ 2, 0.2f });
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

