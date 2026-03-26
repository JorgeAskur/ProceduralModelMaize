#include <GL/freeglut.h>
#include <GL/glu.h>
#include <GL/glext.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <vector>
#include <string>
#include <cstddef>
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
    static const char* kVertexShader = R"GLSL(
#version 120
varying vec3 vEyePos;
varying vec3 vEyeNormal;
varying vec3 vColor;
varying vec2 vTexCoord;

void main() {
    vec4 eyePos = gl_ModelViewMatrix * gl_Vertex;
    vEyePos = eyePos.xyz;
    vEyeNormal = normalize(gl_NormalMatrix * gl_Normal);
    vColor = gl_Color.rgb;
    vTexCoord = gl_MultiTexCoord0.xy;
    gl_Position = gl_ProjectionMatrix * eyePos;
}
)GLSL";

    static const char* kFragmentShader = R"GLSL(
#version 120
uniform sampler2D uTexture;
uniform float uUseTexture;
uniform float uUseLighting;
uniform float uLeafFactor;
uniform vec3 uLightPosEye;
uniform vec3 uSkyDirEye;
uniform vec3 uAmbientGround;
uniform vec3 uAmbientSky;
uniform float uDiffuseStrength;
uniform float uWrap;
uniform float uSpecularStrength;
uniform float uShininess;
uniform float uTransmission;
uniform float uExposure;

varying vec3 vEyePos;
varying vec3 vEyeNormal;
varying vec3 vColor;
varying vec2 vTexCoord;

void main() {
    vec4 texSample = vec4(1.0);
    if (uUseTexture > 0.5) texSample = texture2D(uTexture, vTexCoord);
    vec3 albedo = vColor * mix(vec3(1.0), texSample.rgb, uUseTexture);
    if (uUseTexture > 0.5 && texSample.a < 0.05) discard;

    vec3 linear = albedo;
    if (uUseLighting > 0.5) {
        vec3 N = normalize(vEyeNormal);
        vec3 V = normalize(-vEyePos);
        vec3 L = normalize(uLightPosEye - vEyePos);
        vec3 H = normalize(L + V);
        vec3 skyDir = normalize(uSkyDirEye);

        float hemiMask = clamp(0.5 + 0.5 * dot(N, skyDir), 0.0, 1.0);
        vec3 ambient = mix(uAmbientGround, uAmbientSky, hemiMask);

        float wrapped = max((dot(N, L) + uWrap) / (1.0 + uWrap), 0.0);
        float diffuse = wrapped * uDiffuseStrength;

        float specPower = pow(max(dot(N, H), 0.0), max(uShininess, 1.0));
        float specMask = mix(0.45, 1.0, 1.0 - uLeafFactor);
        vec3 specular = vec3(specPower * uSpecularStrength * specMask);

        float rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 0.75);
        float backLight = pow(max(dot(-N, L), 0.0), 1.35);
        vec3 transmission = albedo * vec3(0.92, 1.0, 0.88) * (uLeafFactor * uTransmission * backLight * (0.3 + 0.7 * rim));

        linear = albedo * (ambient + diffuse) + specular + transmission;
    }

    vec3 mapped = vec3(1.0) - exp(-linear * uExposure);
    vec3 gammaCorrected = pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / 2.2));
    gl_FragColor = vec4(gammaCorrected, 1.0);
}
)GLSL";

    std::string error;
    GLuint vertShader = CompileShader(GL_VERTEX_SHADER, kVertexShader, error);
    if (!vertShader) {
        gPlantRenderer.error = "Vertex shader compile failed:\n" + error;
        return false;
    }

    GLuint fragShader = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader, error);
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
    // Draw all stored center splines as black polylines
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
    Maize::rebuildGeometry();
    InvalidatePlantRendererMeshes();
}

static void RebuildFromGenerator(bool useMeansOnly = true, unsigned int seed = 1337) {
    // Regenerates the editable PlantDesc from the procedural generator and then rebuilds meshes from that fresh description
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

static void ApplyTillerAzimuthPattern(TillerDesc& tiller) {
    for (int i = 0; i < (int)tiller.leaves.size(); ++i) {
        tiller.leaves[i].azimuthDeg = Maize::computeLeafAzimuthDeg(tiller, i);
    }
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

    if (ImGui::TreeNodeEx("Correlation Matrix", ImGuiTreeNodeFlags_DefaultOpen)) {
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

    ImGui::TextWrapped("Edit global traits and defaults for the plant.");

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
    needRegen |= ImGui::SliderInt("Spline Points", &gGen.splinePointsDefault, 4, 80);

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

    ImGui::Separator();
    ImGui::Text("Leaf Visual Shaping Defaults");
    needRegen |= ImGui::SliderFloat("Surface Noise Amp", &gGen.leafSurfaceNoiseAmp, 0.0f, 0.05f);
    needRegen |= ImGui::SliderFloat("Surface Noise Freq", &gGen.leafSurfaceNoiseFreq, 0.1f, 24.0f);
    needRegen |= ImGui::SliderFloat("Midrib Tip Taper", &gGen.midribTipTaperStart, 0.2f, 0.98f);
    needRegen |= ImGui::SliderFloat("Midrib Texture Strength", &gGen.midribTextureStrength, 0.0f, 0.95f);
    needRegen |= ImGui::SliderFloat("Ligule Wrap Length", &gGen.liguleWrapLengthScale, 0.5f, 8.0f);
    needRegen |= ImGui::SliderFloat("Ligule Unfold Sharpness", &gGen.liguleUnfoldSharpness, 0.2f, 4.0f);
    needRegen |= ImGui::SliderFloat("Sheath Outer Scale", &gGen.sheathOuterScale, 1.0f, 1.6f);

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
    RenderCorrelationMatrixEditor(needRegen);
    ImGui::TextWrapped("Viewport preview uses parameter means. Use Set Generation to sample full plant sets from the configured distributions.");
}

static void RenderSetGenerationPanel() {
    // Batch export samples a fresh plant per output file using the current
    // generator distributions and latent trait settings
    if (!ImGui::CollapsingHeader("Set Generation", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextWrapped("Generate multiple plants by sampling the configured mean/std dev distributions for each generator parameter.");
    PathInputWithBrowseButton("Set Base Path / Prefix", gExportBasePath, sizeof(gExportBasePath), "Choose...", "Choose plant set base path / prefix", kAllFilesFilter, true);
    bool texturePathChanged = false;
    texturePathChanged |= PathInputWithBrowseButton("Leaf Texture Path", gTexPath, sizeof(gTexPath), "Browse...", "Choose leaf texture", kImageFilesFilter, false);
    texturePathChanged |= PathInputWithBrowseButton("Stem Texture Path", gStemTexPath, sizeof(gStemTexPath), "Browse...", "Choose stem texture", kImageFilesFilter, false);
    ImGui::SliderInt("Set Count", &gBatchCount, 1, 500);

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

    if (ImGui::Button("Generate Plant Set")) {
        gGen.generateBatch(gBatchCount, gExportBasePath, gTexPath, gStemTexPath, false);
    }
}

static void RenderGeneratorConfigPanel() {
    // Saves/loads the procedural generator settings only, not the full plant
    if (!ImGui::CollapsingHeader("Generator Config")) return;

    ImGui::TextWrapped("Save or load the current generator parameter settings, including distributions and canopy shaping controls.");
    PathInputWithBrowseButton("Config Path", gGeneratorConfigPath, sizeof(gGeneratorConfigPath), "Browse...", "Choose generator config", kXmlFilesFilter, false, "xml");
    if (ImGui::Button("Load Generator Config")) {
        if (gGen.loadConfig(gGeneratorConfigPath)) {
            RebuildFromGenerator(true, 1337);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) {
        BrowseForFilePath(gGeneratorConfigPath, sizeof(gGeneratorConfigPath), "Save generator config", kXmlFilesFilter, true, "xml");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Generator Config")) {
        gGen.saveConfig(gGeneratorConfigPath);
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
    if (!ImGui::CollapsingHeader("Leaf Traits")) return;

    ImGui::TextWrapped("Edit individual leaf traits and control points.");
    auto& plant = Maize::plant();
    if (plant.tillers.empty()) return;

    gSelTiller = std::min(gSelTiller, (int)plant.tillers.size() - 1);
    auto& tiller = plant.tillers[gSelTiller];

    ImGui::Separator();
    ImGui::Text("Tiller");
    needUpdate |= ImGui::DragFloat("Radius", &tiller.radius, 0.001f);
    needUpdate |= ImGui::DragFloat("Stem Shrink", &tiller.stemShrink, 0.0002f);
    bool azimuthPatternChanged = false;
    azimuthPatternChanged |= ImGui::DragFloat("Azimuth Base", &tiller.azimuthDeg, 1.0f);
    azimuthPatternChanged |= ImGui::DragFloat("Azimuth Noise", &tiller.azimuthNoise, 0.5f);
    if (azimuthPatternChanged) {
        ApplyTillerAzimuthPattern(tiller);
        needUpdate = true;
    }
    ImGui::TextWrapped("Tiller azimuth settings rewrite the current per-leaf azimuth values. Use the leaf slider below for individual edits.");

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
    ImGui::SliderInt("Select Leaf", &gSelLeaf, 0, (int)tiller.leaves.size() - 1);
    auto& leaf = tiller.leaves[gSelLeaf];

    ImGui::Separator();
    ImGui::Text("Leaf #%d Parameters", leaf.id);
    needUpdate |= ImGui::DragFloat("Distance", &leaf.distance, 0.01f);
    needUpdate |= ImGui::DragFloat("Length", &leaf.leafLength, 0.01f);
    needUpdate |= ImGui::DragFloat("Width", &leaf.leafWidth, 0.005f);
    needUpdate |= ImGui::SliderFloat("Azimuth", &leaf.azimuthDeg, -360.f, 360.f);
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

    PathInputWithBrowseButton("XML Path", gXmlPath, sizeof(gXmlPath), "Browse...", "Choose plant XML", kXmlFilesFilter, false, "xml");
    if (ImGui::Button("Load XML")) {
        if (Descriptor::loadPlantXML(gXmlPath)) {
            gSkeletonOnlyView = false;
            RebuildGeometryAndRenderer();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As XML")) {
        BrowseForFilePath(gXmlPath, sizeof(gXmlPath), "Save plant XML", kXmlFilesFilter, true, "xml");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save XML")) {
        Descriptor::savePlantXML(gXmlPath);
    }

    PathInputWithBrowseButton("Skeleton XML Path", gSkeletonXmlPath, sizeof(gSkeletonXmlPath), "Browse...", "Choose skeleton XML", kXmlFilesFilter, false, "xml");
    if (ImGui::Button("Load Skeleton XML")) {
        if (Descriptor::loadSkeletonXML(gSkeletonXmlPath)) {
            gSkeletonOnlyView = true;
            gShowSkeletonSpline = true;
            RebuildGeometryAndRenderer();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As Skeleton XML")) {
        BrowseForFilePath(gSkeletonXmlPath, sizeof(gSkeletonXmlPath), "Save skeleton XML", kXmlFilesFilter, true, "xml");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Skeleton XML")) {
        Descriptor::saveSkeletonXML(gSkeletonXmlPath);
    }

    PathInputWithBrowseButton("OBJ Path", gObjPath, sizeof(gObjPath), "Save As...", "Save plant OBJ", kObjFilesFilter, true, "obj");
    if (ImGui::Button("Save OBJ")) {
        Maize::savePlantOBJ(gObjPath, gTexPath, gStemTexPath);
    }

    PathInputWithBrowseButton("Skeleton OBJ Path", gSkeletonObjPath, sizeof(gSkeletonObjPath), "Save As...", "Save skeleton OBJ", kObjFilesFilter, true, "obj");
    if (ImGui::Button("Save Skeleton OBJ")) {
        Maize::saveSkeletonOBJ(gSkeletonObjPath);
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
    RenderGlobalTraitsPanel(needRegen);
    RenderSetGenerationPanel();
    RenderGeneratorConfigPanel();
    RenderLeafTraitsPanel(needUpdate);
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

int main(int argc, char** argv) {
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

    loadTexture(gTexPath, &gTextureID, &gTextureOK);
    loadTexture(gStemTexPath, &gStemTextureID, &gStemTextureOK);
    RebuildFromGenerator();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutMainLoop();
    return 0;
}
