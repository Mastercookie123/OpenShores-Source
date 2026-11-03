#include "aubodypartsres.h"
#include "augl.h"
#include "dpbody.h"
#include "auglmodel.h"
#include "au3dsglsl.h"
#include "augleffects.h"
#include "auglobal.h"
#include "hashpack.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QDir>
#include <QMatrix4x4>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <iostream>
#include <cstring>
void AuFilamentWorldOrientation(const AuMatrix4x4&);

static QJsonArray assetAudit,materialAudit,warnings,filamentAudit;
static void auditMessage(QtMsgType type,const QMessageLogContext&,const QString& message)
{
    if(message.startsWith("VISUAL_ASSET "))assetAudit.append(QJsonDocument::fromJson(message.mid(13).toUtf8()).object());
    else if(message.startsWith("VISUAL_MATERIAL "))materialAudit.append(QJsonDocument::fromJson(message.mid(16).toUtf8()).object());
    else if(message.startsWith("LIVE_FILAMENT ")||message.startsWith("FILAMENT_WORLD "))filamentAudit.append(message);
    else if(type==QtWarningMsg || type==QtCriticalMsg || type==QtFatalMsg)warnings.append(message);
    std::cerr<<message.toStdString()<<'\n';
}
static QString sha256(const QString& path)
{
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(&file);return QString::fromLatin1(hash.result().toHex());
}
static QJsonArray point(const AuPoint& p){return {p.x,p.y,p.z};}
static QVector<GLint> glState(AuGLEffects& gl){
    QVector<GLint> result;
    for(GLenum name:{GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_VIEWPORT,GL_ACTIVE_TEXTURE,GL_DEPTH_FUNC,GL_DEPTH_WRITEMASK,GL_COLOR_WRITEMASK,GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA,GL_DEPTH_TEST,GL_BLEND,GL_CULL_FACE,GL_FRAMEBUFFER_SRGB,GL_PIXEL_PACK_BUFFER_BINDING,GL_PIXEL_UNPACK_BUFFER_BINDING,GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS}){
        GLint values[4]={};gl.glGetIntegerv(name,values);for(int i=0;i<4;i++)result.append(values[i]);
    }
    GLint active;gl.glGetIntegerv(GL_ACTIVE_TEXTURE,&active);for(int i=0;i<3;i++){gl.glActiveTexture(GL_TEXTURE0+i);GLint texture;gl.glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);result.append(texture);}gl.glActiveTexture(active);return result;
}

static AuMatrix4x4 gameMatrix(const QMatrix4x4& matrix)
{
    double values[16];for(int i=0;i<16;i++)values[i]=matrix.constData()[i];return AuMatrix4x4(values);
}

// Exercise actual shader families through the same draw hook as a live world.
// These are deterministic geometry fixtures, not replacement gameplay assets.
static bool worldFixtures(AuGLEffects& gl,const QMatrix4x4& projection,const QMatrix4x4& view,bool previewFloor=false)
{
    // Initialize normally releases these compressed sources after building all
    // production programs. The fixture creates extra isolated programs.
    extern QHash<QString,QByteArray> s_GLSLProgramSrc;
    if(s_GLSLProgramSrc.isEmpty())Hashpack::ReadFiles(":glsl.har",s_GLSLProgramSrc);
    QFile manifest(QDir(QCoreApplication::applicationDirPath()).filePath("filament-world-r8/manifest.json"));
    if(!manifest.open(QIODevice::ReadOnly))return false;
    auto materials=QJsonDocument::fromJson(manifest.readAll()).object().value("materials").toArray();
    QStringList families={"SkyBox","TerrainLit","Sc","Flora","ModelTex","Rock","WaterNoBumpNoHole","SpriteSizeColor"};
    if(previewFloor)families=QStringList{QStringLiteral("Model")};
    for(int familyIndex=0;familyIndex<families.size();familyIndex++){
        QString family=families[familyIndex];QJsonObject spec;
        qInfo().noquote()<<"WORLD_FIXTURE"<<family;
        for(auto material:materials)if(material.toObject().value("key").toString()==family+"__"+family){spec=material.toObject();break;}
        if(spec.isEmpty())return false;
        AuGLSLShader vertex(gl,GL_VERTEX_SHADER,family+".v",4),fragment(gl,GL_FRAGMENT_SHADER,family+".f",4);
        GLuint program=gl.glCreateProgram();gl.glAttachShader(program,vertex.Id());gl.glAttachShader(program,fragment.Id());
        auto attributes=spec.value("attributes").toArray();for(int i=0;i<attributes.size();i++)gl.glBindAttribLocation(program,i,attributes[i].toObject().value("name").toString().toLatin1().constData());
        gl.glBindFragDataLocation(program,0,"fragColor");gl.glLinkProgram(program);AuFilamentShader(program,"@program");GLint linked=0;gl.glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return false;gl.glUseProgram(program);
        QMatrix4x4 model=view;if(family.startsWith("Water"))model.translate(0,0,-10000);
        if(family=="SkyBox"){model.setColumn(3,QVector4D(0,0,0,1));}
        QMatrix4x4 identity;
        for(auto uniform:spec.value("uniforms").toArray()){
            auto u=uniform.toObject();QString name=u.value("name").toString(),type=u.value("type").toString();int count=u.value("count").toInt(1);
            for(int i=0;i<count;i++){QByteArray element=(name+(count>1?QString("[%1]").arg(i):QString())).toLatin1();GLint at=gl.glGetUniformLocation(program,element.constData());if(at<0)continue;
                if(type.startsWith("sampler"))gl.glUniform1i(at,0);
                else if(type=="mat4")gl.glUniformMatrix4fv(at,1,false,name=="p"?projection.constData():name=="mv"?model.constData():name=="pmv"?(projection*model).constData():identity.constData());
                else if(type=="int"||type=="bool")gl.glUniform1i(at,0);
                else if(type=="float")gl.glUniform1f(at,name=="vertAlpha"?1:name=="opacity"?.65f:0);
                else if(type=="vec2")gl.glUniform2f(at,0,0);
                else if(type=="vec3")gl.glUniform3f(at,0,0,0);
                else if(type=="vec4"){if(previewFloor&&name=="color")gl.glUniform4f(at,.16,.18,.20,1);else gl.glUniform4f(at,0,0,0,name=="glowDiscard"?.15f:1);}
            }
        }
        GLint instances=gl.glGetUniformLocation(program,"instances");if(instances>=0)gl.glUniform1i(instances,31);
        struct V{QVector3D p,n;float u,v;};QVector<V> vertices;
        auto tri=[&](QVector3D a,QVector3D b,QVector3D c,QVector3D n){vertices.append({a,n,0,0});vertices.append({b,n,0,1});vertices.append({c,n,1,0});};
        auto quad=[&](QVector3D a,QVector3D b,QVector3D c,QVector3D d,QVector3D n){tri(a,b,c,n);tri(c,b,d,n);};
        if(previewFloor)quad({-13.12336f,-13.12336f,-3.0206467f},{-13.12336f,13.12336f,-3.0206467f},{13.12336f,-13.12336f,-3.0206467f},{13.12336f,13.12336f,-3.0206467f},{0,0,1});
        else if(family=="SpriteSizeColor")vertices.append({QVector3D(-1,0,0),QVector3D(0,0,1),0,0});
        else if(family=="TerrainLit")quad({-5,-4,-3.3f},{-5,4,-3.3f},{5,-4,-3.3f},{5,4,-3.3f},{0,0,1});
        else if(family=="Flora")quad({2,1,-3.3f},{2,1,-.3f},{3.2f,1,-3.3f},{3.2f,1,-.3f},{0,1,0});
        else if(family.startsWith("Water"))quad({-4,-2,9996.8f},{-4,2,9996.8f},{-2,-2,9996.8f},{-2,2,9996.8f},{0,0,1});
        else{
            QVector3D center=family=="SkyBox"?QVector3D(0,0,0):family=="Sc"?QVector3D(-2,0,-2.7f):family=="Rock"?QVector3D(1,-2,-2.9f):QVector3D(2,-1,-2.7f);float r=family=="SkyBox"?500.f:family=="Sc"?.6f:family=="Rock"?.4f:.25f;
            QVector3D p[8];for(int i=0;i<8;i++)p[i]=center+QVector3D(i&1?r:-r,i&2?r:-r,i&4?r:-r);
            quad(p[0],p[2],p[1],p[3],{0,0,-1});quad(p[4],p[5],p[6],p[7],{0,0,1});quad(p[0],p[1],p[4],p[5],{0,-1,0});quad(p[2],p[6],p[3],p[7],{0,1,0});quad(p[0],p[4],p[2],p[6],{-1,0,0});quad(p[1],p[3],p[5],p[7],{1,0,0});
        }
        GLuint vao;gl.glGenVertexArrays(1,&vao);gl.glBindVertexArray(vao);QVector<GLuint> buffers;
        for(int a=0;a<attributes.size();a++){
            QString name=attributes[a].toObject().value("name").toString();QVector<float> data(vertices.size()*4,0);
            for(int i=0;i<vertices.size();i++){auto&v=vertices[i];float*row=data.data()+i*4;
                if(name.startsWith("position")){row[0]=v.p.x();row[1]=v.p.y();row[2]=v.p.z();if(name=="positionShine")row[3]=16;if(family=="SpriteSizeColor")row[3]=100;}
                else if(name=="color"){row[0]=1;row[1]=.6;row[2]=.15;row[3]=1;}
                else if(name.startsWith("normal")){row[0]=v.n.x();row[1]=v.n.y();row[2]=v.n.z();if(previewFloor&&name=="normalShine")row[3]=2/std::pow(.85f,4)-2;}
                else if(name=="tangent")row[0]=1;
                else if(name=="paint"){row[0]=.55;row[1]=.4;row[2]=.25;row[3]=1;}
                else if(name.startsWith("texCoord")||name=="texEffectAppl"){row[0]=v.u;row[1]=v.v;if(name=="texCoordIndex")row[3]=1;}
            }
            GLuint buffer;gl.glGenBuffers(1,&buffer);buffers.append(buffer);gl.glBindBuffer(GL_ARRAY_BUFFER,buffer);gl.glBufferData(GL_ARRAY_BUFFER,data.size()*sizeof(float),data.constData(),GL_STATIC_DRAW);gl.glVertexAttribPointer(a,4,GL_FLOAT,false,0,nullptr);gl.glEnableVertexAttribArray(a);
        }
        gl.glActiveTexture(GL_TEXTURE0);GLuint texture;gl.glGenTextures(1,&texture);gl.glBindTexture(GL_TEXTURE_2D,texture);
        QColor color=family=="Flora"?QColor(70,155,50):family.startsWith("Water")?QColor(30,100,170):family=="TerrainLit"?QColor(130,115,80):QColor(170,150,120);
        unsigned char pixels[16];for(int i=0;i<4;i++){pixels[i*4]=color.red();pixels[i*4+1]=color.green();pixels[i*4+2]=color.blue();pixels[i*4+3]=family=="Flora"&&i==0?0:255;}
        if(family=="SkyBox"){
            // A texture name has a fixed target; use a new name for this cube.
            gl.glDeleteTextures(1,&texture);gl.glGenTextures(1,&texture);gl.glBindTexture(GL_TEXTURE_CUBE_MAP,texture);
            for(int face=0;face<6;face++){for(int i=0;i<4;i++){pixels[i*4]=12+face*3;pixels[i*4+1]=20+face*3;pixels[i*4+2]=40+face*3;}gl.glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);}
            gl.glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        }else{gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);}
        gl.glDisable(GL_CULL_FACE);gl.glEnable(GL_DEPTH_TEST);
        GLuint instanceTexture=0;
        if(family=="Flora"){
            // Real game instancing uses six float4 records per plant in a
            // texture buffer, including its matrix, wind/glow and LOD state.
            float records[48]={};for(int i=0;i<2;i++){QMatrix4x4 placement;placement.translate(i?1.5f:0,0,0);std::memcpy(records+i*24,placement.constData(),64);records[i*24+23]=1;}
            GLuint buffer;gl.glGenBuffers(1,&buffer);buffers.append(buffer);gl.glBindBuffer(GL_TEXTURE_BUFFER,buffer);gl.glBufferData(GL_TEXTURE_BUFFER,sizeof records,records,GL_STATIC_DRAW);
            gl.glActiveTexture(GL_TEXTURE31);gl.glGenTextures(1,&instanceTexture);gl.glBindTexture(GL_TEXTURE_BUFFER,instanceTexture);gl.glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,buffer);gl.glActiveTexture(GL_TEXTURE0);
            gl.glUniform1i(gl.glGetUniformLocation(program,"instanced"),1);
        }
        if(family.startsWith("Water")||family=="SpriteSizeColor"){gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);gl.glDepthMask(false);}else{gl.glDisable(GL_BLEND);gl.glDepthMask(family!="SkyBox");}
        if(family=="Flora")gl.glDrawArraysInstanced(GL_TRIANGLES,0,vertices.size(),2);
        else gl.glDrawArrays(family=="SpriteSizeColor"?GL_POINTS:GL_TRIANGLES,0,vertices.size());
        gl.glBindVertexArray(0);gl.glDeleteBuffers(buffers.size(),buffers.constData());gl.glDeleteVertexArrays(1,&vao);gl.glDeleteTextures(1,&texture);if(instanceTexture)gl.glDeleteTextures(1,&instanceTexture);gl.glDeleteProgram(program);
    }
    gl.glUseProgram(0);gl.glDepthMask(true);gl.glDisable(GL_BLEND);return true;
}

int main(int argc,char** argv)
{
    qInstallMessageHandler(auditMessage);qputenv("OPENSHORES_RENDER_AUDIT","1");
    QGuiApplication app(argc,argv);if(argc!=4 && argc!=5 && argc!=6)return 2;
    QJsonObject fixture;
    if(argc==6 && QByteArray(argv[4])=="--case")
    {
        QFile file(QString::fromLocal8Bit(argv[5]));if(!file.open(QIODevice::ReadOnly))return 13;
        auto doc=QJsonDocument::fromJson(file.readAll());if(!doc.isObject())return 14;fixture=doc.object();
    }
    bool shadows=(argc==5 && QByteArray(argv[4])=="shadows") || fixture.value("shadows").toBool();
    double animationSeconds=fixture.value("seconds").toDouble(.25);
    qputenv("OPENSHORES_PREVIEW_SECONDS",QByteArray::number(animationSeconds));
    if(QByteArray(argv[1])=="none")qunsetenv("OPENSHORES_BODY_PARTS");else qputenv("OPENSHORES_BODY_PARTS",argv[1]);
    QDir().mkpath(QString::fromLocal8Bit(argv[2]));QDir output(QString::fromLocal8Bit(argv[2]));
    if(!QDir::setCurrent(QString::fromLocal8Bit(argv[3])))return 3;
    AuGL graphics;AuBodyPartsRes resources;
    QSurfaceFormat format;format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;context.setFormat(format);if(!context.create())return 4;
    QOffscreenSurface surface;surface.setFormat(context.format());surface.create();if(!context.makeCurrent(&surface))return 5;
    AuGlobal global("ProceduralAvatarRender","OpenShoresTools",AuGlobal::SceneClient);gp_AuGlobal=&global;
    AuGLSLShader::SetPerPixelLighting(true);AuGLSLShader::SetShadows(shadows);AuGLSLShader::SetShadowMaps(shadows);
    AuGLSLShader::SetShadowLights(fixture.value("lampShadows").toInt());
    AuGLEffects renderer;renderer.InitializeGL(4);renderer.DisableLights();renderer.DisableSuns();renderer.SetAmbientLight(.22,.23,.25,1);
    AuGLModelCache cache;global.SetModelCache(&cache);
    bool preview=fixture.value("previewReference").toBool();if(preview)qputenv("OPENSHORES_FILAMENT_STUDIO","1");
    DhDNA dna;dna.InitDefaultHuman();if(fixture.contains("dna")&&!dna.FromString(fixture.value("dna").toString()))return 22;dna.TopSkinSubtype=fixture.value("palette").toInt();dna.BottomSkinSubtype=fixture.value("bottomPalette").toInt(dna.TopSkinSubtype);
    dna.SkinType=fixture.value("skin").toInt(2);
    QString anatomy=fixture.value("anatomy").toString();
    if(anatomy=="long"){dna.UpperArmLength=28;dna.ForeArmLength=25;dna.UpperArmThickness=8;dna.TorsoThickness=10;}
    if(anatomy=="multi"){dna.ArmPairs=3;dna.LegPairs=3;dna.TorsoVertebrae=6;dna.NeckVertebrae=3;}
    if(anatomy=="quadruped"){dna.ArmPairs=0;dna.LegPairs=2;dna.TorsoVertebrae=3;dna.BodyCarriage=1;}
    QString poseName=fixture.value("pose").toString("stand");
    AuPose::POSE pose=AuPose::Stand;
    if(poseName=="walk")pose=AuPose::StandWalk;
    else if(poseName=="crouch")pose=AuPose::Crouch;
    float phaseSpeed=poseName=="walk"?1.f:0.f;
    DpBody body(fixture.value("sex").toInt(1),false,dna);body.Pose(AuPose(pose),false,phaseSpeed);
    body.CreateDeviceObjects(renderer,cache,fixture.value("armor").toBool()?CID_MetalPowerArmor:fixture.value("clothes").toBool()?CID_JeansAndShirt:CID_Invalid,fixture.value("armor").toBool()?CID_MetalPowerArmorHelmet:CID_Invalid,phaseSpeed);
    if(poseName=="strike")
    {
        AuPose sections[DpBody::PS_Count];DpBody::InitPoses(AuPose::Stand,sections);
        sections[DpBody::PS_RightArm]=AuPose::Strike;sections[DpBody::PS_LeftArm]=AuPose::Block;
        sections[DpBody::PS_UpperTorso]=AuPose::Strike;body.Pose(sections,false,0);
    }
    QJsonArray parts;int triangles=0;
    for(auto* part:body.m_Models)
    {
        Au3dsGLSLTriBodyList mesh;part->AppendBodyTriangles(mesh);triangles+=mesh.size();
        parts.append(QJsonObject{{"name",part->FileName()},{"triangles",mesh.size()}});
    }
    QFile shaderResource(":glsl.har");
    QJsonObject report{{"fixture",fixture},{"executable",QCoreApplication::applicationFilePath()},
        {"pack",QString::fromLocal8Bit(argv[1])},{"manifestSha256",sha256(QDir(QString::fromLocal8Bit(argv[1])).filePath("manifest.json"))},
        {"shaderArchiveSha256",sha256(":glsl.har")},{"triangles",triangles},{"parts",parts},
        {"vendor",QString::fromLatin1(reinterpret_cast<const char*>(renderer.glGetString(GL_VENDOR)))},
        {"renderer",QString::fromLatin1(reinterpret_cast<const char*>(renderer.glGetString(GL_RENDERER)))},
        {"glVersion",QString::fromLatin1(reinterpret_cast<const char*>(renderer.glGetString(GL_VERSION)))},
        {"dna",QJsonObject{{"skin",int(dna.SkinType)},{"hand",int(dna.HandType)},{"foot",int(dna.FootType)},
            {"nose",int(dna.NoseType)},{"mouth",int(dna.MouthType)},{"eye",int(dna.EyeType)},
            {"armPairs",int(dna.ArmPairs)},{"legPairs",int(dna.LegPairs)}}},
        {"leftGrip",point(body.CurrentLeftHandLoc())},{"rightGrip",point(body.CurrentRightHandLoc())},
        {"camera",point(body.CameraLocation())},{"phaseSpeed",phaseSpeed},{"animationSeconds",animationSeconds}};
    int imageWidth=fixture.value("width").toInt(900),imageHeight=fixture.value("height").toInt(1000);
    QOpenGLFramebufferObject fbo(imageWidth,imageHeight,QOpenGLFramebufferObject::CombinedDepthStencil);
    if(!fbo.isValid())return 6;
    for(bool portrait:{false,true})
    {
        QMatrix4x4 projection;projection.perspective(preview?29.862834:35,double(imageWidth)/imageHeight,.1,100);
        QMatrix4x4 view;
        if(portrait)view.lookAt(QVector3D(2,6,2.8),QVector3D(0,0,1.7),QVector3D(0,0,1));
        else view.lookAt(QVector3D(5,11,3.5),QVector3D(0,0,0),QVector3D(0,0,1));
        if(preview&&!portrait){view.setToIdentity();view.lookAt(QVector3D(6.0985419,14.3818234,1.2077348),QVector3D(.00021851,.152401,-.0119297),QVector3D(0,0,1));}
        double yaw=fixture.value("yaw").toDouble();
        if(yaw)view.rotate(yaw,0,0,1);
        if(anatomy=="multi" || anatomy=="quadruped")view.scale(.65f);
        AuMatrix4x4 model=gameMatrix(view);
        auto key=model.Project(AuPoint(-4,6,7));auto fill=model.Project(AuPoint(4,4,2));
        renderer.SetLight(0,.05,.008,QColor(255,245,230),key.x,key.y,key.z);
        renderer.SetLight(1,.08,.015,QColor(120,150,185),fill.x,fill.y,fill.z);
        QString lighting=fixture.value("lighting").toString("studio");
        if(lighting=="night")
        {
            renderer.SetAmbientLight(.035,.045,.075,1);renderer.DisableLights();
            renderer.SetLight(0,.09,.02,QColor(130,165,255),key.x,key.y,key.z);
        }
        if(lighting=="orange")
        {
            renderer.SetAmbientLight(.13,.09,.035,1);
            renderer.SetLight(0,.05,.008,QColor(255,145,40),key.x,key.y,key.z);
        }
        if(lighting=="campfire-backlit")
        {
            renderer.DisableLights();renderer.SetAmbientLight(.012,.015,.023,1);
            auto fire=model.Project(AuPoint(1,-2,1.5));
            renderer.SetLight(0,.14,.025,QColor(255,125,35),fire.x,fire.y,fire.z);
        }
        if(lighting=="unlit-night")
        {
            renderer.DisableLights();renderer.SetAmbientLight(.012,.015,.023,1);
        }
        if(lighting=="unlit-black")
        {
            renderer.DisableLights();renderer.SetAmbientLight(0,0,0,1);
        }
        if(shadows && lighting!="point-shadow"&&!preview)
        {
            auto sun=model.Project(AuPoint(-4000,6000,7000));
            auto direction=model.ProjectVector(AuPoint(-4000,6000,7000));
            auto origin=model.Project(AuPoint(0,0,0));
            auto axisA=model.ProjectVector(AuPoint(1,0,0));
            auto axisB=model.ProjectVector(AuPoint(0,0,1));
            double sd[]={direction.x,direction.y,direction.z};
            double ro[]={origin.x,origin.y,origin.z};
            double a[]={axisA.x,axisA.y,axisA.z},b[]={axisB.x,axisB.y,axisB.z};
            AuSunShadowCascades cascades,empty={};
            if(!AuSunShadowCascadesCompute(sd,ro,a,b,256,2048,1,cascades))return 9;
            if(!renderer.GLSL().BeginSunShadowMap(2048,1))return 10;
            for(int k=0;k<cascades.m_Count;k++)
            {
                renderer.GLSL().SunShadowMapLayer(k);
                renderer.GLSL().BodyDepthShader().Select(AuMatrix4x4(cascades.m_Fit[k].m_EyeToClip));
                if(!body.RenderSunShadowDepth(model,renderer))return 11;
                renderer.GLSL().BodyDepthShader().Deselect();
            }
            renderer.GLSL().EndSunShadowMap(cascades,empty);
            if(!renderer.GLSL().SunShadowMapOn() || renderer.glGetError()!=GL_NO_ERROR)return 12;
            renderer.SetSun(0,1.e-8,QColor(220,210,195),sun.x,sun.y,sun.z,70);
        }
        if(preview){renderer.DisableLights();renderer.DisableSuns();renderer.SetAmbientLight(.23,.23,.23,1);auto sun=model.Project(AuPoint(4000,5000,8000));renderer.SetSun(0,1.0/(sun.x*sun.x+sun.y*sun.y+sun.z*sun.z),QColor(255,255,255),sun.x,sun.y,sun.z,70);}
        renderer.GLSL().InvalidateLights();
        renderer.GLSL().BodyShader().Select(gameMatrix(projection),renderer);
        GLint program=0,linked=0;renderer.glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        renderer.glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 15;
        report["bodyShaderLinked"]=bool(linked);
        report["detailUniformPresent"]=renderer.glGetUniformLocation(program,"surfaceDetailEnabled")>=0;
        renderer.GLSL().BodyShader().SetGlowAlpha(0,0,0,1);
        renderer.GLSL().BodyShader().Deselect();
        fbo.bind();renderer.glViewport(0,0,imageWidth,imageHeight);renderer.glEnable(GL_DEPTH_TEST);
        renderer.glEnable(GL_CULL_FACE);renderer.glCullFace(GL_BACK);renderer.glFrontFace(GL_CW);
        renderer.glClearColor(.055,.065,.08,1);renderer.glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        if(preview){renderer.glClearColor(.025,.035,.05,1);renderer.glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
        if(fixture.value("depthOccluder").toBool()){
            renderer.glEnable(GL_SCISSOR_TEST);renderer.glScissor(0,0,450,1000);
            renderer.glClearColor(0,1,0,1);renderer.glClearDepth(.1);renderer.glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            renderer.glDisable(GL_SCISSOR_TEST);renderer.glClearDepth(1);
        }
        if(renderer.glGetError()!=GL_NO_ERROR)return 16;
        auto before=glState(renderer);
        bool worldFrame=AuFilamentWorldBegin(renderer,gameMatrix(projection));
        if(worldFrame)AuFilamentWorldOrientation(model);
        if(fixture.value("npcGlobe").toBool()){
            renderer.SetFog(.2,.3,.4,.0001);
            body.RenderSkinFogGlobe(gameMatrix(projection),model,renderer,model.ProjectVector(AuPoint(0,0,1)),model.Project(AuPoint(0,0,-10000)),false);
        }else if(fixture.value("fog").toBool()){
            renderer.SetFog(.2,.3,.4,.2);
            body.RenderSkinFog(gameMatrix(projection),model,renderer,model.ProjectVector(AuPoint(0,0,1)),false);
        }else body.RenderSkin(gameMatrix(projection),model,renderer,model.ProjectVector(AuPoint(0,0,1)),fixture.value("headless").toBool());
        if(fixture.value("secondBody").toBool()){
            DpBody other(0,false,dna);other.Pose(AuPose(AuPose::Stand),false,0);
            other.CreateDeviceObjects(renderer,cache,CID_Invalid,CID_Invalid,0);
            AuMatrix4x4 otherModel=model;otherModel.Translate(2.5,0,0);
            other.RenderSkinFogGlobe(gameMatrix(projection),otherModel,renderer,otherModel.ProjectVector(AuPoint(0,0,1)),otherModel.Project(AuPoint(0,0,-10000)),false);
        }
        if(fixture.value("worldMixed").toBool()&&!worldFixtures(renderer,projection,view))return 19;
        if(preview&&!worldFixtures(renderer,projection,view,true))return 19;
        if(worldFrame){auto beforeComposite=glState(renderer);AuFilamentWorldEnd(renderer);if(beforeComposite!=glState(renderer)){std::cerr<<"World compositor changed caller GL state\n";return 18;}}
        if(qgetenv("OPENSHORES_RENDERER")=="filament" && before!=glState(renderer)){std::cerr<<"Filament changed caller GL state\n";return 18;}
        renderer.glFinish();auto error=renderer.glGetError();if(error!=GL_NO_ERROR){std::cerr<<"GL error "<<error<<'\n';return 7;}
        auto rendered=fbo.toImage();
        if(preview){
            int skinPixels=0;for(int y=100;y<850;y++)for(int x=350;x<650;x++){auto c=rendered.pixelColor(x,y);if(c.red()>65&&c.red()>c.green()*1.12&&c.green()>c.blue()*1.08)skinPixels++;}
            report[portrait?"previewPortraitSkinPixels":"previewBodySkinPixels"]=skinPixels;
            if(skinPixels<30000){std::cerr<<"Preview character missing or incorrectly lit: "<<skinPixels<<" skin pixels\n";return 22;}
        }
        if(fixture.value("npcGlobe").toBool()){
            int visible=0;for(int y=100;y<600;y++)for(int x=280;x<550;x++){auto c=rendered.pixelColor(x,y);if(c.red()>100&&c.green()>70&&c.blue()>50&&c.red()>c.green()*1.04)visible++;}
            report[portrait?"npcPortraitPixels":"npcBodyPixels"]=visible;
            if(visible<12000){std::cerr<<"NPC globe body vanished: "<<visible<<" visible pixels\n";return 20;}
        }
        if(!rendered.save(output.filePath(portrait?"portrait.png":"body.png")))return 8;
    }
    report["assets"]=assetAudit;report["materials"]=materialAudit;report["warnings"]=warnings;
    if(fixture.value("secondBody").toBool())for(auto line:filamentAudit)if(!line.toString().contains("bonesRead=2 ")){std::cerr<<"Bone cache did not reset between procedural bodies\n";return 21;}
    report["openGLRenderer"]=report["renderer"];report["renderer"]=qEnvironmentVariable("OPENSHORES_RENDERER","opengl");report["filamentFrames"]=filamentAudit;report["glStatePreserved"]=true;
    if(qgetenv("OPENSHORES_RENDERER")=="filament-world"){QFile status(QDir(QCoreApplication::applicationDirPath()).filePath("filament-world-status.json"));if(status.open(QIODevice::ReadOnly))report["worldCoverage"]=QJsonDocument::fromJson(status.readAll()).object();}
    report["glErrors"]=0;report["selfShadows"]=shadows;
    QFile metadata(output.filePath("render.json"));if(!metadata.open(QIODevice::WriteOnly))return 17;
    metadata.write(QJsonDocument(report).toJson());
    std::cout<<"PASS: production DpBody rendering and material binding; no OpenGL errors\n";
    return 0;
}
