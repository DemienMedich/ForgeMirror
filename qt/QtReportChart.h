#pragma once

#include <QWidget>
#include <QString>
#include <QDate>
#include <QFont>
#include <QPolygonF>
#include <QRectF>
#include <QTextOption>
#include "AppDomainTypes.h"
#include <array>
#include <vector>

class QtReportChart final : public QWidget {
public:
    struct TextRegion {
        QString role;
        QString text;
        QRectF rect;
        QFont font;
        Qt::Alignment alignment = Qt::AlignLeft;
        QTextOption::WrapMode wrapMode = QTextOption::WrapAtWordBoundaryOrAnywhere;
    };
    struct StatusRegion {
        TextRegion label;
        TextRegion value;
        QRectF track;
    };
    struct TrendLabel {
        int monthIndex = 0;
        TextRegion label;
    };
    struct LayoutMetrics {
        int requiredHeight = 0;
        int statusColumns = 3;
        TextRegion title;
        TextRegion period;
        TextRegion trendCaption;
        TextRegion emptyMessage;
        std::array<StatusRegion, 3> statuses;
        QRectF plot;
        QPolygonF trendPoints;
        std::vector<TrendLabel> valueLabels;
        std::vector<TrendLabel> dateLabels;
        std::vector<TextRegion> texts;
    };
    explicit QtReportChart(QWidget* parent = nullptr);
    LayoutMetrics layoutMetrics(int width) const;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    void setValues(int newTasks, int inProgressTasks, int doneTasks, const QString& periodLabel);
    void setCompletionTrend(const std::array<int, 12>& monthlyCompletions);
    const std::array<int, 12>& completionTrend() const { return monthlyCompletions_; }
    static std::array<int, 12> BuildMonthlyCompletionTrend(const std::vector<TaskAuditEntry>& audit,
                                                            const QDate& currentDate = QDate::currentDate());

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void updateAccessibleDescription();
    int values_[3] = {0, 0, 0};
    QString periodLabel_;
    QString completionTrendDescription_;
    QDate trendFirstMonth_;
    std::array<int, 12> monthlyCompletions_{};
};
