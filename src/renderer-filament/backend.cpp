#include "filament_api.h"
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <filament/RenderTarget.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/LightManager.h>
#include <filament/IndirectLight.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <utils/EntityManager.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <math/quat.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace filament;using namespace filament::math;using utils::Entity;
static_assert(sizeof(OFVertex)==60 && sizeof(OFMesh)==112 && sizeof(OFFrame)==4088,"C ABI layout mismatch");
namespace {
thread_local std::string error;
struct GPUVertex {float3 position;quatf tangent;float2 uv;ushort4 joints;float4 weights;ubyte4 color;};
struct BoneBounds {bool used=false;float3 lo={1e30f,1e30f,1e30f},hi={-1e30f,-1e30f,-1e30f};};
struct Mesh {Entity entity;VertexBuffer* vertices=nullptr;IndexBuffer* indices=nullptr;MaterialInstance *color=nullptr,*depth=nullptr;BoneBounds bounds[OF_BONES];};
struct Target {Texture *color=nullptr,*depthColor=nullptr,*z=nullptr;RenderTarget *colorTarget=nullptr,*depthTarget=nullptr;uint64_t age=0;};
struct Lamp {Entity entity;bool directional=false;};
struct Context {
 Engine* engine=nullptr;Renderer* renderer=nullptr;Scene* scene=nullptr;View *view=nullptr,*depthView=nullptr;
 Camera* camera=nullptr;Entity cameraEntity;SwapChain* swap=nullptr;Material *body=nullptr,*depth=nullptr;
 IndirectLight* indirect=nullptr;
 std::map<uint64_t,Mesh> meshes;std::map<uint64_t,Texture*> textures;
 std::map<uint64_t,Target> targets;std::vector<Lamp> lamps;std::vector<Entity> active;uint64_t age=0;
 ~Context(){if(!engine)return;engine->flushAndWait();
  for(auto& pair:meshes){auto& m=pair.second;engine->destroy(m.entity);utils::EntityManager::get().destroy(m.entity);engine->destroy(m.vertices);engine->destroy(m.indices);engine->destroy(m.color);engine->destroy(m.depth);}
  for(auto& pair:textures)engine->destroy(pair.second);
  for(auto& pair:targets){auto&t=pair.second;engine->destroy(t.colorTarget);engine->destroy(t.depthTarget);engine->destroy(t.color);engine->destroy(t.depthColor);engine->destroy(t.z);}
  for(auto& l:lamps){engine->destroy(l.entity);utils::EntityManager::get().destroy(l.entity);}
  engine->destroy(indirect);engine->destroy(body);engine->destroy(depth);engine->destroy(view);engine->destroy(depthView);engine->destroy(scene);engine->destroy(renderer);engine->destroy(swap);
  engine->destroyCameraComponent(cameraEntity);utils::EntityManager::get().destroy(cameraEntity);Engine::destroy(&engine);
 }
 Material* load(const std::string& path){std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Missing material: "+path);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});auto* m=Material::Builder().package(bytes.data(),bytes.size()).build(*engine);if(!m)throw std::runtime_error("Invalid Filament material: "+path);return m;}
 Texture* texture(const OFTexture& t){
  uint64_t key=t.key;if(!key)key=~uint64_t(0);auto found=textures.find(key);if(found!=textures.end())return found->second;
  uint32_t w=t.key?t.width:1,h=t.key?t.height:1;if(!w||!h||!t.rgba && t.key)throw std::runtime_error("Invalid texture pixels");
  auto* pixels=new std::vector<uint8_t>(size_t(w)*h*4,255);if(t.key)std::memcpy(pixels->data(),t.rgba,pixels->size());
  auto* result=Texture::Builder().width(w).height(h).levels(0xff).usage(Texture::Usage::SAMPLEABLE|Texture::Usage::UPLOADABLE|Texture::Usage::GEN_MIPMAPPABLE).format(Texture::InternalFormat::SRGB8_A8).build(*engine);
  result->setImage(*engine,0,Texture::PixelBufferDescriptor(pixels->data(),pixels->size(),Texture::Format::RGBA,Texture::Type::UBYTE,
   [](void*,size_t,void* p){delete static_cast<std::vector<uint8_t>*>(p);},pixels));result->generateMipmaps(*engine);textures[key]=result;return result;
 }
 Mesh& mesh(const OFMesh& packet){auto found=meshes.find(packet.key);if(found!=meshes.end())return found->second;
  if(!packet.vertices||!packet.vertexCount||packet.vertexCount%3)throw std::runtime_error("Invalid triangle mesh");
  auto* data=new std::vector<GPUVertex>();data->reserve(packet.vertexCount);
  for(uint32_t i=0;i<packet.vertexCount;i++){
   const auto&v=packet.vertices[i];float3 n(v.normal[0],v.normal[1],v.normal[2]);if(length(n)<1e-7f)n={0,0,1};n=normalize(n);
   float3 t=normalize(cross(std::abs(n.z)<.9f?float3(0,0,1):float3(0,1,0),n));float3 b=cross(n,t);
   float total=v.weights[0]+v.weights[1]+v.weights[2]+v.weights[3];if(!(total>0))throw std::runtime_error("Invalid bone weights");
   for(int j=0;j<4;j++)if(v.joints[j]>=OF_BONES||v.weights[j]<0)throw std::runtime_error("Invalid bone index or weight");
   data->push_back({float3(v.position[0],v.position[1],v.position[2]),mat3f::packTangentFrame(mat3f(t,b,n)),
    float2(v.uv[0],v.uv[1]),ushort4(v.joints[0],v.joints[1],v.joints[2],v.joints[3]),
    float4(v.weights[0],v.weights[1],v.weights[2],v.weights[3])/total,ubyte4(v.top>.5f?255:0,0,0,255)});
  }
  Mesh m;for(uint32_t i=0;i<packet.vertexCount;i++){const auto& v=packet.vertices[i];float3 p(v.position[0],v.position[1],v.position[2]);for(int j=0;j<4;j++)if(v.weights[j]>0){auto& b=m.bounds[v.joints[j]];b.used=true;b.lo=min(b.lo,p);b.hi=max(b.hi,p);}}
  m.entity=utils::EntityManager::get().create();m.vertices=VertexBuffer::Builder().vertexCount(packet.vertexCount).bufferCount(1)
   .attribute(VertexAttribute::POSITION,0,VertexBuffer::AttributeType::FLOAT3,offsetof(GPUVertex,position),sizeof(GPUVertex))
   .attribute(VertexAttribute::TANGENTS,0,VertexBuffer::AttributeType::FLOAT4,offsetof(GPUVertex,tangent),sizeof(GPUVertex))
   .attribute(VertexAttribute::UV0,0,VertexBuffer::AttributeType::FLOAT2,offsetof(GPUVertex,uv),sizeof(GPUVertex))
   .attribute(VertexAttribute::BONE_INDICES,0,VertexBuffer::AttributeType::USHORT4,offsetof(GPUVertex,joints),sizeof(GPUVertex))
   .attribute(VertexAttribute::BONE_WEIGHTS,0,VertexBuffer::AttributeType::FLOAT4,offsetof(GPUVertex,weights),sizeof(GPUVertex))
   .attribute(VertexAttribute::COLOR,0,VertexBuffer::AttributeType::UBYTE4,offsetof(GPUVertex,color),sizeof(GPUVertex)).normalized(VertexAttribute::COLOR).build(*engine);
  m.vertices->setBufferAt(*engine,0,VertexBuffer::BufferDescriptor(data->data(),data->size()*sizeof(GPUVertex),[](void*,size_t,void*p){delete static_cast<std::vector<GPUVertex>*>(p);},data));
  auto* ids=new std::vector<uint32_t>(packet.vertexCount);for(uint32_t i=0;i<packet.vertexCount;i+=3){(*ids)[i]=i;(*ids)[i+1]=i+2;(*ids)[i+2]=i+1;}
  m.indices=IndexBuffer::Builder().indexCount(packet.vertexCount).bufferType(IndexBuffer::IndexType::UINT).build(*engine);
  m.indices->setBuffer(*engine,IndexBuffer::BufferDescriptor(ids->data(),ids->size()*4,[](void*,size_t,void*p){delete static_cast<std::vector<uint32_t>*>(p);},ids));
  m.color=body->createInstance();m.depth=depth->createInstance();
  RenderableManager::Builder(1).boundingBox({{0,0,0},{100000,100000,100000}}).geometry(0,RenderableManager::PrimitiveType::TRIANGLES,m.vertices,m.indices)
   .material(0,m.color).skinning(OF_BONES).culling(false).castShadows(true).receiveShadows(true).build(*engine,m.entity);
  return meshes.emplace(packet.key,m).first->second;
 }
 Target& target(uint32_t w,uint32_t h){uint64_t key=(uint64_t(w)<<32)|h;auto it=targets.find(key);if(it!=targets.end()){it->second.age=++age;return it->second;}
  if(targets.size()>=16){auto oldest=std::min_element(targets.begin(),targets.end(),[](auto&a,auto&b){return a.second.age<b.second.age;});auto&t=oldest->second;
   engine->destroy(t.colorTarget);engine->destroy(t.depthTarget);engine->destroy(t.color);engine->destroy(t.depthColor);engine->destroy(t.z);targets.erase(oldest);}
  Target t;t.age=++age;
  t.color=Texture::Builder().width(w).height(h).levels(1).usage(Texture::Usage::COLOR_ATTACHMENT|Texture::Usage::SAMPLEABLE|Texture::Usage::BLIT_SRC).format(Texture::InternalFormat::RGBA8).build(*engine);
  t.depthColor=Texture::Builder().width(w).height(h).levels(1).usage(Texture::Usage::COLOR_ATTACHMENT|Texture::Usage::SAMPLEABLE|Texture::Usage::BLIT_SRC).format(Texture::InternalFormat::R32F).build(*engine);
  t.z=Texture::Builder().width(w).height(h).levels(1).usage(Texture::Usage::DEPTH_ATTACHMENT).format(Texture::InternalFormat::DEPTH32F).build(*engine);
  t.colorTarget=RenderTarget::Builder().texture(RenderTarget::AttachmentPoint::COLOR,t.color).texture(RenderTarget::AttachmentPoint::DEPTH,t.z).build(*engine);
  t.depthTarget=RenderTarget::Builder().texture(RenderTarget::AttachmentPoint::COLOR,t.depthColor).texture(RenderTarget::AttachmentPoint::DEPTH,t.z).build(*engine);
  return targets.emplace(key,t).first->second;
 }
};
}
extern "C" OF_EXPORT const char* of_error(){return error.c_str();}
extern "C" OF_EXPORT void* of_create(const char* directory){try{
 Engine::Config config;config.driverHandleArenaSizeMB=16;
 auto c=std::make_unique<Context>();c->engine=Engine::create(Engine::Backend::VULKAN,nullptr,nullptr,&config);if(!c->engine)throw std::runtime_error("Vulkan engine creation failed");
 c->renderer=c->engine->createRenderer();c->scene=c->engine->createScene();c->view=c->engine->createView();c->depthView=c->engine->createView();
 c->cameraEntity=utils::EntityManager::get().create();c->camera=c->engine->createCamera(c->cameraEntity);c->camera->setModelMatrix(mat4());c->camera->setExposure(16,1.f/125,100);
 c->view->setScene(c->scene);c->view->setCamera(c->camera);c->view->setBlendMode(View::BlendMode::TRANSLUCENT);c->view->setAntiAliasing(View::AntiAliasing::NONE);c->view->setSampleCount(1);
 c->depthView->setScene(c->scene);c->depthView->setCamera(c->camera);c->depthView->setPostProcessingEnabled(false);c->depthView->setAntiAliasing(View::AntiAliasing::NONE);c->depthView->setSampleCount(1);
 c->swap=c->engine->createSwapChain(1,1);c->body=c->load(std::string(directory)+"/body.filamat");c->depth=c->load(std::string(directory)+"/depth.filamat");
 error.clear();return c.release();
 }catch(const std::exception&e){error=e.what();return nullptr;}}
extern "C" OF_EXPORT void of_destroy(void* pointer){delete static_cast<Context*>(pointer);}
extern "C" OF_EXPORT void of_release(void* pointer,uint64_t key){
 auto&c=*static_cast<Context*>(pointer);auto it=c.meshes.find(key);if(it==c.meshes.end())return;
 auto&m=it->second;c.scene->remove(m.entity);c.active.erase(std::remove(c.active.begin(),c.active.end(),m.entity),c.active.end());
 c.engine->destroy(m.entity);utils::EntityManager::get().destroy(m.entity);c.engine->destroy(m.vertices);c.engine->destroy(m.indices);c.engine->destroy(m.color);c.engine->destroy(m.depth);c.meshes.erase(it);
}
extern "C" OF_EXPORT int of_draw(void* pointer,const OFFrame* f,uint8_t* rgba,float* depths){try{
 if(!pointer||!f||f->abiVersion!=OF_ABI_VERSION||!f->width||!f->height||f->width>4096||f->height>4096||!rgba||!depths)throw std::runtime_error("Invalid Filament ABI frame");
 auto& c=*static_cast<Context*>(pointer);auto& e=*c.engine;auto& rm=e.getRenderableManager();auto& tm=e.getTransformManager();
 if(!c.active.empty())c.scene->removeEntities(c.active.data(),c.active.size());c.active.clear();
 mat4f projection,model,bones[OF_BONES];std::memcpy(&projection,f->projection,64);std::memcpy(&model,f->modelView,64);std::memcpy(bones,f->bones,sizeof bones);
 double nearPlane=.001,farPlane=100000;if(std::abs(f->projection[11])>.5f){nearPlane=std::abs(f->projection[14]/(f->projection[10]-1));if(std::abs(f->projection[10]+1)>1e-7)farPlane=std::abs(f->projection[14]/(f->projection[10]+1));}
 c.camera->setCustomProjection(mat4(projection),std::max(.0001,nearPlane),std::max(nearPlane+1,farPlane));
 TextureSampler sampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,TextureSampler::MagFilter::LINEAR,TextureSampler::WrapMode::REPEAT);
 for(uint32_t i=0;i<f->meshCount;i++){
  const auto& p=f->meshes[i];auto&m=c.mesh(p);auto instance=rm.getInstance(m.entity);rm.setBones(instance,bones,OF_BONES);tm.setTransform(tm.getInstance(m.entity),model);
  float3 lo(1e30f),hi(-1e30f);for(int b=0;b<OF_BONES;b++)if(m.bounds[b].used){auto& box=m.bounds[b];for(int k=0;k<8;k++){float4 p=bones[b]*float4(k&1?box.hi.x:box.lo.x,k&2?box.hi.y:box.lo.y,k&4?box.hi.z:box.lo.z,1);float3 v=p.xyz/p.w;lo=min(lo,v);hi=max(hi,v);}}
  rm.setAxisAlignedBoundingBox(instance,{(lo+hi)*.5f,max((hi-lo)*.5f,float3(.001f))});
  for(auto* material:{m.color,m.depth}){
   material->setParameter("skin",c.texture(p.skin),sampler);material->setParameter("clothes",c.texture(p.clothes),sampler);
   material->setParameter("skinOffset",float4(p.skinOffset[0],p.skinOffset[1],p.skinOffset[2],p.skinOffset[3]));
   material->setParameter("clothesOffsetWear",float3(p.clothesOffsetWear[0],p.clothesOffsetWear[1],p.clothesOffsetWear[2]));
  }
  m.color->setParameter("roughness",p.roughness);m.color->setParameter("metallic",p.metallic);m.depth->setParameter("projection",projection);
  rm.setMaterialInstanceAt(instance,0,m.color);c.active.push_back(m.entity);
 }
 if(!c.active.empty())c.scene->addEntities(c.active.data(),c.active.size());
 auto& lm=e.getLightManager();
 uint32_t shadowLamps=0;
 for(uint32_t i=0;i<std::min<uint32_t>(OF_MAX_LIGHTS,f->lightCount);i++){
  auto&p=f->lights[i];if(c.lamps.size()<=i)c.lamps.push_back({utils::EntityManager::get().create(),bool(p.directional)});
  auto&l=c.lamps[i];auto instance=lm.getInstance(l.entity);
  if(!instance||l.directional!=bool(p.directional)){
   if(instance){e.destroy(l.entity);c.scene->remove(l.entity);}l.directional=bool(p.directional);
   LightManager::Builder(l.directional?LightManager::Type::DIRECTIONAL:LightManager::Type::POINT).color({1,1,1}).intensity(1).castShadows(l.directional).build(e,l.entity);
   c.scene->addEntity(l.entity);instance=lm.getInstance(l.entity);
  }
  lm.setColor(instance,{p.color[0],p.color[1],p.color[2]});lm.setIntensity(instance,p.intensity);
  bool cast=p.directional||(shadowLamps++<std::min<uint32_t>(4,f->shadowLights));lm.setShadowCaster(instance,cast);
  LightManager::ShadowOptions options;options.mapSize=1024;options.constantBias=.001f;options.normalBias=.5f;options.shadowFar=300;lm.setShadowOptions(instance,options);
  if(l.directional)lm.setDirection(instance,normalize(float3(p.position[0],p.position[1],p.position[2])));else{lm.setPosition(instance,{p.position[0],p.position[1],p.position[2]});lm.setFalloff(instance,20000);}
 }
 for(size_t i=f->lightCount;i<c.lamps.size();i++){auto instance=lm.getInstance(c.lamps[i].entity);if(instance)lm.setIntensity(instance,0);}
 if(c.indirect){c.scene->setIndirectLight(nullptr);e.destroy(c.indirect);}
 float3 ambient(f->ambient[0]*3.5449f,f->ambient[1]*3.5449f,f->ambient[2]*3.5449f);
 c.indirect=IndirectLight::Builder().irradiance(1,&ambient).intensity(20000).build(e);c.scene->setIndirectLight(c.indirect);
 auto&t=c.target(f->width,f->height);c.view->setViewport({0,0,f->width,f->height});c.depthView->setViewport({0,0,f->width,f->height});c.view->setRenderTarget(t.colorTarget);c.depthView->setRenderTarget(t.depthTarget);
 if(!c.renderer->beginFrame(c.swap))throw std::runtime_error("Filament skipped offscreen frame");
 c.renderer->setClearOptions({{0,0,0,0},true,true});c.renderer->render(c.view);
 c.renderer->readPixels(t.colorTarget,0,0,f->width,f->height,backend::PixelBufferDescriptor(rgba,size_t(f->width)*f->height*4,backend::PixelDataFormat::RGBA,backend::PixelDataType::UBYTE));
 for(uint32_t i=0;i<f->meshCount;i++){auto&m=c.meshes.at(f->meshes[i].key);rm.setMaterialInstanceAt(rm.getInstance(m.entity),0,m.depth);}
 c.renderer->setClearOptions({{1,1,1,1},true,true});c.renderer->render(c.depthView);
 c.renderer->readPixels(t.depthTarget,0,0,f->width,f->height,backend::PixelBufferDescriptor(depths,size_t(f->width)*f->height*4,backend::PixelDataFormat::R,backend::PixelDataType::FLOAT));
 c.renderer->endFrame();e.flushAndWait();
 // Vulkan readback is top-down. The C ABI deliberately returns OpenGL-style
 // bottom-row-first color AND depth; this also keeps world occlusion aligned.
 for(uint32_t y=0;y<f->height/2;y++){
  size_t opposite=f->height-1-y;
  std::swap_ranges(rgba+size_t(y)*f->width*4,rgba+size_t(y+1)*f->width*4,rgba+opposite*f->width*4);
  std::swap_ranges(depths+size_t(y)*f->width,depths+size_t(y+1)*f->width,depths+opposite*f->width);
 }
 error.clear();return 1;
 }catch(const std::exception&ex){error=ex.what();return 0;}}
