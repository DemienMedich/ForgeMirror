#include "QtProfileAnalytics.h"

#include <QDateTime>
#include <QPainter>
#include <QPaintEvent>
#include <QSpinBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

QtProfileAnalytics::QtProfileAnalytics(QWidget* parent) : QWidget(parent) {
    setObjectName("profileAnalyticsCharts");
    setMinimumHeight(380);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto* toolbar = new QHBoxLayout;
    auto* label = new QLabel(QString::fromUtf8("Оси радара"), this);
    toolbar->addWidget(label);
    axisCount_ = new QSpinBox(this);
    axisCount_->setObjectName("profileRadarAxes");
    axisCount_->setRange(3, 16);
    axisCount_->setValue(8);
    axisCount_->setFixedWidth(74);
    axisCount_->setAccessibleName(QString::fromUtf8("Количество навыков на радаре"));
    axisCount_->setToolTip(QString::fromUtf8("От 3 до 16 навыков с наибольшим общим XP"));
    toolbar->addWidget(axisCount_);
    toolbar->addStretch(1);
    layout->addLayout(toolbar);
    connect(axisCount_, qOverload<int>(&QSpinBox::valueChanged), this, [this] {
        updateAccessibleDescription();
        update();
    });
    setAccessibleName(QString::fromUtf8("Аналитика профиля: категории, топ навыков и радар"));
}

std::vector<QtProfileSkillMetric> QtProfileAnalytics::TopSkills(std::vector<QtProfileSkillMetric> skills, int limit) {
    std::stable_sort(skills.begin(), skills.end(), [](const auto& left, const auto& right) {
        if (left.totalXp != right.totalXp) return left.totalXp > right.totalXp;
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
    if (limit >= 0 && int(skills.size()) > limit) skills.resize(size_t(limit));
    return skills;
}

void QtProfileAnalytics::setData(const std::array<int, 5>& categoryScores,
                                 const QStringList& categoryLabels,
                                 std::vector<QtProfileSkillMetric> skills) {
    categoryScores_ = categoryScores;
    categoryLabels_ = categoryLabels;
    skills_ = TopSkills(std::move(skills), 16);
    const int maximum = std::min(16, int(skills_.size()));
    axisCount_->setEnabled(maximum >= 3);
    axisCount_->setRange(3, std::max(3, maximum));
    if (!skills_.empty() && maximum < axisCount_->value()) axisCount_->setValue(maximum);
    updateAccessibleDescription();
    update();
}

void QtProfileAnalytics::updateAccessibleDescription() {
    QStringList description;
    QStringList categories;
    for (int i = 0; i < categoryLabels_.size() && i < int(categoryScores_.size()); ++i)
        categories << QString::fromUtf8("%1: %2/10").arg(categoryLabels_[i]).arg(categoryScores_[size_t(i)]);
    description << QString::fromUtf8("Оценки категорий из 10: ") + categories.join(QStringLiteral("; "));

    const auto topSix = TopSkills(skills_, 6);
    QStringList topSkillValues;
    for (const auto& skill : topSix)
        topSkillValues << QString::fromUtf8("%1: %2 XP").arg(skill.name).arg(skill.totalXp);
    description << QString::fromUtf8("Топ навыков по общему XP: ") + topSkillValues.join(QStringLiteral("; "));

    const int radarAxes = std::min(axisCount_->value(), int(skills_.size()));
    QStringList radarValues;
    for (int i = 0; i < radarAxes; ++i) {
        const auto& skill = skills_[size_t(i)];
        if (skill.nextLevelXp > 0) {
            radarValues << QString::fromUtf8("%1: уровень %2 + %3/%4 XP")
                .arg(skill.name).arg(skill.level).arg(skill.currentXp).arg(skill.nextLevelXp);
        } else {
            radarValues << QString::fromUtf8("%1: уровень %2, прогресс XP недоступен")
                .arg(skill.name).arg(skill.level);
        }
    }
    if (radarAxes >= 3) {
        description << QString::fromUtf8("Радар: %1 навыков с наибольшим общим XP; показан уровень и прогресс до следующего уровня: ")
            .arg(radarAxes) + radarValues.join(QStringLiteral("; "));
    } else {
        description << QString::fromUtf8("Радар недоступен: нужно не менее 3 навыков с XP.");
    }
    setAccessibleDescription(description.join(QString::fromUtf8(". ")));
}

void QtProfileAnalytics::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor text = palette().color(QPalette::WindowText);
    const QColor muted = palette().color(QPalette::Disabled, QPalette::Text);
    const QColor track = palette().color(QPalette::AlternateBase);
    const QColor surface = palette().color(QPalette::Base);
    const QColor accent = palette().color(QPalette::Highlight);
    const QColor accentFill(accent.red(), accent.green(), accent.blue(), 42);
    const int gap = 10;
    // The spin-box toolbar is a real child layout above this custom-painted area.
    // Reserve its height so chart panels never paint underneath the control.
    const int contentTop = 34;
    const int contentHeight = std::max(0, height() - contentTop);
    const int top = contentTop;
    const int panelHeight = std::min(172, std::max(148, contentHeight / 3));
    const int halfWidth = std::max(1, (width() - gap) / 2);
    const QRect categories(0, top, halfWidth, panelHeight);
    const QRect topSkills(halfWidth + gap, top, width() - halfWidth - gap, panelHeight);
    auto panel = [&](const QRect& rect, const QString& heading) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(surface);
        painter.drawRoundedRect(rect, 8, 8);
        painter.setPen(text);
        auto font = painter.font(); font.setWeight(QFont::DemiBold); painter.setFont(font);
        painter.drawText(rect.adjusted(12, 8, -10, 0), Qt::AlignLeft | Qt::AlignTop, heading);
        font.setWeight(QFont::Normal); painter.setFont(font);
    };
    panel(categories, QString::fromUtf8("Категории"));
    panel(topSkills, QString::fromUtf8("Топ навыков"));

    if (categoryLabels_.isEmpty()) {
        painter.setPen(muted);
        painter.drawText(categories.adjusted(12, 34, -12, -8), Qt::AlignCenter,
            QString::fromUtf8("Нет данных по категориям"));
    } else {
        int maximum = 1;
        for (const int score : categoryScores_) maximum = std::max(maximum, std::clamp(score, 0, 10));
        const int rows = std::min(int(categoryLabels_.size()), int(categoryScores_.size()));
        const int rowHeight = std::max(18, (panelHeight - 42) / std::max(1, rows));
        for (int i = 0; i < rows; ++i) {
            const int y = categories.top() + 34 + i * rowHeight;
            const int score = std::clamp(categoryScores_[size_t(i)], 0, 10);
            painter.setPen(text);
            painter.drawText(QRect(categories.left() + 12, y, 62, rowHeight), Qt::AlignLeft | Qt::AlignVCenter, categoryLabels_[i]);
            const int valueWidth = 36;
            const int trackLeft = categories.left() + 78;
            const int trackRight = categories.right() - valueWidth - 14;
            const QRect bar(trackLeft, y + rowHeight / 2 - 4, std::max(0, trackRight - trackLeft), 8);
            painter.setPen(Qt::NoPen); painter.setBrush(track); painter.drawRoundedRect(bar, 4, 4);
            const int filled = qRound(bar.width() * double(score) / 10.0);
            painter.setBrush(score == maximum ? accent : accentFill);
            painter.drawRoundedRect(QRect(bar.left(), bar.top(), filled, bar.height()), 4, 4);
            painter.setPen(muted);
            painter.drawText(QRect(categories.right() - valueWidth - 8, y, valueWidth, rowHeight), Qt::AlignRight | Qt::AlignVCenter,
                QStringLiteral("%1/10").arg(score));
        }
    }

    const auto topSix = TopSkills(skills_, 6);
    if (topSix.empty()) {
        painter.setPen(muted);
        painter.drawText(topSkills.adjusted(12, 34, -12, -8), Qt::AlignCenter,
            QString::fromUtf8("У профиля пока нет навыков"));
    } else {
        const int maxXp = std::max(1, topSix.front().totalXp);
        const int rowHeight = std::max(19, (panelHeight - 42) / 6);
        const int maxNameWidth = std::max(50, topSkills.width() / 3);
        for (int i = 0; i < int(topSix.size()); ++i) {
            const auto& skill = topSix[size_t(i)];
            const int y = topSkills.top() + 34 + i * rowHeight;
            const QRect nameRect(topSkills.left() + 12, y, maxNameWidth, rowHeight);
            painter.setPen(text);
            painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(painter.font()).elidedText(skill.name, Qt::ElideRight, nameRect.width()));
            const int valueWidth = 52;
            const int trackLeft = nameRect.right() + 7;
            const int trackRight = topSkills.right() - valueWidth - 14;
            const QRect bar(trackLeft, y + rowHeight / 2 - 4, std::max(0, trackRight - trackLeft), 8);
            painter.setPen(Qt::NoPen); painter.setBrush(track); painter.drawRoundedRect(bar, 4, 4);
            painter.setBrush(accent);
            painter.drawRoundedRect(QRect(bar.left(), bar.top(), qRound(bar.width() * double(std::max(0, skill.totalXp)) / maxXp), bar.height()), 4, 4);
            painter.setPen(muted);
            painter.drawText(QRect(topSkills.right() - valueWidth - 8, y, valueWidth, rowHeight), Qt::AlignRight | Qt::AlignVCenter,
                QString::number(skill.totalXp));
        }
    }

    const int radarTop = panelHeight + top + gap;
    const QRect radarPanel(0, radarTop, width(), std::max(0, height() - radarTop));
    panel(radarPanel, QString::fromUtf8("Радар навыков · уровень с прогрессом"));
    const int axisCount = std::min(axisCount_->value(), int(skills_.size()));
    if (axisCount < 3) {
        painter.setPen(muted);
        painter.drawText(radarPanel.adjusted(12, 34, -12, -8), Qt::AlignCenter,
            QString::fromUtf8("Для радара нужно не менее 3 навыков"));
        return;
    }
    const auto axes = TopSkills(skills_, axisCount);
    const qreal radius = std::max<qreal>(8.0, std::min(radarPanel.width() * 0.24, radarPanel.height() * 0.35));
    const QPointF center(radarPanel.center().x(), radarPanel.center().y() + 7);
    double maxLevel = 1.0;
    std::vector<double> values;
    values.reserve(axes.size());
    for (const auto& skill : axes) {
        const double value = std::max(0, skill.level) + (skill.nextLevelXp > 0
            ? std::clamp(double(skill.currentXp) / skill.nextLevelXp, 0.0, 1.0) : 0.0);
        values.push_back(value); maxLevel = std::max(maxLevel, value);
    }
    constexpr double pi = 3.14159265358979323846;
    for (int ring = 1; ring <= 4; ++ring) {
        QPolygonF polygon;
        const qreal ringRadius = radius * ring / 4.0;
        for (int i = 0; i < axisCount; ++i) {
            const double angle = -pi / 2.0 + (2.0 * pi * i / axisCount);
            polygon << QPointF(center.x() + std::cos(angle) * ringRadius, center.y() + std::sin(angle) * ringRadius);
        }
        painter.setPen(QPen(track, 1)); painter.setBrush(Qt::NoBrush); painter.drawPolygon(polygon);
    }
    QPolygonF dataPolygon;
    for (int i = 0; i < axisCount; ++i) {
        const double angle = -pi / 2.0 + (2.0 * pi * i / axisCount);
        const QPointF end(center.x() + std::cos(angle) * radius, center.y() + std::sin(angle) * radius);
        painter.setPen(QPen(track, 1)); painter.drawLine(center, end);
        const qreal dataRadius = radius * values[size_t(i)] / maxLevel;
        dataPolygon << QPointF(center.x() + std::cos(angle) * dataRadius, center.y() + std::sin(angle) * dataRadius);
        const qreal labelRadius = radius + 20;
        const QPointF labelPoint(center.x() + std::cos(angle) * labelRadius, center.y() + std::sin(angle) * labelRadius);
        const QRect labelRect(qRound(labelPoint.x() - 58), qRound(labelPoint.y() - 9), 116, 18);
        painter.setPen(text);
        painter.drawText(labelRect, Qt::AlignCenter,
            QFontMetrics(painter.font()).elidedText(axes[size_t(i)].name, Qt::ElideRight, labelRect.width()));
    }
    painter.setPen(QPen(accent, 2)); painter.setBrush(accentFill); painter.drawPolygon(dataPolygon);
    painter.setBrush(accent);
    for (const auto& point : dataPolygon) painter.drawEllipse(point, 3, 3);
}
