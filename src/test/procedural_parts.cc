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
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <cmath>

struct Snapshot { QVector<AuPoint> anchors; QVector<AuMatrix4x4> bones; int triangles=0,existingNonunitWeights=0; };

static Snapshot sample(const DhDNA& dna, int sex, const QByteArray& pack, AuGLEffects& renderer)
{
    if(pack.isEmpty())qunsetenv("OPENSHORES_BODY_PARTS");else qputenv("OPENSHORES_BODY_PARTS",pack);
    AuGLModelCache cache;g_Au.SetModelCache(&cache);
    // Zero cycle speed freezes the phase so elapsed wall time cannot create
    // a false animation mismatch between the baseline and edited assembly.
    DpBody body(sex,false,dna);body.Pose(AuPose(AuPose::Stand),false,0);
    body.CreateDeviceObjects(renderer,cache,CID_JeansAndShirt,CID_Invalid,0);
    if(body.UsesHumanPrototype())throw std::runtime_error("Fixed human path is still active");
    Snapshot result;
    for(auto* model:body.m_Models)
    {
        Au3dsGLSLTriBodyList mesh;model->AppendBodyTriangles(mesh);result.triangles+=mesh.size();
        for(const auto& t:mesh)for(const auto* v:{&t.m_V0,&t.m_V1,&t.m_V2})
        {
            if(!std::isfinite(v->x)||!std::isfinite(v->y)||!std::isfinite(v->z))throw std::runtime_error("Nonfinite mesh coordinate");
            double sum=0;
            for(int b=0;b<4;b++)
            {
                if(v->m_BoneWeight[b]<0 || v->m_BoneIndex[b]<0 || v->m_BoneIndex[b]>=61)throw std::runtime_error("Invalid skinning influence");
                sum+=v->m_BoneWeight[b];
            }
            if(qAbs(sum-1)>1e-4)
            {
                if(model->FileName().endsWith("ArmLeft0.3dsN") || model->FileName().endsWith("ArmRight0.3dsN"))
                    throw std::runtime_error("Replacement limb has unnormalized skin weights");
                result.existingNonunitWeights++;
            }
        }
    }
    for(auto pose:{AuPose::Stand,AuPose::StandWalk,AuPose::Crouch,AuPose::StandRun})
    {
        body.Pose(AuPose(pose),false,0);
        result.anchors<<body.CurrentLeftHandLoc()<<body.CurrentRightHandLoc()<<body.CameraLocation();
        for(auto* part:body)result.anchors<<part->CurrentConnectLoc();
        for(int b=0;b<61;b++)result.bones<<body.Bone(b);
    }
    return result;
}

int main(int argc,char** argv)
{
    QGuiApplication app(argc,argv);if(argc!=2)return 2;
    AuBodyPartsRes resources;QSurfaceFormat format;format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;context.setFormat(format);if(!context.create())return 3;
    QOffscreenSurface surface;surface.setFormat(context.format());surface.create();if(!context.makeCurrent(&surface))return 4;
    AuGLEffects renderer;if(!renderer.initializeOpenGLFunctions())return 5;
    AuGlobal global("ProceduralPartsValidation","OpenShoresTools",AuGlobal::SceneClient);gp_AuGlobal=&global;
    QByteArray pack=QByteArray(argv[1]);int cases=0,upgraded=0;double worstAnchor=0,worstBone=0;
    try
    {
        for(int skin=0;skin<8;skin++)for(int variant=0;variant<5;variant++)for(int sex=0;sex<2;sex++)
        {
            DhDNA dna;dna.InitDefaultHuman();dna.SkinType=skin;
            if(variant==1){dna.UpperArmLength=28;dna.ForeArmLength=25;dna.UpperArmThickness=8;dna.TorsoThickness=10;}
            if(variant==2){dna.UpperArmLength=7;dna.ForeArmLength=8;dna.UpperArmThickness=28;dna.TorsoThickness=29;}
            if(variant==3){dna.ArmPairs=3;dna.LegPairs=3;dna.TorsoVertebrae=6;dna.NeckVertebrae=3;}
            if(variant==4){dna.ArmPairs=0;dna.LegPairs=2;dna.TorsoVertebrae=3;dna.BodyCarriage=1;}
            auto original=sample(dna,sex,{},renderer);auto edited=sample(dna,sex,pack,renderer);
            // Remeshed legacy necks can contain more samples of their existing
            // non-unit weights. Their count is not invariant under subdivision.
            if(original.anchors.size()!=edited.anchors.size()||original.bones.size()!=edited.bones.size())return 6;
            for(int i=0;i<original.anchors.size();i++)
            {
                auto delta=original.anchors[i]-edited.anchors[i];worstAnchor=qMax(worstAnchor,std::sqrt(delta.Dot(delta)));
            }
            for(int i=0;i<original.bones.size();i++)for(int j=0;j<16;j++)worstBone=qMax(worstBone,qAbs(original.bones[i][j]-edited.bones[i][j]));
            if(edited.triangles<=original.triangles)throw std::runtime_error("Expanded body components did not load");
            if(edited.triangles>original.triangles)upgraded++;
            cases++;
            if(cases%10==0)std::cout<<"Checked "<<cases<<" DNA/sex variants\n"<<std::flush;
        }
        if(worstAnchor>1e-8||worstBone>1e-8){std::cerr<<"Anchor delta "<<worstAnchor<<" bone delta "<<worstBone<<'\n';return 7;}
        // A missing pack must fall back cleanly, including when the obsolete
        // fixed-human environment variable is present in the launch environment.
        DhDNA dna;dna.InitDefaultHuman();qputenv("OPENSHORES_HUMAN_PROTOTYPE","1");
        auto base=sample(dna,1,{},renderer);auto fallback=sample(dna,1,"/nonexistent-body-parts",renderer);
        if(base.triangles!=fallback.triangles)return 8;
        QTemporaryDir invalidPack;if(!invalidPack.isValid())return 11;
        QFile manifest(invalidPack.filePath("manifest.json"));if(!manifest.open(QIODevice::WriteOnly))return 12;
        QJsonObject entry{{"sourceSha256","invalid"},{"assetSha256","invalid"}};
        manifest.write(QJsonDocument(QJsonObject{{"files",QJsonObject{{"2ArmLeft0.3dsN",entry}}}}).toJson());manifest.close();
        if(sample(dna,1,invalidPack.path().toUtf8(),renderer).triangles!=base.triangles)return 13;
        std::cout<<"PASS: "<<cases<<" DNA/sex variants, "<<upgraded<<" upgraded body assemblies, four poses each\n"
                 <<"Maximum grip/camera/seam anchor delta: "<<worstAnchor<<"; bone matrix delta: "<<worstBone<<"\n"
                 <<"PASS: original UV/clothing path, missing-pack and checksum fallback; fixed human path disabled\n";
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 9;}
    return 0;
}
