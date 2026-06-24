#include "Maize.h"
#include <cstdio>
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {
    // Solid light-yellow texture for the tassel. Written next to the OBJ/MTL so
    // viewers that paint a single texture across the mesh (e.g. MeshLab) still
    // show the tassel yellow instead of bleeding the green leaf texture onto it.
    constexpr char kTasselTextureName[] = "maize_tassel.png";

    void writeTasselTexture(const std::string& dir) {
        const std::string path = dir.empty() ? std::string(kTasselTextureName) : (dir + "/" + kTasselTextureName);
        // 8x8 RGB, all pixels light yellow (0.95, 0.91, 0.55) * 255.
        unsigned char px[8 * 8 * 3];
        for (int i = 0; i < 8 * 8; ++i) { px[i * 3 + 0] = 242; px[i * 3 + 1] = 232; px[i * 3 + 2] = 140; }
        stbi_write_png(path.c_str(), 8, 8, 3, px, 8 * 3);
    }

    std::string baseNameFromPath(const std::string& p) {
        size_t s = p.find_last_of("/\\");
        return (s == std::string::npos) ? p : p.substr(s + 1);
    }

    FILE* openWritableFile(const char* path) {
        if (!path) return nullptr;
        FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, path, "w");
#else
        f = std::fopen(path, "w");
#endif
        return f;
    }

    void writeObjVertexStreams(FILE* f, const std::vector<LeafGeom>& geoms) {
        for (const auto& geom : geoms) {
            for (size_t i = 0; i < geom.pos.size(); ++i) {
                const auto& p = geom.pos[i];
                const auto& c = geom.col[i];
                std::fprintf(f, "v %.9g %.9g %.9g %.9g %.9g %.9g\n", p.x(), p.y(), p.z(), c.x(), c.y(), c.z());
            }
            for (const auto& uv : geom.uv) {
                std::fprintf(f, "vt %.9g %.9g\n", uv.u, uv.v);
            }
            for (const auto& n : geom.nrm) {
                std::fprintf(f, "vn %.9g %.9g %.9g\n", n.x(), n.y(), n.z());
            }
        }
    }

    void writeObjFaces(FILE* f, const std::vector<LeafGeom>& geoms) {
        size_t vOffset = 1;
        size_t vtOffset = 1;
        size_t vnOffset = 1;

        for (const auto& geom : geoms) {
            const char* material = (geom.type == LeafGeom::GEOM_SHEATH) ? "maize_stem" : "maize_leaf";
            std::fprintf(f, "usemtl %s\n", material);

            for (int i = 0; i < geom.rows - 1; ++i) {
                for (int j = 0; j < geom.cols - 1; ++j) {
                    int a = int(vOffset + i * geom.cols + j);
                    int b = int(vOffset + (i + 1) * geom.cols + j);
                    int c = int(vOffset + (i + 1) * geom.cols + (j + 1));
                    int d = int(vOffset + i * geom.cols + (j + 1));

                    int ta = int(vtOffset + i * geom.cols + j);
                    int tb = int(vtOffset + (i + 1) * geom.cols + j);
                    int tc = int(vtOffset + (i + 1) * geom.cols + (j + 1));
                    int td = int(vtOffset + i * geom.cols + (j + 1));

                    int na = int(vnOffset + i * geom.cols + j);
                    int nb = int(vnOffset + (i + 1) * geom.cols + j);
                    int nc = int(vnOffset + (i + 1) * geom.cols + (j + 1));
                    int nd = int(vnOffset + i * geom.cols + (j + 1));

                    std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, ta, na, b, tb, nb, c, tc, nc);
                    std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, ta, na, c, tc, nc, d, td, nd);
                }
            }

            vOffset += geom.pos.size();
            vtOffset += geom.uv.size();
            vnOffset += geom.nrm.size();
        }
    }

    // Append the tassel (indexed triangle soup) after the leaf/stem streams.
    // leafV/leafVT/leafVN are the totals already written, so the tassel's
    // 0-based indices are rebased onto the shared OBJ vertex space.
    void writeObjTassel(FILE* f, const l_system_plugin::TasselMesh& t,
                        size_t leafV, size_t leafVT, size_t leafVN) {
        if (t.indices.empty()) return;
        const float cr = 0.95f, cg = 0.91f, cb = 0.55f;  // light yellow
        // The tassel has its own solid-yellow texture (maize_tassel.png), so its
        // faces carry texture coordinates that sample that image rather than the
        // green leaf map. Per-vertex colour is also light yellow as a fallback.
        for (size_t i = 0; i < t.pos.size(); ++i) {
            const auto& p = t.pos[i];
            std::fprintf(f, "v %.9g %.9g %.9g %.9g %.9g %.9g\n", p.x(), p.y(), p.z(), cr, cg, cb);
        }
        for (size_t i = 0; i < t.u.size(); ++i) {
            std::fprintf(f, "vt %.9g %.9g\n", t.u[i], t.v[i]);
        }
        for (size_t i = 0; i < t.nrm.size(); ++i) {
            const auto& n = t.nrm[i];
            std::fprintf(f, "vn %.9g %.9g %.9g\n", n.x(), n.y(), n.z());
        }
        // Own object + group so viewers treat the tassel as a separate sub-mesh.
        std::fprintf(f, "o tassel\ng tassel\n");
        std::fprintf(f, "usemtl maize_tassel\n");
        for (size_t k = 0; k + 2 < t.indices.size(); k += 3) {
            const int a = (int)(leafV + 1 + t.indices[k]);
            const int b = (int)(leafV + 1 + t.indices[k + 1]);
            const int c = (int)(leafV + 1 + t.indices[k + 2]);
            const int ta = (int)(leafVT + 1 + t.indices[k]);
            const int tb = (int)(leafVT + 1 + t.indices[k + 1]);
            const int tc = (int)(leafVT + 1 + t.indices[k + 2]);
            const int na = (int)(leafVN + 1 + t.indices[k]);
            const int nb = (int)(leafVN + 1 + t.indices[k + 1]);
            const int nc = (int)(leafVN + 1 + t.indices[k + 2]);
            std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, ta, na, b, tb, nb, c, tc, nc);
        }
    }
}

bool MaizeModel::savePlantOBJ(const char* path, const char* leafTextureFullPath, const char* stemTextureFullPath, bool writeMTL) {
    if (!path) return false;

    const std::string objPath(path);
    const std::string objDir = Maize::dirFromPath(objPath);
    const std::string mtlFileName = "maize.mtl";
    const std::string mtlPath = objDir.empty() ? mtlFileName : (objDir + "/" + mtlFileName);

    FILE* f = openWritableFile(path);
    if (!f) return false;

    std::fprintf(f, "# Maize export\nmtllib %s\n", mtlFileName.c_str());
    writeObjVertexStreams(f, m_leafGeoms);
    writeObjFaces(f, m_leafGeoms);

    // Tassel triangles, rebased after the leaf/stem vertex streams.
    size_t leafV = 0, leafVT = 0, leafVN = 0;
    for (const auto& g : m_leafGeoms) { leafV += g.pos.size(); leafVT += g.uv.size(); leafVN += g.nrm.size(); }
    writeObjTassel(f, m_tasselMesh, leafV, leafVT, leafVN);

    std::fclose(f);

    if (!writeMTL) {
        // MTL is shared across all plants in a batch. The batch driver writes it
        // once upfront so concurrent workers don't race on maize.mtl.
        return true;
    }

    const char* leafTexName = nullptr;
    const char* stemTexName = nullptr;
    std::string leafTexTmp;
    std::string stemTexTmp;

    if (leafTextureFullPath && leafTextureFullPath[0]) {
        leafTexTmp = baseNameFromPath(leafTextureFullPath);
        leafTexName = leafTexTmp.c_str();
    }
    if (stemTextureFullPath && stemTextureFullPath[0]) {
        stemTexTmp = baseNameFromPath(stemTextureFullPath);
        stemTexName = stemTexTmp.c_str();
    } else {
        stemTexName = leafTexName;
    }

    saveMTL(mtlPath.c_str(), leafTexName, stemTexName);
    return true;
}

bool MaizeModel::saveMTL(const char* mtlPath, const char* leafTextureFileNameOnly, const char* stemTextureFileNameOnly) {
    FILE* m = openWritableFile(mtlPath);
    if (!m) return false;

    std::fprintf(m, "newmtl maize_leaf\nKa 0 0 0\nKd 1 1 1\nKs 0.04 0.04 0.04\nNs 18\nillum 2\n");
    if (leafTextureFileNameOnly && leafTextureFileNameOnly[0]) {
        std::fprintf(m, "map_Kd %s\n", leafTextureFileNameOnly);
    }

    std::fprintf(m, "\nnewmtl maize_stem\nKa 0 0 0\nKd 1 1 1\nKs 0.04 0.04 0.04\nNs 18\nillum 2\n");
    if (stemTextureFileNameOnly && stemTextureFileNameOnly[0]) {
        std::fprintf(m, "map_Kd %s\n", stemTextureFileNameOnly);
    }

    // Light-yellow material for the procedural tassel, backed by its own solid
    // yellow texture so single-texture viewers don't bleed the leaf map onto it.
    std::fprintf(m, "\nnewmtl maize_tassel\nKa 0 0 0\nKd 0.95 0.91 0.55\nKs 0.05 0.05 0.05\nNs 24\nillum 2\n");
    std::fprintf(m, "map_Kd %s\n", kTasselTextureName);

    std::fclose(m);

    // Write the tassel's solid-yellow texture into the same directory as the MTL.
    writeTasselTexture(Maize::dirFromPath(std::string(mtlPath)));
    return true;
}

bool MaizeModel::saveSkeletonOBJ(const char* path) const {
    // Skeleton export uses stemCurve for both stem (smooth B-spline)
    // and leaves (stem exit point + blade center, sheath skipped).
    FILE* f = openWritableFile(path);
    if (!f) return false;

    std::fprintf(f, "# Plant skeleton (leaves connected to stem)\n");

    int vertexOffset = 1;
    for (const auto& geom : m_leafGeoms) {
        if (geom.stemCurve.size() < 2) continue;
        for (const auto& p : geom.stemCurve) {
            std::fprintf(f, "v %.9g %.9g %.9g\n", p.x(), p.y(), p.z());
        }
        std::fprintf(f, "l");
        for (int i = 0; i < (int)geom.stemCurve.size(); ++i) {
            std::fprintf(f, " %d", vertexOffset + i);
        }
        std::fprintf(f, "\n");
        vertexOffset += (int)geom.stemCurve.size();
    }

    // Tassel skeleton: centerline segments (consecutive vertex pairs).
    const auto& tsk = m_tasselMesh.skeleton;
    for (size_t i = 0; i + 1 < tsk.size(); i += 2) {
        std::fprintf(f, "v %.9g %.9g %.9g\n", tsk[i].x(), tsk[i].y(), tsk[i].z());
        std::fprintf(f, "v %.9g %.9g %.9g\n", tsk[i + 1].x(), tsk[i + 1].y(), tsk[i + 1].z());
        std::fprintf(f, "l %d %d\n", vertexOffset, vertexOffset + 1);
        vertexOffset += 2;
    }

    std::fclose(f);
    return true;
}
