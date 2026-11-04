#ifndef OPENSHORES_MODERN_UI_H
#define OPENSHORES_MODERN_UI_H
#include "auclientapp.h"
#include "auclientframe.h"
#include "audesignerview.h"
#include "auglobal.h"
#include "dgdata.h"
#include "auether.h"
#include "daperson.h"
#include <QToolBar>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QDialog>
#include <QVBoxLayout>
#include <QShortcut>
#include <QMenu>
#include <QTimer>
#include <QDockWidget>
#include <QTreeView>
#include <QJsonObject>
#include <QStyle>
#include <QEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QEventLoop>
#include <QSet>
#include <QProgressBar>
#include <algorithm>
#include <cmath>
#include "auinventorygrid.h"
#include "game_hud.h"

class ModernGameUi : public QObject {
    struct Command {QString title,detail,dock,slot,unavailable;};
    AuClientFrame& frame;
    QToolBar *header=nullptr,*rail=nullptr;
    GameVitalsHud* hud=nullptr;QWidget* loadout=nullptr;
    QProgressBar *health=nullptr,*stamina=nullptr,*food=nullptr;
    QToolButton* equipped=nullptr; QAction* saveAction=nullptr;
    QStringList recent;
    QWidget* railBody=nullptr;
    QLabel *identity=nullptr,*sectionLabel=nullptr;
    QToolButton *collapse=nullptr,*findButton=nullptr;
    QList<QToolButton*> nav;
    QDialog* palette=nullptr; QLineEdit* query=nullptr; QListWidget* results=nullptr;QLabel* paletteHint=nullptr;
    QVector<Command> commands; bool compact=false,forceExpanded=false; QTimer refresh;

    QDockWidget* dock(const QString& id) const {
        QDockWidget* found=nullptr;for(auto* d:frame.findChildren<QDockWidget*>())if(d->objectName()==id){if(!d->isHidden())return d;if(!found)found=d;}
        return found;
    }
    QString commandKey(const Command& c) const{return c.dock.isEmpty()?"action/"+c.slot:c.dock;}
    void remember(const Command& c){auto key=commandKey(c);recent.removeAll(key);recent.prepend(key);while(recent.size()>6)recent.removeLast();g_Au.Settings().setValue("UI/RecentPanels",recent);}
    void run(const Command& c,bool toggle=false){
        if(!c.unavailable.isEmpty())return;
        if(c.slot=="QuickGuide"){showHelp();return;}
        if(c.slot=="ClosePanels"){closePanels();return;}
        if(!c.slot.isEmpty()&&!c.dock.isEmpty()){auto* existing=dock(c.dock);if(existing&&!existing->isHidden()&&!toggle){existing->raise();return;}QSet<QDockWidget*> before;for(auto* d:frame.findChildren<QDockWidget*>())if(!d->isHidden())before.insert(d);QMetaObject::invokeMethod(&frame,c.slot.toUtf8().constData(),Qt::DirectConnection);for(auto* d:frame.findChildren<QDockWidget*>())if(!d->isHidden()&&!before.contains(d)){auto opened=c;opened.slot.clear();opened.dock=d->objectName();run(opened);}sync();return;}
        if(auto* d=dock(c.dock)){
            if(toggle&&!d->isHidden()){d->hide();return;}
            if(d->isFloating())d->setFloating(false);
            auto area=c.dock.contains("Comm")?Qt::BottomDockWidgetArea:Qt::RightDockWidgetArea;
            if(!(d->allowedAreas()&area))area=Qt::LeftDockWidgetArea;
            QDockWidget* peer=nullptr;if(area==Qt::RightDockWidgetArea)for(auto* other:frame.findChildren<QDockWidget*>())if(other!=d&&!other->isHidden()&&frame.dockWidgetArea(other)==area){peer=other;break;}
            frame.addDockWidget(area,d);if(peer)frame.tabifyDockWidget(peer,d);d->show();d->raise();
            frame.resizeDocks(QList<QDockWidget*>()<<d,QList<int>()<<(area==Qt::BottomDockWidgetArea?230:360),area==Qt::BottomDockWidgetArea?Qt::Vertical:Qt::Horizontal);remember(c);
        }else if(!c.slot.isEmpty()){QMetaObject::invokeMethod(&frame,c.slot.toUtf8().constData(),Qt::DirectConnection);remember(c);}
        sync();
    }
    Command panel(const QString& title,const QString& detail,const QString& id,const QString& slot={}){return {title,detail,id,slot};}
    void addNav(QVBoxLayout* layout,const Command& command,const char* icon){
        auto* b=new QToolButton(railBody);b->setText(command.title);b->setIcon(QIcon(icon));
        if(b->icon().isNull())b->setIcon(frame.style()->standardIcon(QStyle::SP_DirIcon));
        b->setIconSize(QSize(22,22));b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);b->setCheckable(true);
        b->setObjectName("Nav"+command.dock);b->setAccessibleName(command.title);b->setToolTip(command.title+"\n"+command.detail);b->setFocusPolicy(Qt::NoFocus);
        b->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);b->setProperty("dockId",command.dock);
        connect(b,&QToolButton::clicked,this,[this,command]{run(command,true);});layout->addWidget(b);nav<<b;
    }
    void rebuildCommands(){
        commands.clear();QSet<QString> seen;
        for(auto* d:frame.findChildren<QDockWidget*>()){
            if(d->objectName().isEmpty()||d->windowTitle().isEmpty()||seen.contains(d->objectName()))continue;seen.insert(d->objectName());
            auto* view=frame.ViewWidget();bool empire=view&&view->SceneAtomEmpire();bool ship=empire||(view&&view->SceneAtomIsOfficer());if(d->objectName().contains("Govern")&&!empire)continue;if(d->objectName().contains("Spacecraft")&&d->objectName()!="SpacecraftDock"&&!ship)continue;
            QString title=d->windowTitle();
            if(d->objectName()=="GearDock")title="Gear & inventory";
            else if(d->objectName()=="BioDock")title="Character & skills";
            else if(d->objectName().contains("StarMap",Qt::CaseInsensitive))title="Star map";
            QString detail="Open panel · "+d->windowTitle();
            if(d->objectName()=="GearDock")detail="Equipment, items, tools and clothing · inventory backpack wear equip hold weapon";
            else if(d->objectName()=="BioDock")detail="Health, skills, biography and jobs";
            else if(d->objectName().contains("Spacecraft"))detail="Ship systems · "+d->windowTitle();
            else if(d->objectName().contains("Govern"))detail="Empire management · "+d->windowTitle();
            QString slot;if(d->objectName()=="SpacecraftDock")slot="ToggleSpacecraftUi";if(d->objectName()=="HoldDock")slot="ToggleHoldUi";if(d->objectName()=="GovernDock")slot="ToggleGovernUi";commands.append(panel(title,detail,d->objectName(),slot));
        }
        if(frame.ViewWidget()&&!frame.ViewWidget()->SceneAtomEmpire()){auto locked=panel("Empire governance","Cities, citizens and empire management","GovernDock","ToggleGovernUi");locked.unavailable="Join or establish an empire to unlock governance.";commands.append(locked);}
        commands.append(panel("Handcraft","Make tools, clothing and supplies",{},"ToggleHandCraftUi"));
        commands.append(panel("Settings","Display, sound, controls and interface",{},"ToggleSettingsUi"));
        commands.append(panel("Help & about","Game information and credits",{},"HelpAbout"));
        commands.append(panel("Getting started & controls","How to play · inventory, crafting, movement and shortcuts",{},"QuickGuide"));
        commands.append(panel("Return to world","Close all panels · clear screen · resume playing",{},"ClosePanels"));
        std::sort(commands.begin(),commands.end(),[](const Command&a,const Command&b){return a.title.localeAwareCompare(b.title)<0;});
    }
    void filter(){
        results->clear();const QString needle=query->text().simplified();
        const auto words=needle.split(' ',Qt::SkipEmptyParts);
        QVector<int> order;QSet<int> added;
        for(const auto& key:recent)for(int i=0;i<commands.size();i++)if(commandKey(commands[i])==key&&!added.contains(i)){order<<i;added.insert(i);}
        for(int i=0;i<commands.size();i++)if(!added.contains(i))order<<i;
        int firstEnabled=-1;
        for(int i:order){const auto& c=commands[i];const auto haystack=c.title+" "+c.detail+" "+c.dock+" "+c.unavailable;bool matches=true;for(const auto& word:words)if(!haystack.contains(word,Qt::CaseInsensitive)){matches=false;break;}if(!matches)continue;
            QString title=c.title;if(needle.isEmpty()&&recent.contains(commandKey(c)))title="Recent  ·  "+title;
            auto* item=new QListWidgetItem(title+"\n"+(c.unavailable.isEmpty()?c.detail:c.unavailable),results);item->setData(Qt::UserRole,i);item->setSizeHint(QSize(0,58));
            if(!c.unavailable.isEmpty()){item->setFlags(item->flags()&~Qt::ItemIsEnabled&~Qt::ItemIsSelectable);item->setToolTip(c.unavailable);}else if(firstEnabled<0)firstEnabled=results->row(item);
        }
        if(firstEnabled>=0)results->setCurrentRow(firstEnabled);
        if(paletteHint)paletteHint->setText(!results->count()?"No panels found. Try gear, map, ship or settings.":firstEnabled<0?"These tools unlock as you progress. Hover for details.":"Enter to open  ·  ↑ ↓ to navigate  ·  Esc to close");
    }
    void updateMeter(QProgressBar* bar,const QString& label,int current,int maximum,const QString&){
        bool valid=maximum>0;int percent=valid?std::max(0,std::min(100,int(std::round(current*100.0/maximum)))):0;
        bar->setEnabled(valid);bar->setValue(percent);bar->setAccessibleName(valid?QString("%1: %2 / %3").arg(label).arg(current).arg(maximum):"Waiting for character data");
    }
    void positionHud(){
        auto* view=frame.ViewWidget();if(!view||!hud)return;int w=view->width(),h=view->height();bool tiny=w<520;
        int pod=tiny?54:w<750?140:170;int gauge=std::min(344,std::max(200,w-pod-40));
        hud->setGeometry(12,std::max(0,h-hud->height()-14),gauge,hud->height());
        loadout->setGeometry(std::max(0,w-pod-12),std::max(0,h-84),pod,70);equipped->setToolButtonStyle(tiny?Qt::ToolButtonIconOnly:Qt::ToolButtonTextBesideIcon);
        hud->raise();loadout->raise();
    }
    void updateVitals(){
        if(!hud)return;int hp=0,maxHp=0,energy=0,maxEnergy=0,nutrition=0,maxNutrition=0;COMMODITYID item=CID_Invalid;bool ready=false;
        if(auApp->IsSingleUserMode()||auApp->IsSceneConnected()){
            auto& ether=g_Au.LockEtheriumDelete();auto* person=dynamic_cast<DaPerson*>(ether.FindNormalAtom(auApp->AvatarId()));
            if(person){ready=true;hp=person->CurrentHitPoints();maxHp=person->MaxHitPoints();energy=person->CurrentStamina();maxEnergy=person->MaxStamina();nutrition=person->CurrentHunger();maxNutrition=person->MaxHunger();item=person->CurrentItemCID();}g_Au.UnlockEtheriumDelete();
        }
        updateMeter(health,"Health",hp,maxHp,"#285b50");updateMeter(stamina,"Stamina",energy,maxEnergy,"#314e72");updateMeter(food,"Food",nutrition,maxNutrition,"#634c70");
        hud->setStats(hp,maxHp,energy,maxEnergy,nutrition,maxNutrition);
        if(auto* view=frame.ViewWidget())hud->setPower(view->property("hudMovePower").toFloat(),view->property("hudJumpPower").toFloat());
        QString name=ready?(item==CID_Invalid?QString("Unarmed"):g_Au.CommodityName(item)):QString("Waiting for character...");
        equipped->setEnabled(ready);equipped->setToolTip("Selected item: "+name+"\nOpen gear to change equipment.");
        equipped->setText("HELD ITEM"+QString("\n")+equipped->fontMetrics().elidedText(name,Qt::ElideRight,std::max(45,equipped->width()-70)));
        equipped->setIcon(ready&&item!=CID_Invalid?QIcon(AuItem(item,100,0).BitmapName()):QIcon(":data/Inventory.png"));
        if(saveAction)saveAction->setEnabled(auApp->IsSingleUserMode());
    }
    void settle(){if(frame.ViewWidget())frame.ViewWidget()->update();QEventLoop loop;QTimer::singleShot(500,&loop,&QEventLoop::quit);loop.exec(QEventLoop::ExcludeUserInputEvents);if(frame.ViewWidget())frame.ViewWidget()->grabFramebuffer();}
    void choose(){auto* item=results->currentItem();if(!item)return;auto c=commands[item->data(Qt::UserRole).toInt()];palette->hide();run(c);}
    bool eventFilter(QObject* target,QEvent* e) override {
        if((target==&frame||target==frame.ViewWidget())&&e->type()==QEvent::Resize)QTimer::singleShot(0,this,[this]{sizeRail();});
        if(target==query&&e->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(e);if(key->key()==Qt::Key_Down){results->setFocus();for(int r=0;r<results->count();r++)if(results->item(r)->flags()&Qt::ItemIsEnabled){results->setCurrentRow(r);break;}return true;}if(key->key()==Qt::Key_Escape){palette->hide();return true;}}
        return QObject::eventFilter(target,e);
    }
    void sizeRail(){
        if(!railBody)return;bool small=compact||(frame.width()<1100&&!forceExpanded);
        railBody->setFixedWidth(small?58:174);rail->setFixedWidth(small?60:176);
        for(auto* b:nav){b->setToolButtonStyle(small?Qt::ToolButtonIconOnly:Qt::ToolButtonTextBesideIcon);b->setMinimumHeight(42);}
        sectionLabel->setVisible(!small);collapse->setText(small?">>":"<<  Collapse navigation");
        identity->setVisible(frame.width()>=1050);
        positionHud();
        if(auto* all=railBody->findChild<QToolButton*>("AllPanels"))all->setText(small?"...":"All panels  ·  Ctrl+K");
    }
    void sync(){
        if(!header)return;
        bool game=frame.ViewWidget()&&frame.ViewWidget()->SceneContext()!=SCENE_Designer;
        header->setVisible(game);rail->setVisible(game);if(hud)hud->setVisible(game);if(loadout)loadout->setVisible(game);qApp->setProperty("modernNavigationReady",game);
        if(frame.ViewWidget())frame.ViewWidget()->setProperty("modernVitals",game&&hud);
        if(!game)return;
        const auto& data=g_Au.LockDataForRead();QString name=data.ClientUnitName(data.ClientUnitId());g_Au.UnlockData();
        identity->setText((auApp->IsSingleUserMode()?"LOCAL UNIVERSE  /  ":auApp->IsSceneConnected()?"CONNECTED  /  ":"RECONNECTING  /  ")+name);
        for(auto* b:nav){auto id=b->property("dockId").toString();auto* d=dock(id);b->setChecked(d&&!d->isHidden());if(id=="GovernDock"){bool available=frame.ViewWidget()->SceneAtomEmpire()!=0;b->setEnabled(available);b->setToolTip(available?"Empire governance, cities and citizens":"Join or establish an empire to unlock governance.");}}
        sizeRail();updateVitals();
    }
    void create(){
        header=new QToolBar("Command bar",&frame);header->setObjectName("ModernCommandBar");header->setMovable(false);header->setFloatable(false);header->setContextMenuPolicy(Qt::PreventContextMenu);frame.addToolBar(Qt::TopToolBarArea,header);
        auto* brand=new QLabel("  OPEN<span style='color:#d9b66e'>SHORES</span>  ",&frame);brand->setObjectName("CommandBrand");header->addWidget(brand);
        findButton=new QToolButton(&frame);findButton->setObjectName("CommandSearch");findButton->setText("Find panels & actions...     Ctrl+K");findButton->setAccessibleName("Find panels and actions");findButton->setMinimumWidth(260);findButton->setToolButtonStyle(Qt::ToolButtonTextOnly);connect(findButton,&QToolButton::clicked,this,[this]{showPalette();});header->addWidget(findButton);
        auto* space=new QWidget(&frame);space->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);header->addWidget(space);
        auto* world=new QToolButton(&frame);world->setObjectName("ReturnToWorld");world->setText("Return to world");world->setToolTip("Close all panels and return focus to the world · Ctrl+Shift+Space");connect(world,&QToolButton::clicked,this,[this]{closePanels();});header->addWidget(world);
        identity=new QLabel(&frame);identity->setObjectName("CommandIdentity");header->addWidget(identity);
        auto* game=new QToolButton(&frame);game->setText("Game");game->setAccessibleName("Game menu");game->setPopupMode(QToolButton::InstantPopup);auto* menu=new QMenu(game);
        menu->addAction("Settings",this,[this]{QMetaObject::invokeMethod(&frame,"ToggleSettingsUi");});
        menu->addAction("Controls & quick help",this,[this]{showHelp();});
        menu->addAction("Reset camera",this,[this]{if(frame.ViewWidget())QMetaObject::invokeMethod(frame.ViewWidget(),"OnViewResetCamera");});
        saveAction=menu->addAction("Save single-player game",this,[this]{if(auApp->IsSingleUserMode()&&frame.ViewWidget())QMetaObject::invokeMethod(frame.ViewWidget(),"SaveSolitaireCheckPoint");});
        menu->addAction("Fullscreen",this,[this]{QMetaObject::invokeMethod(&frame,"ToggleFullScreen");});menu->addSeparator();
        menu->addAction("Close all panels",this,[this]{closePanels();});menu->addAction("Reset panel layout",this,[this]{resetLayout();});menu->addSeparator();
        menu->addAction("About OpenShores",this,[this]{QMetaObject::invokeMethod(&frame,"HelpAbout");});game->setMenu(menu);header->addWidget(game);

        rail=new QToolBar("Navigation",&frame);rail->setObjectName("ModernNavigation");rail->setMovable(false);rail->setFloatable(false);rail->setContextMenuPolicy(Qt::PreventContextMenu);frame.addToolBar(Qt::LeftToolBarArea,rail);
        railBody=new QWidget(&frame);railBody->setObjectName("NavigationBody");auto* layout=new QVBoxLayout(railBody);layout->setContentsMargins(8,12,8,10);layout->setSpacing(5);
        sectionLabel=new QLabel("YOUR JOURNEY",railBody);sectionLabel->setObjectName("NavigationSection");layout->addWidget(sectionLabel);
        addNav(layout,panel("Gear","Equipment, items and inventory","GearDock"),":data/Inventory.png");
        addNav(layout,panel("Character","Health, biography, skills and jobs","BioDock"),":data/c_VacSuitHelmet.png");
        addNav(layout,panel("Craft","Create tools and supplies",{},"ToggleHandCraftUi"),":data/it_HandCraft.png");
        addNav(layout,panel("Star map","Find worlds and plan your journey","Star MapDock","ToggleLocatorStarMapUi"),":data/RangeGalaxy.png");
        addNav(layout,panel("Build","Construction and settlement tools","ConstructionDock","ToggleConstructionUi"),":data/ConstructionSign.png");
        addNav(layout,panel("Communications","Chat and communication channels","CommDock"),":data/Comm.png");
        addNav(layout,panel("Spacecraft","Ship systems, crew and cargo","SpacecraftDock","ToggleSpacecraftUi"),":data/GovSpacecraft.png");
        addNav(layout,panel("Empire","Cities, citizens and governance","GovernDock","ToggleGovernUi"),":data/CommGovernment.png");
        layout->addStretch();auto* all=new QToolButton(railBody);all->setObjectName("AllPanels");all->setText("All panels  ·  Ctrl+K");all->setToolTip("Search every game panel");connect(all,&QToolButton::clicked,this,[this]{showPalette();});layout->addWidget(all);
        collapse=new QToolButton(railBody);collapse->setObjectName("CollapseNavigation");collapse->setAccessibleName("Collapse or expand navigation");connect(collapse,&QToolButton::clicked,this,[this]{bool small=rail->width()<100;compact=!small;forceExpanded=small;g_Au.Settings().writeEntry("UI/CompactNavigation",compact);sizeRail();});layout->addWidget(collapse);rail->addWidget(railBody);
        auto* shortcut=new QShortcut(QKeySequence("Ctrl+K"),&frame);connect(shortcut,&QShortcut::activated,this,[this]{showPalette();});
        auto* clean=new QShortcut(QKeySequence("Ctrl+Shift+Space"),&frame);connect(clean,&QShortcut::activated,this,[this]{closePanels();});
        hud=new GameVitalsHud(frame.ViewWidget());
        auto meter=[this](const QString& name){auto* bar=new QProgressBar(hud);bar->setObjectName(name);bar->setRange(0,100);bar->hide();return bar;};
        health=meter("PlayerHealth");stamina=meter("PlayerStamina");food=meter("PlayerFood");
        loadout=new QWidget(frame.ViewWidget());loadout->setObjectName("GameLoadout");auto* pod=new QVBoxLayout(loadout);pod->setContentsMargins(0,0,0,0);pod->setSpacing(4);
        equipped=new QToolButton(loadout);equipped->setObjectName("SelectedEquipment");equipped->setIconSize(QSize(28,28));equipped->setMinimumHeight(44);equipped->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);equipped->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);connect(equipped,&QToolButton::clicked,this,[this]{run(panel("Gear",{},"GearDock"));});pod->addWidget(equipped);
        auto* help=new QToolButton(loadout);help->setText("Controls ?");connect(help,&QToolButton::clicked,this,[this]{showHelp();});pod->addWidget(help);
        frame.ViewWidget()->installEventFilter(this);
        frame.installEventFilter(this);sync();
    }
public:
    static void applyTheme(){
        qApp->setProperty("modernUi",true);qApp->setProperty("titleBarHeight",34);
        QFont font=qApp->font();font.setFamily("Segoe UI");font.setPointSize(10);qApp->setFont(font);
        qApp->setStyleSheet(qApp->styleSheet()+QString::fromUtf8(R"QSS(
QWidget { color: #dce7f5; selection-background-color: #2a4970; selection-color: #ffffff; }
QMainWindow, QDialog, QDockWidget, QDockWidget > QWidget { background: #101c30; }
QMainWindow::separator { background: #263b57; width: 4px; height: 4px; }
QLabel { background: transparent; }
QPushButton, QToolButton { background-image: none; border-image: none; background-color: #1a2c46; border: 1px solid #35506f; border-radius: 3px; padding: 5px 8px; color: #e3ebf5; }
QPushButton:hover, QToolButton:hover { background-color: #263f60; border-color: #8296b3; }
QPushButton:pressed, QToolButton:pressed, QToolButton:checked { background-color: #314b6c; border-color: #d9b66e; color: #ffe4a9; }
QPushButton:disabled, QToolButton:disabled { color: #8190a3; border-color: #26384d; background: #152237; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QTextEdit, QPlainTextEdit { background: #0a1423; border: 1px solid #344c69; border-radius: 3px; padding: 5px; color: #edf2f8; selection-background-color: #335b83; }
QLineEdit:focus, QTextEdit:focus, QComboBox:focus { border-color: #d9b66e; }
QTreeView, QListView, QTableView { background: #0c1728; alternate-background-color: #122138; border: 1px solid #2a405e; color: #dce7f5; outline: 0; }
QTreeView::item, QTableView::item { padding: 4px; }
QTreeView::item:selected, QListView::item:selected, QTableView::item:selected { background: #294665; color: #fff0cb; }
QHeaderView::section { background: #1a2d47; color: #becde0; border: 0; border-right: 1px solid #344c68; padding: 7px; }
QTabWidget::pane { border: 1px solid #344c68; background: #101c30; }
QTabBar::tab { background-image: none; border-image: none; background: #17263d; color: #b8c9df; border: 1px solid #304765; padding: 7px 11px; }
QTabBar::tab:selected { background: #253e5d; color: #ffe4a9; border-bottom-color: #d9b66e; }
QMenu { background: #122139; color: #e7eff9; border: 1px solid #425d7d; padding: 5px; }
QMenu::item { padding: 8px 24px 8px 12px; } QMenu::item:selected { background: #2a4566; }
QToolTip { color: #eff4fa; background: #1b304c; border: 1px solid #7186a3; padding: 7px; }
QScrollBar:vertical { background: #0d1829; width: 12px; margin: 0; } QScrollBar::handle:vertical { background: #435b78; min-height: 26px; border-radius: 4px; }
QToolBar#ModernCommandBar { background: #0c1420; border: 0; border-bottom: 1px solid #53617b; padding: 8px; spacing: 10px; }
QLabel#CommandBrand { color: #f2f4f8; font-size: 18px; font-weight: 600; letter-spacing: 2px; padding-right: 12px; }
QLabel#CommandIdentity { color: #9eafc5; font-size: 11px; padding: 0 14px; }
QToolButton#CommandSearch { text-align: left; background: #0a1423; border-color: #405572; padding: 8px 14px; color: #adbed3; }
QToolBar#ModernNavigation { background: #0e1a2d; border: 0; border-right: 1px solid #344966; spacing: 0; padding: 0; }
QWidget#NavigationBody { background: #0e1a2d; }
QWidget#NavigationBody QToolButton { text-align: left; border: 1px solid transparent; background: transparent; padding: 7px; }
QWidget#NavigationBody QToolButton:hover { background: #1c304b; border-color: #415a78; }
QWidget#NavigationBody QToolButton:checked { background: #26313c; border:1px solid #6a604b; border-left:3px solid #e5bd6f; color: #ffe4a9; }
QLabel#NavigationSection { color: #9cabc0; font-size: 10px; letter-spacing: 2px; padding: 5px 4px 12px; }
QDialog#CommandPalette { background: #101e33; border: 1px solid #8f7958; }
QWidget#GameLoadout { background:transparent; }
QToolButton#SelectedEquipment { background:#0b1420; border:1px solid #a38d60; border-left:3px solid #d3b273; color:#f2dfb7; padding:4px; font-size:10px; font-weight:600; }
QProgressBar { background:#0a1423; border:1px solid #3a506c; border-radius:3px; color:#edf2f8; font-size:11px; text-align:center; }
QProgressBar::chunk { border-radius:2px; }
QListWidget#CommandResults { background: #101e33; border: 0; font-size: 13px; }
QListWidget#CommandResults::item { border-bottom: 1px solid #23384f; padding: 6px 12px; }
QListWidget#CommandResults::item:selected { background: #284765; color: #fff0ca; }
)QSS"));
    }
    ModernGameUi(AuClientFrame& f):QObject(&f),frame(f){setObjectName("ModernGameUi");recent=g_Au.Settings().value("UI/RecentPanels").toStringList();compact=g_Au.Settings().readBoolEntry("UI/CompactNavigation",false);refresh.setInterval(500);connect(&refresh,&QTimer::timeout,this,[this]{if(!header){if(frame.findChild<QDockWidget*>("GearDock"))create();}else sync();});refresh.start();}
    void showPalette(){
        if(!palette){palette=new QDialog(&frame,Qt::Dialog);palette->setObjectName("CommandPalette");palette->setWindowTitle("Find panels & actions");palette->setModal(false);auto* l=new QVBoxLayout(palette);l->setContentsMargins(16,16,16,14);l->setSpacing(12);
            auto* title=new QLabel("GO TO A PANEL",palette);title->setStyleSheet("color:#d9b66e;font-size:11px;letter-spacing:2px");l->addWidget(title);
            query=new QLineEdit(palette);query->setObjectName("CommandQuery");query->setPlaceholderText("Search gear, crafting, star map, ship systems...");query->setClearButtonEnabled(true);query->setAccessibleName("Search panels and actions");l->addWidget(query);
            results=new QListWidget(palette);results->setObjectName("CommandResults");l->addWidget(results);paletteHint=new QLabel("Enter to open  ·  ↑ ↓ to navigate  ·  Esc to close",palette);paletteHint->setStyleSheet("color:#9baec7;font-size:11px");l->addWidget(paletteHint);
            connect(query,&QLineEdit::textChanged,this,[this]{filter();});connect(query,&QLineEdit::returnPressed,this,[this]{choose();});connect(results,&QListWidget::itemActivated,this,[this](QListWidgetItem*){choose();});query->installEventFilter(this);
        }
        rebuildCommands();query->clear();filter();palette->resize(std::min(620,frame.width()-60),std::min(540,frame.height()-80));palette->move(frame.geometry().center()-palette->rect().center());palette->show();palette->raise();query->setFocus();
    }
    void closePanels(){for(auto* d:frame.findChildren<QDockWidget*>())d->hide();if(frame.ViewWidget())frame.ViewWidget()->setFocus();sync();}
    void resetLayout(){closePanels();for(auto* d:frame.findChildren<QDockWidget*>())if(d->isFloating())d->setFloating(false);compact=false;forceExpanded=false;sizeRail();}
    void showHelp(){
        if(auto* existing=frame.findChild<QDialog*>("QuickGuide")){existing->show();existing->raise();return;}
        auto* d=new QDialog(&frame);d->setObjectName("QuickGuide");d->setAttribute(Qt::WA_DeleteOnClose);d->setWindowTitle("Getting started & controls");auto* l=new QVBoxLayout(d);l->setSpacing(12);
        auto* intro=new QLabel("<h2>What would you like to do?</h2>Choose a task to open its tools. Return here using Controls ? or search.",d);intro->setWordWrap(true);l->addWidget(intro);
        auto task=[this,d,l](const QString& text,const QString& hint,const Command& c,const QString& id){auto* b=new QPushButton(text,d);b->setObjectName(id);b->setToolTip(hint);b->setAccessibleDescription(hint);connect(b,&QPushButton::clicked,this,[this,d,c]{d->close();run(c);});l->addWidget(b);};
        task("Manage items and equipment","Select an item in Gear, then choose Hold, Inspect or Drop.",panel("Gear",{},"GearDock"),"GuideGear");
        task("Make tools and supplies","Choose a recipe and see its requirements in handcrafting.",panel("Craft",{},{},"ToggleHandCraftUi"),"GuideCraft");
        task("Explore worlds and plan travel","Find worlds and inspect your surroundings on the star map.",panel("Star map",{},"Star MapDock","ToggleLocatorStarMapUi"),"GuideMap");
        task("View or change controls","Open Settings for your actual movement, camera and action bindings.",panel("Settings",{},{},"ToggleSettingsUi"),"GuideSettings");
        auto* tips=new QLabel("<b>Find a tool:</b> Ctrl+K, then type what you want to do.<br><b>Return to the world:</b> Ctrl+Shift+Space or Return to world.<br><b>Manage panels:</b> click a navigation button again to close its panel; drag a panel title to dock or float it.<br><b>Locked tools:</b> hover for their requirements.",d);tips->setWordWrap(true);l->addWidget(tips);
        auto* done=new QPushButton("Back to playing",d);connect(done,&QPushButton::clicked,d,&QDialog::close);l->addWidget(done);d->resize(510,480);d->show();
    }
    bool verify(const QDir& out,QString& error){
        if(!header){error="Modern navigation did not initialize";return false;}
        showHelp();auto* guide=frame.findChild<QDialog*>("QuickGuide");auto* guideGear=guide?guide->findChild<QPushButton*>("GuideGear"):nullptr;if(!guideGear){error="Task guide missing";return false;}guide->grab().save(out.filePath("ui-guide.png"));guideGear->click();if(!dock("GearDock")||dock("GearDock")->isHidden()){error="Guide did not open inventory";return false;}auto* back=frame.findChild<QToolButton*>("ReturnToWorld");if(!back){error="Return-to-world shortcut missing";return false;}back->click();if(!dock("GearDock")->isHidden()){error="Return to world did not close panels";return false;}
        showPalette();query->setText("backpack");if(!results->count()){error="Task synonym search failed";return false;}palette->hide();
        sync();if(!health||!food||health->maximum()!=100){error="Character status missing";return false;}
        auto& statusEther=g_Au.LockEtheriumDelete();auto* testPerson=dynamic_cast<DaPerson*>(statusEther.FindNormalAtom(auApp->AvatarId()));int oldFood=testPerson?testPerson->CurrentHunger():0;int maxFood=testPerson?testPerson->MaxHunger():0;if(testPerson)testPerson->SetHunger(maxFood/4);g_Au.UnlockEtheriumDelete();
        updateVitals();bool bound=maxFood>0&&std::abs(food->value()-25)<=1;settle();frame.grab().save(out.filePath("ui-status.png"));hud->grab().save(out.filePath("ui-hud-detail.png"));
        auto& restoreEther=g_Au.LockEtheriumDelete();testPerson=dynamic_cast<DaPerson*>(restoreEther.FindNormalAtom(auApp->AvatarId()));if(testPerson)testPerson->SetHunger(oldFood);g_Au.UnlockEtheriumDelete();updateVitals();if(!bound){error="Status did not follow live character data";return false;}
        hud->setStats(5,100,8,100,10,100);if(!hud->property("critical").toBool()){error="Critical HUD state missing";return false;}hud->grab().save(out.filePath("ui-hud-critical.png"));updateVitals();
        closePanels();equipped->click();if(!dock("GearDock")||dock("GearDock")->isHidden()){error="Selected-equipment shortcut failed";return false;}
        closePanels();auto* gearButton=frame.findChild<QToolButton*>("NavGearDock");if(!gearButton){error="Gear navigation button missing";return false;}gearButton->click();auto* gear=dock("GearDock");if(!gear||gear->isHidden()){error="Gear panel did not open";return false;}
        auto* search=gear->findChild<QLineEdit*>("InventorySearch");auto* tree=gear->findChild<QTreeView*>("InventoryTree");if(!search||!tree){error="Inventory search is missing";return false;}
        settle();auto* spatial=static_cast<AuInventoryGrid*>(gear->findChild<QWidget*>("SpatialInventory"));auto* locations=gear->findChild<QComboBox*>("InventoryContainer");
        if(!spatial||!locations||locations->count()<2){error="Spatial containers missing";return false;}locations->setCurrentIndex(1);settle();
        int expectedUnits=AuItem(CID_Hammer,100,0).VolumeInPackUnits()+AuItem(CID_Knife,100,0).VolumeInPackUnits();
        if(spatial->property("capacityUnits").toInt()!=40||spatial->property("usedUnits").toInt()!=expectedUnits||spatial->property("itemCount").toInt()!=2){error="Spatial fullness differs from actual container contents";return false;}
        auto* canvas=static_cast<AuInventoryCanvas*>(gear->findChild<QWidget*>("InventoryGridCanvas"));if(!canvas||canvas->tiles.size()!=2||canvas->tiles[0].cells.intersects(canvas->tiles[1].cells)){error="Spatial item footprints overlap";return false;}
        QPoint from=canvas->pixels(canvas->tiles[0].cells).center();QMouseEvent click(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&click);QMouseEvent release(QEvent::MouseButtonRelease,from,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&release);
        if(tree->currentIndex().data(Qt::UserRole+1).value<AuGearPos>()!=canvas->tiles[0].pos){error="Grid selection did not reach item actions";return false;}
        QRect originalTile=canvas->tiles[0].cells,destination;for(int y=0;y<canvas->rows&&destination.isNull();y++)for(int x=0;x<canvas->columns;x++){QRect candidate(QPoint(x,y),originalTile.size());if(candidate!=originalTile&&canvas->fits(candidate,0)){destination=candidate;break;}}
        auto drag=[&](const QPoint& a,const QPoint& b){QMouseEvent press(QEvent::MouseButtonPress,a,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&press);QMouseEvent move(QEvent::MouseMove,b,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&move);QMouseEvent release(QEvent::MouseButtonRelease,b,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&release);};
        if(destination.isNull()){error="No test space for spatial rearrangement";return false;}drag(canvas->pixels(originalTile).center(),canvas->pixels(destination).center());if(canvas->tiles[0].cells==originalTile||canvas->tiles[0].cells.intersects(canvas->tiles[1].cells)){error="Spatial drag rearrangement failed";return false;}
        QRect arranged=canvas->tiles[0].cells;drag(canvas->pixels(arranged).center(),canvas->pixels(canvas->tiles[1].cells).center());if(canvas->tiles[0].cells!=arranged){error="Spatial collision allowed overlapping items";return false;}
        frame.grab().save(out.filePath("ui-container.png"));locations->setCurrentIndex(0);
        int source=-1,target=-1;for(int i=0;i<canvas->tiles.size();i++){if(!canvas->tiles[i].container&&source<0)source=i;if(canvas->tiles[i].container)target=i;}
        if(source<0||target<0){error="Container transfer fixture missing";return false;}int addedUnits=canvas->tiles[source].size.width()*canvas->tiles[source].size.height();drag(canvas->pixels(canvas->tiles[source].cells).center(),canvas->pixels(canvas->tiles[target].cells).center());settle();settle();locations->setCurrentIndex(1);settle();if(spatial->property("usedUnits").toInt()!=expectedUnits+addedUnits){error="Grid drag did not transfer item through game inventory rules";return false;}frame.grab().save(out.filePath("ui-container-transfer.png"));locations->setCurrentIndex(0);
        AuGear testGear;AuItemContainer fullPack(AuItem(CID_Knife,100,0).VolumeInPackUnits(),CID_Backpack,100,0);fullPack.FitItemIntoContainer(AuItem(CID_Knife,100,0));testGear.Insert(AuGearSlot::Hand,0,fullPack);AuItemContainer emptyPack(8,CID_UtilityPouch,100,0);testGear.Insert(AuGearSlot::Hand,1,emptyPack);AuInventoryGrid fixture(testGear,nullptr);fixture.refresh();auto* fixtureLocations=fixture.findChild<QComboBox*>("InventoryContainer");fixtureLocations->setCurrentIndex(1);if(fixture.property("usedUnits")!=fixture.property("capacityUnits")){error="Full container occupancy incorrect";return false;}fixtureLocations->setCurrentIndex(2);if(fixture.property("usedUnits").toInt()!=0||fixture.property("itemCount").toInt()!=0){error="Empty container occupancy incorrect";return false;}
        search->setText("no-item-with-this-name");auto* inventorySummary=gear->findChild<QLabel*>("InventorySummary");if(!inventorySummary||!inventorySummary->text().contains("No items")){error="Missing inventory empty-result guidance";return false;}for(int i=0;i<tree->model()->rowCount();i++)if(!tree->isRowHidden(i,QModelIndex())){error="Inventory filter did not hide unmatched rows";return false;}
        search->setText("Hammer");bool match=false;for(int i=0;i<tree->model()->rowCount();i++)if(!tree->isRowHidden(i,QModelIndex()))match=true;if(!match){error="Inventory search lost matching equipment";return false;}
        search->clear();if(tree->model()->rowCount())tree->setCurrentIndex(tree->model()->index(0,0));settle();frame.grab().save(out.filePath("ui-inventory.png"));
        showPalette();int gameKeys=0;auto keyConnection=connect(auApp,&AuApp::KeyPressed,palette,[&gameKeys](QKeyEvent*){++gameKeys;});QKeyEvent typed(QEvent::KeyPress,Qt::Key_W,Qt::NoModifier,"w");QCoreApplication::sendEvent(query,&typed);disconnect(keyConnection);if(gameKeys||query->text()!="w"){error="Search typing leaked to game controls";return false;}query->setText("equipment clothing");if(!results->count()){error="Multi-word command search failed";return false;}
        query->setText("empire governance");if(!frame.ViewWidget()->SceneAtomEmpire()){if(!results->count()||(results->item(0)->flags()&Qt::ItemIsEnabled)||!results->item(0)->toolTip().contains("empire")){error="Unavailable tools do not explain their requirements";return false;}palette->grab().save(out.filePath("ui-unavailable.png"));}
        query->setText("star map");if(!results->count()){error="Command search did not find star map";return false;}settle();palette->grab().save(out.filePath("ui-search.png"));choose();settle();
        showPalette();if(!results->count()||!results->item(0)->text().contains("Recent")||!results->item(0)->text().contains("Star map",Qt::CaseInsensitive)){error="Recent panels were not remembered";return false;}palette->grab().save(out.filePath("ui-recent.png"));palette->hide();
        bool starMap=false;for(auto* d:frame.findChildren<QDockWidget*>())if(d->windowTitle().contains("Star Map",Qt::CaseInsensitive)&&!d->isHidden())starMap=true;if(!starMap){error="Command search did not open star map";return false;}
        closePanels();auto* shipButton=frame.findChild<QToolButton*>("NavSpacecraftDock");if(!shipButton){error="Spacecraft navigation missing";return false;}shipButton->click();int shipPanels=0;for(auto* d:frame.findChildren<QDockWidget*>())if(d->objectName()=="SpacecraftDock"&&!d->isHidden())++shipPanels;if(shipPanels!=1){error="Spacecraft navigation did not select one context-appropriate panel";return false;}closePanels();int oldWidth=rail->width();collapse->click();if(rail->width()==oldWidth&&frame.width()>=1100){error="Navigation did not collapse";return false;}settle();frame.grab().save(out.filePath("ui-compact.png"));QSize original=frame.size();frame.resize(1024,768);sizeRail();settle();collapse->click();if(rail->width()<100){error="Compact navigation could not expand";return false;}collapse->click();run(panel("Gear",{},"GearDock"));settle();if(findButton->mapTo(&frame,QPoint(findButton->width(),0)).x()>frame.width()){error="Search control clipped in compact layout";return false;}if(!frame.ViewWidget()->rect().contains(hud->geometry())||hud->geometry().intersects(loadout->geometry())){error="Game HUD clipped or overlapped in narrow layout";return false;}frame.grab().save(out.filePath("ui-small.png"));closePanels();frame.resize(original);compact=false;forceExpanded=false;sizeRail();settle();frame.grab().save(out.filePath("ui-overview.png"));return true;
    }
};
#endif
