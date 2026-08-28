#pragma once
#include "QtWorkspace.h"
#include "QtProfileSession.h"
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
    void createEntry(bool edit = false);
    void changeStatus();
    bool requireAdmin();
    void message(const std::string& error);
    QString selectedId() const;
    QtWorkspace& workspace_;
    bool admin_ = false;
    QtProfileSession profileSession_;
    QAction* profileAccessAction_;
    QAction* ownPasswordAction_;
    QComboBox* profiles_;
    QListWidget* navigation_;
    QLineEdit* search_;
    QComboBox* statusFilter_;
    QLabel* summary_;
    QLabel* title_;
    QLabel* mode_;
    QWidget* profileMetrics_;
    QLabel* profileValues_[5];
    QTableWidget* table_;
    QTextBrowser* details_;
    QPushButton* primary_;
    QPushButton* changeStatus_;
    QPushButton* editEntry_;
    QPushButton* advanceStage_;
    QPushButton* achievements_;
    QPushButton* removeSpirit_;
};
