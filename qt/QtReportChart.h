#pragma once

#include <QWidget>
#include <QString>
#include <QDate>
#include "AppDomainTypes.h"
#include <array>
#include <vector>

class QtReportChart final : public QWidget {
public:
    explicit QtReportChart(QWidget* parent = nullptr);
    void setValues(int newTasks, int inProgressTasks, int doneTasks, const QString& periodLabel);
    void setCompletionTrend(const std::array<int, 12>& monthlyCompletions);
    static std::array<int, 12> BuildMonthlyCompletionTrend(const std::vector<TaskAuditEntry>& audit,
                                                            const QDate& currentDate = QDate::currentDate());

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int values_[3] = {0, 0, 0};
    QString periodLabel_;
    std::array<int, 12> monthlyCompletions_{};
};
