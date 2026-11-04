#ifndef OPENSHORES_GAME_HUD_H
#define OPENSHORES_GAME_HUD_H
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

// Painted directly over the world: no application-style cards or reserved footer.
class GameVitalsHud : public QWidget {
    int hp=0,hpMax=0,energy=0,energyMax=0,food=0,foodMax=0;
    int movePower=100,jumpPower=100;
    static double fraction(int n,int d){return d>0?std::max(0.,std::min(1.,double(n)/d)):0.;}
    static QPainterPath bevel(const QRectF& r,qreal cut){QPainterPath p;p.moveTo(r.left()+cut,r.top());p.lineTo(r.right(),r.top());p.lineTo(r.right(),r.bottom()-cut);p.lineTo(r.right()-cut,r.bottom());p.lineTo(r.left(),r.bottom());p.lineTo(r.left(),r.top()+cut);p.closeSubpath();return p;}
public:
    GameVitalsHud(QWidget* parent):QWidget(parent){setObjectName("GameVitalsHUD");setFixedHeight(101);setAttribute(Qt::WA_TranslucentBackground);setFocusPolicy(Qt::NoFocus);setAccessibleName("Player vitals");}
    void setStats(int h,int hm,int s,int sm,int f,int fm){if(h==hp&&hm==hpMax&&s==energy&&sm==energyMax&&f==food&&fm==foodMax)return;hp=h;hpMax=hm;energy=s;energyMax=sm;food=f;foodMax=fm;setToolTip(QString("Health %1/%2\nStamina %3/%4\nFood %5/%6").arg(h).arg(hm).arg(s).arg(sm).arg(f).arg(fm));setAccessibleDescription(toolTip());setProperty("critical",hm>0&&fraction(h,hm)<=.1);update();}
    void setPower(float move,float jump){int m=int(std::round(std::max(0.f,std::min(1.f,move))*100)),j=int(std::round(std::max(0.f,std::min(1.f,jump))*100));if(m==movePower&&j==jumpPower)return;movePower=m;jumpPower=j;update();}
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.scale(.8,.8);const double w=width()/.8;
        auto shell=bevel(QRectF(1,1,w-3,122),12);p.fillPath(shell,QColor(7,13,21,235));p.setPen(QPen(QColor("#71818c"),1));p.drawPath(shell);
        p.setPen(QPen(QColor("#c3a366"),2));p.drawLine(QPointF(14,2),QPointF(w-26,2));p.setPen(QColor(255,255,255,25));p.drawLine(QPointF(14,5),QPointF(w-14,5));
        auto font=[&](int size,bool bold=false){QFont f("Segoe UI",size);f.setBold(bold);p.setFont(f);};
        font(9,true);p.setPen(QColor("#e6cc92"));p.drawText(QRectF(57,9,w-70,18),Qt::AlignLeft|Qt::AlignVCenter,"VITALS");
        if(hpMax>0&&fraction(hp,hpMax)<=.3){font(8,true);p.setPen(QColor("#ffae9a"));p.drawText(QRectF(120,9,w-135,18),Qt::AlignRight|Qt::AlignVCenter,fraction(hp,hpMax)<=.1?"CRITICAL":"INJURED");}
        else if(w>=340){font(7,true);p.setPen(QColor("#9daebc"));p.drawText(QRectF(120,9,w-135,18),Qt::AlignRight|Qt::AlignVCenter,QString("MOVE %1%  ·  JUMP %2%").arg(movePower).arg(jumpPower));}
        // Heart crest, drawn as vector geometry so it stays sharp at UI scale.
        p.save();p.translate(17,37);QPainterPath heart;heart.moveTo(15,25);heart.cubicTo(10,20,-3,12,2,5);heart.cubicTo(6,-1,12,1,15,7);heart.cubicTo(18,1,25,-1,29,5);heart.cubicTo(34,12,20,22,15,25);p.fillPath(heart,QColor("#ef7d75"));p.restore();
        auto gauge=[&](QRectF r,double amount,const QColor& dark,const QColor& bright){auto path=bevel(r,4);p.fillPath(path,QColor("#070b10"));p.save();p.setClipPath(path);QLinearGradient g(r.topLeft(),r.bottomLeft());g.setColorAt(0,bright.lighter(125));g.setColorAt(.38,bright);g.setColorAt(1,dark);p.fillRect(QRectF(r.left(),r.top(),r.width()*amount,r.height()),g);p.setPen(QColor(255,255,255,55));p.drawLine(r.topLeft()+QPointF(2,2),QPointF(r.left()+r.width()*amount-2,r.top()+2));for(int i=1;i<10;i++){double x=r.left()+r.width()*i/10.;p.setPen(QColor(0,0,0,90));p.drawLine(QPointF(x,r.top()),QPointF(x,r.bottom()));}p.restore();p.setPen(QColor("#586474"));p.drawPath(path);};
        gauge(QRectF(57,31,w-73,28),fraction(hp,hpMax),QColor("#842c37"),QColor("#dc5c62"));
        font(10,true);p.setPen(Qt::white);p.drawText(QRectF(66,32,w-92,26),Qt::AlignLeft|Qt::AlignVCenter,"HP");p.drawText(QRectF(90,32,w-116,26),Qt::AlignRight|Qt::AlignVCenter,hpMax>0?QString("%1 / %2").arg(std::max(0,hp)).arg(hpMax):"—");
        // Stamina lightning crest and a smaller green energy gauge.
        QPainterPath bolt;bolt.moveTo(33,68);bolt.lineTo(23,80);bolt.lineTo(30,80);bolt.lineTo(26,91);bolt.lineTo(40,75);bolt.lineTo(32,75);bolt.closeSubpath();p.fillPath(bolt,QColor("#b5d78a"));
        gauge(QRectF(57,68,w-73,17),fraction(energy,energyMax),QColor("#3b5b36"),QColor("#91b66b"));font(8,true);p.setPen(QColor("#f2f5ea"));p.drawText(QRectF(65,68,w-89,17),Qt::AlignLeft|Qt::AlignVCenter,"STAMINA");p.drawText(QRectF(65,68,w-89,17),Qt::AlignRight|Qt::AlignVCenter,energyMax>0?QString::number(int(fraction(energy,energyMax)*100))+"%":"—");
        // Food is a row of ten amber rations rather than another spreadsheet meter.
        p.setPen(QPen(QColor("#d4b075"),2));p.setBrush(QColor("#765234"));p.drawRoundedRect(QRectF(23,99,18,13),4,4);p.drawLine(QPointF(28,100),QPointF(27,105));p.drawLine(QPointF(35,100),QPointF(34,105));
        int count=int(std::ceil(fraction(food,foodMax)*10));double pip=(w-155)/10.;for(int i=0;i<10;i++){QRectF r(57+i*pip,100,pip-3,10);p.fillPath(bevel(r,2),QColor(i<count?(fraction(food,foodMax)<=.3?"#d69150":"#c8a263"):"#27303c"));}
        font(8,true);p.setPen(QColor("#e7c792"));p.drawText(QRectF(w-92,94,76,22),Qt::AlignRight|Qt::AlignVCenter,foodMax<=0?"FOOD —":fraction(food,foodMax)<=.3?"HUNGRY":QString("FOOD %1%").arg(int(fraction(food,foodMax)*100)));
    }
};
#endif
