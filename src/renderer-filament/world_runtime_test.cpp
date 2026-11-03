#include "world_api.h"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <vector>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>
int main(int argc,char**argv){
    if(argc!=4)return 2;auto dll=LoadLibraryA(argv[1]);if(!dll)return 3;
    auto create=reinterpret_cast<decltype(&ofw_create)>(GetProcAddress(dll,"ofw_create"));auto render=reinterpret_cast<decltype(&ofw_render)>(GetProcAddress(dll,"ofw_render"));auto destroy=reinterpret_cast<decltype(&ofw_destroy)>(GetProcAddress(dll,"ofw_destroy"));auto error=reinterpret_cast<decltype(&ofw_error)>(GetProcAddress(dll,"ofw_error"));if(!create||!render||!destroy||!error)return 4;
    void*context=create(argv[2],argv[3]);if(!context){printf("%s\n",error());return 5;}
    float identity[16]={};for(int i=0;i<16;i++)identity[i]=i%5==0?1.f:0.f;
    float projection[16]={};projection[0]=projection[5]=1;projection[10]=-11.f/9;projection[11]=-1;projection[14]=-20.f/9;
    float inverse[16]={};inverse[0]=inverse[5]=1;inverse[11]=-9.f/20;inverse[14]=-1;inverse[15]=11.f/20;
    float white[]={1,1,1,1},zero[]={0,0,0},screen[]={320,192,0,0},drawViewport[]={0,0,320,192},one=1,instance=0;
    std::vector<OFWParameter> parameters;
    auto param=[&](const char*name,uint32_t type,const float*values){OFWParameter p={};p.name=name;p.type=type;p.count=1;p.values=values;parameters.push_back(p);};
    param("os_color",OFW_FLOAT4,white);param("os_glow",OFW_FLOAT3,zero);param("os_mv",OFW_MAT4,identity);param("os_p",OFW_MAT4,projection);param("os_invProjection",OFW_MAT4,inverse);param("os_projection",OFW_MAT4,projection);param("os_normalMatrix",OFW_MAT4,identity);param("os_viewport",OFW_FLOAT4,screen);param("os_instanceID",OFW_FLOAT,&instance);param("os_writeDepth",OFW_FLOAT,&one);
    param("os_drawViewport",OFW_FLOAT4,drawViewport);
    auto plane=[](float x0,float y0,float x1,float y1,float z){std::vector<OFWVertex> vertices(4);for(int i=0;i<4;i++){auto&v=vertices[i];v.position[0]=i&1?x1:x0;v.position[1]=i&2?y1:y0;v.position[2]=z;v.normal[2]=1;v.attributes[0][2]=1;v.attributes[0][3]=8;std::memcpy(v.attributes[1],v.position,12);}return vertices;};
    auto receiver=plane(-3,-2,3,2,-5),blocker=plane(-.5f,-.5f,.5f,.5f,-3);uint32_t indices[]={0,1,2,1,3,2};OFWDraw draws[2]={};
    for(int i=0;i<2;i++){auto&d=draws[i];d.geometryKey=d.instanceKey=i+1;d.material="Model__Model";d.vertexCount=4;d.indexCount=6;d.vertices=i?blocker.data():receiver.data();d.indices=indices;d.parameters=parameters.data();d.parameterCount=parameters.size();d.depthTest=d.depthWrite=d.colorWrite=1;d.priority=4;d.center[2]=i?-3:-5;d.extent[0]=3;d.extent[1]=2;d.extent[2]=.01f;}
    OFLight light={{2,1,-1},{1,1,1},6000000,0};OFWFrame frame={};frame.abiVersion=OFW_ABI;frame.width=320;frame.height=192;frame.drawCount=2;frame.draws=draws;frame.lightCount=1;frame.lights=&light;std::memcpy(frame.projection,projection,64);
    std::vector<uint8_t> rgba(320*192*4);std::vector<float> depth(320*192);
    bool ok=render(context,&frame,rgba.data(),depth.data())!=0;if(!ok)printf("%s\n",error());int at=(76*320+96)*4;int off=rgba[at];float z=depth[76*320+96];frame.shadowLights=1;
    if(!render(context,&frame,rgba.data(),depth.data())){printf("%s\n",error());ok=false;}int on=rgba[at];printf("WORLD native material: shadow off=%d on=%d depth=%.9f\n",off,on,z);ok=ok&&off>30&&on<off/2&&std::abs(z-8.f/9)<1e-5f;
    auto reference=rgba;
    if(!render(context,&frame,rgba.data(),nullptr))ok=false;
    int difference=0;double totalDifference=0;for(size_t i=0;i<rgba.size();i++){int delta=std::abs(int(rgba[i])-int(reference[i]));difference=std::max(difference,delta);totalDifference+=delta;}
    double meanDifference=totalDifference/rgba.size();
    // Low-resolution SSAO rotates its sample pattern between frames. Bound
    // both its local variation and the whole image's mean difference.
    ok=ok&&difference<=8&&meanDifference<.25;printf("%s color-only export matches depth-export color (max %d mean %.4f /255)\n",ok?"PASS":"FAIL",difference,meanDifference);
    // A material can remain inactive while still holding a texture binding.
    // Exercise past both the texture GC age and the instance GC age, then
    // reactivate it. The old cache crashed on commit after only five frames.
    std::vector<OFWParameter> textured;
    for(auto p:parameters)if(std::strcmp(p.name,"os_color")&&std::strcmp(p.name,"os_glow"))textured.push_back(p);
    float glowAlpha[]={0,0,0,1};uint8_t texel[]={255,255,255,255};
    OFWParameter glow={};glow.name="os_glowAlpha";glow.type=OFW_FLOAT4;glow.count=1;glow.values=glowAlpha;textured.push_back(glow);
    OFWParameter tex={};tex.name="os_tex";tex.type=OFW_TEXTURE2D;tex.count=1;tex.texture={101,1,1,1,OFW_TEXTURE2D,texel};textured.push_back(tex);
    OFWDraw texturedDraw=draws[0];texturedDraw.material="ModelTex__ModelTex";texturedDraw.instanceKey=101;texturedDraw.parameters=textured.data();texturedDraw.parameterCount=textured.size();
    frame.shadowLights=0;frame.drawCount=1;frame.draws=&texturedDraw;
    if(!render(context,&frame,rgba.data(),nullptr))ok=false;
    frame.draws=&draws[0];
    for(int i=0;i<130&&ok;i++)if(!render(context,&frame,rgba.data(),depth.data())){printf("%s\n",error());ok=false;}
    frame.draws=&texturedDraw;if(!render(context,&frame,rgba.data(),depth.data()))ok=false;
    ok=ok&&rgba[(96*320+160)*4]>20;
    printf("%s inactive texture/material lifetime across 130 frames\n",ok?"PASS":"FAIL");
    destroy(context);FreeLibrary(dll);printf("%s shared-world geometry, original shader adapter, cross-object point shadow and depth test\n",ok?"PASS":"FAIL");return ok?0:6;
}
