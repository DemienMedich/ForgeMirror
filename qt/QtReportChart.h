#pragma once

#include <QWidget>
#include <QString>

class QtReportChart final : public QWidget {
public:
    explicit QtReportChart(QWidget* parent = nullptr);
    void setValues(int newTasks, int inProgressTasks, int doneTasks, const QString& periodLabel);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int values_[3] = {0, 0, 0};
    QString periodLabel_;
};
