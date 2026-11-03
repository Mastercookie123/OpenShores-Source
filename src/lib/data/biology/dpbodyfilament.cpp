// Live hybrid renderer: game DNA geometry + current absolute bone transforms.
// The C DLL owns Vulkan. Only plain data crosses the MSVC/MinGW boundary.
#include "dpbody.h"
#include "filamentbridge.h"
#include "au3dsglsl.h"
#include "aubodysurface.h"
#include "augleffects.h"
#include "../../../renderer-filament/filament_api.h"
#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions_3_2_Core>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <map>
#include <memory>
#include <vector>
#include <array>
#include <limits>
#include <cmath>

// A procedural body uploads one skeleton, then draws several mesh parts.
// The full-scene bridge can reuse that skeleton until the next body begins.
void AuFilamentBodyBegin(int skin,unsigned armor,unsigned helmet);

namespace {
static_assert(sizeof(OFVertex)==60 && sizeof(OFMesh)==112 && sizeof(OFFrame)==4088,"Filament C ABI layout mismatch");
struct API {
    QLibrary library;
    decltype(&of_create) create=nullptr;decltype(&of_draw) draw=nullptr;
    decltype(&of_destroy) destroy=nullptr;decltype(&of_release) release=nullptr;
    decltype(&of_error) error=nullptr;
    API():library(QDir(QCoreApplication::applicationDirPath()).filePath("OpenShoresFilament-r2")) {
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if(!library.load())qFatal("Requested live Filament renderer cannot load: %s",qPrintable(library.errorString()));
        create=reinterpret_cast<decltype(create)>(library.resolve("of_create"));
        draw=reinterpret_cast<decltype(draw)>(library.resolve("of_draw"));
        destroy=reinterpret_cast<decltype(destroy)>(library.resolve("of_destroy"));
        release=reinterpret_cast<decltype(release)>(library.resolve("of_release"));
        error=reinterpret_cast<decltype(error)>(library.resolve("of_error"));
        if(!create||!draw||!destroy||!release||!error)qFatal("Incompatible live Filament DLL");
    }
};
API& api(){static API* instance=new API;return *instance;}
struct Bounds {
    bool used=false;float lo[3],hi[3];
    Bounds(){for(int i=0;i<3;i++){lo[i]=std::numeric_limits<float>::max();hi[i]=-lo[i];}}
    void add(const float* p){used=true;for(int i=0;i<3;i++){lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}}
};
struct Geometry {uint64_t key=0;std::vector<OFVertex> vertices;std::array<Bounds,OF_BONES> bounds;};
struct Image {OFTexture packet={};std::vector<uint8_t> pixels;};
struct State {
    void* native=nullptr;QOpenGLShaderProgram composite;GLuint vao=0,color=0,depth=0;
    std::map<const Au3dsGLSLModelInstance*,Geometry> geometry;
    std::map<GLuint,Image> images;std::vector<uint64_t> retired;
    std::vector<uint8_t> rgba;std::vector<float> z;bool announced=false,announcedWorldShadows=false;uint64_t frames=0;
    ~State(){if(native)api().destroy(native);}
};
QMutex filamentMutex;
std::map<QOpenGLContext*,std::unique_ptr<State>> states;
uint64_t nextKey=1;
State& state(AuGLEffects& gl) {
    auto* context=QOpenGLContext::currentContext();auto& ptr=states[context];if(ptr)return *ptr;
    ptr.reset(new State);auto& s=*ptr;
    QByteArray directory=QDir(QCoreApplication::applicationDirPath()).filePath("filament-materials").toUtf8();
    s.native=api().create(directory.constData());if(!s.native)qFatal("Filament creation failed: %s",api().error());
    const char* vertex="#version 150\nout vec2 uv;void main(){vec2 p=vec2(gl_VertexID&1,(gl_VertexID>>1)&1);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}";
    const char* fragment="#version 150\nin vec2 uv;out vec4 outColor;uniform sampler2D bodyColor;uniform sampler2D bodyDepth;uniform vec4 fog;uniform vec4 depthProjection;void main(){vec4 c=texture(bodyColor,uv);if(c.a<0.1)discard;float d=texture(bodyDepth,uv).r;gl_FragDepth=d;float n=d*2.0-1.0;float ze=(depthProjection.y-n*depthProjection.w)/(n*depthProjection.z-depthProjection.x);float cz=max(depthProjection.x*ze+depthProjection.y,0.0);float f=1.0-exp(-pow(cz*fog.w,2.0));c.rgb=mix(c.rgb,fog.rgb*c.a,f);outColor=c;}";
    if(!s.composite.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex)||!s.composite.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment)||!s.composite.link())
        qFatal("Filament compositor shader: %s",qPrintable(s.composite.log()));
    gl.glGenVertexArrays(1,&s.vao);gl.glGenTextures(1,&s.color);gl.glGenTextures(1,&s.depth);
    QObject::connect(context,&QOpenGLContext::aboutToBeDestroyed,context,[context](){
        QMutexLocker lock(&filamentMutex);auto it=states.find(context);if(it==states.end())return;
        auto& s=*it->second;
        if(QOpenGLContext::currentContext()==context){QOpenGLFunctions_3_2_Core gl;gl.initializeOpenGLFunctions();gl.glDeleteVertexArrays(1,&s.vao);gl.glDeleteTextures(1,&s.color);gl.glDeleteTextures(1,&s.depth);}
        states.erase(it);
    },Qt::DirectConnection);
    return s;
}
Geometry& geometry(State& s,Au3dsGLSLModelInstance* part) {
    auto it=s.geometry.find(part);if(it!=s.geometry.end())return it->second;
    Geometry g;g.key=nextKey++;
    Au3dsGLSLTriBodyList triangles;part->AppendBodyTriangles(triangles);AuBodySurface::SmoothNormals(triangles);
    g.vertices.reserve(size_t(triangles.size())*3);
    for(const auto& tri:triangles)for(const auto* v:{&tri.m_V0,&tri.m_V1,&tri.m_V2}) {
        OFVertex p={};p.position[0]=v->x;p.position[1]=v->y;p.position[2]=v->z;
        p.normal[0]=v->m_N.x;p.normal[1]=v->m_N.y;p.normal[2]=v->m_N.z;p.uv[0]=v->m_Tx;p.uv[1]=v->m_Ty;p.top=v->m_TopFace?1:0;
        for(int i=0;i<4;i++){p.weights[i]=v->m_BoneWeight[i];p.joints[i]=p.weights[i]>0?v->m_BoneIndex[i]:0;
            if(p.weights[i]>0 && p.joints[i]<OF_BONES)g.bounds[p.joints[i]].add(p.position);}
        g.vertices.push_back(p);
    }
    return s.geometry.emplace(part,std::move(g)).first->second;
}
OFTexture image(State& s,AuGLEffects& gl,GLuint name) {
    if(!name)return {};auto it=s.images.find(name);if(it!=s.images.end())return it->second.packet;
    GLint active,binding,packBuffer,packAlign,rowLength,skipRows,skipPixels;
    gl.glGetIntegerv(GL_ACTIVE_TEXTURE,&active);gl.glActiveTexture(GL_TEXTURE0);gl.glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    gl.glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&packBuffer);gl.glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    gl.glGetIntegerv(GL_PACK_ALIGNMENT,&packAlign);gl.glGetIntegerv(GL_PACK_ROW_LENGTH,&rowLength);gl.glGetIntegerv(GL_PACK_SKIP_ROWS,&skipRows);gl.glGetIntegerv(GL_PACK_SKIP_PIXELS,&skipPixels);
    gl.glPixelStorei(GL_PACK_ALIGNMENT,1);gl.glPixelStorei(GL_PACK_ROW_LENGTH,0);gl.glPixelStorei(GL_PACK_SKIP_ROWS,0);gl.glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    gl.glBindTexture(GL_TEXTURE_2D,name);GLint width=0,height=0;gl.glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);gl.glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
    Image data;if(width>0&&height>0){data.pixels.resize(size_t(width)*height*4);gl.glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,data.pixels.data());data.packet={nextKey++,uint32_t(width),uint32_t(height),nullptr};}
    gl.glPixelStorei(GL_PACK_ALIGNMENT,packAlign);gl.glPixelStorei(GL_PACK_ROW_LENGTH,rowLength);gl.glPixelStorei(GL_PACK_SKIP_ROWS,skipRows);gl.glPixelStorei(GL_PACK_SKIP_PIXELS,skipPixels);gl.glBindBuffer(GL_PIXEL_PACK_BUFFER,packBuffer);
    gl.glBindTexture(GL_TEXTURE_2D,binding);gl.glActiveTexture(active);
    auto& stored=s.images.emplace(name,std::move(data)).first->second;stored.packet.rgba=stored.pixels.data();return stored.packet;
}
// Restore the entire state touched by the compositor, including PBO/pixel-store
// state. The existing game's GL state cache must see exactly what it left.
void composite(State& s,AuGLEffects& gl,int x,int y,int w,int h,int tw,int th,const OFFrame& frame,bool fogEnabled) {
    GLint program,vao,viewport[4],active,binding[2],depthFunc,srcRGB,dstRGB,srcA,dstA,eqRGB,eqA,unpackBuffer,align,row,skipR,skipP;GLboolean depthMask,colorMask[4];
    gl.glGetIntegerv(GL_CURRENT_PROGRAM,&program);gl.glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);gl.glGetIntegerv(GL_VIEWPORT,viewport);gl.glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    for(int i=0;i<2;i++){gl.glActiveTexture(GL_TEXTURE0+i);gl.glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding[i]);}
    gl.glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);gl.glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);gl.glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);
    gl.glGetIntegerv(GL_BLEND_SRC_RGB,&srcRGB);gl.glGetIntegerv(GL_BLEND_DST_RGB,&dstRGB);gl.glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcA);gl.glGetIntegerv(GL_BLEND_DST_ALPHA,&dstA);gl.glGetIntegerv(GL_BLEND_EQUATION_RGB,&eqRGB);gl.glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eqA);
    GLboolean depthTest=gl.glIsEnabled(GL_DEPTH_TEST),blend=gl.glIsEnabled(GL_BLEND),cull=gl.glIsEnabled(GL_CULL_FACE),srgb=gl.glIsEnabled(GL_FRAMEBUFFER_SRGB);
    gl.glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackBuffer);gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    gl.glGetIntegerv(GL_UNPACK_ALIGNMENT,&align);gl.glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);gl.glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skipR);gl.glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skipP);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT,1);gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,0);gl.glPixelStorei(GL_UNPACK_SKIP_ROWS,0);gl.glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    for(int i=0;i<2;i++){gl.glActiveTexture(GL_TEXTURE0+i);gl.glBindTexture(GL_TEXTURE_2D,i?s.depth:s.color);
        gl.glTexImage2D(GL_TEXTURE_2D,0,i?GL_R32F:GL_RGBA8,tw,th,0,i?GL_RED:GL_RGBA,i?GL_FLOAT:GL_UNSIGNED_BYTE,i?static_cast<void*>(s.z.data()):static_cast<void*>(s.rgba.data()));
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,i?GL_NEAREST:GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,i?GL_NEAREST:GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
    gl.glViewport(x,y,w,h);gl.glEnable(GL_DEPTH_TEST);gl.glDepthFunc(GL_LEQUAL);gl.glDepthMask(GL_TRUE);gl.glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    gl.glEnable(GL_BLEND);gl.glBlendFuncSeparate(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);gl.glBlendEquation(GL_FUNC_ADD);gl.glDisable(GL_CULL_FACE);gl.glDisable(GL_FRAMEBUFFER_SRGB);
    s.composite.bind();s.composite.setUniformValue("bodyColor",0);s.composite.setUniformValue("bodyDepth",1);
    float fr=0,fg=0,fb=0,fd=0;if(fogEnabled)gl.CurrentFog(fr,fg,fb,fd);
    s.composite.setUniformValue("fog",QVector4D(fr,fg,fb,fd));
    s.composite.setUniformValue("depthProjection",QVector4D(frame.projection[10],frame.projection[14],frame.projection[11],frame.projection[15]));
    gl.glBindVertexArray(s.vao);gl.glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    gl.glUseProgram(program);gl.glBindVertexArray(vao);gl.glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);gl.glDepthFunc(depthFunc);gl.glDepthMask(depthMask);gl.glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
    gl.glBlendFuncSeparate(srcRGB,dstRGB,srcA,dstA);gl.glBlendEquationSeparate(eqRGB,eqA);
    for(auto p:{std::make_pair(GL_DEPTH_TEST,depthTest),{GL_BLEND,blend},{GL_CULL_FACE,cull},{GL_FRAMEBUFFER_SRGB,srgb}}){if(p.second)gl.glEnable(p.first);else gl.glDisable(p.first);}
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT,align);gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,row);gl.glPixelStorei(GL_UNPACK_SKIP_ROWS,skipR);gl.glPixelStorei(GL_UNPACK_SKIP_PIXELS,skipP);gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpackBuffer);
    for(int i=0;i<2;i++){gl.glActiveTexture(GL_TEXTURE0+i);gl.glBindTexture(GL_TEXTURE_2D,binding[i]);}gl.glActiveTexture(active);
}
}
void FilamentForgetModel(const Au3dsGLSLModelInstance* model) {
    QMutexLocker lock(&filamentMutex);for(auto& pair:states){auto& s=*pair.second;auto it=s.geometry.find(model);if(it!=s.geometry.end()){s.retired.push_back(it->second.key);s.geometry.erase(it);}}
}
bool DpBody::RenderFilament(const AuMatrix4x4& projection,const AuMatrix4x4& model,AuGLEffects& gl,
    bool headless,bool headOnly,int skin,int clothes,int armor,int helmet,bool fog,const AuPoint* globe,const float* shadowAngles) {
    if(qgetenv("OPENSHORES_RENDERER")=="filament-world"){AuFilamentBodyBegin(m_DNA.SkinType,HasBodyPowerArmorOn()?armor:0,HasHelmetPowerArmorOn()?helmet:0);return false;}
    if(qgetenv("OPENSHORES_RENDERER")!="filament")return false;
    QMutexLocker lock(&filamentMutex);auto& s=state(gl);for(auto key:s.retired)api().release(s.native,key);s.retired.clear();
    QElapsedTimer timer;timer.start();std::vector<OFMesh> meshes;std::vector<const Geometry*> geometries;
    bool clothUpper=false;switch(m_eBodyClothesType){
        case CID_Underwear:case CID_LeatherClothes:case CID_JeansAndShirt:case CID_BlackUniform:case CID_BlueUniform:case CID_BrownUniform:case CID_GoldUniform:case CID_GreenUniform:case CID_GreyUniform:case CID_OrangeUniform:case CID_PinkUniform:case CID_RedUniform:case CID_TealUniform:case CID_WhiteUniform:case CID_ParcXUniform:case CID_GreyBDU:case CID_KhakiBDU:case CID_OliveDrabBDU:clothUpper=true;break;default:break;}
    for(auto* part:m_Models){QString name=part->FileName();bool head=name.contains("Head"),neck=name.contains("Neck"),wing=name.contains("Wing"),tail=name.contains("Tail")||name.contains("Tip");
        if((headOnly&&!head)||(headless&&(head||neck)))continue;
        auto& g=geometry(s,part);if(g.vertices.empty())continue;OFMesh p={};p.key=g.key;p.vertexCount=g.vertices.size();p.vertices=g.vertices.data();p.roughness=.66f;
        for(int i=0;i<4;i++)p.skinOffset[i]=m_skinOffset[i];GLuint skinID=skin,clothesID=0;
        if((head||neck)&&HasHelmetPowerArmorOn()){
            skinID=0;clothesID=helmet;p.clothesOffsetWear[2]=1;p.roughness=.38f;p.metallic=.35f;
            if(neck){int type=m_eHelmetClothesType==CID_MagmiumPowerArmorHelmet?1:m_eHelmetClothesType==CID_VulciumPowerArmorHelmet?2:m_eHelmetClothesType==CID_VacSuitHelmet?3:0;clothesID=armor?armor:m_glTexArmor[type];}
        }else if(wing){skinID=m_glTexWings;float v=m_DNA.WingSubType*128.f/1024.f;if(v>0)v=1-v;p.skinOffset[0]=p.skinOffset[2]=0;p.skinOffset[1]=p.skinOffset[3]=v;
        }else if(!head&&!neck&&!tail){
            if(HasBodyPowerArmorOn()){skinID=0;clothesID=armor;p.clothesOffsetWear[2]=1;p.roughness=.38f;p.metallic=.35f;}
            else if(clothes&&m_eBodyClothesType!=CID_Invalid){clothesID=clothes;p.clothesOffsetWear[1]=clothUpper?1:0;p.clothesOffsetWear[2]=2;}
        }
        p.skin=image(s,gl,skinID);p.clothes=image(s,gl,clothesID);meshes.push_back(p);geometries.push_back(&g);
    }
    if(meshes.empty())return true;
    OFFrame f={};f.abiVersion=OF_ABI_VERSION;f.meshCount=meshes.size();f.meshes=meshes.data();
    std::array<AuMatrix4x4,OF_BONES> bones;for(int b=0;b<OF_BONES;b++){bones[b]=RenderBone(b);for(int i=0;i<16;i++)f.bones[b*16+i]=bones[b][i];}
    float left=1,right=-1,bottom=1,top=-1;bool crossesEye=false;
    for(auto* g:geometries)for(int b=0;b<OF_BONES;b++){auto& box=g->bounds[b];if(!box.used)continue;auto m=projection*model*bones[b];
        for(int c=0;c<8;c++){double x=c&1?box.hi[0]:box.lo[0],y=c&2?box.hi[1]:box.lo[1],z=c&4?box.hi[2]:box.lo[2];double w=m[3]*x+m[7]*y+m[11]*z+m[15];if(w<=1e-6){crossesEye=true;continue;}
            float px=(m[0]*x+m[4]*y+m[8]*z+m[12])/w,py=(m[1]*x+m[5]*y+m[9]*z+m[13])/w;left=std::min(left,px);right=std::max(right,px);bottom=std::min(bottom,py);top=std::max(top,py);}}
    if(crossesEye){left=bottom=-1;right=top=1;}
    GLint viewport[4];gl.glGetIntegerv(GL_VIEWPORT,viewport);if(viewport[2]<=0||viewport[3]<=0)return true;
    if(right<=-1||left>=1||top<=-1||bottom>=1)return true;
    left=std::max(-1.f,left);right=std::min(1.f,right);bottom=std::max(-1.f,bottom);top=std::min(1.f,top);
    int x=std::max(0,int(std::floor((left+1)*.5*viewport[2]))-2),y=std::max(0,int(std::floor((bottom+1)*.5*viewport[3]))-2);
    int xmax=std::min(viewport[2],int(std::ceil((right+1)*.5*viewport[2]))+2),ymax=std::min(viewport[3],int(std::ceil((top+1)*.5*viewport[3]))+2);int w=xmax-x,h=ymax-y;if(w<=0||h<=0)return true;
    left=2.f*x/viewport[2]-1;right=2.f*xmax/viewport[2]-1;bottom=2.f*y/viewport[3]-1;top=2.f*ymax/viewport[3]-1;
    float scale=std::min(1.f,1024.f/std::max(w,h));f.width=std::max(64,int(std::ceil(w*scale/64))*64);f.height=std::max(64,int(std::ceil(h*scale/64))*64);
    for(int c=0;c<4;c++)for(int r=0;r<4;r++){int i=c*4+r;f.modelView[i]=model[i];f.projection[i]=projection[i];if(r==0)f.projection[i]=2/(right-left)*projection[i]-(right+left)/(right-left)*projection[c*4+3];if(r==1)f.projection[i]=2/(top-bottom)*projection[i]-(top+bottom)/(top-bottom)*projection[c*4+3];}
    float alpha;gl.CurrentAmbientLight(f.ambient[0],f.ambient[1],f.ambient[2],alpha);
    std::vector<OFLight> lights;
    AuPoint eye=model.Project(AuPoint(0,0,0));
    for(int i=0;i<MAX_SUNS;i++){auto& sun=gl.Sun(i);if(!sun.m_Enabled)continue;AuPoint delta=AuPoint(sun.m_LocX,sun.m_LocY,sun.m_LocZ)-eye;double distance2=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;if(distance2<1e-8||sun.m_Attenuation<=0)continue;
        float modulation=1;
        if(globe){AuPoint up=eye-*globe;double length=std::sqrt(up.x*up.x+up.y*up.y+up.z*up.z);if(length>1e-8){double dot=(delta.x*up.x+delta.y*up.y+delta.z*up.z)/(std::sqrt(distance2)*length);double angle=std::acos(std::max(-1.,std::min(1.,dot))),disk=std::atan2(sun.m_DiskRadius,std::sqrt(distance2));modulation=std::max(0.,std::max(dot,std::max(std::cos(angle+disk),std::cos(angle-disk))));}}
        if(shadowAngles&&sun.m_ShadowCount>0){AuPoint ps=eye-AuPoint(sun.m_LocX,sun.m_LocY,sun.m_LocZ);double sx=ps.x*sun.m_XVec.x+ps.y*sun.m_XVec.y+ps.z*sun.m_XVec.z,sy=ps.x*sun.m_YVec.x+ps.y*sun.m_YVec.y+ps.z*sun.m_YVec.z;modulation*=std::max(0.,std::min(1.,2*std::sin((std::atan2(sy,sx)-shadowAngles[i])*sun.m_ShadowCount)));}
        OFLight l={{float(-delta.x),float(-delta.y),float(-delta.z)},{sun.m_ColorR,sun.m_ColorG,sun.m_ColorB},float(std::min(160000.,80000./(sun.m_Attenuation*distance2))*modulation),1};
        // Filament supports one directional sun. Select the strongest star.
        if(lights.empty())lights.push_back(l);else if(l.intensity>lights[0].intensity)lights[0]=l;}
    for(int i=0;i<gl.MaxLights()&&lights.size()<OF_MAX_LIGHTS;i++){auto& lamp=gl.Light(i);if(!lamp.m_bEnabled)continue;float attenuation=1.f/std::max(.001f,lamp.m_kL*5+lamp.m_kQ*25);
        OFLight l={{float(lamp.m_Position.x),float(lamp.m_Position.y),float(lamp.m_Position.z)},{lamp.m_ColorR,lamp.m_ColorG,lamp.m_ColorB},float(4*3.141592653589793*25*attenuation*20000),0};lights.push_back(l);}
    f.shadowLights=AuGLSLShader::Shadows()&&AuGLSLShader::ShadowMaps()?AuGLSLShader::ShadowLights():0;
    f.lightCount=lights.size();f.lights=lights.data();s.rgba.resize(size_t(f.width)*f.height*4);s.z.resize(size_t(f.width)*f.height);
    if(!api().draw(s.native,&f,s.rgba.data(),s.z.data()))qFatal("Live Filament frame failed: %s",api().error());
    composite(s,gl,viewport[0]+x,viewport[1]+y,w,h,f.width,f.height,f,fog);
    if(!s.announced){qInfo("LIVE_FILAMENT active: Vulkan, procedural meshes, 61 live bones, original palettes/clothing, depth compositor");s.announced=true;}
    if(!s.announcedWorldShadows&&gl.GLSL().LightShadowMaps().m_Count>0){qInfo("LIVE_FILAMENT world lamp maps=%d native shadow lamps=%u",gl.GLSL().LightShadowMaps().m_Count,f.shadowLights);s.announcedWorldShadows=true;}
    if(qEnvironmentVariableIsSet("OPENSHORES_RENDER_AUDIT"))qInfo("LIVE_FILAMENT frame=%llu parts=%u size=%ux%u elapsedMs=%lld",static_cast<unsigned long long>(++s.frames),f.meshCount,f.width,f.height,static_cast<long long>(timer.elapsed()));
    return true;
}
