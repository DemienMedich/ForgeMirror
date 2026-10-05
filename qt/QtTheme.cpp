#include "QtTheme.h"
#include "QtActionIcons.h"
#include <QtWidgets>
#include <algorithm>
#include <cmath>

// Keep step arrows independent of platform glyphs and stylesheet border tricks.
class ForgeMirrorStyle final : public QProxyStyle {
public:
    ForgeMirrorStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
    QIcon standardIcon(StandardPixmap icon, const QStyleOption* option=nullptr,
        const QWidget* widget=nullptr) const override {
        if (qobject_cast<const QCalendarWidget*>(widget) && (icon==SP_ArrowLeft || icon==SP_ArrowRight))
            return CreateQtActionIcon(QtActionIcon::ChevronRight,widget->palette(),icon==SP_ArrowLeft ? 180.0 : 0.0);
        return QProxyStyle::standardIcon(icon,option,widget);
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
        QPainter* painter, const QWidget* widget = nullptr) const override {
        const auto* dateTime=qobject_cast<const QDateTimeEdit*>(widget);
        const bool calendarArrow=element==PE_IndicatorArrowDown && dateTime && dateTime->calendarPopup();
        const bool comboArrow=element==PE_IndicatorArrowDown && qobject_cast<const QComboBox*>(widget);
        if (element != PE_IndicatorSpinUp && element != PE_IndicatorSpinDown && !comboArrow && !calendarArrow) {
            QProxyStyle::drawPrimitive(element,option,painter,widget);
            return;
        }
        // QWindowsStyle draws a beveled button before delegating its arrow.
        // Repaint only that subcontrol with the field's own rounded/focus frame.
        QRect buttonRect;
        if (calendarArrow) {
            QStyleOptionComboBox full; full.initFrom(dateTime); full.rect=dateTime->rect(); full.editable=true;
            buttonRect=dateTime->style()->subControlRect(CC_ComboBox,&full,SC_ComboBoxArrow,dateTime);
        } else if (const auto* combo=qobject_cast<const QComboBox*>(widget)) {
            QStyleOptionComboBox full; full.initFrom(combo); full.rect=combo->rect(); full.editable=combo->isEditable();
            buttonRect=combo->style()->subControlRect(CC_ComboBox,&full,SC_ComboBoxArrow,combo);
        } else if (const auto* spin=qobject_cast<const QAbstractSpinBox*>(widget)) {
            QStyleOptionSpinBox full; full.initFrom(spin); full.rect=spin->rect(); full.buttonSymbols=spin->buttonSymbols();
            buttonRect=spin->style()->subControlRect(CC_SpinBox,&full,element==PE_IndicatorSpinUp ? SC_SpinBoxUp : SC_SpinBoxDown,spin);
        }
        if (widget && !buttonRect.isEmpty()) {
            // Restore the same rounded field surface beneath the arrow, including
            // the edge overwritten by QWindowsStyle's square fallback button.
            QStyleOptionFrame frame; frame.initFrom(widget); frame.rect=widget->rect(); frame.lineWidth=1;
            painter->save(); painter->setClipRect(buttonRect,Qt::IntersectClip);
            widget->style()->drawPrimitive(PE_PanelLineEdit,&frame,painter,widget);
            painter->restore();
        }
        const QRectF rect(option->rect);
        const qreal height=std::max(2.0,std::min(rect.height()-4.0,option->fontMetrics.height()*0.3));
        const qreal width=std::max(2.0,std::min(rect.width()-4.0,height*2.0));
        const qreal stroke=std::clamp(height/3.0,1.0,2.4);
        const QRectF inner(rect.center().x()-width/2,rect.center().y()-height/2,width,height);
        // QSS recolors every option palette group with the enabled field text.
        // Keep the widget's disabled color for an unavailable step at a limit.
        const auto color=(widget ? widget->palette() : option->palette).color(
            option->state & State_Enabled ? QPalette::Active : QPalette::Disabled,QPalette::Text);
        const bool up=element==PE_IndicatorSpinUp;
        QPainterPath path;
        path.moveTo(inner.left(),up ? inner.bottom() : inner.top());
        path.lineTo(inner.center().x(),up ? inner.top() : inner.bottom());
        path.lineTo(inner.right(),up ? inner.bottom() : inner.top());
        painter->save(); painter->setRenderHint(QPainter::Antialiasing);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(color,stroke,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter->drawPath(path); painter->restore();
    }
};

static double clampMetric(double value, double maxValue, double fallback) {
    return std::isfinite(value) ? std::clamp(value, 0.0, maxValue) : fallback;
}

void ApplyQtLayoutMetrics(QApplication& app, int spacingPercent, int cornerRadius,
    double windowRounding, double frameRounding, double scrollbarRounding, double grabRounding,
    double framePaddingX, double framePaddingY, double itemSpacingX, double itemSpacingY) {
    const double factor = std::clamp(spacingPercent, 80, 120) / 100.0;
    const double basePointSize = app.property("forgeBasePointSize").toDouble();
    const double textScale = basePointSize > 0.0
        ? std::clamp(app.font().pointSizeF() / basePointSize, 0.9, 2.0) : 1.0;
    const int controlHeight = qRound(std::max(26.0, 18.0 + clampMetric(framePaddingY, 24.0, 0.0) * 2.0) * factor * textScale);
    const int horizontalPadding = qRound(clampMetric(framePaddingX, 24.0, 8.0) * factor * textScale);
    const int verticalPadding = qRound(clampMetric(framePaddingY, 24.0, 0.0) * factor * textScale);
    const int listPaddingX = qRound(clampMetric(itemSpacingX, 32.0, 8.0) * factor * textScale);
    const int listPaddingY = qRound(clampMetric(itemSpacingY, 32.0, 6.0) * factor * textScale);
    const int radius = std::clamp(cornerRadius, 0, 12);
    const int windowRadius = qRound(clampMetric(windowRounding, 24.0, double(radius)));
    const int frameRadius = qRound(clampMetric(frameRounding, 24.0, double(radius)));
    const int scrollbarRadius = qRound(clampMetric(scrollbarRounding, 24.0, 6.0));
    const int grabRadius = qRound(clampMetric(grabRounding, 24.0, 4.0));
    const int headerPaddingY = qRound(4.0 * factor * textScale);
    const int stepButtonWidth = qRound(20.0 * textScale);
    const int arrowEdge = qRound(4.0 * textScale);
    QString sheet = QStringLiteral(
        "QPushButton, QToolButton, QComboBox, QLineEdit, QAbstractSpinBox { min-height: %1px; padding: %2px %3px; }"
        "QPushButton, QToolButton { background: #33333b; color: #eeeeef; border: 1px solid #414149; border-radius: %4px; }"
        "QPushButton:hover, QToolButton:hover { background: #3b3b43; border-color: #53535d; }"
        "QPushButton:pressed, QToolButton:pressed { background: #2c2c32; border-color: #7554ad; }"
        "QPushButton:checked, QToolButton:checked { background: #2c2c32; border-color: #7554ad; }"
        "QPushButton:focus, QToolButton:focus { border-color: #eeeeef; }"
        "QPushButton:disabled, QToolButton:disabled { background: #26262c; color: #99999f; border-color: #33333b; }"
        "QPushButton#primary, QPushButton[primary=true] { background: #7554ad; color: white; border: 1px solid transparent; border-radius: %4px; }"
        "QPushButton#primary:hover, QPushButton[primary=true]:hover { background: #8764bf; } QLabel#title { font-weight: 600; }"
        "QPushButton#primary:pressed, QPushButton[primary=true]:pressed { background: #7554ad; border-color: #eeeeef; }"
        "QPushButton#primary:focus, QPushButton[primary=true]:focus { border-color: #eeeeef; }"
        "QPushButton#primary:disabled, QPushButton[primary=true]:disabled { background: #33333b; color: #99999f; border-color: #414149; }"
        "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QAbstractSpinBox { background: #26262c; color: #eeeeef; placeholder-text-color: #99999f; border: 1px solid #414149; border-radius: %4px; selection-background-color: #7554ad; selection-color: #ffffff; }"
        "QLineEdit:hover, QTextEdit:hover, QPlainTextEdit:hover, QComboBox:hover, QAbstractSpinBox:hover { border-color: #53535d; }"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { border-color: #eeeeef; }"
        "QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QComboBox:disabled, QAbstractSpinBox:disabled { background: #202024; color: #99999f; border-color: #33333b; }"
        "QAbstractSpinBox QLineEdit, QComboBox QLineEdit { background: transparent; border: 0; padding: 0; min-height: 0; }"
        "QAbstractSpinBox { padding-right: %11px; }"
        "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { subcontrol-origin: border; width: %11px; }"
        "QAbstractSpinBox::up-button { subcontrol-position: top right; } QAbstractSpinBox::down-button { subcontrol-position: bottom right; }"
        "QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow { width: %12px; height: %13px; }"
        "QComboBox { padding-right: %11px; }"
        "QComboBox::drop-down, QDateTimeEdit::drop-down { subcontrol-origin: border; subcontrol-position: top right; width: %11px; }"
        "QComboBox::down-arrow, QDateTimeEdit::down-arrow { width: %12px; height: %13px; }"
        "QCalendarWidget QWidget#qt_calendar_navigationbar { background: #26262c; }"
        "QCalendarWidget QToolButton { icon-size: %14px; }"
        "QHeaderView::section { background: #2c2c32; color: #eeeeef; border: 0; border-bottom: 1px solid #414149; padding: %10px %3px; }"
        "QHeaderView::section:pressed { background: #33333b; }"
        "QFrame[metric=true] { background: #26262c; border-radius: %5px; } QLabel[metricValue=true] { font-weight: 600; }"
        "QLabel[timerValue=true] { font-size: %15px; font-weight: 600; } QProgressBar { min-height: 8px; max-height: 8px; }"
        "QProgressBar::chunk { background: #7554ad; }"
        "QListWidget#navigation { background: #202024; border: 0; outline: 0; padding: 6px 4px; }"
        "QListWidget#navigation::item { border: 1px solid transparent; border-radius: 6px; color: #b9b9c4; padding: 5px 7px; margin: 1px 0; }"
        "QListWidget#navigation::item:hover { background: #2c2c32; color: #eeeeef; }"
        "QListWidget#navigation::item:selected { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #55406f,stop:1 #332e40); color: #ffffff; padding-left: 8px; }"
        "QListWidget#navigation::item:focus { border-color: #eeeeef; }"
        "QFrame#navigationActiveIndicator { background: #b899e6; border-radius: 1px; }"
        "QListWidget::item, QTableWidget::item { padding: %6px %7px; }"
        "QScrollBar::handle { border-radius: %8px; }"
        "QSlider:horizontal { min-height: %1px; }"
        "QSlider::groove:horizontal { height: %16px; background: #414149; border-radius: %17px; }"
        "QSlider::sub-page:horizontal { background: #7554ad; border-radius: %17px; }"
        "QSlider::handle:horizontal { width: %18px; margin: -%19px 0; background: #b9b9c4; border: 1px solid #414149; border-radius: %9px; }"
        "QSlider::handle:horizontal:hover { background: #eeeeef; }"
        "QSlider::handle:horizontal:pressed { background: #7554ad; border-color: #eeeeef; }"
        "QSlider::handle:horizontal:focus { border-color: #eeeeef; }"
        "QSlider::groove:horizontal:disabled, QSlider::sub-page:horizontal:disabled { background: #33333b; }"
        "QSlider::handle:horizontal:disabled { background: #53535d; border-color: #414149; }")
        .arg(controlHeight).arg(verticalPadding).arg(horizontalPadding).arg(frameRadius).arg(windowRadius)
        .arg(listPaddingY).arg(listPaddingX).arg(scrollbarRadius).arg(grabRadius).arg(headerPaddingY)
        .arg(stepButtonWidth).arg(arrowEdge*2).arg(arrowEdge).arg(qRound(16.0*textScale)).arg(qRound(30.0*textScale))
        .arg(qRound(4.0*textScale)).arg(qRound(2.0*textScale)).arg(qRound(16.0*textScale)).arg(qRound(6.0*textScale));
    // Dialog hierarchy shares the dashboard surfaces without changing actions.
    sheet += QStringLiteral(
        "QFrame#dialogHeadingSurface { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #40324f,stop:1 #2c2934); border: 0; border-left: 3px solid #b899e6; border-radius: %1px; }"
        "QLabel#dialogHeading { color: #eeeeef; font-size: %2px; font-weight: 600; background: transparent; }"
        "QWidget#dialogFormContent > QLabel { color: #b9b9c4; }"
        "QLabel#pipelineMapSummary { color: #b9b9c4; }"
        "QLabel#pipelineMapZoom { color: #d1b4f2; font-weight: 600; }"
        "QTextBrowser#pipelineMapDetails { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #352e40,stop:1 #29282f); border: 0; border-left: 3px solid #b899e6; border-radius: %1px; }"
        "QWidget#dialogFooter { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #302c38,stop:1 #29282f); border: 0; border-radius: %1px; }"
        "QTabWidget#pipelineTabs::pane, QTabWidget#cloudConflictTabs::pane, QTabWidget#profileHistoryTabs::pane { border: 0; border-top: 1px solid #414149; }"
        "QTabWidget#pipelineTabs QTabBar::tab, QTabWidget#cloudConflictTabs QTabBar::tab, QTabWidget#profileHistoryTabs QTabBar::tab { background: transparent; color: #b9b9c4; border: 0; border-bottom: 2px solid transparent; padding: %3px %4px; }"
        "QTabWidget#pipelineTabs QTabBar::tab:hover, QTabWidget#cloudConflictTabs QTabBar::tab:hover, QTabWidget#profileHistoryTabs QTabBar::tab:hover { background: #302c38; color: #eeeeef; }"
        "QTabWidget#pipelineTabs QTabBar::tab:selected, QTabWidget#cloudConflictTabs QTabBar::tab:selected, QTabWidget#profileHistoryTabs QTabBar::tab:selected { background: #302938; color: #e4d4fa; border-bottom-color: #b899e6; }"
        "QTabWidget#pipelineTabs QTabBar::tab:focus, QTabWidget#cloudConflictTabs QTabBar::tab:focus, QTabWidget#profileHistoryTabs QTabBar::tab:focus { border-bottom-color: #eeeeef; }"
        "QTabWidget#pipelineTabs QTabBar::tab:disabled, QTabWidget#cloudConflictTabs QTabBar::tab:disabled, QTabWidget#profileHistoryTabs QTabBar::tab:disabled { background: transparent; color: #99999f; border-bottom-color: transparent; }")
        .arg(windowRadius > 0 ? std::min(12,windowRadius+6) : 0).arg(qRound(20.0*textScale))
        .arg(qRound(5.0*textScale)).arg(qRound(10.0*textScale));
    // Profile dashboard: stronger hierarchy, quieter utility actions, one focus surface.
    sheet += QStringLiteral(
        "QPushButton#primary[profileSurface=false]:enabled, QPushButton#profileOverviewTaskAction0:enabled { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8764bf,stop:1 #68469f); border-color: #9876cc; }"
        "QPushButton#primary[profileSurface=false]:enabled:hover, QPushButton#profileOverviewTaskAction0:enabled:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8764bf,stop:1 #7554ad); }"
        "QPushButton#primary[profileSurface=false]:enabled:pressed, QPushButton#profileOverviewTaskAction0:enabled:pressed { background: #68469f; }"
        "QPushButton#primary[profileSurface=false]:focus, QPushButton#profileOverviewTaskAction0:focus { border-color: #eeeeef; }"
        "QLabel#title[profileSurface=true] { font-size: %1px; font-weight: 600; }"
        "QLabel#summary[profileSurface=true] { color: #b9b9c4; }"
        "QPushButton#primary[profileSurface=true] { background: transparent; color: #b9b9c4; border: 1px solid #414149; }"
        "QPushButton#primary[profileSurface=true]:hover { background: #33333b; color: #eeeeef; }"
        "QPushButton#primary[profileSurface=true]:pressed { background: #414149; }"
        "QPushButton#primary[profileSurface=true]:focus { border-color: #eeeeef; }"
        "QPushButton#primary[profileSurface=true]:disabled { background: transparent; color: #99999f; border-color: #33333b; }"
        "QWidget#profileMetrics QFrame[metric=true], QWidget#adminStatsKpiRow QFrame[metric=true] { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #37303f,stop:1 #29282f); border-radius: %2px; }"
        "QWidget#adminStatsKpiRow QLabel { color: #b9b9c4; }"
        "QWidget#adminStatsKpiRow QLabel[metricValue=true] { color: #e4d4fa; }"
        "QWidget#profileMetrics QLabel { color: #b9b9c4; }"
        "QWidget#profileMetrics QLabel[metricValue=true] { font-size: %5px; font-weight: 600; color: #e4d4fa; }"
        "QWidget#profileViewModes QPushButton { background: transparent; color: #b9b9c4; border: 1px solid transparent; border-bottom: 2px solid transparent; border-radius: 0; }"
        "QWidget#profileViewModes QPushButton:hover { color: #eeeeef; background: #26262c; }"
        "QWidget#profileViewModes QPushButton:pressed { background: #33333b; }"
        "QWidget#profileViewModes QPushButton:checked { color: #e4d4fa; background: #302938; border-bottom-color: #b899e6; }"
        "QWidget#profileViewModes QPushButton:focus { border-color: #eeeeef; }"
        "QWidget#profileViewModes QPushButton:disabled { color: #99999f; background: transparent; border-color: transparent; }"
        "QLabel#profileCollectionSummary, QLabel#profileAchievementHeading { color: #b9b9c4; }"
        "QFrame#profileSignalCard0, QFrame#profileSignalCard1, QFrame#profileSignalCard2, QFrame#profileSignalCard3,"
        "QFrame#profileTaskBriefCard, QFrame#profileBalanceCard { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #313139,stop:1 #26262c); border-radius: %2px; }"
        "QFrame#profileSignalCard1 { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #513965,stop:0.6 #382c48,stop:1 #2c2936); border-left: 3px solid #b899e6; }"
        "QFrame#profileSignalCard0 { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #3b3349,stop:1 #292830); }"
        "QLabel#profileSignalHeading0, QLabel#profileSignalHeading1, QLabel#profileSignalHeading2, QLabel#profileSignalHeading3,"
        "QLabel#profileSignalDetail0, QLabel#profileSignalDetail1, QLabel#profileSignalDetail2, QLabel#profileSignalDetail3 { color: #b9b9c4; }"
        "QLabel#profileSignalHeading1, QLabel#profileSignalDetail1 { color: #d6c8e5; }"
        "QLabel#profileSignalValue0, QLabel#profileSignalValue2 { font-size: %3px; font-weight: 600; }"
        "QLabel#profileSignalValue1 { font-size: %4px; font-weight: 600; }"
        "QWidget#profileOverview QPushButton[dashboardUtility=true] { background: transparent; border: 1px solid transparent; color: #b9b9c4; }"
        "QWidget#profileOverview QPushButton[dashboardUtility=true]:hover { background: #33333b; color: #eeeeef; }"
        "QWidget#profileOverview QPushButton[dashboardUtility=true]:pressed { background: #414149; }"
        "QWidget#profileOverview QPushButton[dashboardUtility=true]:focus { border-color: #eeeeef; }"
        "QWidget#profileOverview QPushButton[dashboardUtility=true]:disabled { background: transparent; color: #99999f; }"
        "QTableWidget#profileTaskBriefTable, QTableWidget#profileBalanceTable { background: transparent; border: 0; }"
        "QTableWidget#profileTaskBriefTable QHeaderView::section, QTableWidget#profileBalanceTable QHeaderView::section { background: transparent; color: #b9b9c4; border: 0; }"
        "QTableWidget#profileBalanceTable QProgressBar { background: #33333b; border: 0; min-height: 6px; max-height: 6px; border-radius: 3px; }"
        "QTableWidget#profileBalanceTable QProgressBar::chunk { border-radius: 3px; }")
        .arg(qRound(24.0*textScale)).arg(windowRadius > 0 ? std::min(12,windowRadius+6) : 0)
        .arg(qRound(18.0*textScale)).arg(qRound(22.0*textScale)).arg(qRound(17.0*textScale));
    // Reapplying identical settings must not repolish every open widget.
    sheet += QStringLiteral(
        "QLabel#title[workList=true] { font-size: %1px; font-weight: 600; }"
        "QLabel#summary[workList=true] { color: #b9b9c4; }"
        "QFrame#listFilters[workList=true], QWidget#adminProfileStatsFilters { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #35303e,stop:1 #29282f); border: 0; border-radius: %2px; }"
        "QTableWidget#records[workList=true] { background: #26262c; alternate-background-color: #26262c; border: 0; selection-background-color: #7554ad; selection-color: #ffffff; }"
        "QTableWidget#records[workList=true]::item { border-bottom: 1px solid #33333b; }"
        "QTableWidget#records[workList=true]::item:hover { background: #34303e; }"
        "QTableWidget#records[workList=true]::item:selected { background: #594372; color: #ffffff; }"
        "QTableWidget#records[workList=true]::item:focus { border: 1px solid #eeeeef; }"
        "QHeaderView#recordsHeader[workList=true]::section { background: #322c3d; color: #d6c8e5; border: 0; border-bottom: 1px solid #65517e; }"
        "QPushButton[workUtility=true], QToolButton[workUtility=true] { background: transparent; border: 1px solid transparent; color: #b9b9c4; }"
        "QPushButton[workUtility=true]:hover, QToolButton[workUtility=true]:hover { background: #33333b; color: #eeeeef; }"
        "QPushButton[workUtility=true]:checked, QToolButton[workUtility=true]:checked { background: #33333b; color: #eeeeef; border-color: #7554ad; }"
        "QPushButton[workUtility=true]:pressed, QToolButton[workUtility=true]:pressed { background: #414149; color: #eeeeef; }"
        "QPushButton[workUtility=true]:focus, QToolButton[workUtility=true]:focus { border-color: #eeeeef; }"
        "QPushButton[workUtility=true]:disabled, QToolButton[workUtility=true]:disabled { background: transparent; color: #99999f; border-color: transparent; }"
        "QToolButton[workUtility=true]::menu-button { background: transparent; border: 0; }"
        "QToolButton[workUtility=true]::menu-button:hover { background: #33333b; }"
        "QToolButton[workUtility=true]::menu-button:pressed { background: #414149; }"
        "QToolButton[workUtility=true]::menu-button:disabled { background: transparent; }"
        "QWidget#navigationColumn { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #2f2938,stop:0.55 #26262c,stop:1 #232329); border-radius: %2px; }"
        "QListWidget#navigation { background: transparent; }"
        "QFrame#localClockCard { background: transparent; }")
        .arg(qRound(24.0*textScale)).arg(windowRadius > 0 ? std::min(12,windowRadius+6) : 0);
    sheet += QStringLiteral(
        "QFrame#pomodoroSession { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #513965,stop:0.6 #382c48,stop:1 #2c2936); border-left: 3px solid #b899e6; border-radius: %1px; }"
        "QLabel#pomodoroHeading, QLabel#pomodoroCycles, QLabel#pomodoroRewardStatus { color: #d6c8e5; }"
        "QLabel#pomodoroPhase { font-size: %2px; font-weight: 600; color: #eeeeef; }"
        "QLabel#pomodoroTime { color: #f0e5ff; }"
        "QProgressBar#pomodoroProgress { background: #292431; border: 0; border-radius: 4px; }"
        "QProgressBar#pomodoroProgress::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #9975c6,stop:1 #d1b4f2); border-radius: 4px; }"
        "QFrame#pomodoroSettingsBody { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #313039,stop:1 #26262c); border-radius: %1px; }"
        "QFrame#pomodoroSettingsBody QLabel, QLabel#pomodoroSettingsSummary { color: #b9b9c4; }"
        "QPushButton#pomodoroReset, QToolButton#pomodoroSettingsToggle { background: transparent; border: 1px solid transparent; color: #d6c8e5; }"
        "QPushButton#pomodoroReset:hover, QToolButton#pomodoroSettingsToggle:hover { background: #40344e; color: #ffffff; }"
        "QToolButton#pomodoroSettingsToggle:checked { background: #302938; color: #e4d4fa; }"
        "QPushButton#pomodoroReset:pressed, QToolButton#pomodoroSettingsToggle:pressed { background: #51415f; }"
        "QPushButton#pomodoroReset:focus, QToolButton#pomodoroSettingsToggle:focus { border-color: #eeeeef; }"
        "QPushButton#pomodoroReset:disabled, QToolButton#pomodoroSettingsToggle:disabled { background: transparent; color: #99999f; border-color: transparent; }")
        .arg(windowRadius > 0 ? std::min(12,windowRadius+6) : 0).arg(qRound(16.0*textScale));
    sheet += QStringLiteral(
        "QFrame[modelSection=true] { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #35303e,stop:1 #29282f); border-radius: %1px; }"
        "QFrame[modelSection=true] QLabel { color: #b9b9c4; }"
        "QFrame[modelSection=true] QLabel[modelSectionHeading=true] { color: #e4d4fa; font-size: %2px; font-weight: 600; }"
        "QLabel#modelYawValue, QLabel#modelPitchValue, QLabel#modelZoomValue, QLabel#modelSpeedValue { color: #e4d4fa; font-weight: 600; }"
        "QLabel#modelSpeedValue:disabled { color: #99999f; }"
        "QLabel#modelStatus { color: #b9b9c4; }")
        .arg(windowRadius > 0 ? std::min(12,windowRadius+6) : 0).arg(qRound(16.0*textScale));
    if (app.styleSheet() != sheet) app.setStyleSheet(sheet);
}

void ApplyQtTheme(QApplication& app) {
    QApplication::setStyle(new ForgeMirrorStyle);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#202024"));
    palette.setColor(QPalette::WindowText, QColor("#eeeeef"));
    palette.setColor(QPalette::Base, QColor("#26262c"));
    palette.setColor(QPalette::AlternateBase, QColor("#2c2c32"));
    palette.setColor(QPalette::Text, QColor("#eeeeef"));
    palette.setColor(QPalette::PlaceholderText, QColor("#99999f"));
    palette.setColor(QPalette::Button, QColor("#33333b"));
    palette.setColor(QPalette::ButtonText, QColor("#eeeeef"));
    palette.setColor(QPalette::Highlight, QColor("#7554ad"));
    palette.setColor(QPalette::Link, QColor("#b899e6"));
    palette.setColor(QPalette::LinkVisited, QColor("#d1b4f2"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#99999f"));
    app.setPalette(palette);
    app.setFont(QFont("Segoe UI", 10));
    ApplyQtLayoutMetrics(app, 100, 4, 4.0, 4.0, 6.0, 4.0, 8.0, 0.0, 8.0, 6.0);
}
