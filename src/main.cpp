// Silence MSVC's deprecation warnings for the portable C functions
// (fopen, strncpy, etc.) used in the cross-platform fallback paths
#ifdef _MSC_VER
#  ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS
#  endif
#endif

#include <GL/freeglut.h>
#include <GL/glu.h>
#include <GL/glext.h>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <commdlg.h>
#endif
#include <vector>
#include <string>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <iostream>
#include <math/vect3d.h>

#include "imgui.h"
#include "imgui_impl_glut.h"
#include "imgui_impl_opengl2.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Maize.h"
#include "Descriptor.h"
#include "MaizeGenerator.h"
#include "tinyxml2.h"

using namespace std;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int   gWinW = 1280, gWinH = 860;
static float gYaw = 30.f, gPitch = 18.f, gDist = 3.5f;
static Vect3d gCamTarget = { 0.0f, 1.0f, 0.0f };
static float gFovDeg = 60.0f;
static float gMoveSpeed = 1.0f;

// Current editor selection
static int gSelTiller = 0;
static int gSelLeaf = 0;

// View toggles for debug visualization
static bool gShowCtrlPts = false;
static bool gShowCtrlPoly = false;
static bool gShowSkeletonSpline = false;
static bool gSkeletonOnlyView = false;
static bool gWireframe = false;
static bool gShowNormals = false;

static bool gLightingOn = true;
static float gLightPos[4] = { 2.0f, 8.0f, 4.0f, 1.0f };
static float gShininess = 32.0f;

// Paths and export destinations shown in the UI
static char gTexPath[256] = "maize_leaf.png";
static char gStemTexPath[256] = "maize_stem_texture.png";
static char gExportBasePath[256] = "plant_export/plant";
static char gGeneratorConfigPath[256] = "maize_generator_config.xml";
static int gBatchCount = 25;
static int gBatchStartIndex = 0;

// Background batch-generation state. The worker thread owns the global Maize
// model while gBatchRunning is true; the UI thread must not touch the model
// (render it, rebuild it, save it) during that window.
static std::thread gBatchThread;
static std::atomic<bool> gBatchRunning{ false };
static std::atomic<bool> gBatchCancelRequested{ false };
static std::atomic<int>  gBatchProgress{ 0 };
static int               gBatchTotalSnapshot = 0;

static char gXmlPath[256] = "plant_spline.xml";
static char gSkeletonXmlPath[256] = "plant_skeleton.xml";
static char gObjPath[256] = "plant_spline.obj";
static char gSkeletonObjPath[256] = "plant_skeleton.obj";

static GLuint gTextureID = 0;
static bool   gTextureOK = false;
static GLuint gStemTextureID = 0;
static bool   gStemTextureOK = false;

struct GpuVertex {
    float px, py, pz;
    float nx, ny, nz;
    float cr, cg, cb;
    float u, v;
};

struct GpuMesh {
    LeafGeom::GeomType type = LeafGeom::GEOM_BLADE;
    bool isTassel = false;
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLsizei indexCount = 0;
};

struct PlantGpuRenderer {
    bool initialized = false;
    bool available = false;
    bool meshDirty = true;
    GLuint program = 0;
    GLint locTexture = -1;
    GLint locUseTexture = -1;
    GLint locUseLighting = -1;
    GLint locLeafFactor = -1;
    GLint locLightPosEye = -1;
    GLint locSkyDirEye = -1;
    GLint locAmbientGround = -1;
    GLint locAmbientSky = -1;
    GLint locDiffuseStrength = -1;
    GLint locWrap = -1;
    GLint locSpecularStrength = -1;
    GLint locShininess = -1;
    GLint locTransmission = -1;
    GLint locExposure = -1;
    std::string error;
    std::vector<GpuMesh> meshes;
};

static PlantGpuRenderer gPlantRenderer;
static float gAmbientGroundStrength = 0.18f;
static float gAmbientSkyStrength = 0.60f;
static float gDiffuseStrength = 1.05f;
static float gDiffuseWrap = 0.28f;
static float gSpecularStrengthModern = 0.18f;
static float gTransmissionStrength = 0.45f;
static float gExposure = 1.10f;

static PFNGLACTIVETEXTUREPROC gglActiveTexture = nullptr;
static PFNGLGENBUFFERSPROC gglGenBuffers = nullptr;
static PFNGLBINDBUFFERPROC gglBindBuffer = nullptr;
static PFNGLBUFFERDATAPROC gglBufferData = nullptr;
static PFNGLDELETEBUFFERSPROC gglDeleteBuffers = nullptr;
static PFNGLCREATESHADERPROC gglCreateShader = nullptr;
static PFNGLSHADERSOURCEPROC gglShaderSource = nullptr;
static PFNGLCOMPILESHADERPROC gglCompileShader = nullptr;
static PFNGLGETSHADERIVPROC gglGetShaderiv = nullptr;
static PFNGLGETSHADERINFOLOGPROC gglGetShaderInfoLog = nullptr;
static PFNGLDELETESHADERPROC gglDeleteShader = nullptr;
static PFNGLCREATEPROGRAMPROC gglCreateProgram = nullptr;
static PFNGLATTACHSHADERPROC gglAttachShader = nullptr;
static PFNGLLINKPROGRAMPROC gglLinkProgram = nullptr;
static PFNGLGETPROGRAMIVPROC gglGetProgramiv = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC gglGetProgramInfoLog = nullptr;
static PFNGLDELETEPROGRAMPROC gglDeleteProgram = nullptr;
static PFNGLUSEPROGRAMPROC gglUseProgram = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC gglGetUniformLocation = nullptr;
static PFNGLUNIFORM1IPROC gglUniform1i = nullptr;
static PFNGLUNIFORM1FPROC gglUniform1f = nullptr;
static PFNGLUNIFORM3FPROC gglUniform3f = nullptr;

// The procedural generator
static MaizeGenerator gGen;

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float radiansf(float d) { return d * float(M_PI) / 180.f; }

static const char kAllFilesFilter[] = "All Files\0*.*\0\0";
static const char kXmlFilesFilter[] = "XML Files\0*.xml\0All Files\0*.*\0\0";
static const char kObjFilesFilter[] = "OBJ Files\0*.obj\0All Files\0*.*\0\0";
static const char kImageFilesFilter[] = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0All Files\0*.*\0\0";

static void CopyStringToBuffer(const std::string& value, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return;
    std::snprintf(buffer, bufferSize, "%s", value.c_str());
    buffer[bufferSize - 1] = '\0';
}

#ifdef _WIN32
static bool BrowseForFilePath(char* buffer, size_t bufferSize, const char* title, const char* filter, bool saveDialog, const char* defaultExt = nullptr) {
    char fileBuffer[4096] = {};
    CopyStringToBuffer(buffer, fileBuffer, sizeof(fileBuffer));

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = (DWORD)sizeof(fileBuffer);
    ofn.lpstrFilter = filter;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    if (saveDialog) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
    } else {
        ofn.Flags |= OFN_FILEMUSTEXIST;
    }

    BOOL ok = saveDialog ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if (!ok) return false;

    CopyStringToBuffer(fileBuffer, buffer, bufferSize);
    return true;
}
#else
// Cross-platform fallback that shells out to a native dialog helper:
//   - Linux:  zenity     (GNOME) — `apt install zenity`
//   - macOS:  osascript  (built-in)
// If neither is available the call returns false and the user can still edit
// the path text field directly.
static bool BrowseForFilePath(char* buffer, size_t bufferSize, const char* title, const char* /*filter*/, bool saveDialog, const char* /*defaultExt*/ = nullptr) {
    std::string cmd;
#  if defined(__APPLE__)
    // AppleScript "choose file" / "choose file name" returns a POSIX path via the trailing transform
    if (saveDialog) {
        cmd  = "osascript -e 'POSIX path of (choose file name with prompt \"";
        cmd += title ? title : "Save File";
        cmd += "\")' 2>/dev/null";
    } else {
        cmd  = "osascript -e 'POSIX path of (choose file with prompt \"";
        cmd += title ? title : "Open File";
        cmd += "\")' 2>/dev/null";
    }
#  else
    // Linux: prefer zenity (most common), fall back to kdialog if available
    cmd  = "command -v zenity >/dev/null 2>&1 && zenity --file-selection ";
    if (saveDialog) cmd += "--save --confirm-overwrite ";
    cmd += "--title=\"";
    cmd += title ? title : (saveDialog ? "Save File" : "Open File");
    cmd += "\" 2>/dev/null";
#  endif

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;

    char line[4096] = {};
    char* got = fgets(line, sizeof(line), pipe);
    int rc = pclose(pipe);
    if (!got || rc != 0) return false;

    // Strip trailing newline
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    if (n == 0) return false;

    CopyStringToBuffer(line, buffer, bufferSize);
    return true;
}
#endif

static bool PathInputWithBrowseButton(const char* label, char* buffer, size_t bufferSize, const char* buttonLabel, const char* dialogTitle, const char* filter, bool saveDialog, const char* defaultExt = nullptr) {
    ImGui::PushID(label);
    float buttonWidth = 96.0f;
    float inputWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x*3/4 - buttonWidth);
    ImGui::SetNextItemWidth(inputWidth);
    bool changed = ImGui::InputText(label, buffer, bufferSize);
    ImGui::SameLine();
    if (ImGui::Button(buttonLabel)) {
        changed |= BrowseForFilePath(buffer, bufferSize, dialogTitle, filter, saveDialog, defaultExt);
    }
    ImGui::PopID();
    return changed;
}

template <typename T>
static bool LoadGLProc(T& outProc, const char* name) {
    outProc = reinterpret_cast<T>(glutGetProcAddress(name));
    return outProc != nullptr;
}

static void DestroyGpuMeshes() {
    if (!gglDeleteBuffers) {
        gPlantRenderer.meshes.clear();
        return;
    }
    for (auto& mesh : gPlantRenderer.meshes) {
        if (mesh.vbo) gglDeleteBuffers(1, &mesh.vbo);
        if (mesh.ibo) gglDeleteBuffers(1, &mesh.ibo);
    }
    gPlantRenderer.meshes.clear();
}

static void InvalidatePlantRendererMeshes() {
    gPlantRenderer.meshDirty = true;
}

static Vect3d TransformPoint(const float m[16], const Vect3d& p) {
    return {
        m[0] * p.x() + m[4] * p.y() + m[8] * p.z() + m[12],
        m[1] * p.x() + m[5] * p.y() + m[9] * p.z() + m[13],
        m[2] * p.x() + m[6] * p.y() + m[10] * p.z() + m[14]
    };
}

static Vect3d TransformVector(const float m[16], const Vect3d& v) {
    return {
        m[0] * v.x() + m[4] * v.y() + m[8] * v.z(),
        m[1] * v.x() + m[5] * v.y() + m[9] * v.z(),
        m[2] * v.x() + m[6] * v.y() + m[10] * v.z()
    };
}

static bool LoadTextFile(const char* path, std::string& out, std::string& error) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { error = std::string("Cannot open: ") + path; return false; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    out.resize((size_t)sz);
    fread(&out[0], 1, (size_t)sz, f);
    fclose(f);
    return true;
}

static GLuint CompileShader(GLenum type, const char* source, std::string& error) {
    GLuint shader = gglCreateShader(type);
    if (!shader) {
        error = "glCreateShader failed.";
        return 0;
    }

    gglShaderSource(shader, 1, &source, nullptr);
    gglCompileShader(shader);

    GLint compiled = GL_FALSE;
    gglGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return shader;

    GLint logLength = 0;
    gglGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log((std::max)(1, logLength), '\0');
    gglGetShaderInfoLog(shader, (GLsizei)log.size(), nullptr, &log[0]);
    gglDeleteShader(shader);
    error = log;
    return 0;
}

static bool BuildModernPlantShader() {
    std::string vertSrc, fragSrc, error;
    if (!LoadTextFile("shaders/plant.vert", vertSrc, error)) {
        gPlantRenderer.error = "Vertex shader load failed: " + error;
        return false;
    }
    if (!LoadTextFile("shaders/plant.frag", fragSrc, error)) {
        gPlantRenderer.error = "Fragment shader load failed: " + error;
        return false;
    }

    GLuint vertShader = CompileShader(GL_VERTEX_SHADER, vertSrc.c_str(), error);
    if (!vertShader) {
        gPlantRenderer.error = "Vertex shader compile failed:\n" + error;
        return false;
    }

    GLuint fragShader = CompileShader(GL_FRAGMENT_SHADER, fragSrc.c_str(), error);
    if (!fragShader) {
        gglDeleteShader(vertShader);
        gPlantRenderer.error = "Fragment shader compile failed:\n" + error;
        return false;
    }

    GLuint program = gglCreateProgram();
    if (!program) {
        gglDeleteShader(vertShader);
        gglDeleteShader(fragShader);
        gPlantRenderer.error = "glCreateProgram failed.";
        return false;
    }

    gglAttachShader(program, vertShader);
    gglAttachShader(program, fragShader);
    gglLinkProgram(program);

    GLint linked = GL_FALSE;
    gglGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint logLength = 0;
        gglGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log((std::max)(1, logLength), '\0');
        gglGetProgramInfoLog(program, (GLsizei)log.size(), nullptr, &log[0]);
        gglDeleteProgram(program);
        gglDeleteShader(vertShader);
        gglDeleteShader(fragShader);
        gPlantRenderer.error = "Program link failed:\n" + log;
        return false;
    }

    gglDeleteShader(vertShader);
    gglDeleteShader(fragShader);

    gPlantRenderer.program = program;
    gPlantRenderer.locTexture = gglGetUniformLocation(program, "uTexture");
    gPlantRenderer.locUseTexture = gglGetUniformLocation(program, "uUseTexture");
    gPlantRenderer.locUseLighting = gglGetUniformLocation(program, "uUseLighting");
    gPlantRenderer.locLeafFactor = gglGetUniformLocation(program, "uLeafFactor");
    gPlantRenderer.locLightPosEye = gglGetUniformLocation(program, "uLightPosEye");
    gPlantRenderer.locSkyDirEye = gglGetUniformLocation(program, "uSkyDirEye");
    gPlantRenderer.locAmbientGround = gglGetUniformLocation(program, "uAmbientGround");
    gPlantRenderer.locAmbientSky = gglGetUniformLocation(program, "uAmbientSky");
    gPlantRenderer.locDiffuseStrength = gglGetUniformLocation(program, "uDiffuseStrength");
    gPlantRenderer.locWrap = gglGetUniformLocation(program, "uWrap");
    gPlantRenderer.locSpecularStrength = gglGetUniformLocation(program, "uSpecularStrength");
    gPlantRenderer.locShininess = gglGetUniformLocation(program, "uShininess");
    gPlantRenderer.locTransmission = gglGetUniformLocation(program, "uTransmission");
    gPlantRenderer.locExposure = gglGetUniformLocation(program, "uExposure");
    return true;
}

static bool EnsureModernPlantRendererReady() {
    if (gPlantRenderer.initialized) return gPlantRenderer.available;
    gPlantRenderer.initialized = true;

    bool ok = true;
    LoadGLProc(gglActiveTexture, "glActiveTexture");
    ok &= LoadGLProc(gglGenBuffers, "glGenBuffers");
    ok &= LoadGLProc(gglBindBuffer, "glBindBuffer");
    ok &= LoadGLProc(gglBufferData, "glBufferData");
    ok &= LoadGLProc(gglDeleteBuffers, "glDeleteBuffers");
    ok &= LoadGLProc(gglCreateShader, "glCreateShader");
    ok &= LoadGLProc(gglShaderSource, "glShaderSource");
    ok &= LoadGLProc(gglCompileShader, "glCompileShader");
    ok &= LoadGLProc(gglGetShaderiv, "glGetShaderiv");
    ok &= LoadGLProc(gglGetShaderInfoLog, "glGetShaderInfoLog");
    ok &= LoadGLProc(gglDeleteShader, "glDeleteShader");
    ok &= LoadGLProc(gglCreateProgram, "glCreateProgram");
    ok &= LoadGLProc(gglAttachShader, "glAttachShader");
    ok &= LoadGLProc(gglLinkProgram, "glLinkProgram");
    ok &= LoadGLProc(gglGetProgramiv, "glGetProgramiv");
    ok &= LoadGLProc(gglGetProgramInfoLog, "glGetProgramInfoLog");
    ok &= LoadGLProc(gglDeleteProgram, "glDeleteProgram");
    ok &= LoadGLProc(gglUseProgram, "glUseProgram");
    ok &= LoadGLProc(gglGetUniformLocation, "glGetUniformLocation");
    ok &= LoadGLProc(gglUniform1i, "glUniform1i");
    ok &= LoadGLProc(gglUniform1f, "glUniform1f");
    ok &= LoadGLProc(gglUniform3f, "glUniform3f");
    if (!ok) {
        gPlantRenderer.error = "Required OpenGL shader/buffer entry points are unavailable, so the plant renderer cannot start on this machine.";
        gPlantRenderer.available = false;
        return false;
    }

    gPlantRenderer.available = BuildModernPlantShader();
    return gPlantRenderer.available;
}

static bool UploadPlantRendererMeshes() {
    if (!EnsureModernPlantRendererReady()) return false;
    if (!gPlantRenderer.meshDirty) return true;

    DestroyGpuMeshes();

    for (const auto& geom : Maize::leafGeoms()) {
        if (geom.rows <= 1 || geom.cols <= 1 || geom.pos.size() != geom.nrm.size() || geom.pos.size() != geom.col.size() || geom.pos.size() != geom.uv.size()) {
            continue;
        }

        std::vector<GpuVertex> vertices(geom.pos.size());
        for (size_t i = 0; i < geom.pos.size(); ++i) {
            vertices[i] = {
                (float)geom.pos[i].x(), (float)geom.pos[i].y(), (float)geom.pos[i].z(),
                (float)geom.nrm[i].x(), (float)geom.nrm[i].y(), (float)geom.nrm[i].z(),
                (float)geom.col[i].x(), (float)geom.col[i].y(), (float)geom.col[i].z(),
                geom.uv[i].u, geom.uv[i].v
            };
        }

        std::vector<unsigned int> indices;
        indices.reserve((size_t)(geom.rows - 1) * (size_t)(geom.cols - 1) * 6);
        for (int row = 0; row < geom.rows - 1; ++row) {
            for (int col = 0; col < geom.cols - 1; ++col) {
                unsigned int a = (unsigned int)(row * geom.cols + col);
                unsigned int b = a + 1;
                unsigned int c = a + (unsigned int)geom.cols;
                unsigned int d = c + 1;
                indices.push_back(a); indices.push_back(c); indices.push_back(b);
                indices.push_back(b); indices.push_back(c); indices.push_back(d);
            }
        }

        if (indices.empty()) continue;

        GpuMesh mesh;
        mesh.type = geom.type;
        mesh.indexCount = (GLsizei)indices.size();
        gglGenBuffers(1, &mesh.vbo);
        gglBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        gglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertices.size() * sizeof(GpuVertex)), vertices.data(), GL_STATIC_DRAW);

        gglGenBuffers(1, &mesh.ibo);
        gglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
        gglBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);

        gPlantRenderer.meshes.push_back(mesh);
    }

    // Tassel: a single indexed triangle mesh produced by the L-system port.
    {
        const auto& tassel = Maize::tasselMesh();
        if (tassel.pos.size() == tassel.nrm.size() && tassel.pos.size() == tassel.u.size() &&
            tassel.pos.size() == tassel.v.size() && !tassel.indices.empty()) {
            const float tcr = 0.86f, tcg = 0.80f, tcb = 0.55f;  // pale straw
            std::vector<GpuVertex> vertices(tassel.pos.size());
            for (size_t i = 0; i < tassel.pos.size(); ++i) {
                vertices[i] = {
                    (float)tassel.pos[i].x(), (float)tassel.pos[i].y(), (float)tassel.pos[i].z(),
                    (float)tassel.nrm[i].x(), (float)tassel.nrm[i].y(), (float)tassel.nrm[i].z(),
                    tcr, tcg, tcb,
                    tassel.u[i], tassel.v[i]
                };
            }

            GpuMesh mesh;
            mesh.isTassel = true;
            mesh.type = LeafGeom::GEOM_SHEATH;  // opaque/solid shading path
            mesh.indexCount = (GLsizei)tassel.indices.size();
            gglGenBuffers(1, &mesh.vbo);
            gglBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
            gglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertices.size() * sizeof(GpuVertex)), vertices.data(), GL_STATIC_DRAW);
            gglGenBuffers(1, &mesh.ibo);
            gglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
            gglBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(tassel.indices.size() * sizeof(unsigned int)), tassel.indices.data(), GL_STATIC_DRAW);
            gPlantRenderer.meshes.push_back(mesh);
        }
    }

    gglBindBuffer(GL_ARRAY_BUFFER, 0);
    gglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    gPlantRenderer.meshDirty = false;
    return true;
}

static void loadTexture(const char* path, GLuint* textureId, bool* textureOk) {
    // The editor uses immediate texture reloads so the viewport always reflects the current export texture paths
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
    // Draw skeleton using stemCurve for both stem (smooth B-spline)
    // and leaves (stem exit point + blade center, sheath skipped)
    if (!gShowSkeletonSpline) return;
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    glColor3f(0.0f, 0.0f, 0.0f);
    for (const auto& G : Maize::leafGeoms()) {
        if (G.stemCurve.size() < 2) continue;
        glBegin(GL_LINE_STRIP);
        for (const auto& p : G.stemCurve) {
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
    }

    // Tassel skeleton: rachis + branch centerlines (segment pairs).
    const auto& tsk = Maize::tasselMesh().skeleton;
    if (tsk.size() >= 2) {
        glBegin(GL_LINES);
        for (size_t i = 0; i + 1 < tsk.size(); i += 2) {
            glVertex3f(tsk[i].x(), tsk[i].y(), tsk[i].z());
            glVertex3f(tsk[i + 1].x(), tsk[i + 1].y(), tsk[i + 1].z());
        }
        glEnd();
    }
    glEnable(GL_DEPTH_TEST);
}

static const LeafGeom* GetStemGeom() {
    // The stem is represented as the first sheath geometry that also retains control points for the stem centerline
    for (const auto& G : Maize::leafGeoms()) {
        if (G.type == LeafGeom::GEOM_SHEATH && !G.ctrlCenter.empty()) return &G;
    }
    return nullptr;
}

static bool drawPlant() {
    if (!UploadPlantRendererMeshes()) return false;

    float modelView[16] = {};
    glGetFloatv(GL_MODELVIEW_MATRIX, modelView);

    Vect3d lightEye = (std::fabs(gLightPos[3]) > 0.5f)
        ? TransformPoint(modelView, { gLightPos[0], gLightPos[1], gLightPos[2] })
        : TransformVector(modelView, { gLightPos[0], gLightPos[1], gLightPos[2] }).GetNormalized();
    Vect3d skyDirEye = TransformVector(modelView, { 0.0f, 1.0f, 0.0f }).GetNormalized();
    Vect3d ambientGround = Vect3d(0.28f, 0.26f, 0.23f) * gAmbientGroundStrength;
    Vect3d ambientSky = Vect3d(0.72f, 0.82f, 0.98f) * gAmbientSkyStrength;

    gglUseProgram(gPlantRenderer.program);
    gglUniform1i(gPlantRenderer.locTexture, 0);
    gglUniform1f(gPlantRenderer.locUseLighting, gLightingOn ? 1.0f : 0.0f);
    gglUniform3f(gPlantRenderer.locLightPosEye, (float)lightEye.x(), (float)lightEye.y(), (float)lightEye.z());
    gglUniform3f(gPlantRenderer.locSkyDirEye, (float)skyDirEye.x(), (float)skyDirEye.y(), (float)skyDirEye.z());
    gglUniform3f(gPlantRenderer.locAmbientGround, (float)ambientGround.x(), (float)ambientGround.y(), (float)ambientGround.z());
    gglUniform3f(gPlantRenderer.locAmbientSky, (float)ambientSky.x(), (float)ambientSky.y(), (float)ambientSky.z());
    gglUniform1f(gPlantRenderer.locDiffuseStrength, gDiffuseStrength);
    gglUniform1f(gPlantRenderer.locWrap, gDiffuseWrap);
    gglUniform1f(gPlantRenderer.locSpecularStrength, gSpecularStrengthModern);
    gglUniform1f(gPlantRenderer.locShininess, gShininess);
    gglUniform1f(gPlantRenderer.locTransmission, gTransmissionStrength);
    gglUniform1f(gPlantRenderer.locExposure, gExposure);

    if (gglActiveTexture) gglActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    glEnable(GL_NORMALIZE);
    glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);
    glEnable(GL_TEXTURE_2D);

    for (const auto& mesh : gPlantRenderer.meshes) {
        bool useTexture = (mesh.type == LeafGeom::GEOM_SHEATH) ? gStemTextureOK : gTextureOK;
        GLuint texId = (mesh.type == LeafGeom::GEOM_SHEATH) ? gStemTextureID : gTextureID;
        if (mesh.isTassel) { useTexture = false; }  // tassel uses its pale vertex color
        glBindTexture(GL_TEXTURE_2D, useTexture ? texId : 0);
        gglUniform1f(gPlantRenderer.locUseTexture, useTexture ? 1.0f : 0.0f);
        gglUniform1f(gPlantRenderer.locLeafFactor, mesh.type == LeafGeom::GEOM_BLADE ? 1.0f : 0.0f);

        gglBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        gglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
        glVertexPointer(3, GL_FLOAT, sizeof(GpuVertex), reinterpret_cast<const void*>(offsetof(GpuVertex, px)));
        glNormalPointer(GL_FLOAT, sizeof(GpuVertex), reinterpret_cast<const void*>(offsetof(GpuVertex, nx)));
        glColorPointer(3, GL_FLOAT, sizeof(GpuVertex), reinterpret_cast<const void*>(offsetof(GpuVertex, cr)));
        glTexCoordPointer(2, GL_FLOAT, sizeof(GpuVertex), reinterpret_cast<const void*>(offsetof(GpuVertex, u)));
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    gglBindBuffer(GL_ARRAY_BUFFER, 0);
    gglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    gglUseProgram(0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    return true;
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

static void RebuildGeometryAndRenderer() {
    // Never mutate the global Maize model while the batch worker owns it
    if (gBatchRunning.load(std::memory_order_acquire)) return;
    Maize::rebuildGeometry();
    InvalidatePlantRendererMeshes();
}

static void RebuildFromGenerator(bool useMeansOnly = true, unsigned int seed = 1337) {
    // Regenerates the editable PlantDesc from the procedural generator and then rebuilds meshes from that fresh description
    if (gBatchRunning.load(std::memory_order_acquire)) return;
    gSkeletonOnlyView = false;
    Maize::plant() = gGen.generate(seed, useMeansOnly);
    RebuildGeometryAndRenderer();
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
    // The render geometry list stores one blade mesh per leaf in tiller order
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
    // Copies the currently displayed world-space blade splines back into the
    // leaf's local override space so the user can edit the exact current shape
    if (tillerIndex < 0 || tillerIndex >= (int)plant.tillers.size()) return false;
    const auto* bladeGeom = GetBladeGeomForLeaf(plant, tillerIndex, leafIndex);
    if (!bladeGeom) return false;

    const auto& td = plant.tillers[tillerIndex];
    float current_y = 0.f;
    for (int i = 0; i <= leafIndex && i < (int)td.leaves.size(); ++i) {
        current_y += std::max(0.f, td.leaves[i].distance);
    }
    Vect3d sheath_exit_pos = { 0, current_y, 0 };

    float phi = radiansf(leaf.azimuthDeg);
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

static bool DistUI(const char* label, Dist& d, float v_min, float v_max) {
    // Standardized UI for all mean/std-dev controls so they stay consistent
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    bool changed = false;
    float stdMax = (std::max)(0.001f, (v_max - v_min) * 0.5f);
    changed |= ImGui::SliderFloat("Mean", &d.mean, v_min, v_max);
    changed |= ImGui::SliderFloat("Std Dev", &d.stdDev, 0.0f, stdMax);
    ImGui::PopID();
    return changed;
}

static bool Curve3UI(const char* label, Curve3& c, float v_min, float v_max) {
    // Standardized UI for bottom/middle/top canopy profile curves
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    bool changed = false;
    changed |= ImGui::SliderFloat("Bottom", &c.bottom, v_min, v_max);
    changed |= ImGui::SliderFloat("Middle", &c.middle, v_min, v_max);
    changed |= ImGui::SliderFloat("Top", &c.top, v_min, v_max);
    ImGui::PopID();
    return changed;
}

static void RenderCorrelationMatrixEditor(bool& needRegen) {
    // The matrix editor exposes the latent trait model directly
    needRegen |= ImGui::SliderFloat("Trait Correlation", &gGen.traitCorrelationStrength, 0.0f, 1.0f);
    ImGui::TextWrapped("Sampled plants draw four independent latent traits per plant: vigor, slenderness, posture, and texture, each from N(0, 1). Mean preview fixes all four latents at 0.");
    ImGui::TextWrapped("Edit how each distribution-backed parameter responds to those latent traits. The per-parameter row is multiplied by Trait Correlation before sampling.");
    ImGui::TextWrapped("If a row's scaled magnitude exceeds 1, the sampler normalizes it internally so the resulting variance stays stable.");

    if (ImGui::Button("Reset Trait Matrix")) {
        gGen.resetTraitLoadings();
        needRegen = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Zero Trait Matrix")) {
        for (int param = 0; param < MaizeGenerator::CorrelatedParameter_Count; ++param) {
            auto& loadings = gGen.getTraitLoadings(param);
            loadings.vigor = 0.0f;
            loadings.slenderness = 0.0f;
            loadings.posture = 0.0f;
            loadings.texture = 0.0f;
        }
        needRegen = true;
    }

    if (ImGui::TreeNode("Correlation Matrix")) {
        if (ImGui::BeginTable("TraitLoadingsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Parameter");
            ImGui::TableSetupColumn("Vigor");
            ImGui::TableSetupColumn("Slender");
            ImGui::TableSetupColumn("Posture");
            ImGui::TableSetupColumn("Texture");
            ImGui::TableHeadersRow();

            for (int param = 0; param < MaizeGenerator::CorrelatedParameter_Count; ++param) {
                auto& loadings = gGen.getTraitLoadings(param);
                ImGui::TableNextRow();
                ImGui::PushID(param);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(MaizeGenerator::getCorrelatedParameterName(param));
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(72.0f);
                needRegen |= ImGui::DragFloat("##vigor", &loadings.vigor, 0.01f, -1.0f, 1.0f, "%.2f");
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(72.0f);
                needRegen |= ImGui::DragFloat("##slender", &loadings.slenderness, 0.01f, -1.0f, 1.0f, "%.2f");
                ImGui::TableSetColumnIndex(3);
                ImGui::SetNextItemWidth(72.0f);
                needRegen |= ImGui::DragFloat("##posture", &loadings.posture, 0.01f, -1.0f, 1.0f, "%.2f");
                ImGui::TableSetColumnIndex(4);
                ImGui::SetNextItemWidth(72.0f);
                needRegen |= ImGui::DragFloat("##texture", &loadings.texture, 0.01f, -1.0f, 1.0f, "%.2f");
                ImGui::PopID();
            }

            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
}

static void RenderGlobalTraitsPanel(bool& needRegen) {
    // This panel edits the procedural generator
    if (!ImGui::CollapsingHeader("Global Traits", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextWrapped("Edit global traits and defaults for the plant. Viewport preview uses parameter means; use Set Generation to sample full distributions.");

    if (ImGui::TreeNodeEx("Stem", ImGuiTreeNodeFlags_DefaultOpen)) {
        needRegen |= DistUI("Height (m)", gGen.height, 0.1f, 4.0f);
        needRegen |= DistUI("Stem Radius", gGen.stemRadius, 0.01f, 0.1f);
        needRegen |= DistUI("Leaf Count", gGen.numLeaves, 3.0f, 20.0f);
        needRegen |= ImGui::SliderFloat("Tiller Azimuth Base", &gGen.tillerAzimuthDeg, -360.0f, 360.0f);
        needRegen |= ImGui::SliderFloat("Tiller Azimuth Noise", &gGen.tillerAzimuthNoise, 0.0f, 90.0f);
        needRegen |= ImGui::SliderFloat("Stem Shrink Factor", &gGen.stemShrinkFactor, 0.0f, 2.0f);
        needRegen |= ImGui::SliderFloat("Stem Shrink Min", &gGen.stemShrinkMin, 0.0f, 0.02f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Leaf Distributions", ImGuiTreeNodeFlags_DefaultOpen)) {
        needRegen |= DistUI("Length", gGen.leafLengthBase, 0.2f, 1.5f);
        needRegen |= DistUI("Width", gGen.leafWidthBase, 0.02f, 0.2f);
        needRegen |= DistUI("Width Taper", gGen.widthTaper, 0.0f, 10.0f);
        needRegen |= DistUI("Angle", gGen.leafAngleBase, 0.f, 90.f);
        needRegen |= DistUI("Droopiness", gGen.droopinessBase, -150.f, 150.f);
        needRegen |= DistUI("Stem Inclination", gGen.stemInclination, -90.f, 90.f);
        needRegen |= DistUI("Leaf Twist", gGen.leafTwist, -180.f, 180.f);
        needRegen |= DistUI("Leaf Curl", gGen.leafCurl, -1.0f, 1.0f);
        needRegen |= ImGui::SliderInt("Spline Points", &gGen.splinePointsDefault, 4, 80);
        needRegen |= ImGui::SliderFloat("Leaf Jitter", &gGen.leafJitterScale, 0.0f, 1.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Waves", ImGuiTreeNodeFlags_DefaultOpen)) {
        needRegen |= DistUI("Wave Amp", gGen.waveAmp, 0.f, 0.05f);
        needRegen |= DistUI("Wave Freq", gGen.waveFreq, 0.f, 40.f);
        needRegen |= DistUI("Wave Phase", gGen.wavePhaseShift, -6.283f, 6.283f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Architecture", ImGuiTreeNodeFlags_DefaultOpen)) {
        needRegen |= ImGui::SliderFloat("Whorl Start", &gGen.whorlStart, 0.0f, 0.95f);
        needRegen |= ImGui::SliderFloat("Whorl Compression", &gGen.whorlCompression, 0.0f, 1.5f);
        needRegen |= ImGui::SliderFloat("Whorl Min Internode", &gGen.whorlMinInternodeWeight, 0.01f, 1.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Visual Defaults", ImGuiTreeNodeFlags_DefaultOpen)) {
        needRegen |= ImGui::SliderFloat("Surface Noise Amp", &gGen.leafSurfaceNoiseAmp, 0.0f, 0.05f);
        needRegen |= ImGui::SliderFloat("Surface Noise Freq", &gGen.leafSurfaceNoiseFreq, 0.1f, 24.0f);
        needRegen |= ImGui::SliderFloat("Midrib Tip Taper", &gGen.midribTipTaperStart, 0.2f, 0.98f);
        needRegen |= ImGui::SliderFloat("Midrib Texture Strength", &gGen.midribTextureStrength, 0.0f, 0.95f);
        needRegen |= ImGui::SliderFloat("Midrib Width", &gGen.midribWidth, 0.01f, 0.25f, "%.3f");
        needRegen |= ImGui::SliderFloat("Ligule Wrap Length", &gGen.liguleWrapLengthScale, 0.5f, 8.0f);
        needRegen |= ImGui::SliderFloat("Ligule Unfold Sharpness", &gGen.liguleUnfoldSharpness, 0.2f, 4.0f);
        needRegen |= ImGui::SliderFloat("Sheath Outer Scale", &gGen.sheathOuterScale, 1.0f, 1.6f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Canopy Curves (Bottom / Middle / Top)")) {
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
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Correlation")) {
        RenderCorrelationMatrixEditor(needRegen);
        ImGui::TreePop();
    }
}

static bool gExportSkeleton = false;
// Shared by the manual "Save Skeleton OBJ" and the batch "Export Skeleton":
// when false, the tassel is omitted from skeleton exports (the full mesh always
// keeps the tassel).
static bool gSkeletonIncludeTassel = true;

static void JoinBatchThreadIfFinished() {
    if (!gBatchRunning.load(std::memory_order_acquire) && gBatchThread.joinable()) {
        gBatchThread.join();
    }
}

static void RenderSetGenerationPanel() {
    // Batch export samples a fresh plant per output file using the current
    // generator distributions and latent trait settings
    if (!ImGui::CollapsingHeader("Set Generation", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextWrapped("Generate multiple plants by sampling the configured mean/std dev distributions for each generator parameter.");
    PathInputWithBrowseButton("Set Base Path / Prefix", gExportBasePath, sizeof(gExportBasePath), "Browse...", "Choose plant set base path / prefix", kAllFilesFilter, true);
    bool texturePathChanged = false;
    texturePathChanged |= PathInputWithBrowseButton("Leaf Texture Path", gTexPath, sizeof(gTexPath), "Browse...", "Choose leaf texture", kImageFilesFilter, false);
    texturePathChanged |= PathInputWithBrowseButton("Stem Texture Path", gStemTexPath, sizeof(gStemTexPath), "Browse...", "Choose stem texture", kImageFilesFilter, false);
    ImGui::InputInt("Set Count", &gBatchCount);
    ImGui::InputInt("Start Index", &gBatchStartIndex);
    if (gBatchStartIndex < 0) gBatchStartIndex = 0;
    ImGui::Checkbox("Export Skeleton", &gExportSkeleton);
    if (gExportSkeleton) {
        ImGui::Checkbox("Include Tassel in Skeleton", &gSkeletonIncludeTassel);
    }

    // Global identity parameters applied to every plant in this set
    {
        char speciesBuf[128];
        memset(speciesBuf, 0, sizeof(speciesBuf));
        memcpy(speciesBuf, gGen.speciesName.c_str(), (std::min)(gGen.speciesName.size(), sizeof(speciesBuf) - 1));
        if (ImGui::InputText("Species Name", speciesBuf, sizeof(speciesBuf))) {
            gGen.speciesName = speciesBuf;
        }

        char phenoBuf[128];
        memset(phenoBuf, 0, sizeof(phenoBuf));
        memcpy(phenoBuf, gGen.phenotypeId.c_str(), (std::min)(gGen.phenotypeId.size(), sizeof(phenoBuf) - 1));
        if (ImGui::InputText("Global Phenotype ID", phenoBuf, sizeof(phenoBuf))) {
            gGen.phenotypeId = phenoBuf;
        }
    }

    if (texturePathChanged || ImGui::Button("Reload Textures")) {
        loadTexture(gTexPath, &gTextureID, &gTextureOK);
        loadTexture(gStemTexPath, &gStemTextureID, &gStemTextureOK);
    }

    if (ImGui::Button("Preview Mean Plant")) {
        RebuildFromGenerator(true, 1337);
    }
    ImGui::SameLine();
    if (ImGui::Button("Preview Random Sample")) {
        RebuildFromGenerator(false, (unsigned int)rand());
    }

    JoinBatchThreadIfFinished();

    if (gBatchRunning.load(std::memory_order_acquire)) {
        int done = gBatchProgress.load(std::memory_order_relaxed);
        ImGui::Text("Generating: %d / %d", done, gBatchTotalSnapshot);
        float frac = gBatchTotalSnapshot > 0 ? (float)done / (float)gBatchTotalSnapshot : 0.0f;
        ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f));
        if (ImGui::Button("Cancel")) {
            gBatchCancelRequested.store(true, std::memory_order_relaxed);
        }
        ImGui::SameLine();
        ImGui::TextWrapped("Viewport preview is paused while the batch runs.");
    }
    else {
        if (ImGui::Button("Generate Plant Set")) {
            // Snapshot UI state and generator settings so the worker sees a
            // stable view even if the user tweaks things mid-run
            int count = gBatchCount;
            int startIndex = gBatchStartIndex;
            std::string basePath = gExportBasePath;
            std::string leafTex = gTexPath;
            std::string stemTex = gStemTexPath;
            bool exportSkel = gExportSkeleton;
            bool skelIncludeTassel = gSkeletonIncludeTassel;
            MaizeGenerator genCopy = gGen;

            gBatchTotalSnapshot = count;
            gBatchProgress.store(0, std::memory_order_relaxed);
            gBatchCancelRequested.store(false, std::memory_order_relaxed);
            gBatchRunning.store(true, std::memory_order_release);

            gBatchThread = std::thread([genCopy, count, startIndex, basePath, leafTex, stemTex, exportSkel, skelIncludeTassel]() mutable {
                genCopy.generateBatch(count, basePath, leafTex, stemTex, false, exportSkel, &gBatchProgress, startIndex, &gBatchCancelRequested, skelIncludeTassel);
                gBatchRunning.store(false, std::memory_order_release);
            });
        }
    }
}

static void RenderGeneratorConfigPanel() {
    // Saves/loads the procedural generator settings only, not the full plant
    if (!ImGui::CollapsingHeader("Generator Config")) return;

    ImGui::TextWrapped("Save or load the current generator parameter settings, including distributions and canopy shaping controls.");
    PathInputWithBrowseButton("Config Path", gGeneratorConfigPath, sizeof(gGeneratorConfigPath), "Browse...", "Choose generator config", kXmlFilesFilter, false, "xml");
    static std::string sConfigStatus;
    static ImVec4 sConfigStatusColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    if (ImGui::Button("Load Generator Config")) {
        sConfigStatus.clear();
        std::string errMsg;
        if (gGen.loadConfig(gGeneratorConfigPath, &errMsg)) {
            RebuildFromGenerator(true, 1337);
            sConfigStatus = errMsg.empty() ? "Config loaded." : errMsg;
            sConfigStatusColor = errMsg.empty()
                ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f)
                : ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
        } else {
            sConfigStatus = errMsg.empty() ? "Failed to load config." : errMsg;
            sConfigStatusColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) {
        BrowseForFilePath(gGeneratorConfigPath, sizeof(gGeneratorConfigPath), "Save generator config", kXmlFilesFilter, true, "xml");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Generator Config")) {
        if (gGen.saveConfig(gGeneratorConfigPath)) {
            sConfigStatus = "Config saved.";
            sConfigStatusColor = ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
        } else {
            sConfigStatus = "Failed to save config.";
            sConfigStatusColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        }
    }
    if (!sConfigStatus.empty()) {
        ImGui::TextColored(sConfigStatusColor, "%s", sConfigStatus.c_str());
    }
}

static void EditControlPointList(const char* label, const char* idPrefix, std::vector<Vect3d>& ctrl, bool& needUpdate) {
    // Generic editor for explicit spline control points
    if (!ImGui::TreeNode(label)) return;

    for (size_t i = 0; i < ctrl.size(); ++i) {
        float v[3] = { ctrl[i].x(), ctrl[i].y(), ctrl[i].z() };
        ImGui::PushID(idPrefix);
        ImGui::PushID((int)i);
        if (ImGui::DragFloat3("##pt", v, 0.01f)) {
            ctrl[i].Set(v[0], v[1], v[2]);
            needUpdate = true;
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    ImGui::TreePop();
}

static void RenderLeafTraitsPanel(bool& needUpdate) {
    // This panel edits the currently loaded PlantDesc directly
    auto& plant = Maize::plant();
    int tillerIdx = (!plant.tillers.empty()) ? std::min(gSelTiller, (int)plant.tillers.size() - 1) : 0;
    int leafCount = (!plant.tillers.empty()) ? (int)plant.tillers[tillerIdx].leaves.size() : 0;
    char leafHeader[64];
    if (leafCount > 0)
        snprintf(leafHeader, sizeof(leafHeader), "Leaf Traits  (leaf %d / %d)###LeafTraits", std::min(gSelLeaf, leafCount - 1) + 1, leafCount);
    else
        snprintf(leafHeader, sizeof(leafHeader), "Leaf Traits###LeafTraits");
    if (!ImGui::CollapsingHeader(leafHeader)) return;

    ImGui::TextWrapped("Edit individual leaf traits and control points.");
    if (plant.tillers.empty()) return;

    gSelTiller = tillerIdx;
    auto& tiller = plant.tillers[gSelTiller];

    ImGui::Separator();
    ImGui::Text("Plant");
    {
        char speciesBuf[128];
        memset(speciesBuf, 0, sizeof(speciesBuf));
        memcpy(speciesBuf, plant.species.c_str(), (std::min)(plant.species.size(), sizeof(speciesBuf) - 1));
        if (ImGui::InputText("Species", speciesBuf, sizeof(speciesBuf))) {
            plant.species = speciesBuf;
        }

        char phenoBuf[128];
        memset(phenoBuf, 0, sizeof(phenoBuf));
        memcpy(phenoBuf, plant.phenotypeId.c_str(), (std::min)(plant.phenotypeId.size(), sizeof(phenoBuf) - 1));
        if (ImGui::InputText("Phenotype ID", phenoBuf, sizeof(phenoBuf))) {
            plant.phenotypeId = phenoBuf;
        }
    }

    ImGui::Separator();
    ImGui::Text("Tiller");
    needUpdate |= ImGui::DragFloat("Radius", &tiller.radius, 0.001f);
    needUpdate |= ImGui::DragFloat("Stem Shrink", &tiller.stemShrink, 0.0002f);

    ImGui::Separator();
    bool useStemOverrides = tiller.useStemCtrlOverrides;
    if (ImGui::Checkbox("Edit Stem Control Points", &useStemOverrides)) {
        tiller.useStemCtrlOverrides = useStemOverrides;
        if (!tiller.useStemCtrlOverrides) {
            tiller.stemCtrlOverride.clear();
        } else if (tiller.stemCtrlOverride.empty()) {
            if (const auto* stemGeom = GetStemGeom()) {
                tiller.stemCtrlOverride = stemGeom->ctrlCenter;
            }
        }
        needUpdate = true;
    }

    if (tiller.useStemCtrlOverrides) {
        if (ImGui::Button("Copy Current Stem Control Points")) {
            if (const auto* stemGeom = GetStemGeom()) {
                tiller.stemCtrlOverride = stemGeom->ctrlCenter;
                needUpdate = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Stem Control Points")) {
            tiller.useStemCtrlOverrides = false;
            tiller.stemCtrlOverride.clear();
            needUpdate = true;
        }

        EditControlPointList("Stem Control Points", "stem", tiller.stemCtrlOverride, needUpdate);
    }

    if (tiller.leaves.empty()) return;

    gSelLeaf = std::min(gSelLeaf, (int)tiller.leaves.size() - 1);
    int dispLeaf = gSelLeaf + 1;
    if (ImGui::SliderInt("Select Leaf", &dispLeaf, 1, (int)tiller.leaves.size()))
        gSelLeaf = dispLeaf - 1;
    auto& leaf = tiller.leaves[gSelLeaf];

    ImGui::Separator();
    ImGui::Text("Leaf #%d Parameters", leaf.id);
    needUpdate |= ImGui::DragFloat("Distance", &leaf.distance, 0.01f);
    needUpdate |= ImGui::DragFloat("Length", &leaf.leafLength, 0.01f);
    needUpdate |= ImGui::DragFloat("Width", &leaf.leafWidth, 0.005f);
    if (ImGui::SliderFloat("Azimuth", &leaf.azimuthDeg, 0.f, 360.f)) {
        leaf.azimuthDeg = Maize::normalizeAzimuthDeg(leaf.azimuthDeg);
        needUpdate = true;
    }
    needUpdate |= ImGui::SliderFloat("Angle", &leaf.leafAngle, -90.f, 90.f);
    needUpdate |= ImGui::SliderFloat("Leaf Curl", &leaf.leafCurl, -1.0f, 1.0f);
    needUpdate |= ImGui::SliderFloat("Droopiness", &leaf.droopiness, -80.f, 80.f);
    needUpdate |= ImGui::SliderFloat("Stem Inclination", &leaf.stemInclinationDeg, -30.f, 30.f);

    bool splineChanged = ImGui::SliderInt("Spline Pts", &leaf.splinePoints, 4, 40);
    needUpdate |= splineChanged;
    if (splineChanged && leaf.useCtrlOverrides) {
        leaf.useCtrlOverrides = false;
        leaf.ctrlCenterOverride.clear();
        leaf.ctrlLeftOverride.clear();
        leaf.ctrlRightOverride.clear();
    }

    needUpdate |= ImGui::SliderFloat("Taper", &leaf.widthTaper, 0.05f, 10.0f);
    needUpdate |= ImGui::SliderFloat("Twist", &leaf.leafTwist, -180.f, 180.f);

    ImGui::Text("Waves");
    needUpdate |= ImGui::SliderFloat("L Amp", &leaf.waveLAmp, 0.f, 0.05f);
    needUpdate |= ImGui::SliderFloat("L Freq", &leaf.waveLFreq, 0.f, 100.f);
    needUpdate |= ImGui::SliderFloat("L Phase", &leaf.waveLPhase, -6.283f, 6.283f);
    needUpdate |= ImGui::SliderFloat("R Amp", &leaf.waveRAmp, 0.f, 0.05f);
    needUpdate |= ImGui::SliderFloat("R Freq", &leaf.waveRFreq, 0.f, 100.f);
    needUpdate |= ImGui::SliderFloat("R Phase", &leaf.waveRPhase, -6.283f, 6.283f);

    ImGui::Text("Leaf Surface");
    needUpdate |= ImGui::SliderFloat("Surface Noise Amp", &leaf.surfaceNoiseAmp, 0.0f, 0.05f);
    needUpdate |= ImGui::SliderFloat("Surface Noise Freq", &leaf.surfaceNoiseFreq, 0.1f, 24.0f);

    ImGui::Text("Midrib");
    needUpdate |= ImGui::SliderFloat("Midrib Tip Taper Start", &leaf.midribTipTaperStart, 0.2f, 0.98f);
    needUpdate |= ImGui::SliderFloat("Midrib Texture Strength", &leaf.midribTextureStrength, 0.0f, 0.95f);
    needUpdate |= ImGui::SliderFloat("Midrib Width", &leaf.midribWidth, 0.01f, 0.25f, "%.3f");

    ImGui::Text("Sheath & Unfold");
    needUpdate |= ImGui::SliderFloat("Ligule Wrap Length", &leaf.liguleWrapLengthScale, 0.5f, 8.0f);
    needUpdate |= ImGui::SliderFloat("Ligule Unfold Sharpness", &leaf.liguleUnfoldSharpness, 0.2f, 4.0f);
    needUpdate |= ImGui::SliderFloat("Sheath Outer Scale", &leaf.sheathOuterScale, 1.0f, 1.6f);

    ImGui::Separator();
    bool useOverrides = leaf.useCtrlOverrides;
    if (ImGui::Checkbox("Edit Control Points", &useOverrides)) {
        leaf.useCtrlOverrides = useOverrides;
        if (!leaf.useCtrlOverrides) {
            leaf.ctrlCenterOverride.clear();
            leaf.ctrlLeftOverride.clear();
            leaf.ctrlRightOverride.clear();
        } else if (leaf.ctrlCenterOverride.empty()) {
            CopyBladeCtrlToLeafOverrides(plant, gSelTiller, gSelLeaf, leaf);
        }
        needUpdate = true;
    }

    if (!leaf.useCtrlOverrides) return;

    if (ImGui::Button("Copy Current Control Points")) {
        if (CopyBladeCtrlToLeafOverrides(plant, gSelTiller, gSelLeaf, leaf)) {
            needUpdate = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Control Points")) {
        leaf.useCtrlOverrides = false;
        leaf.ctrlCenterOverride.clear();
        leaf.ctrlLeftOverride.clear();
        leaf.ctrlRightOverride.clear();
        needUpdate = true;
    }

    EditControlPointList("Center Control Points", "center", leaf.ctrlCenterOverride, needUpdate);
    EditControlPointList("Left Control Points", "left", leaf.ctrlLeftOverride, needUpdate);
    EditControlPointList("Right Control Points", "right", leaf.ctrlRightOverride, needUpdate);
}

static void RenderExportPanel() {
    // Covers both the full-plant format and the lighter skeleton/OBJ exports
    if (!ImGui::CollapsingHeader("Export")) return;

    static std::string sExportStatus;
    static ImVec4 sExportStatusColor{ 1.0f, 1.0f, 1.0f, 1.0f };

    PathInputWithBrowseButton("XML Path", gXmlPath, sizeof(gXmlPath), "Browse...", "Choose plant XML", kXmlFilesFilter, false, "xml");
    if (ImGui::Button("Load XML")) {
        sExportStatus.clear();
        std::string errMsg;
        if (Descriptor::loadPlantXML(gXmlPath, &errMsg)) {
            gSkeletonOnlyView = false;
            RebuildGeometryAndRenderer();
            sExportStatus = "Plant XML loaded.";
            sExportStatusColor = ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
        } else {
            sExportStatus = errMsg.empty() ? "Failed to load plant XML." : errMsg;
            sExportStatusColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As XML")) {
        BrowseForFilePath(gXmlPath, sizeof(gXmlPath), "Save plant XML", kXmlFilesFilter, true, "xml");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save XML")) {
        if (Descriptor::savePlantXML(gXmlPath)) {
            sExportStatus = "Plant XML saved.";
            sExportStatusColor = ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
        } else {
            sExportStatus = "Failed to save plant XML.";
            sExportStatusColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        }
    }

    PathInputWithBrowseButton("Skeleton XML Path", gSkeletonXmlPath, sizeof(gSkeletonXmlPath), "Browse...", "Choose skeleton XML", kXmlFilesFilter, false, "xml");
    if (ImGui::Button("Load Skeleton XML")) {
        sExportStatus.clear();
        std::string errMsg;
        if (Descriptor::loadSkeletonXML(gSkeletonXmlPath, &errMsg)) {
            gSkeletonOnlyView = true;
            gShowSkeletonSpline = true;
            RebuildGeometryAndRenderer();
            sExportStatus = "Skeleton XML loaded.";
            sExportStatusColor = ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
        } else {
            sExportStatus = errMsg.empty() ? "Failed to load skeleton XML." : errMsg;
            sExportStatusColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As Skeleton XML")) {
        BrowseForFilePath(gSkeletonXmlPath, sizeof(gSkeletonXmlPath), "Save skeleton XML", kXmlFilesFilter, true, "xml");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Skeleton XML")) {
        if (Descriptor::saveSkeletonXML(gSkeletonXmlPath)) {
            sExportStatus = "Skeleton XML saved.";
            sExportStatusColor = ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
        } else {
            sExportStatus = "Failed to save skeleton XML.";
            sExportStatusColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        }
    }

    PathInputWithBrowseButton("OBJ Path", gObjPath, sizeof(gObjPath), "Browse...", "Save plant OBJ", kObjFilesFilter, true, "obj");
    if (ImGui::Button("Save OBJ")) {
        if (Maize::savePlantOBJ(gObjPath, gTexPath, gStemTexPath)) {
            sExportStatus = "Plant OBJ saved.";
            sExportStatusColor = ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
        } else {
            sExportStatus = "Failed to save plant OBJ.";
            sExportStatusColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        }
    }

    PathInputWithBrowseButton("Skeleton OBJ Path", gSkeletonObjPath, sizeof(gSkeletonObjPath), "Browse...", "Save skeleton OBJ", kObjFilesFilter, true, "obj");
    ImGui::Checkbox("Include Tassel in Skeleton", &gSkeletonIncludeTassel);
    if (ImGui::Button("Save Skeleton OBJ")) {
        if (Maize::saveSkeletonOBJ(gSkeletonObjPath, gSkeletonIncludeTassel)) {
            sExportStatus = "Skeleton OBJ saved.";
            sExportStatusColor = ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
        } else {
            sExportStatus = "Failed to save skeleton OBJ.";
            sExportStatusColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        }
    }

    if (!sExportStatus.empty()) {
        ImGui::TextColored(sExportStatusColor, "%s", sExportStatus.c_str());
    }
}

static void RenderTasselPanel(bool& needRegen) {
    // Edits the generator's tassel template. A tassel is grown for every
    // generated plant when enabled; each plant's tassel follows its own seed,
    // and latent vigor modulates uprightness (see Vigor->Upright).
    if (!ImGui::CollapsingHeader("Tassel")) return;

    needRegen |= ImGui::Checkbox("Enable Tassel", &gGen.tasselEnabled);
    needRegen |= ImGui::SliderFloat("Tassel Scale", &gGen.tasselParams.scale, 0.001f, 0.05f, "%.4f");

    ImGui::TextWrapped("A tassel is grown for every generated plant when enabled, attached to the main stem tip. Its genotype follows the plant seed (so a set varies). Tassel droop is a correlated trait: vigorous plants hold it upright. Edit the 'Tassel Droop' row in the Global Traits correlation matrix to tune how vigor/posture/etc. drive uprightness; use Preview Random Sample to see the spread.");

    if (ImGui::TreeNode("Tassel Parameters")) {
        auto& T = gGen.tasselParams;
        ImGui::TextUnformatted("Structure");
        needRegen |= ImGui::SliderInt("Branch Nodes", &T.branchNodeCount, 0, 30);
        needRegen |= ImGui::SliderInt("Spike Nodes", &T.spikeNodeCount, 1, 60);
        needRegen |= ImGui::SliderInt("Secondary Nodes", &T.secondaryNodeCount, 0, 20);

        ImGui::Separator();
        ImGui::TextUnformatted("Shape");
        needRegen |= ImGui::SliderFloat("Phyllotaxis Angle", &T.phyllotaxisAngle, 0.0f, 360.0f);
        needRegen |= ImGui::SliderFloat("Lateral Thickness", &T.lateralThicknessRatio, 0.1f, 1.0f);
        needRegen |= ImGui::SliderFloat("Secondary Angle", &T.secondaryInsertionAngle, 0.0f, 90.0f);
        needRegen |= ImGui::SliderFloat("Branch Relax", &T.branchAngleRelaxation, 0.003f, 0.5f, "%.3f");

        ImGui::Separator();
        ImGui::TextUnformatted("Droop (correlated trait; see matrix loadings)");
        needRegen |= ImGui::SliderFloat("Droop Mean", &gGen.tasselDroop.mean, 0.0f, 60.0f);
        needRegen |= ImGui::SliderFloat("Droop Variation", &gGen.tasselDroop.stdDev, 0.0f, 30.0f);
        needRegen |= ImGui::SliderFloat("Droop Start", &T.matureDroopStartT, 0.0f, 1.0f);
        // Scales the genotype's baked gravitropism (a separate, per-seed droop
        // source). Lower this if some tassels collapse downward.
        needRegen |= ImGui::SliderFloat("Gravitropism", &T.gravitropism, 0.0f, 1.0f);

        ImGui::Separator();
        ImGui::TextUnformatted("Timing (Growing Degree Days)");
        needRegen |= ImGui::SliderFloat("Plastochron GDD", &T.plastochronGdd, 5.0f, 120.0f);
        needRegen |= ImGui::SliderFloat("Maturity GDD", &T.maturityGdd, 30.0f, 600.0f);

        ImGui::TreePop();
    }
}

static void RenderVisualSettingsPanel(bool& needUpdate) {
    // Pure viewport/render controls
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Visual Settings")) return;

    if (!gPlantRenderer.initialized) {
        ImGui::TextUnformatted("Renderer: not initialized yet");
    } else if (gPlantRenderer.available) {
        ImGui::TextUnformatted("Renderer: shader-based buffered path");
    } else {
        ImGui::TextUnformatted("Renderer unavailable:");
        ImGui::TextWrapped("%s", gPlantRenderer.error.c_str());
    }

    ImGui::Checkbox("Lighting", &gLightingOn);
    ImGui::Checkbox("Show Normals", &gShowNormals);
    ImGui::Checkbox("Show Control Points", &gShowCtrlPts);
    ImGui::Checkbox("Show Skeleton Spline", &gShowSkeletonSpline);
    ImGui::Checkbox("Skeleton Only View", &gSkeletonOnlyView);
    ImGui::Checkbox("Wireframe", &gWireframe);

    ImGui::Separator();
    ImGui::Text("Modern Shading");
    ImGui::SliderFloat("Exposure", &gExposure, 0.4f, 2.5f);
    ImGui::SliderFloat("Sky Ambient", &gAmbientSkyStrength, 0.0f, 1.4f);
    ImGui::SliderFloat("Ground Ambient", &gAmbientGroundStrength, 0.0f, 0.8f);
    ImGui::SliderFloat("Diffuse Strength", &gDiffuseStrength, 0.2f, 1.8f);
    ImGui::SliderFloat("Diffuse Wrap", &gDiffuseWrap, 0.0f, 0.8f);
    ImGui::SliderFloat("Leaf Transmission", &gTransmissionStrength, 0.0f, 1.2f);
    ImGui::SliderFloat("Specular Strength", &gSpecularStrengthModern, 0.0f, 0.6f);

    ImGui::Separator();
    ImGui::Text("Stem Mesh");
    needUpdate |= ImGui::SliderFloat("Stem Density", &Maize::gStemDensityScale(), 0.1f, 1.0f, "%.2f");

    ImGui::Separator();
    ImGui::Text("Resolution");
    needUpdate |= ImGui::SliderFloat("Res Vertical", &Maize::gMeshDensityV(), 10.f, 200.f, "%.0f / m");
    needUpdate |= ImGui::SliderFloat("Res Horizontal", &Maize::gMeshDensityU(), 10.f, 200.f, "%.0f / m");
    ImGui::Text("Triangles: %d", Maize::getTriangleCount());
}

static void display() {
    // Main editor frame
    ImGui_ImplOpenGL2_NewFrame(); ImGui_ImplGLUT_NewFrame(); ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, (GLsizei)io.DisplaySize.x, (GLsizei)io.DisplaySize.y);
    glClearColor(0.93f, 0.96f, 1.0f, 1.f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST);

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
    // While a batch is running, the worker thread is mutating the global
    // Maize model. Skip all draws that read from it to avoid data races.
    if (!gBatchRunning.load(std::memory_order_acquire)) {
        if (!gSkeletonOnlyView) {
            drawPlant();
            drawNormals();
            drawCtrlViz();
        }
        drawSkeletonViz();
    }

    ImGui::Begin("Maize Generator");

    bool needRegen = false;
    bool needUpdate = false;

    ImGui::Text("Camera: Left Click=Rotate, WASD=Move, Scroll=Zoom");
    RenderGlobalTraitsPanel(needRegen);
    RenderSetGenerationPanel();
    RenderGeneratorConfigPanel();
    RenderLeafTraitsPanel(needUpdate);
    RenderTasselPanel(needRegen);
    RenderExportPanel();
    RenderVisualSettingsPanel(needUpdate);

    ImGui::End();

    if (needRegen) RebuildFromGenerator();
    else if (needUpdate) RebuildGeometryAndRenderer();

    ImGui::Render(); ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    glutSwapBuffers();
}

static void reshape(int w, int h) {
    gWinW = w > 0 ? w : 1; gWinH = h > 0 ? h : 1;
    ImGuiIO& io = ImGui::GetIO(); io.DisplaySize = ImVec2((float)gWinW, (float)gWinH);
}

static void idle() { glutPostRedisplay(); }

static const char* kAppSettingsPath = "app_settings.xml";

static void LoadAppSettings() {
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(kAppSettingsPath) != tinyxml2::XML_SUCCESS) return;
    auto* root = doc.FirstChildElement("AppSettings");
    if (!root) return;

    auto readStr = [&](const char* name, char* buf, int bufSize) {
        if (auto* e = root->FirstChildElement(name))
            if (const char* v = e->GetText()) {
                std::strncpy(buf, v, (size_t)bufSize - 1);
                buf[bufSize - 1] = '\0';
            }
    };
    auto readInt = [&](const char* name, int& val) {
        if (auto* e = root->FirstChildElement(name))
            e->QueryIntText(&val);
    };

    readStr("TexPath",             gTexPath,             sizeof(gTexPath));
    readStr("StemTexPath",         gStemTexPath,         sizeof(gStemTexPath));
    readStr("ExportBasePath",      gExportBasePath,      sizeof(gExportBasePath));
    readStr("GeneratorConfigPath", gGeneratorConfigPath, sizeof(gGeneratorConfigPath));
    readStr("XmlPath",             gXmlPath,             sizeof(gXmlPath));
    readStr("SkeletonXmlPath",     gSkeletonXmlPath,     sizeof(gSkeletonXmlPath));
    readStr("ObjPath",             gObjPath,             sizeof(gObjPath));
    readStr("SkeletonObjPath",     gSkeletonObjPath,     sizeof(gSkeletonObjPath));
    readInt("BatchCount",          gBatchCount);
    readInt("BatchStartIndex",     gBatchStartIndex);
}

static void SaveAppSettings() {
    tinyxml2::XMLDocument doc;
    auto* root = doc.NewElement("AppSettings");
    doc.InsertFirstChild(root);

    auto addStr = [&](const char* name, const char* val) {
        auto* e = doc.NewElement(name);
        e->SetText(val);
        root->InsertEndChild(e);
    };
    auto addInt = [&](const char* name, int val) {
        auto* e = doc.NewElement(name);
        e->SetText(val);
        root->InsertEndChild(e);
    };

    addStr("TexPath",             gTexPath);
    addStr("StemTexPath",         gStemTexPath);
    addStr("ExportBasePath",      gExportBasePath);
    addStr("GeneratorConfigPath", gGeneratorConfigPath);
    addStr("XmlPath",             gXmlPath);
    addStr("SkeletonXmlPath",     gSkeletonXmlPath);
    addStr("ObjPath",             gObjPath);
    addStr("SkeletonObjPath",     gSkeletonObjPath);
    addInt("BatchCount",          gBatchCount);
    addInt("BatchStartIndex",     gBatchStartIndex);

    doc.SaveFile(kAppSettingsPath);
}

// ---------------------------------------------------------------------------
// Headless batch CLI. Invoked as:
//   Maize.exe --headless --config CFG.xml --output PATH_PREFIX --count N
//             [--seed-start K] [--leaf-tex LEAF.png] [--stem-tex STEM.png]
//             [--export-skeleton]
//
// or, to mesh an existing descriptor with this engine's geometry:
//   Maize.exe --headless --from-xml PLANT.xml --output PATH_PREFIX
//             [--leaf-tex LEAF.png] [--stem-tex STEM.png]
// which writes PATH_PREFIX.obj (+ .mtl) and leaves the XML untouched.
//
// Skips glutInit / window / ImGui entirely so it works on headless machines
// (CI, scripts, Python subprocess invocations from notebooks). The plant
// XMLs end up at <output>_<seed>.xml in PyMaize-compatible format.
// ---------------------------------------------------------------------------
static int RunHeadless(int argc, char** argv) {
    std::string configPath;
    std::string outputPrefix = "plant_export/plant";
    std::string leafTex      = "maize_leaf.png";
    std::string stemTex      = "maize_stem_texture.png";
    int count = 1;
    int seedStart = 0;
    bool exportSkeleton = false;
    std::string fromXml;

    auto needArg = [&](int& i, const char* flag) -> const char* {
        if (i + 1 >= argc) {
            std::cerr << "Maize.exe: missing argument after " << flag << "\n";
            std::exit(2);
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--config")          configPath    = needArg(i, "--config");
        else if (a == "--output")          outputPrefix  = needArg(i, "--output");
        else if (a == "--count")           count         = std::atoi(needArg(i, "--count"));
        else if (a == "--seed-start")      seedStart     = std::atoi(needArg(i, "--seed-start"));
        else if (a == "--leaf-tex")        leafTex       = needArg(i, "--leaf-tex");
        else if (a == "--stem-tex")        stemTex       = needArg(i, "--stem-tex");
        else if (a == "--export-skeleton") exportSkeleton = true;
        else if (a == "--from-xml")        fromXml       = needArg(i, "--from-xml");
        else if (a == "--headless")        {} // already detected
        else { std::cerr << "Maize.exe: unknown flag " << a << "\n"; std::exit(2); }
    }
    if (!fromXml.empty()) {
        std::string err;
        if (!Descriptor::loadPlantXML(fromXml, &err)) {
            std::cerr << "Failed to load descriptor '" << fromXml << "': " << err << "\n";
            return 1;
        }
        Maize::rebuildGeometry();
        std::string objPath = outputPrefix + ".obj";
        Maize::savePlantOBJ(objPath.c_str(), leafTex.c_str(), stemTex.empty() ? nullptr : stemTex.c_str());
        std::cout << "[headless] meshed " << fromXml << " -> " << objPath << "\n";
        if (exportSkeleton) {
            // Centerlines as in batch export, with the tassel's segments (consecutive vertex pairs) last.
            std::string skelPath = outputPrefix + "_skeleton.obj";
            Maize::saveSkeletonOBJ(skelPath.c_str(), true);
            std::cout << "[headless] skeleton -> " << skelPath << "\n";
        }
        return 0;
    }
    if (configPath.empty()) {
        std::cerr << "Maize.exe --headless: --config CONFIG.xml (or --from-xml PLANT.xml) is required\n";
        return 2;
    }

    MaizeGenerator gen;
    std::string err;
    if (!gen.loadConfig(configPath, &err)) {
        std::cerr << "Failed to load config '" << configPath << "': " << err << "\n";
        return 1;
    }
    std::cout << "[headless] config=" << configPath
              << " output=" << outputPrefix
              << " count=" << count
              << " seedStart=" << seedStart << "\n";
    std::atomic<int> progress{0};
    gen.generateBatch(count, outputPrefix, leafTex, stemTex,
                      /*useMeansOnly=*/false, exportSkeleton,
                      &progress, seedStart, /*cancelFlag=*/nullptr);
    std::cout << "[headless] wrote " << progress.load() << " plants\n";
    return 0;
}

int main(int argc, char** argv) {
    // Detect --headless anywhere in argv so we can fully bypass GLUT/ImGui setup.
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--headless") {
            return RunHeadless(argc, argv);
        }
    }

    srand((unsigned int)time(NULL));
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH | GLUT_MULTISAMPLE);
    glutInitWindowSize(gWinW, gWinH);
    glutCreateWindow("Maize Procedural Generator");
    ImGui::CreateContext();
    ImGui_ImplGLUT_Init(); ImGui_ImplGLUT_InstallFuncs(); ImGui_ImplOpenGL2_Init();
    EnsureModernPlantRendererReady();
    glEnable(GL_MULTISAMPLE);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

    LoadAppSettings();

    // Apply the saved generator config (or the bundled default on first launch)
    // before the first rebuild so the viewport reflects it immediately.
    gGen.loadConfig(gGeneratorConfigPath);

    loadTexture(gTexPath, &gTextureID, &gTextureOK);
    loadTexture(gStemTexPath, &gStemTextureID, &gStemTextureOK);
    RebuildFromGenerator();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutCloseFunc(SaveAppSettings);
    glutMainLoop();
    return 0;
}
