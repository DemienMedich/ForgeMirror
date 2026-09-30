#include "QtWindow.h"
#include "QtTheme.h"
#include "QtDisplaySettings.h"
#include "AppShortcutsService.h"
#include <QtWidgets>
#include <QtTest/QTest>
#include <algorithm>
#include <iostream>

struct Snapshot {
    bool okay = true;
    QMap<QString, QByteArray> files;
};
static Snapshot snapshot(const QString& directory) {
    Snapshot result;
    QDirIterator entries(directory, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while(entries.hasNext()) {
        QFile file(entries.next());
        if(!file.open(QIODevice::ReadOnly)) { result.okay=false; break; }
        const auto bytes=file.readAll();
        if(file.error()!=QFileDevice::NoError) { result.okay=false; break; }
        result.files.insert(QDir(directory).relativeFilePath(file.fileName()),bytes);
    }
    return result;
}
int main(int argc,char** argv) {
    QApplication app(argc,argv); ApplyQtTheme(app);
    const QString output=qEnvironmentVariable("FORGEMIRROR_QT_TEST_ARTIFACTS");
    if(output.isEmpty() || !QDir().mkpath(output)) return 3;
    bool okay=true;
    int contexts=0;
    const auto record=[&](bool pass,const QString& context) {
        okay &= pass;
        if(!pass) std::cerr << "Quick menu: " << context.toStdString() << '\n';
    };
    for(const int scale:{100,200}) for(const bool populated:{false,true}) {
        QTemporaryDir temporary; if(!temporary.isValid()) return 3;
        const auto directory=std::filesystem::u8path(temporary.path().toUtf8().toStdString());
        QtDisplaySettings settings; settings.scalePercent=scale; settings.motionEnabled=false;
        if(!SaveQtDisplaySettings(directory,settings)) return 3;
        ApplyQtDisplaySettings(app,settings);
        std::vector<ShortcutEntry> entries;
        if(populated) {
            const QString fullLabel=QString::fromUtf8("Очень длинное название & ярлыка безопасной папки для проверки текста и полного контекста");
            const QString stoppedPath=temporary.path()+"/safe-not-running-probe.exe";
            const QString folderPath=temporary.path()+QString::fromUtf8("/Безопасная папка с длинным русским названием и полным путём");
            QFile stopped(stoppedPath);
            const QByteArray harmlessBytes("not executable; must never be launched");
            if(!stopped.open(QIODevice::WriteOnly) || stopped.write(harmlessBytes)!=harmlessBytes.size()) return 3;
            stopped.close(); if(!QDir().mkpath(folderPath)) return 3;
            const QList<QPair<QString,QString>> fixtures{
                {fullLabel.left(78)+QString::fromUtf8(" · запущена"),QCoreApplication::applicationFilePath()},
                {fullLabel.left(78)+QString::fromUtf8(" · не запущена"),stoppedPath},
                {fullLabel.left(78)+QString::fromUtf8(" · папка"),folderPath}
            };
            for(const auto& fixture:fixtures)
                if(!AppAddShortcut(directory,entries,fixture.first.toUtf8().toStdString(),fixture.second.toUtf8().toStdString()).ok) return 3;
        }
        QtWorkspace workspace(directory); QtWindow window(workspace);
        window.showNormal(); window.resize(1600,900); window.raise(); window.activateWindow();
        record(QTest::qWaitForWindowExposed(&window,2000) && QTest::qWaitForWindowActive(&window,2000),"native main ready");
        QApplication::processEvents(); QApplication::processEvents();
        auto* launcher=window.findChild<QToolButton*>("quickShortcutLauncher");
        auto* menu=launcher ? launcher->menu() : nullptr;
        record(launcher && launcher->isVisible() && launcher->isEnabled() && menu && menu->objectName()=="quickShortcutMenu","actual launcher/menu hook");
        if(!launcher || !launcher->isVisible() || !menu) { window.close(); continue; }
        const auto before=snapshot(temporary.path()); record(before.okay,"before popup complete file reads");
        int triggered=0; bool inspected=false;
        QTimer ready; ready.setInterval(1); QElapsedTimer elapsed; elapsed.start();
        QObject::connect(&ready,&QTimer::timeout,&window,[&] {
            if(QApplication::activePopupWidget()!=menu || !menu->isVisible()) {
                if(elapsed.elapsed()>8000) { ready.stop(); record(false,"popup readiness timeout"); menu->close(); }
                return;
            }
            // A QToolButton InstantPopup press enters QMenu's nested event loop.
            // Wait for that loop so Escape cannot precede exec() and be lost.
            if(QThread::currentThread()->loopLevel()<1) return;
            ready.stop(); inspected=true; ++contexts;
            record(QTest::qWaitForWindowExposed(menu,2000),"native popup exposed");
            record(QApplication::activePopupWidget()==menu,"native popup active");
            const QString stem=QString("quick-menu-%1-%2").arg(populated ? "populated" : "empty").arg(scale);
            record(menu->screen()->availableGeometry().contains(menu->frameGeometry()),stem+" native screen bounds");
            record(menu->toolTipsVisible(),stem+" full context tooltips enabled");
            int shortcuts=0,previousBottom=-1,widestStatus=0;
            for (auto* action:menu->actions())
                widestStatus=std::max(widestStatus,menu->fontMetrics().horizontalAdvance(action->text().section('\t',1)));
            for(auto* action:menu->actions()) {
                QObject::connect(action,&QAction::triggered,&window,[&] { ++triggered; });
                if(!action->isVisible()) continue;
                const QRect rect=menu->actionGeometry(action);
                record(menu->rect().contains(rect),stem+" actual row contained "+action->objectName());
                record(rect.top()>previousBottom,stem+" rows do not overlap"); previousBottom=rect.bottom();
                if(action->isSeparator()) continue;
                record(rect.height()>=menu->fontMetrics().height(),stem+" full row glyph height "+action->objectName());
                const QString label=action->text().section('\t',0,0).replace("&&","&");
                const QString status=action->text().section('\t',1);
                const int iconWidth=action->icon().isNull() ? 0 : menu->style()->pixelMetric(QStyle::PM_SmallIconSize);
                const int glyphWidth=menu->fontMetrics().horizontalAdvance(label)+menu->fontMetrics().horizontalAdvance(status)+iconWidth;
                record(rect.width()>=glyphWidth,stem+" visible label/status glyph widths "+action->objectName());
                if(action->objectName()=="quickShortcutAction") {
                    ++shortcuts;
                    const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry) { return action->data().toString()==QString::fromStdString(entry.id); });
                    record(found!=entries.end(),stem+" preserved shortcut identity");
                    if(found==entries.end()) continue;
                    const int index=int(std::distance(entries.begin(),found)); const int expected=index==0 ? 1 : index==1 ? 2 : 0;
                    const QString expectedStatus=expected==1 ? QString::fromUtf8("Запущена") : expected==2 ? QString::fromUtf8("Не запущена") : QString::fromUtf8("Статус недоступен");
                    record(action->property("shortcutRunState").toInt()==expected && status==expectedStatus && !action->icon().isNull(),stem+" real process status and icon");
                    const QString fullLabel=QString::fromUtf8(found->label); const QString fullPath=QDir::toNativeSeparators(QString::fromUtf8(found->path));
                    record(action->toolTip().contains(fullLabel) && action->toolTip().contains(fullPath) && action->toolTip().contains(expectedStatus) &&
                        action->statusTip()==action->toolTip(),stem+" full label/path/status context retained");
                }
                std::cout << stem.toStdString()<<" action="<<action->objectName().toStdString()<<" row="<<rect.width()<<'x'<<rect.height()
                    <<" glyphWidth="<<glyphWidth<<" fontHeight="<<menu->fontMetrics().height()<<'\n';
            }
            record(shortcuts==(populated ? 3 : 0),stem+" all fixture shortcuts");
            if(!populated) {
                auto* empty=menu->findChild<QAction*>("quickShortcutEmpty");
                record(empty && !empty->isEnabled() && !empty->text().isEmpty(),stem+" explicit disabled empty guidance");
            }
            record(menu->grab().save(output+'/'+stem+".png"),stem+" capture");
            // Paint used to poison QMenu's geometry cache after pre-paint checks.
            record(menu->sizeHint().width()<=menu->maximumWidth(),stem+" post-paint natural width within budget");
            for (auto* action:menu->actions()) {
                if (!action->isVisible()) continue;
                const QRect rect=menu->actionGeometry(action);
                record(menu->rect().contains(rect) && rect.width()<=menu->maximumWidth(),stem+" post-paint actual row contained "+action->objectName());
                if (action->isSeparator()) continue;
                const QString label=action->text().section('\t',0,0).replace("&&","&");
                const int iconWidth=action->icon().isNull()?0:menu->style()->pixelMetric(QStyle::PM_SmallIconSize);
                const int sharedTextWidth=menu->fontMetrics().horizontalAdvance(label)+widestStatus+iconWidth;
                record(rect.width()>=sharedTextWidth,stem+" label and complete shared status column fit "+action->objectName());
                std::cout<<stem.toStdString()<<" post-paint action="<<action->objectName().toStdString()
                    <<" row="<<rect.width()<<'x'<<rect.height()<<" sharedTextWidth="<<sharedTextWidth<<'\n';
            }
            QTest::keyClick(menu,Qt::Key_Escape);
            record(!menu->isVisible(),stem+" Escape closes popup");
        });
        ready.start(); QTest::mouseClick(launcher,Qt::LeftButton); ready.stop();
        record(inspected && triggered==0,"actual popup cancelled without triggering any action");
        const auto after=snapshot(temporary.path());
        record(after.okay && before.okay && before.files==after.files,"popup Cancel preserves all file bytes with successful reads");
        window.close(); QApplication::processEvents();
    }
    std::cout<<"Quick menu native contexts="<<contexts<<" no action launched; synthetic workspaces only\n";
    return okay && contexts==4 ? 0 : 1;
}
