#pragma once
#include "QtWorkspace.h"
#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTextBrowser;

class QtWindow : public QMainWindow {
public:
    explicit QtWindow(QtWorkspace& workspace);
private:
    void reload();
    void render();
    void details();
    void authenticate();
    void createEntry();
    void changeStatus();
    bool requireAdmin();
    void message(const std::string& error);
    QString selectedId() const;
    QtWorkspace& workspace_;
    bool admin_ = false;
    QComboBox* profiles_;
    QListWidget* navigation_;
    QLineEdit* search_;
    QComboBox* statusFilter_;
    QLabel* summary_;
    QLabel* title_;
    QLabel* mode_;
    QTableWidget* table_;
    QTextBrowser* details_;
    QPushButton* primary_;
    QPushButton* changeStatus_;
};
