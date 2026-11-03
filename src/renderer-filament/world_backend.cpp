// The world renderer shares the proven Vulkan/context/readback implementation,
// but submits the complete scene once instead of one engine draw per body.
#include "backend.cpp"
#include "world_api.h"
#include <set>
#include <ktxreader/Ktx1Reader.h>
#include <filesystem>

namespace {
struct WorldGPUVertex {float3 position;quatf tangent;float4 attributes[8];float2 corner;};
struct WorldGeometry {VertexBuffer* vertices=nullptr;IndexBuffer* indices=nullptr;uint32_t primitive=0;uint64_t age=0;};
struct WorldInstance {Entity entity;MaterialInstance *color=nullptr,*depth=nullptr;uint64_t geometry=0,age=0,basisRevision=0;std::string material;uint32_t blend=0;std::vector<uint64_t> parameterHashes,textureKeys;};
uint64_t parameterHash(const OFWParameter&p){
    uint64_t h=1469598103934665603ull;auto hash=[&](const void*data,size_t size){auto*b=static_cast<const uint8_t*>(data);while(size--){h^=*b++;h*=1099511628211ull;}};
    hash(p.name,std::strlen(p.name));hash(&p.type,sizeof p.type);hash(&p.count,sizeof p.count);
    if(p.type>=OFW_TEXTURE2D)hash(&p.texture.key,sizeof p.texture.key);
    else{uint32_t n=p.type<=OFW_FLOAT4?p.type:p.type<=OFW_INT4?p.type-OFW_INT+1:p.type==OFW_MAT4?16:p.type==OFW_MAT3?9:p.type==OFW_MAT2?4:1;hash(p.values,size_t(n)*p.count*sizeof(float));}return h;
}
struct WorldContext {
    std::unique_ptr<Context> core;std::string directory;uint64_t serial=0;bool lastDepth=false;
    std::map<uint64_t,WorldGeometry> geometry;std::map<uint64_t,WorldInstance> instances;
    std::map<std::string,Material*> materials;std::map<uint64_t,Texture*> textures;
    std::map<uint64_t,uint64_t> textureAges;
    Texture* environment=nullptr;float3 environmentSH[9]={};float lastAmbient[3]={-1,-1,-1};bool studio=false;uint64_t basisRevision=0;mat3f lastBasis;
    ~WorldContext(){if(!core)return;auto&e=*core->engine;e.flushAndWait();
        for(auto&p:instances){e.destroy(p.second.entity);utils::EntityManager::get().destroy(p.second.entity);e.destroy(p.second.color);if(p.second.depth)e.destroy(p.second.depth);}
        for(auto&p:geometry){e.destroy(p.second.vertices);e.destroy(p.second.indices);}
        for(auto&p:materials)e.destroy(p.second);for(auto&p:textures)e.destroy(p.second);
        if(core->indirect){core->scene->setIndirectLight(nullptr);e.destroy(core->indirect);core->indirect=nullptr;}if(environment)e.destroy(environment);
    }
    Material* material(const std::string& key,const std::string& variant){std::string name=key+"-"+variant;auto it=materials.find(name);if(it!=materials.end())return it->second;return materials[name]=core->load(directory+"/"+name+".filamat");}
    WorldGeometry& mesh(const OFWDraw& d){auto found=geometry.find(d.geometryKey);if(found!=geometry.end()){found->second.age=serial;return found->second;}
        if(!d.vertices||!d.indices||!d.vertexCount||!d.indexCount)throw std::runtime_error("Empty native world geometry");
        auto* data=new std::vector<WorldGPUVertex>(d.vertexCount);
        for(uint32_t i=0;i<d.vertexCount;i++){auto&v=d.vertices[i];auto&out=(*data)[i];out.position={v.position[0],v.position[1],v.position[2]};float3 n(v.normal[0],v.normal[1],v.normal[2]);if(length(n)<1e-7)n={0,0,1};n=normalize(n);auto t=normalize(cross(std::abs(n.z)<.9f?float3(0,0,1):float3(0,1,0),n));out.tangent=mat3f::packTangentFrame(mat3f(t,cross(n,t),n));std::memcpy(out.attributes,v.attributes,sizeof out.attributes);out.corner={v.corner[0],v.corner[1]};}
        WorldGeometry result;result.age=serial;result.primitive=d.primitive;
        VertexBuffer::Builder builder;builder.vertexCount(d.vertexCount).bufferCount(1)
            .attribute(VertexAttribute::POSITION,0,VertexBuffer::AttributeType::FLOAT3,offsetof(WorldGPUVertex,position),sizeof(WorldGPUVertex))
            .attribute(VertexAttribute::TANGENTS,0,VertexBuffer::AttributeType::FLOAT4,offsetof(WorldGPUVertex,tangent),sizeof(WorldGPUVertex))
            .attribute(VertexAttribute::UV1,0,VertexBuffer::AttributeType::FLOAT2,offsetof(WorldGPUVertex,corner),sizeof(WorldGPUVertex));
        for(int i=0;i<8;i++)builder.attribute(static_cast<VertexAttribute>(uint8_t(VertexAttribute::CUSTOM0)+i),0,VertexBuffer::AttributeType::FLOAT4,offsetof(WorldGPUVertex,attributes)+i*sizeof(float4),sizeof(WorldGPUVertex));
        result.vertices=builder.build(*core->engine);result.vertices->setBufferAt(*core->engine,0,VertexBuffer::BufferDescriptor(data->data(),data->size()*sizeof(WorldGPUVertex),[](void*,size_t,void*p){delete static_cast<std::vector<WorldGPUVertex>*>(p);},data));
        auto* indices=new std::vector<uint32_t>(d.indices,d.indices+d.indexCount);result.indices=IndexBuffer::Builder().indexCount(d.indexCount).bufferType(IndexBuffer::IndexType::UINT).build(*core->engine);
        result.indices->setBuffer(*core->engine,IndexBuffer::BufferDescriptor(indices->data(),indices->size()*4,[](void*,size_t,void*p){delete static_cast<std::vector<uint32_t>*>(p);},indices));
        return geometry.emplace(d.geometryKey,result).first->second;
    }
    Texture* texture(const OFWTexture& source){uint64_t key=source.key?source.key:~uint64_t(source.kind);textureAges[key]=serial;auto it=textures.find(key);if(it!=textures.end())return it->second;
        uint32_t w=source.key?source.width:1,h=source.key?source.height:1,layers=source.key?source.layers:source.kind==OFW_TEXTURE_CUBE?6:1;
        if(!w||!h||!layers)throw std::runtime_error("Invalid world texture extent");
        auto* pixels=new std::vector<uint8_t>(size_t(w)*h*layers*4,0);if(source.key)std::memcpy(pixels->data(),source.rgba,pixels->size());else for(size_t i=3;i<pixels->size();i+=4)(*pixels)[i]=255;
        Texture::Builder b;b.width(w).height(h).levels(0xff).usage(Texture::Usage::SAMPLEABLE|Texture::Usage::UPLOADABLE|Texture::Usage::GEN_MIPMAPPABLE).format(Texture::InternalFormat::SRGB8_A8);
        if(source.kind==OFW_TEXTURE_CUBE)b.sampler(Texture::Sampler::SAMPLER_CUBEMAP);else if(source.kind==OFW_TEXTURE_ARRAY)b.sampler(Texture::Sampler::SAMPLER_2D_ARRAY).depth(layers);
        auto* t=b.build(*core->engine);t->setImage(*core->engine,0,0,0,0,w,h,layers,Texture::PixelBufferDescriptor(pixels->data(),pixels->size(),Texture::Format::RGBA,Texture::Type::UBYTE,[](void*,size_t,void*p){delete static_cast<std::vector<uint8_t>*>(p);},pixels));t->generateMipmaps(*core->engine);return textures[key]=t;
    }
    void parameter(MaterialInstance* m,const OFWParameter& p){
        switch(p.type){
            case OFW_FLOAT:m->setParameter(p.name,p.values,p.count);break;
            case OFW_FLOAT2:m->setParameter(p.name,reinterpret_cast<const float2*>(p.values),p.count);break;
            case OFW_FLOAT3:m->setParameter(p.name,reinterpret_cast<const float3*>(p.values),p.count);break;
            case OFW_FLOAT4:m->setParameter(p.name,reinterpret_cast<const float4*>(p.values),p.count);break;
            case OFW_MAT3:m->setParameter(p.name,reinterpret_cast<const mat3f*>(p.values),p.count);break;
            case OFW_MAT4:m->setParameter(p.name,reinterpret_cast<const mat4f*>(p.values),p.count);break;
            case OFW_BOOL:{std::unique_ptr<bool[]> values(new bool[p.count]);for(uint32_t i=0;i<p.count;i++)values[i]=p.values[i]!=0;m->setParameter(p.name,values.get(),p.count);break;}
            case OFW_INT:case OFW_INT2:case OFW_INT3:case OFW_INT4:{uint32_t n=p.type-OFW_INT+1;std::vector<int32_t> values(p.count*n);for(size_t i=0;i<values.size();i++)values[i]=int32_t(p.values[i]);
                if(n==1)m->setParameter(p.name,values.data(),p.count);if(n==2)m->setParameter(p.name,reinterpret_cast<const int2*>(values.data()),p.count);if(n==3)m->setParameter(p.name,reinterpret_cast<const int3*>(values.data()),p.count);if(n==4)m->setParameter(p.name,reinterpret_cast<const int4*>(values.data()),p.count);break;}
            case OFW_TEXTURE2D:case OFW_TEXTURE_ARRAY:case OFW_TEXTURE_CUBE:{TextureSampler sampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,TextureSampler::MagFilter::LINEAR,TextureSampler::WrapMode::REPEAT);m->setParameter(p.name,texture(p.texture),sampler);break;}
            default:throw std::runtime_error("Unsupported native world uniform type");
        }
    }
};
}
extern "C" OF_EXPORT const char* ofw_error(){return error.c_str();}
extern "C" OF_EXPORT void* ofw_create(const char* directory,const char* bootstrap){try{
    auto world=std::make_unique<WorldContext>();world->directory=directory;world->core.reset(static_cast<Context*>(of_create(bootstrap)));if(!world->core)throw std::runtime_error(error);
    auto path=std::filesystem::path(directory).parent_path()/"filament-environment"/"lightroom_14b_ibl.ktx";
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Missing preview lighting environment: "+path.string());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});auto* bundle=new image::Ktx1Bundle(bytes.data(),uint32_t(bytes.size()));
    if(!bundle->getSphericalHarmonics(world->environmentSH)){delete bundle;throw std::runtime_error("Preview environment is missing irradiance coefficients");}
    world->environment=ktxreader::Ktx1Reader::createTexture(world->core->engine,bundle,false);
    if(!world->environment)throw std::runtime_error("Cannot upload preview environment");
    world->core->engine->flushAndWait();
    world->core->view->setAntiAliasing(std::getenv("OPENSHORES_DISABLE_FXAA")?View::AntiAliasing::NONE:View::AntiAliasing::FXAA);
    return world.release();
}catch(const std::exception&e){error=e.what();return nullptr;}}
extern "C" OF_EXPORT void ofw_destroy(void* pointer){delete static_cast<WorldContext*>(pointer);}
extern "C" OF_EXPORT int ofw_render(void* pointer,const OFWFrame* f,uint8_t* rgba,float* depths){try{
    if(!pointer||!f||f->abiVersion!=OFW_ABI||!f->width||!f->height||f->width>8192||f->height>8192)throw std::runtime_error("Invalid native world frame");
    auto&w=*static_cast<WorldContext*>(pointer);auto&c=*w.core;auto&e=*c.engine;auto&rm=e.getRenderableManager();++w.serial; if(depths&&!w.lastDepth)for(auto&entry:w.instances)entry.second.parameterHashes.clear();w.lastDepth=depths!=nullptr;
    mat3f rotation;bool oriented=false;for(int i=0;i<9;i++)oriented|=std::abs(f->environmentRotation[i])>1e-6f;if(oriented)std::memcpy(&rotation,f->environmentRotation,36);auto eyeToWorld3=transpose(rotation);mat4f eyeToWorld(eyeToWorld3),worldToEye(rotation);c.camera->setModelMatrix(mat4(eyeToWorld));if(!w.basisRevision||std::memcmp(&w.lastBasis,&rotation,36)){w.lastBasis=rotation;++w.basisRevision;}
    auto previousActive=std::move(c.active);c.active.clear();
    mat4f projection;std::memcpy(&projection,f->projection,64);double nearPlane=.001,farPlane=100000; if(std::abs(f->projection[11])>.5f){nearPlane=std::abs(f->projection[14]/(f->projection[10]-1));if(std::abs(f->projection[10]+1)>1e-7)farPlane=std::abs(f->projection[14]/(f->projection[10]+1));} c.camera->setCustomProjection(mat4(projection),std::max(.0001,nearPlane),std::max(nearPlane+1,farPlane));
    std::set<uint32_t> groups;std::map<uint32_t,uint32_t> worldDraws;
    std::vector<WorldInstance*> currentInstances;
    for(uint32_t i=0;i<f->drawCount;i++){const auto&d=f->draws[i];auto&mesh=w.mesh(d);auto it=w.instances.find(d.instanceKey);std::string key=d.material;uint32_t blend=d.blend;
        if(it!=w.instances.end()&&(it->second.geometry!=d.geometryKey||it->second.material!=key||it->second.blend!=blend)){
            e.destroy(it->second.entity);utils::EntityManager::get().destroy(it->second.entity);e.destroy(it->second.color);if(it->second.depth)e.destroy(it->second.depth);w.instances.erase(it);it=w.instances.end();}
        if(it==w.instances.end()){
            WorldInstance instance;instance.entity=utils::EntityManager::get().create();instance.geometry=d.geometryKey;instance.material=key;instance.blend=blend;
            instance.color=w.material(key,blend==2?"additive":blend==1?"transparent":"opaque")->createInstance();if(depths)instance.depth=w.material(key,"depth")->createInstance();
            auto primitive=d.primitive==1?RenderableManager::PrimitiveType::LINES:RenderableManager::PrimitiveType::TRIANGLES;
            RenderableManager::Builder(1).boundingBox({{d.center[0],d.center[1],d.center[2]},{d.extent[0],d.extent[1],d.extent[2]}}).geometry(0,primitive,mesh.vertices,mesh.indices).material(0,instance.color).culling(false).castShadows(d.priority==4&&d.depthWrite).receiveShadows(true).priority(uint8_t(d.priority)).build(e,instance.entity);
            it=w.instances.emplace(d.instanceKey,instance).first;
        }
        auto&instance=it->second;currentInstances.push_back(&instance);instance.age=w.serial;auto ri=rm.getInstance(instance.entity);if(depths&&!instance.depth){instance.depth=w.material(key,"depth")->createInstance();instance.parameterHashes.clear();instance.basisRevision=0;}
        rm.setPriority(ri,uint8_t(d.priority));rm.setCastShadows(ri,d.priority==4&&d.depthWrite);
        bool bounded=key.starts_with("Body")||key.starts_with("Animal")||key.starts_with("Flora")||key.starts_with("Model")||key.starts_with("Sc__")||key.starts_with("Rock");
        rm.setCulling(ri,bounded&&d.priority==4);
        float3 center=eyeToWorld3*float3(d.center[0],d.center[1],d.center[2]),extent;for(int c=0;c<3;c++)extent[c]=std::max(.001f,std::abs(eyeToWorld3[0][c])*d.extent[0]+std::abs(eyeToWorld3[1][c])*d.extent[1]+std::abs(eyeToWorld3[2][c])*d.extent[2]);rm.setAxisAlignedBoundingBox(ri,{center,extent});
        if(instance.basisRevision!=w.basisRevision){for(auto*m:{instance.color,instance.depth})if(m){m->setParameter("os_eyeToWorld",eyeToWorld);m->setParameter("os_worldToEye",worldToEye);}instance.basisRevision=w.basisRevision;}
        rm.setLayerMask(ri,0xff,uint8_t(1u<<std::min(7u,d.group)));groups.insert(std::min(7u,d.group));
        if(d.priority==4&&d.depthTest)++worldDraws[d.group];
        if(instance.parameterHashes.empty()&&instance.color->getMaterial()->hasParameter("os_surfaceProfile")){instance.color->setParameter("os_surfaceProfile",float4(.75f,0.f,.5f,.86f));if(instance.depth)instance.depth->setParameter("os_surfaceProfile",float4(.75f,0.f,.5f,.86f));}
        if(instance.parameterHashes.size()!=d.parameterCount){instance.parameterHashes.assign(d.parameterCount,0);instance.textureKeys.assign(d.parameterCount,0);}
        for(uint32_t p=0;p<d.parameterCount;p++){const auto&parameter=d.parameters[p];if(parameter.type>=OFW_TEXTURE2D){auto key=parameter.texture.key?parameter.texture.key:~uint64_t(parameter.texture.kind);w.textureAges[key]=w.serial;instance.textureKeys[p]=key;}auto h=parameterHash(parameter);if(h!=instance.parameterHashes[p]){w.parameter(instance.color,parameter);if(depths)w.parameter(instance.depth,parameter);instance.parameterHashes[p]=h;}}
        for(auto*m:{instance.color,depths?instance.depth:nullptr}){if(!m)continue;
            m->setDepthCulling(d.depthTest!=0);m->setDepthWrite(d.depthWrite!=0);m->setColorWrite(d.colorWrite!=0);
            m->setCullingMode(d.cull==1?MaterialInstance::CullingMode::BACK:d.cull==2?MaterialInstance::CullingMode::FRONT:MaterialInstance::CullingMode::NONE);
            if(d.scissor)m->setScissor(std::max(0,d.scissorBox[0]),std::max(0,d.scissorBox[1]),std::max(0,d.scissorBox[2]),std::max(0,d.scissorBox[3]));else m->unsetScissor();
        }
        if(depths)instance.depth->setParameter("os_writeDepth",d.depthWrite?1.f:0.f);c.active.push_back(instance.entity);
        if(depths)instance.depth->setColorWrite(d.depthWrite!=0);
    }
    std::set<uint32_t> oldIds,newIds;for(auto entity:previousActive)oldIds.insert(entity.getId());for(auto entity:c.active)newIds.insert(entity.getId());
    std::vector<Entity> removed,added;for(auto entity:previousActive)if(!newIds.count(entity.getId()))removed.push_back(entity);for(auto entity:c.active)if(!oldIds.count(entity.getId()))added.push_back(entity);
    if(!removed.empty())c.scene->removeEntities(removed.data(),removed.size());if(!added.empty())c.scene->addEntities(added.data(),added.size());
    auto&lm=e.getLightManager();uint32_t shadowCount=0;
    for(uint32_t i=0;i<std::min<uint32_t>(OF_MAX_LIGHTS,f->lightCount);i++){auto&p=f->lights[i];if(c.lamps.size()<=i)c.lamps.push_back({utils::EntityManager::get().create(),bool(p.directional)});auto&l=c.lamps[i];auto li=lm.getInstance(l.entity);
        if(!li||l.directional!=bool(p.directional)){if(li)e.destroy(l.entity);l.directional=p.directional!=0;LightManager::Builder(l.directional?LightManager::Type::DIRECTIONAL:LightManager::Type::POINT).intensity(1).build(e,l.entity);c.scene->addEntity(l.entity);li=lm.getInstance(l.entity);}
        lm.setColor(li,{p.color[0],p.color[1],p.color[2]});lm.setIntensity(li,p.intensity);lm.setShadowCaster(li,p.directional||shadowCount++<f->shadowLights);LightManager::ShadowOptions options;options.mapSize=p.directional?2048:1024;options.shadowFar=1500;options.constantBias=.00328f;options.normalBias=.5;lm.setShadowOptions(li,options);
        if(p.directional)lm.setDirection(li,normalize(eyeToWorld3*float3(p.position[0],p.position[1],p.position[2])));else{lm.setPosition(li,eyeToWorld3*float3(p.position[0],p.position[1],p.position[2]));lm.setFalloff(li,20000);}
    }
    for(size_t i=f->lightCount;i<c.lamps.size();i++){auto li=lm.getInstance(c.lamps[i].entity);if(li)lm.setIntensity(li,0);}
    bool studio=f->lightingMode==1;
    // gltf_viewer derives its key light from the IBL after loading settings.
    if(studio&&!c.lamps.empty()){auto d=IndirectLight::getDirectionEstimate(w.environmentSH);auto color=IndirectLight::getColorEstimate(w.environmentSH,d);auto li=lm.getInstance(c.lamps[0].entity);lm.setDirection(li,d);lm.setColor(li,color.rgb);lm.setIntensity(li,color.w*30000.f);}

    if(!c.indirect||studio!=w.studio||std::memcmp(w.lastAmbient,f->ambient,12)){
        if(c.indirect){c.scene->setIndirectLight(nullptr);e.destroy(c.indirect);}
        float mean=(f->ambient[0]+f->ambient[1]+f->ambient[2])/3.f;
        float strength=studio?1.f:std::clamp(mean/.23f,0.f,1.5f);
        float3 tint(1);if(!studio&&mean>1e-6f)tint=float3(1)*.75f+clamp(float3(f->ambient[0],f->ambient[1],f->ambient[2])/mean,float3(.5f),float3(1.5f))*.25f;
        float3 sh[9];for(int i=0;i<9;i++)sh[i]=w.environmentSH[i]*tint;
        c.indirect=IndirectLight::Builder().irradiance(3,sh).reflections(w.environment).intensity(14000*strength).build(e);c.scene->setIndirectLight(c.indirect);
        std::memcpy(w.lastAmbient,f->ambient,12);w.studio=studio;
    }
    if(std::getenv("OPENSHORES_DISABLE_IBL"))c.scene->setIndirectLight(nullptr);
    auto&target=c.target(f->width,f->height);c.view->setViewport({0,0,f->width,f->height});c.depthView->setViewport({0,0,f->width,f->height});c.view->setRenderTarget(target.colorTarget);c.depthView->setRenderTarget(target.depthTarget);
    // The encoded depth pass is unlit and must not regenerate lamp/sun maps.
    c.depthView->setShadowingEnabled(false);
    if(!c.renderer->beginFrame(c.swap))throw std::runtime_error("Native world frame skipped");
    bool first=true;
    for(auto*instance:currentInstances)rm.setMaterialInstanceAt(rm.getInstance(instance->entity),0,instance->color);
    uint32_t aoGroup=0,largest=0;for(auto item:worldDraws)if(item.second>largest){largest=item.second;aoGroup=item.first;}
    for(auto group:groups){c.view->setVisibleLayers(0xff,uint8_t(1u<<group));
        AmbientOcclusionOptions ao;ao.enabled=!std::getenv("OPENSHORES_DISABLE_AO")&&group==aoGroup&&largest>0;ao.radius=.984252f;ao.bias=.00164f;ao.resolution=.5f;ao.intensity=1.f;ao.power=1.f;ao.quality=QualityLevel::LOW;c.view->setAmbientOcclusionOptions(ao);
        Renderer::ClearOptions clear;clear.clear=first;clear.discard=first;clear.clearColor={f->clearColor[0],f->clearColor[1],f->clearColor[2],f->clearColor[3]};c.renderer->setClearOptions(clear);c.renderer->render(c.view);first=false;}
    c.renderer->readPixels(target.colorTarget,0,0,f->width,f->height,backend::PixelBufferDescriptor(rgba,size_t(f->width)*f->height*4,backend::PixelDataFormat::RGBA,backend::PixelDataType::UBYTE));
    if(depths){first=true;
    for(auto*instance:currentInstances)rm.setMaterialInstanceAt(rm.getInstance(instance->entity),0,instance->depth);
    for(auto group:groups){c.depthView->setVisibleLayers(0xff,uint8_t(1u<<group));
        Renderer::ClearOptions clear;clear.clear=first;clear.discard=first;clear.clearColor={1,1,1,1};c.renderer->setClearOptions(clear);c.renderer->render(c.depthView);first=false;}
    c.renderer->readPixels(target.depthTarget,0,0,f->width,f->height,backend::PixelBufferDescriptor(depths,size_t(f->width)*f->height*4,backend::PixelDataFormat::R,backend::PixelDataType::FLOAT));}c.renderer->endFrame();e.flushAndWait();
    for(uint32_t y=0;y<f->height/2;y++){size_t opposite=f->height-1-y;std::swap_ranges(rgba+size_t(y)*f->width*4,rgba+size_t(y+1)*f->width*4,rgba+opposite*f->width*4);if(depths)std::swap_ranges(depths+size_t(y)*f->width,depths+size_t(y+1)*f->width,depths+opposite*f->width);}
    for(auto it=w.instances.begin();it!=w.instances.end();){if(w.serial-it->second.age>120){e.destroy(it->second.entity);utils::EntityManager::get().destroy(it->second.entity);e.destroy(it->second.color);if(it->second.depth)e.destroy(it->second.depth);it=w.instances.erase(it);}else ++it;}
    std::set<uint64_t> referenced;for(auto&instance:w.instances)referenced.insert(instance.second.geometry);
    for(auto it=w.geometry.begin();it!=w.geometry.end();){if(w.serial-it->second.age>120&&!referenced.count(it->first)){e.destroy(it->second.vertices);e.destroy(it->second.indices);it=w.geometry.erase(it);}else ++it;}
    std::set<uint64_t> boundTextures;for(auto&instance:w.instances)for(auto key:instance.second.textureKeys)if(key)boundTextures.insert(key);
    for(auto it=w.textures.begin();it!=w.textures.end();){if(w.serial-w.textureAges[it->first]>4&&!boundTextures.count(it->first)){e.destroy(it->second);w.textureAges.erase(it->first);it=w.textures.erase(it);}else ++it;}
    error.clear();return 1;
}catch(const std::exception&exception){error=exception.what();return 0;}}

