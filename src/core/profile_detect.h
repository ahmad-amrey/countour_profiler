// Extraction of the bent wire / metal strip as a 2D polyline in world (mm)
// coordinates, given the image-to-world calibration.
#pragma once
#include <string>
#include <vector>

#include "geom.h"
#include "grid_detect.h"
#include "image.h"

namespace cp {

enum class DetectMode {
    PatternSubtraction = 0,  // anything that differs from the periodic background pattern
    Color = 1,               // pixels close to a picked colour
};
enum class ProfileOutput { Centerline = 0, Outline = 1 };

struct ProfileParams {
    DetectMode mode = DetectMode::PatternSubtraction;
    ProfileOutput output = ProfileOutput::Centerline;
    PatternType pattern = PatternType::LineGrid;
    double squareMM = 10;
    double threshold = 0;          // 0 = automatic
    uint8_t color[3] = {200, 200, 200};
    double colorTol = 60;
    double smoothMM = 0.3;
    double simplifyMM = 0.05;
    double resolution = 0;         // px/mm for analysis, 0 = automatic
    bool hasSeed = false;          // pick the component nearest to this world point
    Pt seed;
};

struct ProfileResult {
    bool ok = false;
    std::vector<Pt> pts;  // world mm
    bool closed = false;
    std::string message;
    // Diagnostic mask over the analysed world rectangle:
    // 0 = background, 1 = above threshold, 2 = selected object.
    std::vector<uint8_t> mask;
    int maskW = 0, maskH = 0;
    RectD maskArea;
    double maskRes = 0;
    double thresholdUsed = 0;
    double thicknessMM = 0;
};

// Hw2i: world (mm) -> image homography. roi: world rectangle to analyse.
// nativePxPerMM: approximate source resolution on the plane (caps analysis res).
ProfileResult detectProfile(const ImageU8& src, const Mat3& Hw2i, const RectD& roi, double nativePxPerMM,
                            const ProfileParams& prm);

}  // namespace cp
