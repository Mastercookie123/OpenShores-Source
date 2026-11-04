// Opt-in integration driver for the real client. No desktop input or credentials.
#ifndef OPENSHORES_CLIENT_SMOKE_H
#define OPENSHORES_CLIENT_SMOKE_H
#include "auview.h"
#include "audesignerview.h"
#include "auether.h"
#include "daperson.h"
#include "dhdna.h"
#include "dgdata.h"
#include "dicontrolinput.h"
#include "augear.h"
#include "auitem.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QOpenGLWidget>
#include <QSet>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static LONG WINAPI SmokeCrash(EXCEPTION_POINTERS* e){
    fprintf(stderr,"CLIENT_SMOKE CRASH code=%08lx address=%p\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress);
    void* addresses[64];USHORT count=CaptureStackBackTrace(0,64,addresses,nullptr);
    for(int i=-1;i<count;i++){void* address=i<0?e->ExceptionRecord->ExceptionAddress:addresses[i];MEMORY_BASIC_INFORMATION info={};VirtualQuery(address,&info,sizeof info);char module[MAX_PATH]={};GetModuleFileNameA((HMODULE)info.AllocationBase,module,MAX_PATH);fprintf(stderr,"CRASH_FRAME %s +0x%llx\n",module,(unsigned long long)((char*)address-(char*)info.AllocationBase));}
    fflush(stderr);return EXCEPTION_EXECUTE_HANDLER;
}
#endif

class ClientSmoke : public QObject {
    AuClientApp& app; AuClientFrame& frame; QDir out;
    QTimer timer; QElapsedTimer clock, actions; bool started=false, finished=false, thirdPerson=false;
    AuPoint start; AuId parent; QJsonObject result; QJsonArray samples,captures;
    QSet<int> items; bool moving=false,equipmentChanged=false; int attackCommands=0,selectionItem=-1; double maxMove=0; int lastStage=-1; qint64 lastFrame=-1;
    void save(){QSaveFile f(out.filePath("result.json"));if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(result).toJson());f.commit();}}
    void finish(const QString& error={}){if(finished)return;finished=true;timer.stop();result["passed"]=error.isEmpty();result["error"]=error;if(error.isEmpty())result["phase"]="complete";result["samples"]=samples;result["captures"]=captures;result["movementControlObserved"]=moving;result["attackCommandsSent"]=attackCommands;result["distinctItems"]=items.size();result["equipmentChanged"]=equipmentChanged;result["maxMovementFeet"]=maxMove;save();qInfo("CLIENT_SMOKE %s %s",error.isEmpty()?"PASS":"FAIL",qPrintable(error));app.exit(error.isEmpty()?0:23);}
    bool capture(const QString& name){auto* view=frame.ViewWidget();if(!view)return false;auto image=view->grabFramebuffer();if(image.isNull())return false;
        int lit=0;QSet<QRgb> colors;for(int y=0;y<image.height();y+=8)for(int x=0;x<image.width();x+=8){auto c=image.pixelColor(x,y);if(c.red()+c.green()+c.blue()>45)lit++;colors.insert(c.rgb());}
        bool ok=lit>500&&colors.size()>100&&(qApp->property("modernUi").toBool()?frame.grab().toImage():image).save(out.filePath(name+".png"));captures.append(QJsonObject{{"name",name},{"nonBlackSamples",lit},{"distinctColors",colors.size()},{"passed",ok}});return ok;}
    void control(int id,int elapsed=100){DiControlInputArray a;DiControlInput c(-1,quint8(id));c.SetMaxRangeValue();a.append(c);app.ProcessControlInput(app.AvatarId(),a,AuGearPos(),0,elapsed);if(id==DaPerson::CI_MoveForward||id==DaPerson::CI_MoveBackward){auto& e=g_Au.LockEtheriumDelete();auto* p=dynamic_cast<DaPerson*>(e.FindNormalAtom(app.AvatarId()));if(p)moving|=p->IsMoving();g_Au.UnlockEtheriumDelete();}}
    void tick(){
        if(finished)return;auto* renderView=frame.ViewWidget();if(renderView)renderView->update();if(clock.elapsed()>180000){finish("Client did not complete within 180 seconds");return;}
        if(!started){started=true;result["phase"]="creating-disposable-world";save();auto& basic=g_Au.LockDataForWrite();int loaded=basic.LoadBasicGameData();if(!loaded)basic.SoloAdjustGameData();g_Au.UnlockData();if(loaded){finish("Basic game data GD is missing or invalid");return;}g_Au.Dice().Seed(42791);DhDNA dna;if(!dna.FromString("561300G0I0002000010001E21102G77O2A301116811002G2K4520030EM2K0K0")){finish("Invalid fixture DNA");return;}
            app.StartNewAvatarSolo(0,"Renderer Test",dna,AuDNABase::Alpha,RightHanded,0,AndromedaRising,SavageState,AuTime(),0,AuId(),QString(),QImage());const auto& data=g_Au.LockDataForRead();AuId id=data.ClientUnitId();g_Au.UnlockData();if(id.IsNull()){finish("Disposable avatar creation failed");return;}result["phase"]="waiting-for-world";save();return;}
        if(app.RunState()!=AuApp::Solitaire){finish("Test left isolated single-player mode");return;}
        auto& ether=g_Au.LockEtheriumDelete();auto* person=dynamic_cast<DaPerson*>(ether.FindNormalAtom(app.AvatarId()));
        bool ready=person!=nullptr;AuPoint location;AuId currentParent;int currentItem=-1;
        if(person){location=person->Location();currentParent=person->ParentId();currentItem=int(person->CurrentItemCID());items.insert(currentItem);if(actions.isValid()&&actions.elapsed()>=6000&&actions.elapsed()<12000)moving|=person->IsMoving();}
        g_Au.UnlockEtheriumDelete();if(!actions.isValid()){result["avatarReady"]=ready;result["viewReady"]=renderView!=nullptr;result["runState"]=int(app.RunState());save();if(clock.elapsed()>10000&&!out.exists("waiting.png"))capture("waiting");}if(!ready)return;
        QFile status(out.filePath("renderer-status.json"));QJsonObject coverage;if(status.open(QIODevice::ReadOnly))coverage=QJsonDocument::fromJson(status.readAll()).object();
        if(coverage.value("draws").toInt()<100||!coverage.value("families").toObject().contains("Terrain__Terrain")){
            // Terrain variants may include fog/globe suffixes.
            bool terrain=false;for(auto k:coverage.value("families").toObject().keys())terrain|=k.startsWith("Terrain");if(!terrain||coverage.value("draws").toInt()<100)return;
        }
        if(coverage.value("unsupportedDraws").toInt()!=0){finish("Unsupported draw in live scene");return;}
        if(!actions.isValid()){
            start=location;parent=currentParent;actions.start();result["phase"]="world-actions";result["avatarId"]=app.AvatarId().StringId();save();
            thirdPerson=true; // The isolated profile starts in third-person mode.
            AuItemContainer testPack(40,CID_Backpack,100,0);bool packed=testPack.FitItemIntoContainer(AuItem(CID_Hammer,100,0))&&testPack.FitItemIntoContainer(AuItem(CID_Knife,100,0));
            auto& equipmentEther=g_Au.LockEtheriumDelete();auto* equipped=dynamic_cast<DaPerson*>(equipmentEther.FindNormalAtom(app.AvatarId()));bool gearOk=packed&&equipped&&equipped->AddGearItemUseDrop(AuItem(CID_Hammer,100,0),1,false)&&equipped->AddGearItemUseDrop(AuItem(CID_Knife,100,0),1,false)&&equipped->AddGearItemUseDrop(testPack,1,false);g_Au.UnlockEtheriumDelete();if(!gearOk){finish("Test equipment setup failed");return;}control(DaPerson::CI_SelectNextItem);return;
        }
        int ms=actions.elapsed(),stage=ms<6000?0:ms<9000?1:ms<12000?2:ms<15000?3:ms<18000?4:5;
        if(stage!=lastStage){if(lastStage>=0&&!capture(QString("stage-%1").arg(lastStage))){finish("Blank or invalid framebuffer");return;}lastStage=stage;if(stage==1){start=location;parent=currentParent;}if(stage==4)selectionItem=currentItem;result["stage"]=stage;save();}
        if(stage==1||stage==2){double x=location.x-start.x,y=location.y-start.y,z=location.z-start.z;if(parent==currentParent)maxMove=std::max(maxMove,std::sqrt(x*x+y*y+z*z));}if(stage==4&&currentItem!=selectionItem)equipmentChanged=true;
        if(stage==1)control(DaPerson::CI_MoveForward);
        if(stage==2)control(DaPerson::CI_MoveBackward);
        if(stage==3){control(DaPerson::CI_FireItem);++attackCommands;}
        if(stage==4&&ms%700<150)control(DaPerson::CI_SelectNextItem);
        auto f=coverage.value("frame").toVariant().toLongLong();if(ms>18000&&f!=lastFrame){lastFrame=f;samples.append(coverage);}
        if(ms>28000){double dx=location.x-start.x,dy=location.y-start.y,dz=location.z-start.z;result["endDisplacementFeet"]=std::sqrt(dx*dx+dy*dy+dz*dz);result["worldCoverage"]=coverage;result["thirdPerson"]=thirdPerson;
            if(!capture("final")){finish("Final framebuffer is blank");return;}if(!moving||maxMove<.1){finish("Movement control did not change movement state and position");return;}if(!equipmentChanged){finish("Equipment selection did not change");return;}if(samples.size()<3){finish("Insufficient live performance samples");return;}timer.stop();if(qApp->property("modernUi").toBool()){auto* object=frame.findChild<QObject*>("ModernGameUi");QString uiError;if(!object||!static_cast<ModernGameUi*>(object)->verify(out,uiError)){finish(uiError.isEmpty()?"Modern UI missing":uiError);return;}result["uiChecksPassed"]=true;}finish();}
    }
public:
    ClientSmoke(AuClientApp& a,AuClientFrame& f,const QString& path):QObject(&a),app(a),frame(f),out(path){

#ifdef Q_OS_WIN
        SetUnhandledExceptionFilter(SmokeCrash);
#endif
        result={{"test","real-client-disposable-world"},{"passed",false},{"phase","starting"},{"networkRequired",false},{"profile",out.filePath("profile")}};save();clock.start();timer.setInterval(100);connect(&timer,&QTimer::timeout,this,[this]{tick();});timer.start();
    }
};
#endif
