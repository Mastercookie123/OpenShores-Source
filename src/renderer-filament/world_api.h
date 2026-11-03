#pragma once
#include "filament_api.h"
enum { OFW_ABI=2 };
enum OFWType {OFW_FLOAT=1,OFW_FLOAT2,OFW_FLOAT3,OFW_FLOAT4,OFW_INT,OFW_INT2,OFW_INT3,OFW_INT4,OFW_BOOL,OFW_MAT2,OFW_MAT3,OFW_MAT4,OFW_TEXTURE2D,OFW_TEXTURE_ARRAY,OFW_TEXTURE_CUBE};
typedef struct OFWVertex {float position[3],normal[3],attributes[8][4],corner[2];} OFWVertex;
typedef struct OFWTexture {uint64_t key;uint32_t width,height,layers,kind;const uint8_t* rgba;} OFWTexture;
typedef struct OFWParameter {const char* name;uint32_t type,count;const float* values;OFWTexture texture;} OFWParameter;
typedef struct OFWDraw {
    uint64_t geometryKey,instanceKey;const char* material;
    uint32_t vertexCount,indexCount,primitive,parameterCount;
    const OFWVertex* vertices;const uint32_t* indices;const OFWParameter* parameters;
    float center[3],extent[3];
    uint32_t group,blend,depthTest,depthWrite,colorWrite,cull,priority,scissor;
    int32_t scissorBox[4];
} OFWDraw;
typedef struct OFWFrame {
    uint32_t abiVersion,width,height,drawCount,lightCount,shadowLights;
    const OFWDraw* draws;const OFLight* lights;
    float projection[16],ambient[3],clearColor[4],fogColor[3],fogDensity;
    uint32_t lightingMode; // 1: matched preview/studio, 0: live day/night
    float environmentRotation[9]; // cubemap Y-up space into eye space
} OFWFrame;
#ifdef __cplusplus
extern "C" {
#endif
OF_EXPORT void* ofw_create(const char* materialDirectory,const char* bootstrapMaterials);
// depth may be null when the caller consumes only the completed color view.
OF_EXPORT int ofw_render(void*,const OFWFrame*,uint8_t* rgba,float* depth);
OF_EXPORT void ofw_destroy(void*);
OF_EXPORT const char* ofw_error(void);
#ifdef __cplusplus
}
#endif
