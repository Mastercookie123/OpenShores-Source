#ifndef AU_INVENTORY_GRID_H
#define AU_INVENTORY_GRID_H
#include "augear.h"
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QComboBox>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QMenu>
#include <QDataStream>
#include <functional>
#include <algorithm>
#include <cmath>

// Client-side layout only. Item ownership and transfers remain authoritative game operations.
class AuInventoryCanvas : public QWidget {
public:
    struct Tile {AuGearPos pos;QString name,tip,key;QPixmap icon;QSize size;QRect cells;bool container=false;int used=0,capacity=0;};
    QVector<Tile> tiles;QMap<QString,QRect> placements;QString scope,filter;int columns=8,rows=5,capacity=0,used=0;
    std::function<void(const AuGearPos&)> select,open,context;
    std::function<void(const AuGearPos&,const AuGearPos&)> transfer;
    int selected=-1,pressed=-1,hovered=-1;QPoint start,offset,ghost;bool dragging=false;
    AuInventoryCanvas(QWidget* parent):QWidget(parent){setObjectName("InventoryGridCanvas");setMouseTracking(true);setMinimumWidth(250);setAccessibleName("Spatial inventory. Click to select, double-click containers to open, drag to arrange.");}
    static QString key(const AuGearPos& pos){QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream<<pos;return QString::fromLatin1(bytes.toHex());}
    int cell()const{return std::max(12,std::min(42,(width()-16)/columns));}
    QRect pixels(const QRect& r)const{int c=cell();return QRect(8+r.x()*c,8+r.y()*c,r.width()*c,r.height()*c).adjusted(2,2,-2,-2);}
    bool fits(const QRect& r,int ignore)const{if(r.x()<0||r.y()<0||r.right()>=columns||r.bottom()>=rows)return false;for(int i=0;i<tiles.size();i++)if(i!=ignore&&tiles[i].cells.intersects(r))return false;return true;}
    void arrange(bool reset=false){
        int area=0,maxWidth=1;for(const auto& t:tiles){area+=t.size.width()*t.size.height();maxWidth=std::max(maxWidth,t.size.width());}
        columns=std::max(maxWidth,std::min(10,std::max(6,int(std::ceil(std::sqrt(std::max(capacity,area)*1.4))))));
        rows=std::max(4,int(std::ceil(double(std::max(capacity,area))/columns)));
        for(auto& t:tiles)t.cells=QRect();
        for(int i=0;i<tiles.size();i++){
            auto& t=tiles[i];QString id=scope+"/"+t.key;QRect old=placements.value(id);
            if(!reset&&old.size()==t.size&&fits(old,i)){t.cells=old;continue;}
            bool placed=false;while(!placed){for(int y=0;y<=rows-t.size.height()&&!placed;y++)for(int x=0;x<=columns-t.size.width();x++){QRect r(QPoint(x,y),t.size);if(fits(r,i)){t.cells=r;placed=true;break;}}if(!placed)rows++;}
            placements[id]=t.cells;
        }
        setMinimumHeight(16+rows*cell());updateGeometry();update();
    }
    int hit(const QPoint& p)const{for(int i=tiles.size()-1;i>=0;i--)if(pixels(tiles[i].cells).contains(p))return i;return -1;}
protected:
    void resizeEvent(QResizeEvent*)override{setMinimumHeight(16+rows*cell());}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#090f18"));int c=cell();
        p.setPen(QColor("#26303c"));for(int y=0;y<rows;y++)for(int x=0;x<columns;x++){QRect r=QRect(8+x*c,8+y*c,c,c).adjusted(2,2,-2,-2);p.fillRect(r,QColor((x+y)%2?"#101a26":"#0d1621"));p.drawRect(r);}
        for(int i=0;i<tiles.size();i++){
            const auto& t=tiles[i];QRect r=pixels(t.cells);bool match=filter.isEmpty()||t.name.contains(filter,Qt::CaseInsensitive);p.setOpacity(match?1.0:.23);
            QLinearGradient tileShade(r.topLeft(),r.bottomRight());tileShade.setColorAt(0,QColor(i==selected?"#3d493e":t.container?"#303b3c":"#263746"));tileShade.setColorAt(1,QColor("#111d29"));p.fillRect(r,tileShade);p.setPen(QColor(i==selected?"#f0cd83":i==hovered?"#bcc9cc":t.container?"#a38d60":"#4d6477"));p.drawRect(r);p.setPen(QColor(255,255,255,28));p.drawLine(r.topLeft()+QPoint(1,1),r.topRight()+QPoint(-1,1));
            QRect iconArea=r.adjusted(4,3,-4,-(r.height()>40?21:3));QSize size=t.icon.size();size.scale(iconArea.size().boundedTo(QSize(88,88)),Qt::KeepAspectRatio);p.drawPixmap(QRect(iconArea.center()-QPoint(size.width()/2,size.height()/2),size),t.icon);
            if(r.width()>45&&r.height()>40){p.setPen(QColor("#e4edf7"));p.drawText(r.adjusted(4,0,-4,-4),Qt::AlignBottom|Qt::AlignLeft,p.fontMetrics().elidedText(t.name,Qt::ElideRight,r.width()-8));}
            if(t.container&&t.capacity>0){QRect bar(r.left()+4,r.top()+4,r.width()-8,4);p.fillRect(bar,QColor("#0a1423"));bar.setWidth(int(bar.width()*std::min(1.,double(t.used)/t.capacity)));p.fillRect(bar,QColor(t.used>=t.capacity?"#cb755b":"#d8b977"));}
        }
        p.setOpacity(1.);if(dragging&&pressed>=0){QRect r(ghost,tiles[pressed].size);p.setPen(QColor(fits(r,pressed)?"#82d4ae":"#ed927e"));p.setBrush(QColor(110,175,155,50));p.drawRect(pixels(r));}
        if(tiles.isEmpty()){p.setPen(QColor("#afc0d5"));p.drawText(rect().adjusted(20,20,-20,-20),Qt::AlignCenter|Qt::TextWordWrap,"Empty container\nChoose Move to… on an item to store it here.");}
    }
    bool event(QEvent* e)override{if(e->type()==QEvent::ToolTip){auto* h=static_cast<QHelpEvent*>(e);int i=hit(h->pos());if(i>=0)QToolTip::showText(h->globalPos(),tiles[i].tip,this);else QToolTip::hideText();return true;}return QWidget::event(e);}
    void mousePressEvent(QMouseEvent* e)override{pressed=hit(e->pos());selected=pressed;start=e->pos();dragging=false;if(pressed>=0){offset=(e->pos()-QPoint(8,8))/cell()-tiles[pressed].cells.topLeft();if(select)select(tiles[pressed].pos);if(e->button()==Qt::RightButton&&context)context(tiles[pressed].pos);}update();}
    void mouseMoveEvent(QMouseEvent* e)override{int over=hit(e->pos());if(over!=hovered){hovered=over;update();}if(pressed>=0&&(e->buttons()&Qt::LeftButton)&&(e->pos()-start).manhattanLength()>6){dragging=true;ghost=(e->pos()-QPoint(8,8))/cell()-offset;update();}}
    void leaveEvent(QEvent*)override{hovered=-1;update();}
    void mouseReleaseEvent(QMouseEvent* e)override{if(dragging&&pressed>=0){int target=hit(e->pos());if(target>=0&&target!=pressed&&tiles[target].container){if(transfer)transfer(tiles[pressed].pos,tiles[target].pos);}else{QRect r(ghost,tiles[pressed].size);if(fits(r,pressed)){tiles[pressed].cells=r;placements[scope+"/"+tiles[pressed].key]=r;}}}dragging=false;pressed=-1;update();}
    void mouseDoubleClickEvent(QMouseEvent* e)override{int i=hit(e->pos());if(i>=0&&tiles[i].container&&open)open(tiles[i].pos);}
};

class AuInventoryGrid : public QWidget {
    const AuGear& gear;QComboBox* locations;QLabel *fullness,*detail;QProgressBar* meter;AuInventoryCanvas* canvas;QPushButton* move;
    QVector<AuGearPos> containers;AuGearPos current,selected;QString search;
public:
    std::function<void(const AuGearPos&)> onSelect,onContext;
    std::function<void(const AuGearPos&,const AuGearPos&)> onTransfer;
    AuInventoryGrid(const AuGear& g,QWidget* parent):QWidget(parent),gear(g){
        setObjectName("SpatialInventory");auto* l=new QVBoxLayout(this);l->setContentsMargins(0,0,0,0);l->setSpacing(6);
        locations=new QComboBox(this);locations->setObjectName("InventoryContainer");locations->setAccessibleName("View equipment or container contents");l->addWidget(locations);
        fullness=new QLabel(this);fullness->setObjectName("ContainerFullness");fullness->setWordWrap(true);l->addWidget(fullness);meter=new QProgressBar(this);meter->setObjectName("ContainerCapacity");meter->setRange(0,100);meter->setFixedHeight(14);meter->setTextVisible(false);l->addWidget(meter);
        auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setMinimumHeight(160);canvas=new AuInventoryCanvas(scroll);scroll->setWidget(canvas);l->addWidget(scroll,1);
        detail=new QLabel("Select an item to see its size and actions.",this);detail->setObjectName("GridSelection");detail->setWordWrap(true);l->addWidget(detail);
        auto* actions=new QHBoxLayout;move=new QPushButton("Move to…",this);move->setObjectName("InventoryMoveTo");move->setEnabled(false);actions->addWidget(move);auto* arrange=new QPushButton("Auto arrange",this);arrange->setObjectName("InventoryAutoArrange");actions->addWidget(arrange);l->addLayout(actions);
        auto* help=new QLabel("Click to select · Double-click a bag to open\nDrag to arrange or onto a container to store",this);help->setWordWrap(true);help->setStyleSheet("color:#a6b9d0;font-size:11px;");l->addWidget(help);
        connect(locations,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int i){current=i>0&&i<=containers.size()?containers[i-1]:AuGearPos();rebuild();});
        connect(arrange,&QPushButton::clicked,this,[this]{canvas->arrange(true);});
        canvas->select=[this](const AuGearPos& pos){selected=pos;move->setEnabled(true);if(const auto* item=gear.ItemAt(pos)){auto size=item->PackSize();detail->setText(QString("%1 · %2 × %3 units").arg(item->ListName()).arg(size.width()).arg(size.height()));}if(onSelect)onSelect(pos);};
        canvas->context=[this](const AuGearPos& pos){if(onContext)onContext(pos);};
        canvas->open=[this](const AuGearPos& pos){for(int i=0;i<containers.size();i++)if(containers[i]==pos){locations->setCurrentIndex(i+1);break;}};
        canvas->transfer=[this](const AuGearPos& from,const AuGearPos& to){if(onTransfer)onTransfer(from,to);};
        connect(move,&QPushButton::clicked,this,[this]{QMenu menu(this);for(int i=0;i<containers.size();i++){auto target=containers[i];if(target==selected)continue;auto* a=menu.addAction(locations->itemText(i+1));connect(a,&QAction::triggered,this,[this,target]{if(onTransfer)onTransfer(selected,target);});}if(menu.isEmpty()){auto* a=menu.addAction("No other containers carried");a->setEnabled(false);}menu.exec(move->mapToGlobal(QPoint(0,move->height())));});
    }
    void setSearch(const QString& text){search=text.trimmed();canvas->filter=search;canvas->selected=-1;move->setEnabled(false);detail->setText(search.isEmpty()?"Select an item to see its size and actions.":"Matching items are highlighted; occupied space stays visible.");canvas->update();}
    void refresh(){
        AuGearPosList all;gear.ListAll(all);containers.clear();locations->blockSignals(true);locations->clear();locations->addItem("Carried & worn equipment");int index=0;
        for(const auto& pos:all){const auto* item=gear.ItemAt(pos);if(item&&item->Content()&&item->CapacityInPackUnits()>0){containers<<pos;int used=item->Content()->ContainsPackUnits();locations->addItem(QString("%1  ·  %2 / %3 units").arg(item->ListName()).arg(used).arg(item->CapacityInPackUnits()));if(pos==current)index=containers.size();}}
        if(index==0)current=AuGearPos();locations->setCurrentIndex(index);locations->blockSignals(false);rebuild();setProperty("containerCount",containers.size());
    }
    void rebuild(){
        const auto* container=gear.ItemAt(current);bool inside=container&&container->Content();canvas->tiles.clear();canvas->scope=AuInventoryCanvas::key(current);canvas->selected=-1;canvas->used=0;canvas->capacity=inside?container->CapacityInPackUnits():0;
        AuGearPosList all;gear.ListAll(all);for(const auto& pos:all){bool show=pos.IsTopLevel()&&!inside;if(inside){auto path=pos.ContentPath();if(!path.isEmpty()){path.removeLast();show=pos.Slot()==current.Slot()&&pos.Which()==current.Which()&&path==current.ContentPath();}}
            if(!show)continue;const auto* item=gear.ItemAt(pos);if(!item)continue;AuInventoryCanvas::Tile t;t.pos=pos;t.key=AuInventoryCanvas::key(pos);t.name=item->ListName();t.tip=item->HtmlToolTip();t.icon=QPixmap(item->BitmapName());t.size=item->PackSize().expandedTo(QSize(1,1));t.container=item->Content()!=nullptr;t.capacity=item->CapacityInPackUnits();t.used=t.container?item->Content()->ContainsPackUnits():0;canvas->used+=item->VolumeInPackUnits();canvas->tiles<<t;
        }
        move->setEnabled(false);detail->setText("Select an item to see its size and actions.");canvas->filter=search;canvas->arrange();meter->setVisible(inside);meter->setValue(inside&&canvas->capacity?std::min(100,int(100.*canvas->used/canvas->capacity)):0);meter->setStyleSheet("QProgressBar::chunk {background:"+QString(inside&&canvas->used>=canvas->capacity?"#a35b43":"#806e40")+";}");
        fullness->setText(inside?QString("%1 / %2 units used · %3 free%4").arg(canvas->used).arg(canvas->capacity).arg(std::max(0,canvas->capacity-canvas->used)).arg(canvas->used>=canvas->capacity?" · FULL":""):"Carried & worn equipment · Open a container to see its free space.");
        setProperty("usedUnits",canvas->used);setProperty("capacityUnits",canvas->capacity);setProperty("itemCount",canvas->tiles.size());
    }
};
#endif
