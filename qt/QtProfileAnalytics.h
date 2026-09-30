#pragma once

#include <QWidget>
#include <QFont>
#include <QLineF>
#include <QPolygonF>
#include <QRectF>
#include <QTextOption>
#include <QStringList>
#include <array>
#include <vector>

class QString;
class QSpinBox;
class QLabel;
class QBoxLayout;

struct QtProfileSkillMetric {
    QString name;
    int level = 0;
    int currentXp = 0;
    int nextLevelXp = 0;
    int totalXp = 0;
    double weight = 1.0;
};

class QtProfileAnalytics final : public QWidget {
public:
    struct TextRegion {
        QString text;
        QRectF rect;
        QFont font;
        int alignment = Qt::AlignLeft;
        QString role;
        QTextOption::WrapMode wrapMode = QTextOption::WrapAtWordBoundaryOrAnywhere;
    };
    struct BarRegion {
        QRectF track;
        qreal fraction = 0.0;
        bool highlight = true;
    };
    struct LayoutMetrics {
        int requiredHeight = 0;
        std::vector<TextRegion> texts;
        std::vector<BarRegion> bars;
        QRectF categories;
        QRectF topSkills;
        QRectF radar;
        QRectF plot;
        QPointF radarCenter;
        qreal radarRadius = 0.0;
        QPolygonF radarEnds;
        QPolygonF radarPoints;
        std::vector<QLineF> radarLabelLines;
        bool panelsStacked = false;
        bool indexedRadar = false;
        int radarAxes = 0;
    };
    explicit QtProfileAnalytics(QWidget* parent = nullptr);
    void setData(const std::array<int, 5>& categoryScores,
                 const QStringList& categoryLabels,
                 std::vector<QtProfileSkillMetric> skills);
    QSpinBox* axisControl() const { return axisCount_; }
    static std::vector<QtProfileSkillMetric> TopSkills(std::vector<QtProfileSkillMetric> skills, int limit);
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
    void updateAccessibleDescription();
    void updateChartGeometry();
    void updateToolbarGeometry();
    std::array<int, 5> categoryScores_{};
    QStringList categoryLabels_;
    std::vector<QtProfileSkillMetric> skills_;
    QSpinBox* axisCount_ = nullptr;
    QLabel* axisLabel_ = nullptr;
    QBoxLayout* toolbar_ = nullptr;
};
