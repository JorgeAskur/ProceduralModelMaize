#include "Maize.h"
#include <cstdio>
#include <string>

namespace {
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
}

bool MaizeModel::savePlantOBJ(const char* path, const char* leafTextureFullPath, const char* stemTextureFullPath) {
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
    std::fclose(f);

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

    std::fclose(m);
    return true;
}

bool MaizeModel::saveSkeletonOBJ(const char* path) const {
    // Skeleton export writes only center splines as OBJ line
    FILE* f = openWritableFile(path);
    if (!f) return false;

    std::fprintf(f, "# Plant skeleton (center splines only)\n");

    int vertexOffset = 1;
    for (const auto& geom : m_leafGeoms) {
        if (geom.ctrlCenter.size() < 2) continue;
        for (const auto& p : geom.ctrlCenter) {
            std::fprintf(f, "v %.9g %.9g %.9g\n", p.x(), p.y(), p.z());
        }
        std::fprintf(f, "l");
        for (size_t i = 0; i < geom.ctrlCenter.size(); ++i) {
            std::fprintf(f, " %d", vertexOffset + (int)i);
        }
        std::fprintf(f, "\n");
        vertexOffset += (int)geom.ctrlCenter.size();
    }

    std::fclose(f);
    return true;
}
