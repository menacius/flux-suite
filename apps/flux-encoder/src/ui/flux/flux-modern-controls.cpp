#include "ui/flux/flux-modern-controls.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int kSwitchTrackWidth = 28;
constexpr int kSwitchTrackHeight = 14;
constexpr int kSwitchKnobDiameter = 10;
constexpr int kSwitchSpacing = 7;
constexpr int kPanelHeaderHeight = 28;

QColor blended(const QColor &a, const QColor &b, int aw = 1, int bw = 1)
{
    const int total = std::max(1, aw + bw);
    return QColor((a.red()*aw+b.red()*bw)/total, (a.green()*aw+b.green()*bw)/total,
                  (a.blue()*aw+b.blue()*bw)/total, (a.alpha()*aw+b.alpha()*bw)/total);
}

void drawCaret(QPainter &p, const QRectF &rect, bool down, QColor color)
{
    p.save(); p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen); p.setBrush(color);
    const QPointF c = rect.center(); QPolygonF triangle;
    if (down) triangle << QPointF(c.x()-4,c.y()-2) << QPointF(c.x()+4,c.y()-2) << QPointF(c.x(),c.y()+3);
    else triangle << QPointF(c.x()-2,c.y()-4) << QPointF(c.x()-2,c.y()+4) << QPointF(c.x()+3,c.y());
    p.drawPolygon(triangle); p.restore();
}
}

FluxSwitch::FluxSwitch(QWidget *parent) : QCheckBox(parent)
{
    setCursor(Qt::PointingHandCursor); setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); setMinimumHeight(19);
}
FluxSwitch::FluxSwitch(const QString &text, QWidget *parent) : FluxSwitch(parent) { setText(text); }
QSize FluxSwitch::sizeHint() const
{
    const QFontMetrics fm(font()); const int tw = text().isEmpty()?0:fm.horizontalAdvance(text())+kSwitchSpacing;
    return QSize(tw+kSwitchTrackWidth+2, std::max(fm.height(),kSwitchTrackHeight)+4);
}
QSize FluxSwitch::minimumSizeHint() const { return QSize(kSwitchTrackWidth+2,kSwitchTrackHeight+4); }
bool FluxSwitch::hitButton(const QPoint &position) const { return rect().contains(position); }
void FluxSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing); const QPalette pal=palette();
    const QRect track(width()-kSwitchTrackWidth-1,(height()-kSwitchTrackHeight)/2,kSwitchTrackWidth,kSwitchTrackHeight);
    QRect textRect=rect(); textRect.setRight(track.left()-kSwitchSpacing);
    if (!text().isEmpty()) { QColor c=pal.color(isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::WindowText); c.setAlpha(isEnabled()?225:90); p.setPen(c); p.drawText(textRect,Qt::AlignLeft|Qt::AlignVCenter,text()); }
    QColor fill = isChecked()?pal.color(QPalette::Highlight):pal.color(QPalette::Button);
    QColor border = isChecked()?pal.color(QPalette::Highlight):pal.color(QPalette::Mid);
    fill.setAlpha(isEnabled()?(isChecked()?118:88):55); border.setAlpha(isEnabled()?150:70);
    if (underMouse()&&isEnabled()) { fill.setAlpha(std::min(165,fill.alpha()+18)); border.setAlpha(std::min(220,border.alpha()+18)); }
    p.setPen(QPen(border,hasFocus()?1.25:1.0)); p.setBrush(fill); p.drawRoundedRect(QRectF(track),7,7);
    int x=isChecked()?track.right()-kSwitchKnobDiameter-1:track.left()+2;
    QColor knob=pal.color(isChecked()?QPalette::HighlightedText:QPalette::ButtonText); knob.setAlpha(isEnabled()?205:105);
    p.setPen(Qt::NoPen); p.setBrush(knob); p.drawEllipse(QRectF(x,track.top()+2,kSwitchKnobDiameter,kSwitchKnobDiameter));
}

FluxCaretButton::FluxCaretButton(QWidget *parent):QToolButton(parent)
{ setAutoRaise(true); setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::NoFocus); setFixedSize(18,20); }
QSize FluxCaretButton::sizeHint() const { return QSize(18,20); }
void FluxCaretButton::setCaretState(int state) { state=std::clamp(state,0,2); if(state_==state)return; state_=state; update(); emit caretStateChanged(state_); }
void FluxCaretButton::paintEvent(QPaintEvent *)
{ QPainter p(this); QColor c=palette().color(isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::WindowText); c.setAlpha(isEnabled()?175:85); drawCaret(p,rect(),state_>0,c); }

class FluxCollapsiblePanel::Header final : public QWidget {
public:
    explicit Header(FluxCollapsiblePanel *owner):QWidget(owner),owner_(owner)
    { setFixedHeight(kPanelHeaderHeight); setCursor(Qt::PointingHandCursor); setMouseTracking(true); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this); const QPalette pal=palette(); QColor header=blended(pal.color(QPalette::Window),pal.color(QPalette::Button),2,1);
        if(underMouse()) header=header.lightness()<128?header.lighter(106):header.darker(102); p.fillRect(rect(),header);
        QColor accent=pal.color(QPalette::Highlight); accent.setAlpha(155); p.fillRect(QRect(0,0,width(),2),accent);
        QColor title=pal.color(QPalette::WindowText); title.setAlpha(225); QFont f=font(); f.setBold(true); p.setFont(f); p.setPen(title);
        const int actionLeft=owner_->headerActions_&&owner_->headerActions_->count()?owner_->headerActions_->geometry().left()-5:width()-28;
        p.drawText(QRect(10,0,std::max(0,actionLeft-14),height()),Qt::AlignLeft|Qt::AlignVCenter,QFontMetrics(f).elidedText(owner_->title_,Qt::ElideRight,std::max(0,actionLeft-14)));
        QColor caret=pal.color(QPalette::WindowText); caret.setAlpha(175); drawCaret(p,QRect(width()-26,0,24,height()),owner_->expanded_,caret);
    }
    void mouseReleaseEvent(QMouseEvent *e) override { if(e->button()==Qt::LeftButton){ owner_->toggleExpanded(); e->accept(); } else QWidget::mouseReleaseEvent(e); }
private: FluxCollapsiblePanel *owner_;
};

FluxCollapsiblePanel::FluxCollapsiblePanel(const QString &title,QWidget *content,QWidget *parent)
    :QWidget(parent),title_(title),content_(content)
{
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred); auto *layout=new QVBoxLayout(this); layout->setContentsMargins(1,1,1,1); layout->setSpacing(0);
    header_=new Header(this); auto *headerLayout=new QHBoxLayout(header_); headerLayout->setContentsMargins(8,3,29,3); headerLayout->addStretch(); headerActions_=new QHBoxLayout(); headerActions_->setSpacing(3); headerLayout->addLayout(headerActions_); layout->addWidget(header_);
    body_=new QWidget(this); auto *bodyLayout=new QVBoxLayout(body_); bodyLayout->setContentsMargins(10,8,10,10); bodyLayout->setSpacing(6); if(content_){content_->setParent(body_);bodyLayout->addWidget(content_);} layout->addWidget(body_);
}
void FluxCollapsiblePanel::setTitle(const QString &title){title_=title;header_->update();}
void FluxCollapsiblePanel::addHeaderWidget(QWidget *widget){if(!widget)return;widget->setParent(header_);widget->setMaximumHeight(22);headerActions_->addWidget(widget);}
void FluxCollapsiblePanel::setOrderPersistenceEnabled(bool enabled)
{ orderPersistenceEnabled_=enabled; setProperty("fluxPersistOrder", enabled); }
void FluxCollapsiblePanel::setPersistenceKey(const QString &group,const QString &key)
{ persistenceGroup_=group;persistenceKey_=key;QSettings s(QStringLiteral("Flux"),QStringLiteral("FluxEncoderPanels"));const QString k=QStringLiteral("expanded/%1/%2").arg(group,key);if(s.contains(k)){expanded_=s.value(k,true).toBool();body_->setVisible(expanded_);} }
void FluxCollapsiblePanel::setExpanded(bool value)
{ if(expanded_==value)return;expanded_=value;body_->setVisible(value);header_->update();QSettings s(QStringLiteral("Flux"),QStringLiteral("FluxEncoderPanels"));if(!persistenceKey_.isEmpty())s.setValue(QStringLiteral("expanded/%1/%2").arg(persistenceGroup_,persistenceKey_),value);emit expandedChanged(value);updateGeometry(); }
void FluxCollapsiblePanel::toggleExpanded(){setExpanded(!expanded_);}
void FluxCollapsiblePanel::paintEvent(QPaintEvent *)
{ QPainter p(this);const QPalette pal=palette();QColor body=pal.color(QPalette::Window);body=body.lightness()<128?body.lighter(112):body.darker(104);QColor border=pal.color(QPalette::Mid);border.setAlpha(165);p.setPen(QPen(border));p.setBrush(body);p.drawRoundedRect(rect().adjusted(0,0,-1,-1),2,2); }

FluxCollapsiblePanel *flux_add_panel_section(QVBoxLayout *layout,QWidget *section,const QString &title,QWidget *headerWidget)
{ if(!layout||!section)return nullptr;auto *panel=new FluxCollapsiblePanel(title.isEmpty()?QObject::tr("Panel"):title,section,layout->parentWidget());if(headerWidget)panel->addHeaderWidget(headerWidget);layout->addWidget(panel);return panel; }
