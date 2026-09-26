#pragma once

#include "AppDomainTypes.h"
#include <QWidget>
#include <array>
#include <vector>

class QtLogActivityChart final : public QWidget {
public:
    explicit QtLogActivityChart(QWidget* parent = nullptr);
    void setEntries(const std::vector<AppLogEntry>& entries);
    const std::array<int, 16>& values() const { return values_; }
    static std::array<int, 16> BuildHistogram(const std::vector<AppLogEntry>& entries);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::array<int, 16> values_{};
    std::int64_t firstTimestamp_ = 0;
    std::int64_t lastTimestamp_ = 0;
};
