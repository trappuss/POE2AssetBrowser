#pragma once
#include <QString>
#include <QVector>
#include <array>
#include <cstdint>

// The geometry contract the viewport and the .glb exporter consume. Filled by the .smd / .fmt
// parsers (model/SmdParser, model/FmtParser) from decoded bundle bytes; measured layout in
// docs/FORMATS.md §3–4.
//
// PoE2 meshes are Z-down in the native frame; the viewport and exporter apply one shared
// native→Y-up rotation (RigMath::kNativeToYUp) so winding and normals are preserved.

struct MeshVertex {
    float px = 0, py = 0, pz = 0;    // POSITION (native frame)
    float nx = 0, ny = 0, nz = 1;    // NORMAL (unit)
    float tx = 1, ty = 0, tz = 0;    // TANGENT xyz
    float tw = 1;                     // TANGENT w (handedness, ±1)
    float u  = 0, v  = 0;             // TEXCOORD_0
    float u2 = 0, v2 = 0;             // TEXCOORD_1 (only when the format carries it)
    uint8_t joints[4] = {0, 0, 0, 0}; // JOINTS_0
    float   weights[4] = {0, 0, 0, 0};// WEIGHTS_0 (normalised 0..1)
    uint8_t color[4] = {255, 255, 255, 255};  // COLOR_0
};

// One drawable submesh: a contiguous run of the shared index buffer, plus the shape name and the
// material path the .sm/.fmt declared for it.
struct MeshPart {
    QString  name;               // "NecklaceShape", "R_Chest_BonesShape", the Maya DAG leaf
    QString  material;           // "art/…/foo.mat" (lowercase), empty when none declared
    uint32_t indexStart = 0;     // into ModelGeometry::indices
    uint32_t indexCount = 0;
    int      materialIndex = -1; // into ModelGeometry::materialPaths (-1 = default)
    bool     visible = true;
};

struct ModelJoint {
    QString name;
    int     parent = -1;                       // index into joints (-1 = root)
    std::array<float, 16> bindMatrix{};        // model-space bind (row-major, translation in row 3)
    std::array<float, 16> inverseBind{};       // filled by the parser
};

struct ModelGeometry {
    QVector<MeshVertex> vertices;
    QVector<uint32_t>   indices;
    QVector<MeshPart>   parts;
    QVector<ModelJoint> joints;                // empty for a static (.fmt) mesh
    QVector<QString>    materialPaths;         // de-duplicated, lowercase
    float bboxMin[3] = {0, 0, 0};
    float bboxMax[3] = {0, 0, 0};

    // Provenance for the Explain report (docs/FORMATS.md distinctions).
    QString sourcePath;                        // the .smd/.fmt game path
    int     formatVersion = 0;                 // 1/2/3 (smd) or 9 (fmt)
    int     vertexFormat = 0;                  // the raw flag word
    bool    skinned = false;

    bool isEmpty() const { return vertices.isEmpty() || indices.isEmpty(); }
    int  triangleCount() const { return indices.size() / 3; }
    // Highest joint index referenced by a weighted vertex, +1 (the joint-palette size the rig must
    // cover). 0 for an unskinned mesh. Used to pick/validate the skeleton (armour base-rig fallback).
    int  jointPaletteSize() const {
        int m = -1;
        for (const MeshVertex& v : vertices)
            for (int k = 0; k < 4; ++k) if (v.weights[k] > 0.0f && int(v.joints[k]) > m) m = v.joints[k];
        return m + 1;
    }
    void computeBounds();
};
