// Export the game's actual DNA assembly and absolute pose matrices for a
// replacement-renderer bridge. No imported/fixed human or replacement rig.
#include "aubodypartsres.h"
#include "augl.h"
#include "au3dsglsl.h"
#include "dpbody.h"
#include "augleffects.h"
#include "auglmodel.h"
#include "auglobal.h"
#include "aubodysurface.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QCommandLineParser>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>

int main(int argc,char** argv)
{
    QGuiApplication app(argc,argv);QCommandLineParser cli;cli.addHelpOption();
    cli.addOptions({{{"o","output"},"Export JSON","path"},{{"p","pack"},"Optional component pack","path"},
        {"textures","Runtime texture directory","path"},{"variant","human, multi or quadruped","name","human"},
        {"pose","stand, walk or strike","name","stand"},{"seconds","Animation sample time","value","0.25"},
        {"dna","Optional actual 63-character game DNA string","value"},
        {"bones-only","Export pose without repeating geometry"}});cli.process(app);
    if(!cli.isSet("output") || !QDir::setCurrent(cli.value("textures")))return 2;
    qputenv("OPENSHORES_RENDER_AUDIT","1");qputenv("OPENSHORES_PREVIEW_SECONDS",cli.value("seconds").toUtf8());
    if(cli.isSet("pack"))qputenv("OPENSHORES_BODY_PARTS",cli.value("pack").toUtf8());else qunsetenv("OPENSHORES_BODY_PARTS");
    AuGL graphics;AuBodyPartsRes resources;QSurfaceFormat format;
    format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;context.setFormat(format);if(!context.create())return 3;
    QOffscreenSurface surface;surface.setFormat(context.format());surface.create();if(!context.makeCurrent(&surface))return 4;
    AuGLEffects renderer;if(!renderer.initializeOpenGLFunctions())return 5;
    AuGlobal global("RendererSceneExport","OpenShoresTools",AuGlobal::SceneClient);gp_AuGlobal=&global;
    AuGLModelCache cache;global.SetModelCache(&cache);
    DhDNA dna;dna.InitDefaultHuman();dna.TopSkinSubtype=0;dna.BottomSkinSubtype=0;
    QString variant=cli.value("variant");
    if(variant=="multi"){dna.ArmPairs=3;dna.LegPairs=3;dna.TorsoVertebrae=6;dna.NeckVertebrae=3;}
    else if(variant=="quadruped"){dna.ArmPairs=0;dna.LegPairs=2;dna.TorsoVertebrae=3;dna.BodyCarriage=1;dna.SkinType=1;}
    else if(variant!="human")return 6;
    if(cli.isSet("dna") && !dna.FromString(cli.value("dna")))return 9;
    QString name=cli.value("pose");AuPose::POSE pose=name=="walk"?AuPose::StandWalk:AuPose::Stand;
    if(name!="walk" && name!="stand" && name!="strike")return 7;
    DpBody body(1,false,dna);body.Pose(AuPose(pose),false,1);body.CreateSkin(renderer,cache);
    if(name=="strike")
    {
        AuPose sections[DpBody::PS_Count];DpBody::InitPoses(AuPose::Stand,sections);
        sections[DpBody::PS_RightArm]=AuPose::Strike;sections[DpBody::PS_LeftArm]=AuPose::Block;
        sections[DpBody::PS_UpperTorso]=AuPose::Strike;body.Pose(sections,false,1);
    }
    QJsonArray bones;
    for(int b=0;b<61;b++){QJsonArray matrix;for(int i=0;i<16;i++)matrix.append(body.Bone(b)[i]);bones.append(matrix);}
    QJsonArray parts;int count=0;
    if(!cli.isSet("bones-only"))for(auto* model:body.m_Models)
    {
        Au3dsGLSLTriBodyList triangles;model->AppendBodyTriangles(triangles);
        if(cli.isSet("pack"))AuBodySurface::SmoothNormals(triangles);
        QJsonArray rows;
        for(const auto& tri:triangles)for(const auto* v:{&tri.m_V0,&tri.m_V1,&tri.m_V2})
        {
            QJsonArray row{v->x,v->y,v->z,v->m_N.x,v->m_N.y,v->m_N.z,v->m_Tx,v->m_Ty};
            for(int i=0;i<4;i++){row.append(v->m_BoneIndex[i]);row.append(v->m_BoneWeight[i]);}
            row.append(v->m_TopFace?1:0);rows.append(row);
        }
        count+=triangles.size();parts.append(QJsonObject{{"name",model->FileName()},{"vertices",rows}});
    }
    QJsonArray offset;for(float x:body.m_skinOffset)offset.append(x);
    auto point=[](const AuPoint& p){return QJsonArray{p.x,p.y,p.z};};
    QJsonObject result{{"version",1},{"variant",variant},{"dna",dna.ToString()},{"pose",name},{"seconds",cli.value("seconds").toDouble()},
        {"bones",bones},{"parts",parts},{"triangles",count},{"skinType",int(dna.SkinType)},{"skinOffset",offset},{"cycleSeconds",dna.CycleSpeed()},
        {"leftGrip",point(body.CurrentLeftHandLoc())},{"rightGrip",point(body.CurrentRightHandLoc())},
        {"camera",point(body.CameraLocation())}};
    QFile file(cli.value("output"));if(!file.open(QIODevice::WriteOnly))return 8;
    file.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
    std::cout<<"Exported production DNA assembly and pose: "<<count<<" triangles\n";
}
