#ifndef AuBodySurface_H
#define AuBodySurface_H

#include "autriangle.h"
#include <QList>

// Render-only refinement of cached creature meshes. DNA, collision geometry
// and the on-disk body-part format remain unchanged. Configure before loading.
class AUMATH_DLL AuBodySurface
{
public:
    static void SetDetail(int levels);
    static int Detail();
    static void SmoothNormals(QList<AuTriBody>& triangles);
    static void Refine(QList<AuTriBody>& triangles, int levels);
};

#endif
