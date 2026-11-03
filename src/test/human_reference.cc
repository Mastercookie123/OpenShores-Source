#include "aubodypartsres.h"
#include "dpbody.h"
#include "au3dsglsl.h"
#include "auglmodel.h"
#include "augleffects.h"
#include "auglobal.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <QDir>
#include <QTemporaryDir>

int main(int argc, char** argv)
{
    QGuiApplication app(argc,argv);
    if(argc!=2 && argc!=3) return 2;
    AuBodyPartsRes resources;
    QSurfaceFormat format;format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;context.setFormat(format);if(!context.create())return 3;
    QOffscreenSurface surface;surface.setFormat(context.format());surface.create();
    if(!context.makeCurrent(&surface))return 4;
    AuGLEffects renderer;if(!renderer.initializeOpenGLFunctions())return 5;
    AuGLModelCache cache;
    AuGlobal global("HumanReference", "OpenShoresTools", AuGlobal::SceneClient);
    gp_AuGlobal=&global;
    global.SetModelCache(&cache);
    DhDNA dna;dna.InitDefaultHuman();
    DpBody body(1,false,dna);
    body.Pose(AuPose(AuPose::Stand),false,0);
    body.CreateSkin(renderer,cache);
    QJsonArray parts;
    for(auto* model : body.m_Models)
    {
        Au3dsGLSLTriBodyList triangles;model->AppendBodyTriangles(triangles);
        QJsonArray vertices;
        for(const auto& t : triangles)
            for(const auto* v : {&t.m_V0,&t.m_V1,&t.m_V2})
            {
                QJsonArray row{v->x,v->y,v->z,v->m_N.x,v->m_N.y,v->m_N.z,v->m_Tx,v->m_Ty};
                for(int i=0;i<4;i++){row.append(v->m_BoneIndex[i]);row.append(v->m_BoneWeight[i]);}
                vertices.append(row);
            }
        parts.append(QJsonObject{{"name",model->FileName()},{"vertices",vertices}});
    }
    QFile output(QString::fromLocal8Bit(argv[1]));if(!output.open(QIODevice::WriteOnly))return 6;
    QJsonArray bones;
    for(int b=0;b<DhDNA::BONE_Max;b++)
    {
        QJsonArray basis;
        for(const AuPoint& p : {AuPoint(0,0,0),AuPoint(1,0,0),AuPoint(0,1,0),AuPoint(0,0,1)})
        {
            AuPoint v=body.Bone(b).Project(p);basis.append(QJsonArray{v.x,v.y,v.z});
        }
        bones.append(basis);
    }
    output.write(QJsonDocument(QJsonObject{{"parts",parts},{"bones",bones}}).toJson(QJsonDocument::Compact));
    std::cout<<"Exported "<<parts.size()<<" default human body parts\n";
    output.close();
    if(argc==3)
    {
        QDir asset(QString::fromLocal8Bit(argv[2]));
        Au3dsGLSLModelInstance replacement("HumanPrototypeBody");
        Au3dsGLSLModelInstance head("HumanPrototypeHead");
        if(!replacement.CreatePrototypeBody(renderer,asset.filePath("human-prototype.json"),"body") ||
           !head.CreatePrototypeBody(renderer,asset.filePath("human-prototype.json"),"head"))return 8;
        QFile validFile(asset.filePath("human-prototype.json"));if(!validFile.open(QIODevice::ReadOnly))return 16;
        auto validRoot=QJsonDocument::fromJson(validFile.readAll()).object();
        QTemporaryDir temporary;if(!temporary.isValid())return 17;
        for(int test=0;test<6;test++)
        {
            auto invalid=validRoot;auto rows=invalid["body"].toArray();auto row=rows[0].toArray();
            if(test==0)row.removeLast();
            if(test==1)row[8]=61;
            if(test==2)for(int i=0;i<4;i++)row[9+2*i]=0;
            if(test==3)row[0]="invalid";
            rows[0]=row;invalid["body"]=rows;
            if(test==4)invalid["inverseBind"]=QJsonArray();
            if(test==5)invalid["version"]=1;
            QString path=temporary.filePath("invalid.json");QFile file(path);
            if(!file.open(QIODevice::WriteOnly))return 18;
            file.write(QJsonDocument(invalid).toJson(QJsonDocument::Compact));file.close();
            Au3dsGLSLModelInstance rejected("invalid");
            if(rejected.CreatePrototypeBody(renderer,path,"body"))return 19;
        }
        std::cout<<"PASS: six malformed mesh inputs rejected\n";
        QOpenGLShaderProgram shader;
        if(!shader.addShaderFromSourceCode(QOpenGLShader::Vertex,
            "#version 150\nin vec3 position;in vec3 normal;in vec4 texCoordShineTopface;in vec4 boneIndex;in vec4 boneWeight;"
            "uniform mat4 bones[61];uniform mat4 mvp;out vec3 N;out vec2 UV;void main(){"
            "mat4 skin=mat4(0);for(int i=0;i<4;i++)skin+=boneWeight[i]*bones[int(boneIndex[i])];"
            "gl_Position=mvp*skin*vec4(position,1);N=mat3(skin)*normal;UV=texCoordShineTopface.xy;}"))return 9;
        if(!shader.addShaderFromSourceCode(QOpenGLShader::Fragment,
            "#version 150\nin vec3 N;in vec2 UV;uniform sampler2D diffuse;out vec4 color;void main(){"
            "float light=.28+.72*max(0.,dot(normalize(N),normalize(vec3(-.4,.8,1))));"
            "vec4 texel=texture(diffuse,UV);if(texel.a<.15)discard;color=vec4(texel.rgb*light,1);}"))return 10;
        const char* attributes[]={"position","normal","texCoordShineTopface","boneIndex","boneWeight"};
        for(int i=0;i<5;i++)shader.bindAttributeLocation(attributes[i],i);
        if(!shader.link())return 11;
        QImage image(asset.filePath("human-diffuse.png"));if(image.isNull())return 12;
        QOpenGLTexture texture(image.mirrored());
        QOpenGLFramebufferObject fbo(900,1000,QOpenGLFramebufferObject::CombinedDepthStencil);
        if(!fbo.isValid())return 13;
        QMatrix4x4 view;view.perspective(35,.9,.1,100);
        view.lookAt(QVector3D(7,11,3),QVector3D(0,0,0),QVector3D(0,0,1));
        for(auto pose : {AuPose::Stand,AuPose::StandWalk,AuPose::Crouch,AuPose::Strike})
        {
            if(pose==AuPose::Strike)
            {
                AuPose poses[DpBody::PS_Count];DpBody::InitPoses(AuPose(AuPose::Stand),poses);
                poses[DpBody::PS_RightArm]=AuPose::Strike;
                body.Pose(poses,false,1);
            }
            else body.Pose(AuPose(pose),false,1);
            QMatrix4x4 bones[61];
            for(int b=0;b<61;b++)
            {
                AuMatrix4x4 transform=replacement.PrototypeBone(b,body.Bone(b));
                AuPoint origin=transform.Project(AuPoint(0,0,0));
                for(int j=0;j<3;j++)
                {
                    AuPoint unit(0,0,0);if(j==0)unit.x=1;else if(j==1)unit.y=1;else unit.z=1;
                    AuPoint delta=transform.Project(unit)-origin;
                    bones[b](0,j)=delta.x;bones[b](1,j)=delta.y;bones[b](2,j)=delta.z;
                }
                bones[b](0,3)=origin.x;bones[b](1,3)=origin.y;bones[b](2,3)=origin.z;
            }
            fbo.bind();renderer.glViewport(0,0,900,1000);renderer.glEnable(GL_DEPTH_TEST);
            renderer.glClearColor(.035,.045,.06,1);renderer.glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            shader.bind();shader.setUniformValue("mvp",view);shader.setUniformValueArray("bones",bones,61);
            shader.setUniformValue("diffuse",0);texture.bind(0);
            replacement.Render(renderer,false);head.Render(renderer,false);renderer.glFinish();
            if(renderer.glGetError()!=GL_NO_ERROR)return 14;
            if(!fbo.toImage().save(asset.filePath(QString("game-pose-%1.png").arg(int(pose)))))return 15;
        }
        std::cout<<"PASS: replacement loader and four actual game bone poses rendered\n";
    }
    return parts.isEmpty()?7:0;
}
