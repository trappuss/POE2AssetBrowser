#pragma once
#include <QString>

class AssetStore;

// "Explain this material" (template §18) — the single most useful report in an asset browser, and
// the one that embodies this tool's rule against guessing. Given a model path it produces a plain
// text report that answers, for every material the model uses:
//   • where the material roster came from (the .sm descriptor for a .smd, the .fmt itself, or none);
//   • whether each material file is PRESENT in the index or ABSENT (fail-closed, stated in words);
//   • every authored texture role the .mat declares (the role name is authored data, never guessed
//     from a file name) and whether that texture resolves in the index;
//   • and — the whole point — WHICH VALUES ARE AUTHORED and which are stand-ins the tool
//     substituted or interpreted when it built the glTF PBR material (docs/FORMATS.md §6.1).
// A global cross-reference ("everything else that uses this record") is NOT computed here rather
// than guessed cheaply; the report says so instead of implying a number it did not measure.
namespace MaterialReport {

QString explainModel(AssetStore& store, const QString& modelPath);

}  // namespace MaterialReport
