// Host stand-ins for the few out-of-line Skia functions the routing code reaches through
// SkRect.h inlines (quad_tree in commonOsmAndCore.h). Bodies copied from
// core/externals/skia/upstream.patched/src/core/SkRect.cpp, so behaviour is identical.
#include "SkRect.h"

#include <algorithm>

bool SkIRect::intersect(const SkIRect& a, const SkIRect& b)
{
    SkIRect tmp = {std::max(a.fLeft, b.fLeft), std::max(a.fTop, b.fTop),
                   std::min(a.fRight, b.fRight), std::min(a.fBottom, b.fBottom)};
    if (tmp.isEmpty()) return false;
    *this = tmp;
    return true;
}
