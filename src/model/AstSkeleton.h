#pragma once
#include "model/RigMath.h"
#include <QByteArray>
#include <QString>
#include <QVector>
#include <cstdint>

// PoE2 `.ast`: skeleton (bone hierarchy + model-space bind matrices) and animation clips. Versions
// 11 and 12 (the current writers) are parsed fully; 6–10 yield bones only, best-effort, and are
// marked as such. Layout in docs/FORMATS.md §8. The nested key-data bundle is decompressed via
// bundle/Bundle so clips are available for the .glb exporter.
namespace AstSkeleton {

struct Bone {
    QString name;
    int     parent = -1;                 // resolved from the sibling/child links
    RigMath::Mat4 bind{};                // model-space bind (row-major, translation in row 3)
    RigMath::Mat4 inverseBind{};
    uint8_t rawSibling = 255, rawChild = 255;
};

struct KeySet {
    int nodeId = 0;
    QVector<float> scaleTimes;  QVector<std::array<float,3>> scale;
    QVector<float> rotTimes;    QVector<std::array<float,4>> rot;    // xyzw
    QVector<float> posTimes;    QVector<std::array<float,3>> pos;
};

struct Clip {
    QString name, parentName;
    int     fps = 30;
    uint32_t offset = 0, size = 0;
    QVector<KeySet> keys;                // one per animated bone (filled when clips are decoded)
};

struct Skeleton {
    int version = 0;
    QVector<Bone> bones;
    QVector<Clip> clips;
    bool valid = false;
    bool clipsDecoded = false;
    QString note;                        // e.g. "bones only (version 9 clips not decoded)"
};

// Parse the skeleton. When `decodeClips` is true the nested key bundle is decompressed and every
// clip's KeySets are filled; leave it false to list clips without paying the decompression.
Skeleton parse(const QByteArray& data, bool decodeClips, QString* error = nullptr);

// Parse the skeleton and decode ONLY clip `clipIndex`'s KeySets (the others are listed but left
// empty). For the player animation library, where one .ast holds up to ~150 clips and decoding them
// all would cost hundreds of MB: this pays the nested-bundle decompression once but keeps only the
// one clip the user is about to play. clipIndex out of range decodes nothing (bones + clip list only).
Skeleton parseWithClip(const QByteArray& data, int clipIndex, QString* error = nullptr);

// Retarget one clip from `srcSkel` onto `dstSkel` by BONE NAME: returns a copy of srcClip whose every
// KeySet.nodeId is remapped to the matching bone index in dstSkel (matched by name, not index), and
// whose KeySets for bones dstSkel does not have are dropped. This lets a clip authored on the player
// animation rig drive a character whose resolved rig shares those bone names — the template rule of
// binding by authored data (names), never by a positional assumption. `srcClip` must be decoded.
Clip retargetClip(const Skeleton& srcSkel, const Clip& srcClip, const Skeleton& dstSkel);

// Duration of a clip in seconds (largest keyframe time ÷ fps). 0 for an invalid or key-less clip.
float clipDuration(const Skeleton& sk, int clipIndex);

// Per-bone skinning matrices (row-major v·M) at time `t` seconds into clip `clipIndex`: applying
// skin[b] to a bind-pose model-space vertex gives its animated model-space position. A clipIndex
// out of range, or a clip with no decoded keys, yields all-identity (the bind pose), so callers can
// always skin unconditionally. The result is sized to sk.bones.size().
QVector<RigMath::Mat4> skinMatrices(const Skeleton& sk, int clipIndex, float t);

QString selfTest();

}  // namespace AstSkeleton
