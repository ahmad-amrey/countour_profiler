// Shared application state (one open photo).
#pragma once
#include <QImage>
#include <QString>
#include <vector>

#include "calib.h"
#include "geom.h"
#include "grid_detect.h"
#include "image.h"

struct RefPoint {
    cp::Pt img;            // image pixel (full resolution)
    cp::Pt world;          // millimetres on the pattern plane
    bool fromGrid = false; // created by grid detection (world follows square size)
    int gi = 0, gj = 0;    // lattice index when fromGrid
    double errMM = 0;      // residual after calibration
};

struct Document {
    QString path;
    QImage image;          // RGB888, EXIF orientation applied
    cp::ImageU8 core;      // same pixels for the core library
    cp::ImageF gray;

    cp::GridResult grid;
    std::vector<RefPoint> refs;
    cp::Calibration cal;

    cp::RectD detectRoi;   // world rectangle searched for the profile
    std::vector<cp::Pt> profile;  // world mm, image-like axes (Y down)
    bool closed = false;

    // Rectified (top-down) rendering.
    QImage rectified;
    cp::RectD rectArea;
    double rectRes = 0;    // px per mm

    // Detection diagnostics overlay (rectified space).
    QImage maskImage;
    cp::RectD maskArea;

    double squareMM = 10;
    bool hasImage() const { return !image.isNull(); }
};
