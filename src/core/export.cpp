#include "export.h"

#include <algorithm>
#include <limits>
#include <locale>
#include <sstream>

namespace cp {

namespace {
std::ostringstream makeStream() {
    std::ostringstream os;
    os.imbue(std::locale::classic());  // always '.' as decimal separator
    os.setf(std::ios::fixed);
    os.precision(6);
    return os;
}
void bounds(const std::vector<ExportPolyline>& polys, double& x0, double& y0, double& x1, double& y1) {
    x0 = y0 = std::numeric_limits<double>::infinity();
    x1 = y1 = -std::numeric_limits<double>::infinity();
    for (const auto& p : polys)
        for (const Pt& q : p.pts) {
            x0 = std::min(x0, q.x); y0 = std::min(y0, q.y);
            x1 = std::max(x1, q.x); y1 = std::max(y1, q.y);
        }
    if (!(x1 >= x0)) x0 = y0 = x1 = y1 = 0;
}
}  // namespace

std::string makeDXF(const std::vector<ExportPolyline>& polys) {
    auto os = makeStream();
    double x0, y0, x1, y1;
    bounds(polys, x0, y0, x1, y1);
    os << "0\nSECTION\n2\nHEADER\n";
    os << "9\n$ACADVER\n1\nAC1009\n";
    os << "9\n$INSUNITS\n70\n4\n";      // millimetres
    os << "9\n$MEASUREMENT\n70\n1\n";   // metric
    os << "9\n$EXTMIN\n10\n" << x0 << "\n20\n" << y0 << "\n30\n0.0\n";
    os << "9\n$EXTMAX\n10\n" << x1 << "\n20\n" << y1 << "\n30\n0.0\n";
    os << "0\nENDSEC\n";
    // Layer table.
    std::vector<std::pair<std::string, int>> layers;
    for (const auto& p : polys)
        if (std::none_of(layers.begin(), layers.end(), [&](auto& l) { return l.first == p.layer; }))
            layers.push_back({p.layer, p.color});
    os << "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n" << layers.size() << "\n";
    for (const auto& l : layers)
        os << "0\nLAYER\n2\n" << l.first << "\n70\n0\n62\n" << l.second << "\n6\nCONTINUOUS\n";
    os << "0\nENDTAB\n0\nENDSEC\n";
    os << "0\nSECTION\n2\nENTITIES\n";
    for (const auto& p : polys) {
        if (p.pts.size() < 2) continue;
        os << "0\nPOLYLINE\n8\n" << p.layer << "\n66\n1\n70\n" << (p.closed ? 1 : 0)
           << "\n10\n0.0\n20\n0.0\n30\n0.0\n";
        for (const Pt& q : p.pts)
            os << "0\nVERTEX\n8\n" << p.layer << "\n10\n" << q.x << "\n20\n" << q.y << "\n30\n0.0\n";
        os << "0\nSEQEND\n8\n" << p.layer << "\n";
    }
    os << "0\nENDSEC\n0\nEOF\n";
    return os.str();
}

std::string makeSVG(const std::vector<ExportPolyline>& polys) {
    auto os = makeStream();
    os.precision(4);
    double x0, y0, x1, y1;
    bounds(polys, x0, y0, x1, y1);
    const double m = 2.0, W = (x1 - x0) + 2 * m, H = (y1 - y0) + 2 * m;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << W << "mm\" height=\"" << H << "mm\" viewBox=\"0 0 "
       << W << " " << H << "\">\n";
    for (const auto& p : polys) {
        if (p.pts.size() < 2) continue;
        os << "  <path id=\"" << p.layer << "\" fill=\"none\" stroke=\"black\" stroke-width=\"0.1\" d=\"";
        for (size_t i = 0; i < p.pts.size(); ++i)
            os << (i ? " L " : "M ") << (p.pts[i].x - x0 + m) << " " << (y1 - p.pts[i].y + m);
        if (p.closed) os << " Z";
        os << "\"/>\n";
    }
    os << "</svg>\n";
    return os.str();
}

std::string makeCSV(const std::vector<ExportPolyline>& polys) {
    auto os = makeStream();
    os.precision(4);
    os << "x_mm,y_mm\n";
    bool first = true;
    for (const auto& p : polys) {
        if (!first) os << "\n";
        first = false;
        for (const Pt& q : p.pts) os << q.x << "," << q.y << "\n";
    }
    return os.str();
}

}  // namespace cp
