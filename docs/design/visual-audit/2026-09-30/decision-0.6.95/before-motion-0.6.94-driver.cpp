#include "QtWindow.h"
#include "QtWorkspace.h"
#include "QtTheme.h"
#include "QtDisplaySettings.h"
#include <QtWidgets>
#include <QTest>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv); ApplyQtTheme(app);
    bool okay = true;
    auto check = [&](bool condition, const char* text) { okay &= condition; if(!condition) std::cerr << text << '\n'; };
    const QString output=qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if (output.isEmpty() || !QDir().mkpath(output)) return 2;
    for (bool enabled : {true,false}) {
        QTemporaryDir temp; if (!temp.isValid()) return 2;
        const auto directory=std::filesystem::u8path(temp.path().toUtf8().toStdString());
        QtDisplaySettings settings; settings.motionEnabled=enabled;
        if (!SaveQtDisplaySettings(directory,settings)) return 2;
        QtWorkspace workspace(directory); QtWindow window(workspace); window.resize(1120,720);
        window.show(); window.raise(); window.activateWindow();
        check(QTest::qWaitForWindowExposed(&window),"window not exposed");
        check(QTest::qWaitForWindowActive(&window),"window not active");
        QTest::qWait(30);
        auto* navigation=window.findChild<QListWidget*>("navigation");
        auto* animation=window.findChild<QPropertyAnimation*>("navigationIndicatorAnimation");
        auto* marker=window.findChild<QWidget*>("navigationActiveIndicator");
        if (!navigation || !animation || !marker || navigation->count()<2) return 2;
        const bool systemAllowed=IsQtMotionAllowed(settings);
        auto target=[&](int row) { const auto rect=navigation->visualItemRect(navigation->item(row)); return QRect(rect.left()+2,rect.top()+6,3,std::max(12,rect.height()-12)); };
        navigation->setCurrentRow(0); QTest::qWait(240);
        const QRect start=marker->geometry();
        QWidget* focus=window.focusWidget();
        const QSize originalSize=window.size(); const QRect navBounds=navigation->geometry();
        const QString stem=enabled?"motion-on":"motion-off";
        check(window.grab().save(output+'/'+stem+"-start.png"),"start capture failed");
        navigation->setCurrentRow(1); QApplication::processEvents();
        const QRect immediate=marker->geometry(); const int t0=animation->currentTime();
        QTest::qWait(40);
        const QRect middle=marker->geometry(); const int t1=animation->currentTime();
        const auto middleState=animation->state();
        check(window.grab().save(output+'/'+stem+"-middle.png"),"middle capture failed");
        std::cout << stem.toStdString() << " allowed="<<systemAllowed<<" state="<<animation->state()<<" t="<<t0<<':'<<t1
            <<" y="<<start.y()<<':'<<immediate.y()<<':'<<middle.y()<<" target="<<target(1).y()<<'\n';
        if (systemAllowed) check(middleState==QAbstractAnimation::Running && t1>t0 && middle!=start && middle!=target(1),"no actual intermediate navigation frame");
        else check(animation->state()==QAbstractAnimation::Stopped && middle==target(1),"suppressed navigation did not snap");
        QRect stoppedFrame; bool stoppedForReversal=false;
        const auto stopConnection=QObject::connect(animation,&QAbstractAnimation::stateChanged,
            [&](QAbstractAnimation::State state,QAbstractAnimation::State previous) {
                if (state==QAbstractAnimation::Stopped && previous==QAbstractAnimation::Running) {
                    stoppedFrame=marker->geometry(); stoppedForReversal=true;
                }
            });
        navigation->setCurrentRow(0); QApplication::processEvents();
        check(!systemAllowed || (stoppedForReversal && animation->startValue().toRect()==stoppedFrame),
            "reversal jumped instead of continuing from current stop frame");
        QObject::disconnect(stopConnection);
        std::cout<<stem.toStdString()<<" reversal stop="<<stoppedFrame.y()<<" start="<<animation->startValue().toRect().y()<<'\n';
        QTest::qWait(240);
        check(animation->state()==QAbstractAnimation::Stopped && marker->geometry()==target(0),"navigation final target mismatch");
        check(window.size()==originalSize && navigation->geometry()==navBounds,"motion changed shell layout");
        check(window.focusWidget()==focus,"programmatic navigation changed focus");
        navigation->setCurrentRow(1); QApplication::processEvents(); QTest::qWait(30);
        window.hide(); QApplication::processEvents();
        std::cout<<stem.toStdString()<<" hidden state="<<animation->state()<<" t="<<animation->currentTime()<<" y="<<marker->y()<<'\n';
        check(animation->state()==QAbstractAnimation::Stopped,"hidden navigation continues animating");
        window.show(); QTest::qWait(240);
        navigation->setCurrentRow(0); QApplication::processEvents(); QTest::qWait(30);
        window.setEnabled(false); QApplication::processEvents();
        std::cout<<stem.toStdString()<<" disabled state="<<animation->state()<<" y="<<marker->y()<<'\n';
        check(animation->state()==QAbstractAnimation::Stopped,"disabled navigation continues animating");
        check(marker->geometry()==target(0),"disabled navigation did not snap to selected row");
        window.setEnabled(true);
        window.close();
    }
    return okay?0:1;
}
