#pragma once
#include <QString>
#include <functional>

class GLModelWidget;

// Preview-capture helpers behind the Export menu's GIF entries (template §15/§26). The GIF budget
// ladder (palette → dither-off → aimed downscale, ship the smallest attempt) is copied verbatim from
// D4AssetBrowser's ExportCapture — it was "learned the expensive way" and is engine-agnostic. The
// CAPTURE drivers are POE2-native: they drive GLModelWidget::renderToImage + orbitYaw + setAnimTime
// (POE2 animation is time-based; there is no cloth sim, so D4's settle/warm-up machinery is dropped).
namespace ExportCapture {

// Progress callback for the multi-frame captures: (framesDone, framesTotal) after each frame; return
// false to cancel (the capture aborts, restores viewport state and returns false).
using ProgressFn = std::function<bool(int done, int total)>;

// GIF options, resolved by the caller (from Config or the at-export dialog) and passed explicitly so
// the same functions serve the UI and headless tests. Defaults match Config's GIF defaults.
struct GifOptions {
    int  fps = 25;               // 1..60
    int  turntableFrames = 48;   // 8..240 requested; snapped to whole clip loops when a clip plays
    int  scalePercent = 100;     // 25..100 — GIF is not a detail medium, so downscale only
    int  maxColors = 256;        // 2..256 — fewer = smaller, coarser
    bool dither = true;          // ordered Bayer 8x8 (position-only, so no frame-to-frame shimmer)
    bool transparentBg = false;  // 1-bit-alpha GIF vs a dark backdrop
    bool cropToModel = false;    // trim to the model's silhouette across the whole sequence
    bool optimize = false;       // re-encode down toward targetMB (ships the smallest attempt)
    int  targetMB = 10;          // 1..200
};

// One full 360° camera revolution → looping GIF. If a clip is PLAYING, the pose plays through the
// spin and the frame count is snapped so a whole number of clip cycles lands in one revolution
// (seamless wrap); paused/stopped orbits the static pose.
bool turntableGif(GLModelWidget* view, const QString& path, const GifOptions& opt, const ProgressFn& progress = {});

// The current clip sampled at `fps` across its whole duration, one GIF frame each → the GIF loops the
// animation at ~its authored speed. Returns false if no clip is loaded on the preview.
bool animLoopGif(GLModelWidget* view, const QString& path, const GifOptions& opt, const ProgressFn& progress = {});

}
