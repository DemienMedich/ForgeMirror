#pragma once

#include <QPainter>
#include <QApplication>
#include <QStyle>
#include <QWidget>
#include <QStyledItemDelegate>

// Keep the complete display text for accessibility, copying and exports;
// render task attention labels as separate, quiet chips in the title cell.
class QtTaskTitleDelegate final : public QStyledItemDelegate {
public:
    static constexpr int LabelsRole = Qt::UserRole + 42;
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const auto labels = index.data(LabelsRole).toStringList();
        if (labels.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem content(option);
        initStyleOption(&content, index);
        const auto suffix = QStringLiteral("  [%1]").arg(labels.join(' '));
        if (content.text.endsWith(suffix)) content.text.chop(suffix.size());
        const QString title = content.text;
        content.text.clear();
        auto* style = content.widget ? content.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &content, painter, content.widget);

        const bool selected = content.state.testFlag(QStyle::State_Selected);
        const QColor text = content.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        QFont labelFont(content.font);
        labelFont.setWeight(QFont::Normal);
        const QFontMetrics metrics(labelFont);
        const int padding = qMax(4, metrics.height() / 4);
        const int gap = qMax(3, padding / 2);
        int labelsWidth = 0;
        for (const auto& label : labels) labelsWidth += metrics.horizontalAdvance(label) + padding * 2 + gap;
        const QRect bounds = content.rect.adjusted(padding, 0, -padding, 0);
        // At extremely narrow widths, retain the ordinary elided complete text.
        if (labelsWidth + metrics.horizontalAdvance(QStringLiteral("…")) + padding > bounds.width()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        painter->save();
        painter->setClipRect(content.rect);
        painter->setFont(content.font);
        painter->setPen(text);
        const QRect titleRect(bounds.left(), bounds.top(), bounds.width() - labelsWidth - padding, bounds.height());
        painter->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(content.font).elidedText(title, Qt::ElideRight, titleRect.width()));
        painter->setFont(labelFont);
        painter->setRenderHint(QPainter::Antialiasing);
        int x = bounds.right() - labelsWidth + 1;
        QColor fill = content.palette.color(selected ? QPalette::HighlightedText : QPalette::Highlight);
        fill.setAlpha(selected ? 32 : 38);
        for (const auto& label : labels) {
            const int width = metrics.horizontalAdvance(label) + padding * 2;
            const QRect chip(x, bounds.center().y() - (metrics.height() + 2) / 2, width, metrics.height() + 2);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(chip, padding, padding);
            painter->setPen(text);
            painter->drawText(chip, Qt::AlignCenter, label);
            x += width + gap;
        }
        painter->restore();
    }
};
