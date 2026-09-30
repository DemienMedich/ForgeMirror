#include "QtProfileAnalytics.h"

#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSpinBox>
#include <QLabel>
#include <QBoxLayout>
#include <QVBoxLayout>
#include <QFontMetricsF>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

namespace {
constexpr qreal textGap = 4;
constexpr qreal sectionGap = 8;
constexpr qreal panelPadding = 12;
constexpr qreal barHeight = 8;
constexpr double pi = 3.14159265358979323846;

qreal arrangeText(QTextLayout& layout, qreal width, int alignment, QTextOption::WrapMode wrapMode) {
    QTextOption option;
    option.setWrapMode(wrapMode);
    option.setAlignment(Qt::Alignment(alignment & Qt::AlignHorizontal_Mask));
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0;
    while (true) {
        auto line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(std::max<qreal>(1, width));
        line.setPosition(QPointF(0, height));
        height += line.height();
    }
    layout.endLayout();
    return std::ceil(height);
}

QtProfileAnalytics::TextRegion textRegion(const QString& role, const QString& text, const QFont& font,
                                          qreal left, qreal top, qreal width,
                                          int alignment = Qt::AlignLeft | Qt::TextWordWrap) {
    QtProfileAnalytics::TextRegion region;
    region.role = role;
    region.text = text;
    region.font = font;
    region.alignment = alignment;
    region.wrapMode = alignment & Qt::TextWordWrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap;
    QTextLayout layout(text, font);
    region.rect = QRectF(left, top, std::max<qreal>(1, width),
        std::max(arrangeText(layout, width, alignment, region.wrapMode), std::ceil(QFontMetricsF(font).height())));
    return region;
}

void drawTextRegion(QPainter& painter, const QtProfileAnalytics::TextRegion& region) {
    QTextLayout layout(region.text, region.font);
    arrangeText(layout, region.rect.width(), region.alignment, region.wrapMode);
    layout.draw(&painter, region.rect.topLeft());
}

qreal textWidth(const QFontMetricsF& metrics, const QString& text) {
    return std::ceil(std::max(metrics.horizontalAdvance(text), metrics.boundingRect(text).width())) + 2;
}

int spinWidth(const QSpinBox* control) {
    const QFontMetricsF metrics(control->font());
    const qreal numberWidth = std::max(textWidth(metrics, QString::number(control->minimum())),
        textWidth(metrics, QString::number(control->maximum())));
    QStyleOptionSpinBox option;
    option.initFrom(control);
    option.frame = control->hasFrame();
    option.buttonSymbols = control->buttonSymbols();
    const auto contents = control->style()->sizeFromContents(QStyle::CT_SpinBox, &option,
        QSize(int(std::ceil(numberWidth + textGap)), int(std::ceil(metrics.height()))), control);
    return std::max(contents.width(), control->sizeHint().width());
}
}

QtProfileAnalytics::QtProfileAnalytics(QWidget* parent) : QWidget(parent) {
    setObjectName("profileAnalyticsCharts");
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    toolbar_ = new QBoxLayout(QBoxLayout::LeftToRight);
    toolbar_->setContentsMargins(0, 0, 0, 0);
    toolbar_->setSpacing(4);
    axisLabel_ = new QLabel(QString::fromUtf8("Оси радара"), this);
    toolbar_->addWidget(axisLabel_);
    axisCount_ = new QSpinBox(this);
    axisCount_->setObjectName("profileRadarAxes");
    axisCount_->setRange(3, 16);
    axisCount_->setValue(8);
    axisCount_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    axisCount_->setAccessibleName(QString::fromUtf8("Количество навыков на радаре"));
    axisCount_->setToolTip(QString::fromUtf8("От 3 до 16 навыков с наибольшим общим XP"));
    axisLabel_->setBuddy(axisCount_);
    toolbar_->addWidget(axisCount_, 0, Qt::AlignLeft);
    toolbar_->addStretch(1);
    layout->addLayout(toolbar_);
    layout->addStretch(1);
    connect(axisCount_, qOverload<int>(&QSpinBox::valueChanged), this, [this] {
        updateAccessibleDescription();
        updateChartGeometry();
    });
    setAccessibleName(QString::fromUtf8("Аналитика профиля: категории, топ навыков и радар"));
    updateChartGeometry();
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
    updateChartGeometry();
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
            radarValues << QString::fromUtf8("%1. %2: уровень %3 + %4/%5 XP")
                .arg(i + 1).arg(skill.name).arg(skill.level).arg(skill.currentXp).arg(skill.nextLevelXp);
        } else {
            radarValues << QString::fromUtf8("%1. %2: уровень %3, прогресс XP недоступен")
                .arg(i + 1).arg(skill.name).arg(skill.level);
        }
    }
    if (radarAxes >= 3) {
        description << QString::fromUtf8("Радар: %1 навыков с наибольшим общим XP; показан уровень и прогресс до следующего уровня: ")
            .arg(radarAxes) + radarValues.join(QStringLiteral("; "));
    } else {
        description << QString::fromUtf8("Радар недоступен: нужно не менее 3 навыков с XP.");
    }
    setAccessibleDescription(description.join(QString::fromUtf8(". ")));
    setToolTip(description.join(QLatin1Char('\n')));
}

QtProfileAnalytics::LayoutMetrics QtProfileAnalytics::layoutMetrics(int availableWidth) const {
    LayoutMetrics result;
    const qreal width = std::max(1, availableWidth);
    auto bodyFont = font();
    bodyFont.setWeight(QFont::Normal);
    auto headingFont = bodyFont;
    headingFont.setWeight(QFont::DemiBold);
    const QFontMetricsF metrics(bodyFont);
    const qreal lineHeight = std::ceil(metrics.height());
    const int controlWidth = axisCount_ ? spinWidth(axisCount_) : 0;
    const qreal labelWidth = axisLabel_ ? axisLabel_->sizeHint().width() : 0;
    const qreal labelHeight = axisLabel_ ? axisLabel_->sizeHint().height() : 0;
    const qreal controlHeight = axisCount_ ? axisCount_->sizeHint().height() : 0;
    const bool toolbarStacked = labelWidth + controlWidth + textGap > width;
    const qreal toolbarHeight = toolbarStacked ? labelHeight + textGap + controlHeight :
        std::max(labelHeight, controlHeight);
    const qreal top = toolbarHeight + sectionGap;

    const int categoryRows = std::min(int(categoryLabels_.size()), int(categoryScores_.size()));
    const auto topSix = TopSkills(skills_, 6);
    qreal categoryNameWidth = 0, categoryValueWidth = 0, topValueWidth = 0;
    for (int index = 0; index < categoryRows; ++index) {
        categoryNameWidth = std::max(categoryNameWidth, textWidth(metrics, categoryLabels_[index]));
        categoryValueWidth = std::max(categoryValueWidth,
            textWidth(metrics, QStringLiteral("%1/10").arg(std::clamp(categoryScores_[size_t(index)], 0, 10))));
    }
    for (const auto& skill : topSix)
        topValueWidth = std::max(topValueWidth, textWidth(metrics, QString::number(skill.totalXp)));
    const qreal categoryMinimum = panelPadding * 2 + categoryNameWidth + categoryValueWidth + textGap * 2 + 48;
    const qreal topMinimum = panelPadding * 2 + std::max<qreal>(64, metrics.horizontalAdvance(QStringLiteral("MMMMMM"))) +
        topValueWidth + textGap * 2 + 48;
    const qreal halfWidth = std::max<qreal>(1, (width - sectionGap) / 2);
    result.panelsStacked = halfWidth < std::max(categoryMinimum, topMinimum);
    const qreal panelWidth = result.panelsStacked ? width : halfWidth;

    auto categoryPanel = [&](qreal left, qreal panelTop, qreal panelWidth) {
        const qreal innerWidth = std::max<qreal>(1, panelWidth - panelPadding * 2);
        auto heading = textRegion(QStringLiteral("categoryHeading"), QString::fromUtf8("Категории"),
            headingFont, left + panelPadding, panelTop + sectionGap, innerWidth);
        result.texts.push_back(heading);
        qreal y = heading.rect.bottom() + sectionGap;
        if (categoryRows == 0) {
            auto empty = textRegion(QStringLiteral("categoryEmpty"), QString::fromUtf8("Нет данных по категориям"),
                bodyFont, left + panelPadding, y, innerWidth, Qt::AlignHCenter | Qt::TextWordWrap);
            result.texts.push_back(empty);
            y = empty.rect.bottom();
        } else {
            int maximum = 1;
            for (const int score : categoryScores_) maximum = std::max(maximum, std::clamp(score, 0, 10));
            const bool inlineBar = categoryNameWidth + categoryValueWidth + textGap * 2 + 48 <= innerWidth;
            for (int index = 0; index < categoryRows; ++index) {
                const int score = std::clamp(categoryScores_[size_t(index)], 0, 10);
                const QString count = QStringLiteral("%1/10").arg(score);
                const qreal valueWidth = textWidth(metrics, count);
                const bool inlineValue = valueWidth + textGap < innerWidth;
                const qreal nameWidth = inlineBar ? categoryNameWidth :
                    (inlineValue ? innerWidth - valueWidth - textGap : innerWidth);
                auto name = textRegion(QStringLiteral("categoryLabel%1").arg(index), categoryLabels_[index],
                    bodyFont, left + panelPadding, y, nameWidth);
                auto value = textRegion(QStringLiteral("categoryValue%1").arg(index), count, bodyFont,
                    inlineValue ? left + panelWidth - panelPadding - valueWidth : left + panelPadding,
                    inlineValue ? y : name.rect.bottom() + textGap,
                    inlineValue ? valueWidth : innerWidth, Qt::AlignRight);
                result.texts.push_back(name);
                result.texts.push_back(value);
                const qreal bottom = std::max(name.rect.bottom(), value.rect.bottom());
                const qreal trackLeft = inlineBar ? name.rect.right() + textGap : left + panelPadding;
                const qreal trackWidth = inlineBar ? value.rect.left() - textGap - trackLeft : innerWidth;
                const qreal trackTop = inlineBar ? y + (bottom - y - barHeight) / 2 : bottom + textGap;
                result.bars.push_back({QRectF(trackLeft, trackTop, std::max<qreal>(0, trackWidth), barHeight),
                    double(score) / 10.0, score == maximum});
                y = (inlineBar ? bottom : trackTop + barHeight) + sectionGap;
            }
            y -= sectionGap;
        }
        result.categories = QRectF(left, panelTop, panelWidth, y + sectionGap - panelTop);
    };
    auto topPanel = [&](qreal left, qreal panelTop, qreal panelWidth) {
        const qreal innerWidth = std::max<qreal>(1, panelWidth - panelPadding * 2);
        auto heading = textRegion(QStringLiteral("topHeading"), QString::fromUtf8("Топ навыков"),
            headingFont, left + panelPadding, panelTop + sectionGap, innerWidth);
        result.texts.push_back(heading);
        qreal y = heading.rect.bottom() + sectionGap;
        if (topSix.empty()) {
            auto empty = textRegion(QStringLiteral("topEmpty"), QString::fromUtf8("У профиля пока нет навыков"),
                bodyFont, left + panelPadding, y, innerWidth, Qt::AlignHCenter | Qt::TextWordWrap);
            result.texts.push_back(empty);
            y = empty.rect.bottom();
        } else {
            const int maxXp = std::max(1, topSix.front().totalXp);
            for (int index = 0; index < int(topSix.size()); ++index) {
                const auto& skill = topSix[size_t(index)];
                const QString count = QString::number(skill.totalXp);
                const qreal nameWidth = textWidth(metrics, skill.name);
                const qreal valueWidth = textWidth(metrics, count);
                const bool inlineBar = nameWidth + valueWidth + textGap * 2 + 48 <= innerWidth;
                const qreal usefulNameWidth = std::max<qreal>(64, metrics.horizontalAdvance(QStringLiteral("MMMMMMMM")));
                const bool inlineValue = inlineBar || innerWidth - valueWidth - textGap >= usefulNameWidth;
                auto name = textRegion(QStringLiteral("topName%1").arg(index), skill.name, bodyFont,
                    left + panelPadding, y, inlineBar ? nameWidth :
                        (inlineValue ? innerWidth - valueWidth - textGap : innerWidth));
                auto value = textRegion(QStringLiteral("topValue%1").arg(index), count, bodyFont,
                    inlineValue ? left + panelWidth - panelPadding - valueWidth : left + panelPadding,
                    inlineValue ? y : name.rect.bottom() + textGap,
                    inlineValue ? valueWidth : innerWidth, Qt::AlignRight);
                result.texts.push_back(name);
                result.texts.push_back(value);
                const qreal bottom = std::max(name.rect.bottom(), value.rect.bottom());
                const qreal trackLeft = inlineBar ? name.rect.right() + textGap : left + panelPadding;
                const qreal trackWidth = inlineBar ? value.rect.left() - textGap - trackLeft : innerWidth;
                const qreal trackTop = inlineBar ? y + (bottom - y - barHeight) / 2 : bottom + textGap;
                result.bars.push_back({QRectF(trackLeft, trackTop, std::max<qreal>(0, trackWidth), barHeight),
                    double(std::max(0, skill.totalXp)) / maxXp, true});
                y = (inlineBar ? bottom : trackTop + barHeight) + sectionGap;
            }
            y -= sectionGap;
        }
        result.topSkills = QRectF(left, panelTop, panelWidth, y + sectionGap - panelTop);
    };
    categoryPanel(0, top, panelWidth);
    topPanel(result.panelsStacked ? 0 : panelWidth + sectionGap,
        result.panelsStacked ? result.categories.bottom() + sectionGap : top, panelWidth);

    const qreal radarTop = std::max(result.categories.bottom(), result.topSkills.bottom()) + sectionGap;
    const qreal innerWidth = std::max<qreal>(1, width - panelPadding * 2);
    auto radarHeading = textRegion(QStringLiteral("radarHeading"),
        QString::fromUtf8("Радар навыков · уровень с прогрессом"), headingFont,
        panelPadding, radarTop + sectionGap, innerWidth);
    result.texts.push_back(radarHeading);
    qreal y = radarHeading.rect.bottom() + sectionGap;
    const int axisCount = axisCount_ ? std::min(axisCount_->value(), int(skills_.size())) : 0;
    result.radarAxes = axisCount;
    if (axisCount < 3) {
        auto empty = textRegion(QStringLiteral("radarEmpty"),
            QString::fromUtf8("Для радара нужно не менее 3 навыков"), bodyFont,
            panelPadding, y, innerWidth, Qt::AlignHCenter | Qt::TextWordWrap);
        result.texts.push_back(empty);
        result.radar = QRectF(0, radarTop, width, empty.rect.bottom() + sectionGap - radarTop);
        result.requiredHeight = int(std::ceil(result.radar.bottom()));
        return result;
    }

    const auto axes = TopSkills(skills_, axisCount);
    const qreal desiredRadius = std::max<qreal>(72, lineHeight * 4);
    qreal radius = std::max<qreal>(8, std::min(desiredRadius, innerWidth / 4));
    qreal diagramHeight = radius * 2 + lineHeight * 2 + sectionGap * 2;
    QPointF center(width / 2, y + lineHeight + sectionGap + radius);
    std::vector<TextRegion> namedLabels;
    bool named = axisCount <= 8;
    if (named) {
        const QRectF bounds(panelPadding, y, innerWidth, diagramHeight);
        for (int index = 0; index < axisCount; ++index) {
            const double angle = -pi / 2 + 2 * pi * index / axisCount;
            const qreal dx = std::cos(angle), dy = std::sin(angle);
            const QPointF point(center.x() + dx * (radius + sectionGap),
                center.y() + dy * (radius + sectionGap));
            const qreal nameWidth = textWidth(metrics, axes[size_t(index)].name);
            const qreal left = dx > 0.25 ? point.x() : (dx < -0.25 ? point.x() - nameWidth : point.x() - nameWidth / 2);
            const qreal labelTop = dy < -0.8 ? point.y() - lineHeight : (dy > 0.8 ? point.y() : point.y() - lineHeight / 2);
            auto label = textRegion(QStringLiteral("radarAxis%1").arg(index), axes[size_t(index)].name,
                bodyFont, left, labelTop, nameWidth, Qt::AlignHCenter);
            if (!bounds.contains(label.rect) ||
                std::any_of(namedLabels.begin(), namedLabels.end(), [&label](const auto& previous) {
                    return previous.rect.adjusted(-textGap, -textGap, textGap, textGap).intersects(label.rect);
                })) named = false;
            namedLabels.push_back(label);
        }
    }
    result.indexedRadar = !named;
    qreal indexWidth = 0;
    if (result.indexedRadar) {
        for (int index = 1; index <= axisCount; ++index)
            indexWidth = std::max(indexWidth, textWidth(metrics, QString::number(index)));
        radius = std::max<qreal>(8, std::min(desiredRadius, (innerWidth - indexWidth * 2 - sectionGap * 4) / 2));
        const int columnRows = (axisCount + 1) / 2;
        diagramHeight = std::max(radius * 2 + sectionGap * 2,
            columnRows * (lineHeight + textGap) - textGap);
        center = QPointF(width / 2, y + diagramHeight / 2);
    }
    result.radarCenter = center;
    result.radarRadius = radius;
    result.plot = QRectF(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    double maxLevel = 1;
    std::vector<double> values;
    for (const auto& skill : axes) {
        const double value = std::max(0, skill.level) + (skill.nextLevelXp > 0
            ? std::clamp(double(skill.currentXp) / skill.nextLevelXp, 0.0, 1.0) : 0.0);
        values.push_back(value);
        maxLevel = std::max(maxLevel, value);
    }
    for (int index = 0; index < axisCount; ++index) {
        const double angle = -pi / 2 + 2 * pi * index / axisCount;
        result.radarEnds << QPointF(center.x() + std::cos(angle) * radius, center.y() + std::sin(angle) * radius);
        const qreal dataRadius = radius * values[size_t(index)] / maxLevel;
        result.radarPoints << QPointF(center.x() + std::cos(angle) * dataRadius,
            center.y() + std::sin(angle) * dataRadius);
    }
    if (named) {
        result.texts.insert(result.texts.end(), namedLabels.begin(), namedLabels.end());
    } else {
        // Dense labels occupy two measured columns, not overlapping positions
        // around a small circle. The same index is repeated with the full name.
        std::array<std::vector<int>, 2> columns;
        for (int index = 0; index < axisCount; ++index)
            columns[index < (axisCount + 1) / 2 ? 1 : 0].push_back(index);
        for (int column = 0; column < 2; ++column) {
            auto& indices = columns[size_t(column)];
            std::stable_sort(indices.begin(), indices.end(), [&](int left, int right) {
                return result.radarEnds[left].y() < result.radarEnds[right].y();
            });
            const qreal columnHeight = indices.size() * (lineHeight + textGap) - textGap;
            qreal labelTop = y + (diagramHeight - columnHeight) / 2;
            for (const int index : indices) {
                const qreal left = column == 0 ? panelPadding : width - panelPadding - indexWidth;
                auto label = textRegion(QStringLiteral("radarAxis%1").arg(index), QString::number(index + 1),
                    bodyFont, left, labelTop, indexWidth, Qt::AlignHCenter);
                result.texts.push_back(label);
                const QPointF labelEdge(column == 0 ? label.rect.right() + textGap : label.rect.left() - textGap,
                    label.rect.center().y());
                result.radarLabelLines.emplace_back(result.radarEnds[index], labelEdge);
                labelTop = label.rect.bottom() + textGap;
            }
        }
    }
    y += diagramHeight;
    if (result.indexedRadar) {
        y += sectionGap;
        const int legendColumns = innerWidth >= 2 * std::max<qreal>(160, lineHeight * 10) + sectionGap ? 2 : 1;
        const qreal legendWidth = (innerWidth - sectionGap * (legendColumns - 1)) / legendColumns;
        for (int first = 0; first < axisCount; first += legendColumns) {
            qreal rowBottom = y;
            for (int column = 0; column < legendColumns && first + column < axisCount; ++column) {
                const int index = first + column;
                auto legend = textRegion(QStringLiteral("radarLegend%1").arg(index),
                    QStringLiteral("%1. %2").arg(index + 1).arg(axes[size_t(index)].name),
                    bodyFont, panelPadding + column * (legendWidth + sectionGap), y, legendWidth);
                result.texts.push_back(legend);
                rowBottom = std::max(rowBottom, legend.rect.bottom());
            }
            y = rowBottom + textGap;
        }
        y -= textGap;
    }
    result.radar = QRectF(0, radarTop, width, y + sectionGap - radarTop);
    result.requiredHeight = int(std::ceil(result.radar.bottom()));
    return result;
}

bool QtProfileAnalytics::hasHeightForWidth() const { return true; }
int QtProfileAnalytics::heightForWidth(int availableWidth) const { return layoutMetrics(availableWidth).requiredHeight; }
QSize QtProfileAnalytics::sizeHint() const { return QSize(640, heightForWidth(640)); }
QSize QtProfileAnalytics::minimumSizeHint() const { return QSize(0, 0); }

void QtProfileAnalytics::updateToolbarGeometry() {
    if (!axisCount_ || !axisLabel_ || !toolbar_) return;
    // Keep the maximum unbounded: the window's scale pass must not capture an
    // already measured font-dependent width as an immutable base width.
    axisCount_->setMinimumWidth(spinWidth(axisCount_));
    const bool stacked = axisLabel_->sizeHint().width() + axisCount_->sizeHint().expandedTo(axisCount_->minimumSize()).width() +
        toolbar_->spacing() > width();
    const auto direction = stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    if (toolbar_->direction() != direction) toolbar_->setDirection(direction);
}

void QtProfileAnalytics::updateChartGeometry() {
    updateToolbarGeometry();
    const int required = heightForWidth(std::max(1, width()));
    if (minimumHeight() != required) setMinimumHeight(required);
    updateGeometry();
    update();
}

void QtProfileAnalytics::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateChartGeometry();
}

void QtProfileAnalytics::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (event->oldSize().width() != event->size().width()) updateChartGeometry();
}

void QtProfileAnalytics::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    const auto geometry = layoutMetrics(width());
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor text = palette().color(QPalette::WindowText);
    const QColor muted = palette().color(QPalette::Disabled, QPalette::Text);
    const QColor track = palette().color(QPalette::AlternateBase);
    const QColor surface = palette().color(QPalette::Base);
    const QColor accent = palette().color(QPalette::Highlight);
    const QColor accentFill(accent.red(), accent.green(), accent.blue(), 42);
    painter.setPen(Qt::NoPen);
    painter.setBrush(surface);
    for (const auto& rect : {geometry.categories, geometry.topSkills, geometry.radar})
        painter.drawRoundedRect(rect, 8, 8);
    for (const auto& bar : geometry.bars) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(track);
        painter.drawRoundedRect(bar.track, 4, 4);
        painter.setBrush(bar.highlight ? accent : accentFill);
        painter.drawRoundedRect(QRectF(bar.track.left(), bar.track.top(),
            bar.track.width() * bar.fraction, bar.track.height()), 4, 4);
    }
    if (geometry.radarAxes >= 3) {
        for (int ring = 1; ring <= 4; ++ring) {
            QPolygonF polygon;
            for (const auto& point : geometry.radarEnds)
                polygon << geometry.radarCenter + (point - geometry.radarCenter) * (ring / 4.0);
            painter.setPen(QPen(track, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(polygon);
        }
        for (const auto& point : geometry.radarEnds) painter.drawLine(geometry.radarCenter, point);
        for (const auto& line : geometry.radarLabelLines) painter.drawLine(line);
        painter.setPen(QPen(accent, 2));
        painter.setBrush(accentFill);
        painter.drawPolygon(geometry.radarPoints);
        painter.setBrush(accent);
        for (const auto& point : geometry.radarPoints) painter.drawEllipse(point, 3, 3);
    }
    for (const auto& region : geometry.texts) {
        const bool mutedText = region.role.startsWith(QStringLiteral("categoryValue")) ||
            region.role.startsWith(QStringLiteral("topValue")) || region.role.endsWith(QStringLiteral("Empty"));
        painter.setPen(mutedText ? muted : text);
        drawTextRegion(painter, region);
    }
}
