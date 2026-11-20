// Synthetic perspective photos with known ground truth, used by the self-test
// and by the GUI's "Create sample image" command.
#pragma once
#include <vector>

#include "geom.h"
#include "grid_detect.h"
#include "image.h"

namespace cp {

struct SynthParams {
    PatternType pattern = PatternType::LineGrid;
    bool brightLines = true;  // line grid: light lines on a dark mat (cutting mat)
    double squareMM = 10;
    int cols = 16, rows = 12;
    int imgW = 2400, imgH = 1800;
    double wireDiameterMM = 0.6;
    float wireColor[3] = {205, 205, 212};  // silver
    unsigned seed = 1;
    // Image positions (fractions of W/H) of the board corners TL, TR, BR, BL.
    double quad[8] = {0.14, 0.18, 0.88, 0.10, 0.97, 0.90, 0.05, 0.82};
};

struct SynthResult {
    ImageU8 img;
    Mat3 Hw2i;               // board world (mm, origin at board corner) -> image
    std::vector<Pt> truth;   // wire centre line, world mm
};

SynthResult renderSynthetic(const SynthParams& p);

}  // namespace cp
