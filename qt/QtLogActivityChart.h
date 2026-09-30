#pragma once

#include "AppDomainTypes.h"
#include <QFont>
#include <QRectF>
#include <QString>
#include <QTextOption>
#include <QWidget>
#include <array>
#include <vector>

class QtLogActivityChart final : public QWidget {
public:
    struct TextRegion {
        QString text;
        QRectF rect;
        QFont font;
        int alignment = Qt::AlignLeft;
        QString role;
        QTextOption::WrapMode wrapMode = QTextOption::WrapAtWordBoundaryOrAnywhere;
    };
    struct LayoutMetrics {
        int requiredHeight = 0;
        std::vector<TextRegion> texts;
        QRectF plot;
        std::array<QRectF, 16> bars{};
        bool datesStacked = false;
    };
    explicit QtLogActivityChart(QWidget* parent = nullptr);
    void setEntries(const std::vector<AppLogEntry>& entries);
    const std::array<int, 16>& values() const { return values_; }
    static std::array<int, 16> BuildHistogram(const std::vector<AppLogEntry>& entries);
    LayoutMetrics layoutMetrics(int availableWidth) const;
    bool hasHeightForWidth() const override;
    int heightForWidth(int availableWidth) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateChartGeometry();
    std::array<int, 16> values_{};
    std::int64_t firstTimestamp_ = 0;
    std::int64_t lastTimestamp_ = 0;
};
