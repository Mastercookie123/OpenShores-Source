#pragma once
#include <stdint.h>
// Stable C boundary: no Qt or C++ standard-library types cross the MSVC/MinGW ABI.
enum { OF_ABI_VERSION=2,OF_BONES=61,OF_MAX_LIGHTS=16 };
typedef struct OFVertex {
    float position[3],normal[3],uv[2],weights[4];
    uint16_t joints[4];float top;
} OFVertex;
typedef struct OFTexture { uint64_t key;uint32_t width,height;const uint8_t* rgba; } OFTexture;
typedef struct OFMesh {
    uint64_t key;uint32_t vertexCount;const OFVertex* vertices;
    OFTexture skin,clothes;
    float skinOffset[4],clothesOffsetWear[3],roughness,metallic;
} OFMesh;
typedef struct OFLight { float position[3],color[3],intensity;uint32_t directional; } OFLight;
typedef struct OFFrame {
    uint32_t abiVersion,width,height,meshCount,lightCount;
    const OFMesh* meshes;const OFLight* lights;
    float projection[16],modelView[16],bones[OF_BONES*16],ambient[3];
    uint32_t shadowLights;
} OFFrame;
#if defined(_WIN32) && defined(OF_BUILD_DLL)
#define OF_EXPORT __declspec(dllexport)
#else
#define OF_EXPORT
#endif
#ifdef __cplusplus
extern "C" {
#endif
OF_EXPORT void* of_create(const char* materialDirectory);
// Synchronous output: tightly packed bottom-row-first, premultiplied sRGB RGBA8
// and original OpenGL window-depth floats [0,1]. Buffers belong to the caller.
OF_EXPORT int of_draw(void* context,const OFFrame* frame,uint8_t* rgba,float* depth);
OF_EXPORT void of_destroy(void* context);
OF_EXPORT void of_release(void* context,uint64_t meshKey);
OF_EXPORT const char* of_error(void);
#ifdef __cplusplus
}
#endif
