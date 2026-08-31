#pragma once
#include "QtWorkspace.h"
#include "QtProfileSession.h"
#include "QtDisplaySettings.h"
#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class QAction;

class QtWindow : public QMainWindow {
public:
    explicit QtWindow(QtWorkspace& workspace);
private:
    void reload();
    void render();
    void details();
    void authenticate();
    void authenticateProfile();
    void showShortcutHelp();
    void createEntry(bool edit = false);
    void deleteEntry();
    void movePipeline(int delta);
    void changeStatus();
    void exportReport();
    void reapplyRules();
    void grantDirectXp();
    bool requireAdmin();
    void message(const std::string& error);
    QString selectedId() const;
    QtWorkspace& workspace_;
    bool admin_ = false;
    QtProfileSession profileSession_;
    QtDisplaySettings displaySettings_;
    QAction* profileAccessAction_;
    QAction* ownPasswordAction_;
    QComboBox* profiles_;
    QListWidget* navigation_;
    QLineEdit* search_;
    QComboBox* statusFilter_;
    QComboBox* reportView_;
    QLabel* summary_;
    QLabel* title_;
    QLabel* mode_;
    QWidget* profileMetrics_;
    QWidget* pomodoro_;
    QWidget* bottomActions_;
    QLabel* profileValues_[5];
    QTableWidget* table_;
    QTextBrowser* details_;
    QPushButton* primary_;
    QPushButton* detailsToggle_;
    QPushButton* changeStatus_;
    QPushButton* editEntry_;
    QPushButton* deleteEntry_;
    QPushButton* moveUp_;
    QPushButton* moveDown_;
    QPushButton* advanceStage_;
    QPushButton* achievements_;
    QPushButton* removeSpirit_;
    QPushButton* exportReport_;
    QPushButton* reapplyRules_;
    QPushButton* directXp_;
    QPushButton* openShortcut_;
};
