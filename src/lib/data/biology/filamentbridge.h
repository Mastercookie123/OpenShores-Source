#pragma once
class Au3dsGLSLModelInstance;
// Geometry may be destroyed on a loader thread. GPU release is deferred until
// the owning GL/Filament context is current on its render thread.
void FilamentForgetModel(const Au3dsGLSLModelInstance* model);
