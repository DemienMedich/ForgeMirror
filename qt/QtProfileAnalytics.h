#pragma once

#include <QWidget>
#include <QStringList>
#include <array>
#include <vector>

class QString;
class QSpinBox;

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
    explicit QtProfileAnalytics(QWidget* parent = nullptr);
    void setData(const std::array<int, 5>& categoryScores,
                 const QStringList& categoryLabels,
                 std::vector<QtProfileSkillMetric> skills);
    QSpinBox* axisControl() const { return axisCount_; }
    static std::vector<QtProfileSkillMetric> TopSkills(std::vector<QtProfileSkillMetric> skills, int limit);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    void updateAccessibleDescription();
    std::array<int, 5> categoryScores_{};
    QStringList categoryLabels_;
    std::vector<QtProfileSkillMetric> skills_;
    QSpinBox* axisCount_ = nullptr;
};
