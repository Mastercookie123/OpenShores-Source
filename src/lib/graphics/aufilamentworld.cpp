#include "aufilamentworld.h"
#include "augleffects.h"
#include "auglsl.h"
#include "../../renderer-filament/world_api.h"
#include <QCoreApplication>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QElapsedTimer>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <map>
#include <memory>
#include <vector>
#include <array>
#include <cstring>
#include <cmath>
#include <limits>

namespace {
bool enabled(){static bool value=qgetenv("OPENSHORES_RENDERER")=="filament-world";return value;}
struct Native {
    QLibrary library;
    decltype(&ofw_create) create=nullptr;decltype(&ofw_render) render=nullptr;decltype(&ofw_destroy) destroy=nullptr;decltype(&ofw_error) error=nullptr;
    Native():library(QDir(QCoreApplication::applicationDirPath()).filePath("OpenShoresFilamentWorld-r8")){
        library.setLoadHints(QLibrary::PreventUnloadHint);if(!library.load())qFatal("Filament world DLL: %s",qPrintable(library.errorString()));
        create=reinterpret_cast<decltype(create)>(library.resolve("ofw_create"));render=reinterpret_cast<decltype(render)>(library.resolve("ofw_render"));destroy=reinterpret_cast<decltype(destroy)>(library.resolve("ofw_destroy"));error=reinterpret_cast<decltype(error)>(library.resolve("ofw_error"));if(!create||!render||!destroy||!error)qFatal("Incompatible Filament world DLL");
    }
};
Native& native(){static Native* value=new Native;return *value;}
QMutex resourceMutex;
uint64_t resourceSerial=1;
std::map<std::pair<void*,GLuint>,uint64_t> bufferVersions,textureVersions;
std::map<std::pair<void*,GLuint>,QString> shaderNames;
std::map<std::pair<void*,GLuint>,std::pair<QString,QString>> programNames;uint64_t shaderSerial=0;
void* shareGroup(){auto*c=QOpenGLContext::currentContext();return c?c->shareGroup():nullptr;}
uint64_t version(bool texture,GLuint name){QMutexLocker lock(&resourceMutex);return (texture?textureVersions:bufferVersions)[{shareGroup(),name}];}
struct APIParameter {QString name,type,parameter;QByteArray nativeName;int count=1,fields=1,semantic=0;uint32_t nativeType=0;bool sampler=false,integral=false,bodyBones=false;};
struct Attribute {QString name,type;int size=0;Attribute(const QString&n,const QString&t,int count):name(n),type(t),size(count){}};
struct MaterialSpec {QByteArray key;QString vertex,fragment;std::vector<Attribute> attributes;std::vector<APIParameter> uniforms;bool lit=false,points=false,instanced=false;};
struct Program {MaterialSpec* material=nullptr;std::map<QString,GLint> locations;std::map<const APIParameter*,std::vector<GLint>> parameterLocations;std::vector<GLint> attributes;uint64_t boneEpoch=0;std::vector<float> cachedBones;};
using GeometryKey=std::array<uint64_t,112>;
using TextureKey=std::vector<uint64_t>;
struct Image {std::vector<GLuint> sourceNames;uint64_t age=0;OFWTexture packet={};std::vector<uint8_t> pixels;};
struct Bounds {float lo[3],hi[3];bool used=false;Bounds(){for(int i=0;i<3;i++){lo[i]=1.e30f;hi[i]=-1.e30f;}}void add(const float*p){used=true;for(int i=0;i<3;i++){lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}}};
struct Geometry {uint64_t age=0;uint64_t key=0;uint32_t primitive=0;float roughness=.75f;std::vector<OFWVertex> vertices;std::vector<uint32_t> indices;Bounds bounds;std::array<Bounds,61> boneBounds;};
struct OwnedParameter {QByteArray name;uint32_t type=0,count=1;std::vector<float> values;std::shared_ptr<Image> image;};
struct ParameterBlock {size_t used=0;std::vector<OwnedParameter> owned;std::vector<OFWParameter> packets;};
struct Draw {OFWDraw packet={};QByteArray material;std::shared_ptr<Geometry> geometry;std::shared_ptr<ParameterBlock> common;bool buildingCommon=false;std::vector<OwnedParameter> owned;std::vector<OFWParameter> parameters;};
OwnedParameter& nextParameter(Draw&d){if(d.buildingCommon){auto&b=*d.common;if(b.used==b.owned.size())b.owned.emplace_back();return b.owned[b.used++];}d.owned.emplace_back();return d.owned.back();}
struct State {
    AuGLEffects* renderer=nullptr;
    void* engine=nullptr;bool capturing=false,muted=false;GLuint framebuffer=0;GLint viewport[4]={};uint32_t group=0;uint64_t frame=0,knownShaders=0;uint64_t nextKey=1,bodyEpoch=0;uint32_t boneReads=0,boneHits=0;int bodySkin=2;GLuint armorTexture=0,helmetTexture=0;float environmentRotation[9]={1,0,0,0,0,1,0,-1,0};
    QMatrix4x4 projection,inverse;QElapsedTimer timer,auditTimer;
    std::map<QString,MaterialSpec> materials;std::map<GLuint,Program> programs;
    std::map<GeometryKey,std::shared_ptr<Geometry>> geometry;std::map<TextureKey,std::shared_ptr<Image>> images;
    std::map<std::pair<GLuint,uint64_t>,std::shared_ptr<std::vector<uint8_t>>> buffers;std::map<std::pair<GLuint,uint64_t>,uint64_t> bufferAges;
    std::map<uint64_t,uint32_t> geometryOccurrences;std::vector<std::unique_ptr<Draw>> draws;std::vector<OFLight> lights;float ambient[3]={.1f,.1f,.1f},clear[4]={0,0,0,1};bool lightingCaptured=false,mainLightingCaptured=false;
    std::vector<std::shared_ptr<ParameterBlock>> parameterPool;size_t usedParameterBlocks=0;
    std::vector<uint8_t> rgba;std::vector<float> depth;QOpenGLShaderProgram composite;GLuint vao=0,color=0,z=0;
    QJsonObject coverage;~State(){if(engine)native().destroy(engine);}
};
std::map<QOpenGLContext*,std::unique_ptr<State>> states;
State* current(){auto it=states.find(QOpenGLContext::currentContext());return it==states.end()?nullptr:it->second.get();}
int components(const QString& t){if(t=="mat4")return 16;if(t=="mat3")return 9;if(t=="mat2")return 4;if(t.endsWith('4'))return 4;if(t.endsWith('3'))return 3;if(t.endsWith('2'))return 2;return 1;}
GLint location(AuOpenGLFunctions& gl,Program& p,GLuint id,const QString& name){auto found=p.locations.find(name);if(found!=p.locations.end())return found->second;return p.locations[name]=gl.glGetUniformLocation(id,name.toLatin1().constData());}
const std::vector<GLint>& parameterLocations(AuOpenGLFunctions&gl,Program&p,GLuint id,const APIParameter&spec){auto found=p.parameterLocations.find(&spec);if(found!=p.parameterLocations.end())return found->second;std::vector<GLint> result;for(int i=0;i<spec.count;i++)result.push_back(location(gl,p,id,spec.name+(spec.count>1?QString("[%1]").arg(i):QString())));return p.parameterLocations.emplace(&spec,std::move(result)).first->second;}
QMatrix4x4 matrix(const float* values){QMatrix4x4 m;std::memcpy(m.data(),values,64);return m;}
Program& program(State& s,AuOpenGLFunctions& gl,GLuint id){
    if(s.knownShaders!=shaderSerial){s.programs.clear();s.knownShaders=shaderSerial;}
    auto it=s.programs.find(id);if(it!=s.programs.end())return it->second;
    GLuint shaders[8];GLsizei count=0;gl.glGetAttachedShaders(id,8,&count,shaders);QString v,f;
    auto registered=programNames.find({shareGroup(),id});if(registered!=programNames.end()){v=registered->second.first;f=registered->second.second;}
    for(int i=0;i<count;i++){auto found=shaderNames.find({shareGroup(),shaders[i]});if(found==shaderNames.end())continue;if(found->second.endsWith(".v"))v=found->second;if(found->second.endsWith(".f"))f=found->second;}
    QString key=v.left(v.size()-2)+"__"+f.left(f.size()-2);auto spec=s.materials.find(key);
    if(spec==s.materials.end())qFatal("Filament world coverage gap: program %u, vertex %s, fragment %s",id,qPrintable(v),qPrintable(f));
    Program p;p.material=&spec->second;for(auto&a:p.material->attributes)p.attributes.push_back(gl.glGetAttribLocation(id,a.name.toLatin1().constData()));
    return s.programs.emplace(id,std::move(p)).first->second;
}
std::shared_ptr<std::vector<uint8_t>> buffer(State&s,AuOpenGLFunctions&gl,GLuint id){
    auto key=std::make_pair(id,version(false,id));s.bufferAges[key]=s.frame;auto it=s.buffers.find(key);if(it!=s.buffers.end())return it->second;
    GLint old=0;gl.glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&old);gl.glBindBuffer(GL_ARRAY_BUFFER,id);GLint size=0;gl.glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);auto bytes=std::make_shared<std::vector<uint8_t>>(std::max(0,size));if(size)gl.glGetBufferSubData(GL_ARRAY_BUFFER,0,size,bytes->data());gl.glBindBuffer(GL_ARRAY_BUFFER,old);s.buffers[key]=bytes;return bytes;
}
struct Stream {GLint type=GL_FLOAT,size=4,stride=0,normalized=0;size_t offset=0;GLuint buffer=0;std::shared_ptr<std::vector<uint8_t>> bytes;float constant[4]={0,0,0,1};};
int byteSize(GLenum t){switch(t){case GL_DOUBLE:return 8;case GL_FLOAT:case GL_INT:case GL_UNSIGNED_INT:return 4;case GL_SHORT:case GL_UNSIGNED_SHORT:return 2;default:return 1;}}
float number(const uint8_t*p,GLenum type,bool normalized){
    switch(type){case GL_FLOAT:{float v;std::memcpy(&v,p,4);return v;}case GL_DOUBLE:{double v;std::memcpy(&v,p,8);return v;}case GL_INT:{int32_t v;std::memcpy(&v,p,4);return normalized?std::max(-1.f,v/2147483647.f):float(v);}case GL_UNSIGNED_INT:{uint32_t v;std::memcpy(&v,p,4);return normalized?v/4294967295.f:float(v);}case GL_SHORT:{int16_t v;std::memcpy(&v,p,2);return normalized?std::max(-1.f,v/32767.f):v;}case GL_UNSIGNED_SHORT:{uint16_t v;std::memcpy(&v,p,2);return normalized?v/65535.f:v;}case GL_BYTE:return normalized?std::max(-1.f,*reinterpret_cast<const int8_t*>(p)/127.f):*reinterpret_cast<const int8_t*>(p);case GL_UNSIGNED_BYTE:return normalized?*p/255.f:*p;default:qFatal("Unsupported world vertex storage 0x%x",type);return 0;}
}
void read(const Stream&s,uint32_t index,float* result){for(int i=0;i<4;i++)result[i]=s.constant[i];if(!s.bytes)return;size_t at=s.offset+size_t(index)*s.stride;if(at+size_t(s.size)*byteSize(s.type)>s.bytes->size())qFatal("World vertex stream out of bounds");for(int i=0;i<s.size;i++)result[i]=number(s.bytes->data()+at+i*byteSize(s.type),s.type,s.normalized!=0);}
std::shared_ptr<Geometry> geometry(State&s,AuOpenGLFunctions&gl,Program&p,GLenum mode,GLint first,GLsizei count,GLenum indexType,const void* offset){
    GLint vao=0,ibo=0,front=GL_CW;gl.glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);gl.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&ibo);gl.glGetIntegerv(GL_FRONT_FACE,&front);
    GeometryKey key={};size_t keyAt=0;auto push=[&](uint64_t value){key[keyAt++]=value;};push(vao);push(reinterpret_cast<uintptr_t>(p.material));push(mode);push(first);push(count);push(indexType);push(quintptr(offset));push(ibo);push(front);
    std::vector<Stream> streams;
    for(size_t i=0;i<p.attributes.size();i++){Stream stream;GLint at=p.attributes[i];if(at>=0){GLint active=0;gl.glGetVertexAttribiv(at,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&active);if(active){GLint id;gl.glGetVertexAttribiv(at,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&id);stream.buffer=id;gl.glGetVertexAttribiv(at,GL_VERTEX_ATTRIB_ARRAY_SIZE,&stream.size);gl.glGetVertexAttribiv(at,GL_VERTEX_ATTRIB_ARRAY_TYPE,&stream.type);gl.glGetVertexAttribiv(at,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&stream.stride);gl.glGetVertexAttribiv(at,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&stream.normalized);void* pointer=nullptr;gl.glGetVertexAttribPointerv(at,GL_VERTEX_ATTRIB_ARRAY_POINTER,&pointer);stream.offset=quintptr(pointer);if(!stream.stride)stream.stride=stream.size*byteSize(stream.type);stream.bytes=buffer(s,gl,id);}else gl.glGetVertexAttribfv(at,GL_CURRENT_VERTEX_ATTRIB,stream.constant);}
        push(stream.buffer);push(version(false,stream.buffer));push(stream.offset);push(stream.stride);push(stream.type);push(stream.size);push(stream.normalized);for(float value:stream.constant){uint32_t bits;std::memcpy(&bits,&value,4);push(bits);}streams.push_back(stream);}
    if(indexType)push(version(false,ibo));auto found=s.geometry.find(key);if(found!=s.geometry.end()){found->second->age=s.frame;return found->second;}
    std::vector<uint32_t> source(count);if(indexType){if(!ibo)qFatal("Client-side world indices are unsupported in core GL");auto bytes=buffer(s,gl,ibo);size_t base=quintptr(offset);if(base+size_t(count)*byteSize(indexType)>bytes->size())qFatal("World index stream out of bounds");for(int i=0;i<count;i++)source[i]=uint32_t(number(bytes->data()+base+i*byteSize(indexType),indexType,false));}else for(int i=0;i<count;i++)source[i]=first+i;
    auto g=std::make_shared<Geometry>();g->key=s.nextKey++;g->age=s.frame;int pos=-1,norm=-1,bones=-1,weights=-1;for(size_t i=0;i<p.material->attributes.size();i++){auto name=p.material->attributes[i].name;if(name=="position"||name=="positionShine")pos=i;if(name.startsWith("normal"))norm=i;if(name=="boneIndex")bones=i;if(name=="boneWeight")weights=i;}
    std::map<uint32_t,uint32_t> remap;
    auto vertex=[&](uint32_t index){auto it=remap.find(index);if(it!=remap.end())return it->second;OFWVertex v={};v.normal[2]=1;for(size_t i=0;i<streams.size();i++)read(streams[i],index,v.attributes[i]);if(pos>=0)std::memcpy(v.position,v.attributes[pos],12);else if(!streams.empty())std::memcpy(v.position,v.attributes[0],12);if(norm>=0)std::memcpy(v.normal,v.attributes[norm],12);g->bounds.add(v.position);if(bones>=0&&weights>=0)for(int b=0;b<4;b++)if(v.attributes[weights][b]>0){int bone=int(v.attributes[bones][b]);if(bone<0||bone>=61)qFatal("World skin bone index out of range");g->boneBounds[bone].add(v.position);}uint32_t out=g->vertices.size();g->vertices.push_back(v);remap[index]=out;return out;};
    auto tri=[&](uint32_t a,uint32_t b,uint32_t c){g->indices.push_back(vertex(a));g->indices.push_back(vertex(front==GL_CW?c:b));g->indices.push_back(vertex(front==GL_CW?b:c));};
    if(mode==GL_TRIANGLES){for(size_t i=0;i+2<source.size();i+=3)tri(source[i],source[i+1],source[i+2]);}
    else if(mode==GL_TRIANGLE_STRIP){for(size_t i=2;i<source.size();i++)if(i%2)tri(source[i-1],source[i-2],source[i]);else tri(source[i-2],source[i-1],source[i]);}
    else if(mode==GL_TRIANGLE_FAN){for(size_t i=2;i<source.size();i++)tri(source[0],source[i-1],source[i]);}
    else if(mode==GL_POINTS){for(auto index:source){uint32_t original=vertex(index);OFWVertex v=g->vertices[original];uint32_t start=g->vertices.size();for(int c=0;c<4;c++){v.corner[0]=c&1?1:0;v.corner[1]=c&2?1:0;g->vertices.push_back(v);}for(uint32_t i:{0u,1u,2u,1u,3u,2u})g->indices.push_back(start+i);}}
    else if(mode==GL_LINES||mode==GL_LINE_STRIP||mode==GL_LINE_LOOP){g->primitive=1;for(size_t i=1;i<source.size();i++)if(mode!=GL_LINES||i%2){g->indices.push_back(vertex(source[i-1]));g->indices.push_back(vertex(source[i]));}if(mode==GL_LINE_LOOP&&source.size()>1){g->indices.push_back(vertex(source.back()));g->indices.push_back(vertex(source.front()));}}
    else qFatal("Unported world primitive: 0x%x",mode);
    if(p.material->vertex.startsWith("Model")||p.material->vertex.startsWith("Sc")){int shine=-1,channel=3;for(size_t i=0;i<p.material->attributes.size();i++){auto name=p.material->attributes[i].name;if(name.contains("Shine")){shine=i;channel=name.startsWith("texCoord")?2:3;break;}}if(shine>=0){double sum=0;for(auto&v:g->vertices)sum+=std::max(0.f,v.attributes[shine][channel]);float n=g->vertices.empty()?0:sum/g->vertices.size();g->roughness=std::max(.28f,std::min(.95f,std::pow(2.f/(n+2.f),.25f)));}}
    s.geometry[key]=g;return g;
}
void uniform(State&s,AuOpenGLFunctions&gl,Program&p,GLuint id,const APIParameter& spec,std::vector<float>&out){
    bool bodyBones=spec.bodyBones;if(bodyBones&&p.boneEpoch==s.bodyEpoch&&!p.cachedBones.empty()){++s.boneHits;out=p.cachedBones;return;}
    int size=spec.fields;out.assign(size*spec.count,0);bool integral=spec.integral;
    const auto&locations=parameterLocations(gl,p,id,spec);for(int i=0;i<spec.count;i++){GLint at=locations[i];if(at>=0){if(integral){GLint values[16]={};gl.glGetUniformiv(id,at,values);for(int c=0;c<size;c++)out[i*size+c]=values[c];}else gl.glGetUniformfv(id,at,out.data()+i*size);}else if(spec.type=="mat4")for(int c=0;c<16;c++)out[i*16+c]=c%5==0?1:0;}
    if(bodyBones){p.boneEpoch=s.bodyEpoch;p.cachedBones=out;++s.boneReads;}
}
std::shared_ptr<Image> texture(State&s,AuOpenGLFunctions&gl,Program&p,GLuint id,const APIParameter&spec){
    bool array=spec.count>1||spec.type=="sampler2DArray",cube=spec.type=="samplerCube";GLenum target=cube?GL_TEXTURE_CUBE_MAP:spec.type=="sampler2DArray"?GL_TEXTURE_2D_ARRAY:GL_TEXTURE_2D;
    GLenum binding=cube?GL_TEXTURE_BINDING_CUBE_MAP:spec.type=="sampler2DArray"?GL_TEXTURE_BINDING_2D_ARRAY:GL_TEXTURE_BINDING_2D;
    GLint active=GL_TEXTURE0;gl.glGetIntegerv(GL_ACTIVE_TEXTURE,&active);TextureKey key={target,uint64_t(spec.count)};key.reserve(2+spec.count*2);std::vector<GLuint> names;std::vector<GLint> units;int width=1,height=1,layers=spec.count;
    const auto&locations=parameterLocations(gl,p,id,spec);
    for(int i=0;i<spec.count;i++){GLint at=locations[i],unit=0,name=0;if(at>=0)gl.glGetUniformiv(id,at,&unit);gl.glActiveTexture(GL_TEXTURE0+unit);gl.glGetIntegerv(binding,&name);names.push_back(name);units.push_back(unit);key.push_back(name);key.push_back(version(true,name));}
    auto found=s.images.find(key);if(found!=s.images.end()){found->second->age=s.frame;gl.glActiveTexture(active);return found->second;}
    for(int i=0;i<spec.count;i++)if(names[i]){gl.glActiveTexture(GL_TEXTURE0+units[i]);GLint w=0,h=0,d=1;GLenum face=cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target;gl.glGetTexLevelParameteriv(face,0,GL_TEXTURE_WIDTH,&w);gl.glGetTexLevelParameteriv(face,0,GL_TEXTURE_HEIGHT,&h);if(target==GL_TEXTURE_2D_ARRAY)gl.glGetTexLevelParameteriv(target,0,GL_TEXTURE_DEPTH,&d);width=std::max(width,w);height=std::max(height,h);if(target==GL_TEXTURE_2D_ARRAY)layers=d;}
    if(cube)layers=6;
    auto image=std::make_shared<Image>();image->sourceNames=names;image->age=s.frame;image->packet={s.nextKey++,uint32_t(width),uint32_t(height),uint32_t(layers),uint32_t(cube?OFW_TEXTURE_CUBE:array?OFW_TEXTURE_ARRAY:OFW_TEXTURE2D),nullptr};image->pixels.resize(size_t(width)*height*layers*4,0);for(size_t i=3;i<image->pixels.size();i+=4)image->pixels[i]=255;
    GLint pack=0,align=4,row=0,skipR=0,skipP=0;gl.glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);gl.glGetIntegerv(GL_PACK_ALIGNMENT,&align);gl.glGetIntegerv(GL_PACK_ROW_LENGTH,&row);gl.glGetIntegerv(GL_PACK_SKIP_ROWS,&skipR);gl.glGetIntegerv(GL_PACK_SKIP_PIXELS,&skipP);gl.glBindBuffer(GL_PIXEL_PACK_BUFFER,0);gl.glPixelStorei(GL_PACK_ALIGNMENT,1);gl.glPixelStorei(GL_PACK_ROW_LENGTH,0);gl.glPixelStorei(GL_PACK_SKIP_ROWS,0);gl.glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    for(int i=0;i<spec.count;i++){if(!names[i])continue;gl.glActiveTexture(GL_TEXTURE0+units[i]);
        if(target==GL_TEXTURE_2D_ARRAY)gl.glGetTexImage(target,0,GL_RGBA,GL_UNSIGNED_BYTE,image->pixels.data());
        else if(cube){for(int face=0;face<6;face++)gl.glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA,GL_UNSIGNED_BYTE,image->pixels.data()+size_t(face)*width*height*4);}
        else{GLint w,h;gl.glGetTexLevelParameteriv(target,0,GL_TEXTURE_WIDTH,&w);gl.glGetTexLevelParameteriv(target,0,GL_TEXTURE_HEIGHT,&h);QImage layer(w,h,QImage::Format_RGBA8888);gl.glGetTexImage(target,0,GL_RGBA,GL_UNSIGNED_BYTE,layer.bits());if(w!=width||h!=height)layer=layer.scaled(width,height,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);std::memcpy(image->pixels.data()+size_t(i)*width*height*4,layer.constBits(),size_t(width)*height*4);}
    }
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);gl.glPixelStorei(GL_PACK_ALIGNMENT,align);gl.glPixelStorei(GL_PACK_ROW_LENGTH,row);gl.glPixelStorei(GL_PACK_SKIP_ROWS,skipR);gl.glPixelStorei(GL_PACK_SKIP_PIXELS,skipP);gl.glActiveTexture(active);image->packet.rgba=image->pixels.data();s.images[key]=image;return image;
}
uint32_t parameterType(const QString&type){if(type=="float")return OFW_FLOAT;if(type=="vec2")return OFW_FLOAT2;if(type=="vec3")return OFW_FLOAT3;if(type=="vec4")return OFW_FLOAT4;if(type=="int"||type=="uint")return OFW_INT;if(type=="ivec2")return OFW_INT2;if(type=="ivec3")return OFW_INT3;if(type=="ivec4")return OFW_INT4;if(type=="bool")return OFW_BOOL;if(type=="mat3")return OFW_MAT3;if(type=="mat4")return OFW_MAT4;if(type=="mat2")return OFW_MAT2;qFatal("Unknown world parameter type %s",qPrintable(type));return 0;}
void add(Draw&d,const QByteArray&name,uint32_t type,const float* values,int count=1,int size=1){auto&p=nextParameter(d);p.name=name;p.type=type;p.count=count;p.image.reset();p.values.assign(values,values+size*count);}
void captureLighting(State&s,AuGLEffects&gl,bool preferred){
    if(s.mainLightingCaptured||(s.lightingCaptured&&!preferred))return;s.lightingCaptured=true;s.mainLightingCaptured=preferred;s.lights.clear();float a;gl.CurrentAmbientLight(s.ambient[0],s.ambient[1],s.ambient[2],a);
    for(int i=0;i<MAX_SUNS;i++){auto&sun=gl.Sun(i);if(!sun.m_Enabled)continue;double distance2=double(sun.m_LocX)*sun.m_LocX+double(sun.m_LocY)*sun.m_LocY+double(sun.m_LocZ)*sun.m_LocZ;if(distance2<1e-9||sun.m_Attenuation<=0)continue;OFLight lamp={{-sun.m_LocX,-sun.m_LocY,-sun.m_LocZ},{sun.m_ColorR,sun.m_ColorG,sun.m_ColorB},float(std::min(160000.,80000./(sun.m_Attenuation*distance2))),1};if(s.lights.empty())s.lights.push_back(lamp);else if(lamp.intensity>s.lights[0].intensity)s.lights[0]=lamp;}
    for(unsigned i=0;i<gl.MaxLights()&&s.lights.size()<OF_MAX_LIGHTS;i++){auto&l=gl.Light(i);if(!l.m_bEnabled)continue;float attenuation=1/std::max(.001f,5*l.m_kL+25*l.m_kQ);OFLight lamp={{float(l.m_Position.x),float(l.m_Position.y),float(l.m_Position.z)},{l.m_ColorR,l.m_ColorG,l.m_ColorB},float(4*3.141592653589793*25*attenuation*2500),0};s.lights.push_back(lamp);}
}
void bounds(Draw&draw,const Geometry&g,const QMatrix4x4& model,const std::vector<float>*bones){
    QVector3D lo(1e30f,1e30f,1e30f),hi(-1e30f,-1e30f,-1e30f);
    auto box=[&](const Bounds&b,const QMatrix4x4&m){if(!b.used)return;for(int i=0;i<8;i++){auto p=m*QVector4D(b.lo[0]+(i&1?b.hi[0]-b.lo[0]:0),b.lo[1]+(i&2?b.hi[1]-b.lo[1]:0),b.lo[2]+(i&4?b.hi[2]-b.lo[2]:0),1);if(std::abs(p.w())<1e-9)continue;auto v=p.toVector3DAffine();for(int c=0;c<3;c++){lo[c]=std::min(lo[c],v[c]);hi[c]=std::max(hi[c],v[c]);}}};
    if(bones){for(int b=0;b<61&&size_t(b*16+15)<bones->size();b++)box(g.boneBounds[b],model*matrix(bones->data()+b*16));}else box(g.bounds,model);
    for(int c=0;c<3;c++){if(!std::isfinite(lo[c])||lo[c]>hi[c]){lo[c]=-1;hi[c]=1;}draw.packet.center[c]=(lo[c]+hi[c])*.5f;draw.packet.extent[c]=std::max(.01f,(hi[c]-lo[c])*.5f+1.f);}
}
}

void AuFilamentBodyBegin(int skin,unsigned armor,unsigned helmet){if(auto*s=current()){++s->bodyEpoch;s->bodySkin=skin;s->armorTexture=armor;s->helmetTexture=helmet;}}
void AuFilamentWorldOrientation(const AuMatrix4x4&view){auto*s=current();if(!s||!s->capturing)return;AuPoint axes[]={view.ProjectVector(AuPoint(1,0,0)),view.ProjectVector(AuPoint(0,0,1)),view.ProjectVector(AuPoint(0,-1,0))};for(int c=0;c<3;c++){double len=std::sqrt(axes[c].x*axes[c].x+axes[c].y*axes[c].y+axes[c].z*axes[c].z);if(len<1e-8)len=1;s->environmentRotation[c*3]=axes[c].x/len;s->environmentRotation[c*3+1]=axes[c].y/len;s->environmentRotation[c*3+2]=axes[c].z/len;}}
void AuFilamentShader(GLuint shader,const QString&name){if(!enabled())return;QMutexLocker lock(&resourceMutex);
    // Linked programs immediately detach their shader objects. Snapshot their
    // source identities at link time, before those objects are destroyed.
    if(name=="@program"){AuOpenGLBase gl;gl.initializeOpenGLFunctions();GLuint attached[8];GLsizei count=0;gl.glGetAttachedShaders(shader,8,&count,attached);QString vertex,fragment;for(int i=0;i<count;i++){auto it=shaderNames.find({shareGroup(),attached[i]});if(it==shaderNames.end())continue;if(it->second.endsWith(".v"))vertex=it->second;if(it->second.endsWith(".f"))fragment=it->second;}programNames[{shareGroup(),shader}]={vertex,fragment};}
    else shaderNames[{shareGroup(),shader}]=name;shaderSerial++;
}
void AuFilamentBufferChanged(AuOpenGLFunctions&gl,GLenum target){if(!enabled())return;GLenum binding=target==GL_ARRAY_BUFFER?GL_ARRAY_BUFFER_BINDING:target==GL_ELEMENT_ARRAY_BUFFER?GL_ELEMENT_ARRAY_BUFFER_BINDING:target==GL_TEXTURE_BUFFER?GL_TEXTURE_BUFFER_BINDING:0;if(!binding)return;GLint id=0;gl.glGetIntegerv(binding,&id);QMutexLocker lock(&resourceMutex);bufferVersions[{shareGroup(),id}]=++resourceSerial;}
void AuFilamentTextureChanged(AuOpenGLFunctions&gl,GLenum target){if(!enabled())return;GLenum binding=target==GL_TEXTURE_2D?GL_TEXTURE_BINDING_2D:target==GL_TEXTURE_2D_ARRAY?GL_TEXTURE_BINDING_2D_ARRAY:target==GL_TEXTURE_CUBE_MAP||(target>=GL_TEXTURE_CUBE_MAP_POSITIVE_X&&target<=GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)?GL_TEXTURE_BINDING_CUBE_MAP:0;if(!binding)return;GLint id=0;gl.glGetIntegerv(binding,&id);QMutexLocker lock(&resourceMutex);textureVersions[{shareGroup(),id}]=++resourceSerial;}
void AuFilamentDeleted(AuOpenGLFunctions&,bool texture,GLsizei count,const GLuint*names){if(!enabled())return;QMutexLocker lock(&resourceMutex);for(int i=0;i<count;i++)(texture?textureVersions:bufferVersions)[{shareGroup(),names[i]}]=++resourceSerial;}
void AuFilamentClear(AuOpenGLFunctions&gl,GLbitfield mask){auto*s=current();if(!s||!s->capturing||s->muted)return;GLint target;gl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&target);if(GLuint(target)!=s->framebuffer)return;if((mask&GL_DEPTH_BUFFER_BIT)&&!s->draws.empty())s->group=std::min(7u,s->group+1);}

bool AuFilamentDraw(AuOpenGLFunctions&gl,GLenum mode,GLint first,GLsizei count,GLenum indexType,const void* offset,GLsizei instances){
    if(!enabled())return false;auto*s=current();if(!s||!s->capturing||s->muted||count<=0||instances<=0)return false;
    GLint target=0,programID=0;gl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&target);if(GLuint(target)!=s->framebuffer)return false;gl.glGetIntegerv(GL_CURRENT_PROGRAM,&programID);if(!programID)qFatal("Filament world encountered a draw without a shader");
    auto&p=program(*s,gl,programID);auto g=geometry(*s,gl,p,mode,first,count,indexType,offset);if(g->indices.empty())return true;
    if(p.material->lit&&s->renderer)captureLighting(*s,*s->renderer,p.material->vertex.startsWith("Sc")||p.material->vertex.startsWith("Terrain")||p.material->vertex.startsWith("Body")||p.material->vertex.startsWith("Model")||p.material->vertex.startsWith("Animal"));
    Draw common;common.buildingCommon=true;if(s->usedParameterBlocks==s->parameterPool.size())s->parameterPool.push_back(std::make_shared<ParameterBlock>());common.common=s->parameterPool[s->usedParameterBlocks++];common.common->used=0;common.common->owned.reserve(p.material->uniforms.size()+6);
        std::array<const std::vector<float>*,8> values={};bool armorSurface=false;
        for(auto&spec:p.material->uniforms){auto&owned=nextParameter(common);owned.name=spec.nativeName;owned.count=spec.count;if(spec.sampler){owned.image=texture(*s,gl,p,programID,spec);owned.type=owned.image->packet.kind;owned.count=1;owned.values.clear();if(spec.name=="texClothes"&&!owned.image->sourceNames.empty()){auto name=owned.image->sourceNames[0];armorSurface=name&&(name==s->armorTexture||name==s->helmetTexture);}}else{owned.image.reset();uniform(*s,gl,p,programID,spec,owned.values);owned.type=spec.nativeType;if(spec.semantic)values[spec.semantic]=&owned.values;}}

    GLint viewport[4];gl.glGetIntegerv(GL_VIEWPORT,viewport);
        bool depth=gl.glIsEnabled(GL_DEPTH_TEST);GLboolean write=true,colors[4];gl.glGetBooleanv(GL_DEPTH_WRITEMASK,&write);gl.glGetBooleanv(GL_COLOR_WRITEMASK,colors);common.packet.depthTest=depth;common.packet.depthWrite=depth&&write;common.packet.colorWrite=colors[0]||colors[1]||colors[2]||colors[3];common.packet.cull=0;if(mode!=GL_POINTS&&gl.glIsEnabled(GL_CULL_FACE)){GLint face;gl.glGetIntegerv(GL_CULL_FACE_MODE,&face);common.packet.cull=face==GL_BACK?1:face==GL_FRONT?2:0;}
        common.packet.priority=!depth?(s->draws.empty()?0:7):4;QString family=QString::fromUtf8(p.material->key);if(family.startsWith("Sky")||family.startsWith("Galaxy")||family.startsWith("Far")||family.startsWith("Nebula")||family.startsWith("Atmosphere"))common.packet.priority=0;
        if(viewport[2]!=s->viewport[2]||viewport[3]!=s->viewport[3])common.packet.priority=7;
        common.packet.scissor=gl.glIsEnabled(GL_SCISSOR_TEST);if(common.packet.scissor)gl.glGetIntegerv(GL_SCISSOR_BOX,common.packet.scissorBox);
        if(gl.glIsEnabled(GL_BLEND)){GLint destination;gl.glGetIntegerv(GL_BLEND_DST_RGB,&destination);common.packet.blend=destination==GL_ONE?2:1;}
        float writeDepth=depth&&write?1:0;add(common,"os_writeDepth",OFW_FLOAT,&writeDepth);

    if(p.material->lit){float profile[4]={g->roughness,0,.5f,.86f};const auto&family=p.material->vertex;if(family.startsWith("Body")){float skin[]={.48f,.58f,.66f,.90f,.90f,.48f,.58f,.66f};profile[0]=skin[std::max(0,std::min(7,s->bodySkin))];if(armorSurface){profile[1]=.72f;profile[3]=.35f;}}else if(family.startsWith("Terrain")||family.startsWith("Globe"))profile[0]=.96f;else if(family.startsWith("Flora")||family.startsWith("Animal"))profile[0]=.86f;else if(family.startsWith("Rock"))profile[0]=.92f;else if(family.startsWith("Water"))profile[0]=.18f;add(common,"os_surfaceProfile",OFW_FLOAT4,profile,1,4);}
    float screen[4]={float(s->viewport[2]),float(s->viewport[3]),float(s->viewport[0]),float(s->viewport[1])};add(common,"os_viewport",OFW_FLOAT4,screen,1,4);
    float drawViewport[4]={float(viewport[0]-s->viewport[0]),float(viewport[1]-s->viewport[1]),float(viewport[2]),float(viewport[3])};add(common,"os_drawViewport",OFW_FLOAT4,drawViewport,1,4);
    add(common,"os_invProjection",OFW_MAT4,s->inverse.constData(),1,16);add(common,"os_projection",OFW_MAT4,s->projection.constData(),1,16);
    auto shared=common.common;shared->packets.clear();shared->packets.reserve(shared->used);
    for(size_t i=0;i<shared->used;i++){auto&owned=shared->owned[i];OFWParameter parameter={};parameter.name=owned.name.constData();parameter.type=owned.type;parameter.count=owned.count;parameter.values=owned.values.data();if(owned.image)parameter.texture=owned.image->packet;shared->packets.push_back(parameter);}for(size_t i=shared->used;i<shared->owned.size();i++)shared->owned[i].image.reset();
    std::shared_ptr<std::vector<uint8_t>> instanceBytes;
    if(p.material->instanced){GLint active=0,unit=0,name=0,bufferID=0;gl.glGetIntegerv(GL_ACTIVE_TEXTURE,&active);GLint at=location(gl,p,programID,"instances");if(at>=0)gl.glGetUniformiv(programID,at,&unit);gl.glActiveTexture(GL_TEXTURE0+unit);gl.glGetIntegerv(GL_TEXTURE_BINDING_BUFFER,&name);if(name)gl.glGetTexLevelParameteriv(GL_TEXTURE_BUFFER,0,GL_TEXTURE_BUFFER_DATA_STORE_BINDING,&bufferID);if(bufferID)instanceBytes=buffer(*s,gl,bufferID);gl.glActiveTexture(active);}
    static const QByteArray instanceNames[6]={QByteArrayLiteral("os_instanceData0"),QByteArrayLiteral("os_instanceData1"),QByteArrayLiteral("os_instanceData2"),QByteArrayLiteral("os_instanceData3"),QByteArrayLiteral("os_instanceData4"),QByteArrayLiteral("os_instanceData5")};
    for(int instance=0;instance<instances;instance++){
        std::unique_ptr<Draw> d(new Draw);d->packet=common.packet;d->geometry=g;d->material=p.material->key;d->packet.geometryKey=g->key;d->packet.instanceKey=(g->key<<32)|++s->geometryOccurrences[g->key];d->packet.material=d->material.constData();d->packet.vertexCount=g->vertices.size();d->packet.indexCount=g->indices.size();d->packet.vertices=g->vertices.data();d->packet.indices=g->indices.data();d->packet.primitive=g->primitive;d->packet.group=s->group;
        d->common=shared;d->owned.reserve(8);d->parameters.reserve(shared->packets.size()+8);d->parameters.insert(d->parameters.end(),shared->packets.begin(),shared->packets.end());
        float instanceID=instance;add(*d,"os_instanceID",OFW_FLOAT,&instanceID);
        QMatrix4x4 objectToEye;if(values[1])objectToEye=matrix(values[1]->data());else if(values[2])objectToEye=s->inverse*matrix(values[2]->data());
        if(values[4])objectToEye*=matrix(values[4]->data());if(values[5])objectToEye*=matrix(values[5]->data());
        if(p.material->instanced){
            int base=values[7]?int((*values[7])[0]):0;std::array<float,24> data={};if(instanceBytes){size_t start=size_t(base+instance)*96;if(start+96>instanceBytes->size())qFatal("World foliage instance buffer out of bounds");std::memcpy(data.data(),instanceBytes->data()+start,96);}else{QMatrix4x4 identity;std::memcpy(data.data(),identity.constData(),64);}for(int i=0;i<6;i++)add(*d,instanceNames[i],OFW_FLOAT4,data.data()+i*4,1,4);if(values[1]&&values[6]&&(*values[6])[0]!=0)objectToEye=matrix(values[1]->data())*matrix(data.data());}
        add(*d,"os_normalMatrix",OFW_MAT4,objectToEye.constData(),1,16);
        const std::vector<float>*bones=values[3];bounds(*d,*g,objectToEye,bones);
        for(auto&owned:d->owned){OFWParameter parameter={};parameter.name=owned.name.constData();parameter.type=owned.type;parameter.count=owned.count;parameter.values=owned.values.data();if(owned.image)parameter.texture=owned.image->packet;d->parameters.push_back(parameter);}d->packet.parameterCount=d->parameters.size();d->packet.parameters=d->parameters.data();
        QString key=QString::fromUtf8(d->material);s->coverage[key]=s->coverage.value(key).toInt()+1;s->draws.push_back(std::move(d));
    }
    return true;
}

bool AuFilamentWorldBegin(AuGLEffects&gl,const AuMatrix4x4&projection){
    if(!enabled())return false;auto*context=QOpenGLContext::currentContext();auto&pointer=states[context];if(!pointer){pointer.reset(new State);auto&s=*pointer;QString directory=QDir(QCoreApplication::applicationDirPath()).filePath("filament-world-r8");QFile file(QDir(directory).filePath("manifest.json"));if(!file.open(QIODevice::ReadOnly))qFatal("Missing world material manifest");auto document=QJsonDocument::fromJson(file.readAll()).object();for(auto entry:document.value("materials").toArray()){auto object=entry.toObject();MaterialSpec spec;spec.key=object.value("key").toString().toUtf8();spec.vertex=object.value("vertex").toString();spec.fragment=object.value("fragment").toString();spec.lit=object.value("lit").toBool();spec.points=object.value("points").toBool();spec.instanced=object.value("instanced").toBool();for(auto a:object.value("attributes").toArray()){auto item=a.toObject();spec.attributes.push_back({item.value("name").toString(),item.value("type").toString(),item.value("size").toInt()});}for(auto u:object.value("uniforms").toArray()){auto item=u.toObject();APIParameter param;param.name=item.value("name").toString();param.type=item.value("type").toString();param.parameter=item.value("parameter").toString();param.count=item.value("count").toInt(1);param.nativeName=param.parameter.toUtf8();param.sampler=param.type.startsWith("sampler");param.nativeType=param.sampler?0:parameterType(param.type);param.fields=components(param.type);param.integral=param.type=="int"||param.type=="uint"||param.type=="bool"||param.type.startsWith("ivec");param.bodyBones=param.name=="bM"&&spec.vertex.startsWith("Body");const char*semanticNames[]={"","mv","pmv","bM","plantM","rockM","instanced","instanceBase"};for(int i=1;i<8;i++)if(param.name==semanticNames[i])param.semantic=i;spec.uniforms.push_back(param);}s.materials[QString::fromUtf8(spec.key)]=spec;}
        QByteArray world=directory.toUtf8(),bootstrap=QDir(QCoreApplication::applicationDirPath()).filePath("filament-materials").toUtf8();s.engine=native().create(world.constData(),bootstrap.constData());if(!s.engine)qFatal("World engine startup: %s",native().error());
        const char*vertex="#version 150\nout vec2 uv;void main(){vec2 p=vec2(gl_VertexID&1,(gl_VertexID>>1)&1);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}";const char*fragment="#version 150\nin vec2 uv;out vec4 outColor;uniform sampler2D image;uniform sampler2D depthImage;uniform bool exportDepth;void main(){outColor=texture(image,uv);gl_FragDepth=exportDepth?texture(depthImage,uv).r:1.0;}";
        if(!s.composite.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex)||!s.composite.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment)||!s.composite.link())qFatal("World compositor: %s",qPrintable(s.composite.log()));gl.glGenVertexArrays(1,&s.vao);gl.glGenTextures(1,&s.color);gl.glGenTextures(1,&s.z);
        QObject::connect(context,&QOpenGLContext::aboutToBeDestroyed,context,[context](){states.erase(context);},Qt::DirectConnection);s.auditTimer.start();
    }
    auto&s=*pointer;s.renderer=&gl;if(s.capturing)qFatal("Nested world frame");s.draws.clear();s.usedParameterBlocks=0;s.geometryOccurrences.clear();s.boneReads=s.boneHits=0;s.lights.clear();s.coverage=QJsonObject();s.lightingCaptured=false;s.mainLightingCaptured=false;s.group=0;s.frame++;s.projection=QMatrix4x4();for(int i=0;i<16;i++)s.projection.data()[i]=projection[i];s.inverse=s.projection.inverted();gl.glGetIntegerv(GL_VIEWPORT,s.viewport);GLint framebuffer;gl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);s.framebuffer=framebuffer;gl.glGetFloatv(GL_COLOR_CLEAR_VALUE,s.clear);s.capturing=true;s.timer.start();return true;
}
void AuFilamentWorldEnd(AuGLEffects&gl){
    auto*s=current();if(!s||!s->capturing)return;s->capturing=false;if(s->draws.empty())return;s->muted=true;
    std::vector<OFWDraw> draws;for(auto&d:s->draws)draws.push_back(d->packet);OFWFrame frame={};frame.abiVersion=OFW_ABI;frame.width=s->viewport[2];frame.height=s->viewport[3];frame.drawCount=draws.size();frame.draws=draws.data();frame.lightCount=s->lights.size();frame.lights=s->lights.data();frame.shadowLights=AuGLSLShader::ShadowLights();frame.lightingMode=qgetenv("OPENSHORES_FILAMENT_STUDIO")=="1"?1:0;std::memcpy(frame.environmentRotation,s->environmentRotation,36);std::memcpy(frame.projection,s->projection.constData(),64);std::memcpy(frame.ambient,s->ambient,12);std::memcpy(frame.clearColor,s->clear,16);gl.CurrentFog(frame.fogColor[0],frame.fogColor[1],frame.fogColor[2],frame.fogDensity);s->rgba.resize(size_t(frame.width)*frame.height*4);s->depth.resize(size_t(frame.width)*frame.height);
    bool exportDepth=qgetenv("OPENSHORES_FILAMENT_EXPORT_DEPTH")=="1"||(qEnvironmentVariableIsSet("OPENSHORES_RENDER_AUDIT")&&qgetenv("OPENSHORES_FILAMENT_EXPORT_DEPTH")!="0");
    qint64 captureMs=s->timer.elapsed();QElapsedTimer nativeTimer;nativeTimer.start();
    if(!native().render(s->engine,&frame,s->rgba.data(),exportDepth?s->depth.data():nullptr))qFatal("Filament world frame: %s",native().error());
    qint64 nativeMs=nativeTimer.elapsed();
    GLint program=0,vao=0,active=0,bindings[2],pack=0,align=0,unpackRow=0,unpackSkipRows=0,unpackSkipPixels=0,viewport[4],depthFunction=0;GLboolean depthWrite,colorWrite[4];gl.glGetIntegerv(GL_CURRENT_PROGRAM,&program);gl.glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);gl.glGetIntegerv(GL_ACTIVE_TEXTURE,&active);gl.glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pack);gl.glGetIntegerv(GL_UNPACK_ALIGNMENT,&align);gl.glGetIntegerv(GL_UNPACK_ROW_LENGTH,&unpackRow);gl.glGetIntegerv(GL_UNPACK_SKIP_ROWS,&unpackSkipRows);gl.glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&unpackSkipPixels);gl.glGetIntegerv(GL_VIEWPORT,viewport);gl.glGetIntegerv(GL_DEPTH_FUNC,&depthFunction);gl.glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);gl.glGetBooleanv(GL_COLOR_WRITEMASK,colorWrite);
    GLboolean depth=gl.glIsEnabled(GL_DEPTH_TEST),blend=gl.glIsEnabled(GL_BLEND),cull=gl.glIsEnabled(GL_CULL_FACE),scissor=gl.glIsEnabled(GL_SCISSOR_TEST),srgb=gl.glIsEnabled(GL_FRAMEBUFFER_SRGB);gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);gl.glPixelStorei(GL_UNPACK_ALIGNMENT,1);gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,0);gl.glPixelStorei(GL_UNPACK_SKIP_ROWS,0);gl.glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    for(int i=0;i<2;i++){gl.glActiveTexture(GL_TEXTURE0+i);gl.glGetIntegerv(GL_TEXTURE_BINDING_2D,&bindings[i]);if(i&&!exportDepth)continue;gl.glBindTexture(GL_TEXTURE_2D,i?s->z:s->color);gl.glTexImage2D(GL_TEXTURE_2D,0,i?GL_R32F:GL_RGBA8,frame.width,frame.height,0,i?GL_RED:GL_RGBA,i?GL_FLOAT:GL_UNSIGNED_BYTE,i?static_cast<void*>(s->depth.data()):static_cast<void*>(s->rgba.data()));gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,i?GL_NEAREST:GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,i?GL_NEAREST:GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
    gl.glViewport(s->viewport[0],s->viewport[1],frame.width,frame.height);gl.glEnable(GL_DEPTH_TEST);gl.glDepthFunc(GL_ALWAYS);gl.glDepthMask(exportDepth?GL_TRUE:GL_FALSE);gl.glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);gl.glDisable(GL_BLEND);gl.glDisable(GL_CULL_FACE);gl.glDisable(GL_SCISSOR_TEST);gl.glDisable(GL_FRAMEBUFFER_SRGB);s->composite.bind();s->composite.setUniformValue("image",0);s->composite.setUniformValue("depthImage",1);s->composite.setUniformValue("exportDepth",exportDepth);gl.glBindVertexArray(s->vao);gl.AuOpenGLBase::glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    gl.glUseProgram(program);gl.glBindVertexArray(vao);gl.glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);gl.glDepthFunc(depthFunction);gl.glDepthMask(depthWrite);gl.glColorMask(colorWrite[0],colorWrite[1],colorWrite[2],colorWrite[3]);for(auto pair:{std::make_pair(GL_DEPTH_TEST,depth),{GL_BLEND,blend},{GL_CULL_FACE,cull},{GL_SCISSOR_TEST,scissor},{GL_FRAMEBUFFER_SRGB,srgb}}){if(pair.second)gl.glEnable(pair.first);else gl.glDisable(pair.first);}for(int i=0;i<2;i++){gl.glActiveTexture(GL_TEXTURE0+i);gl.glBindTexture(GL_TEXTURE_2D,bindings[i]);}gl.glActiveTexture(active);gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pack);gl.glPixelStorei(GL_UNPACK_ALIGNMENT,align);gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,unpackRow);gl.glPixelStorei(GL_UNPACK_SKIP_ROWS,unpackSkipRows);gl.glPixelStorei(GL_UNPACK_SKIP_PIXELS,unpackSkipPixels);s->muted=false;
    if(s->frame%120==0&&s->parameterPool.size()>s->usedParameterBlocks+64)s->parameterPool.resize(s->usedParameterBlocks+64);
    for(auto it=s->geometry.begin();it!=s->geometry.end();){if(s->frame-it->second->age>120)it=s->geometry.erase(it);else ++it;}
    for(auto it=s->images.begin();it!=s->images.end();){if(s->frame-it->second->age>4)it=s->images.erase(it);else ++it;}
    for(auto it=s->buffers.begin();it!=s->buffers.end();){if(s->frame-s->bufferAges[it->first]>120){s->bufferAges.erase(it->first);it=s->buffers.erase(it);}else ++it;}
    if(s->frame==1||qEnvironmentVariableIsSet("OPENSHORES_RENDER_AUDIT")||s->auditTimer.elapsed()>2000){QJsonObject report{{"renderer","Filament Vulkan full scene"},{"frame",qint64(s->frame)},{"draws",int(frame.drawCount)},{"groups",int(s->group+1)},{"width",int(frame.width)},{"height",int(frame.height)},{"boneArraysRead",int(s->boneReads)},{"boneArraysReused",int(s->boneHits)},{"exportDepth",exportDepth},{"captureMs",captureMs},{"nativeMs",nativeMs},{"elapsedMs",qint64(s->timer.elapsed())},{"families",s->coverage},{"unsupportedDraws",0}};QSaveFile file(qEnvironmentVariable("OPENSHORES_FILAMENT_STATUS_PATH",QDir(QCoreApplication::applicationDirPath()).filePath("filament-world-status.json")));if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(report).toJson());file.commit();}s->auditTimer.restart();qInfo("FILAMENT_WORLD draws=%u families=%d groups=%u bonesRead=%u bonesReused=%u elapsedMs=%lld",frame.drawCount,s->coverage.size(),s->group+1,s->boneReads,s->boneHits,static_cast<long long>(s->timer.elapsed()));}
}
