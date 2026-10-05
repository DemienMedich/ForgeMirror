#pragma once

#include <QtWidgets>
#include <algorithm>
#include <cmath>

// Wrapped nested form rows can advertise a height-for-width smaller than the
// form's intrinsic height. Never let the scroll viewport compress that body
// and hide its last controls; wider/narrower layout still recalculates normally.
class QtDialogFormBody final : public QWidget {
public:
    using QWidget::QWidget;
    int heightForWidth(int width) const override {
        // Simple spanning-row forms reserve wrapped label heights themselves.
        // Their unconstrained sizeHint can retain a taller previous layout.
        if (property("useMeasuredFormHeight").toBool())
            return QWidget::heightForWidth(width);
        return std::max(QWidget::heightForWidth(width), sizeHint().height());
    }
};

// A form that can grow vertically without moving its commit/cancel commands
// outside the screen. The caller owns the field hierarchy and footer contents.
class QtScrollableDialog : public QDialog {
public:
    explicit QtScrollableDialog(QWidget* parent = nullptr,
        QSize preferredSize = QSize(640, 620), QSize minimumSize = QSize(420, 300))
        : QDialog(parent), requestedMinimum_(minimumSize) {
        auto* outer = new QVBoxLayout(this);
        auto* heading = new QFrame(this);
        heading->setObjectName("dialogHeadingSurface");
        auto* headingLayout = new QHBoxLayout(heading);
        headingLayout->setContentsMargins(scaledMetric(12), scaledMetric(8), scaledMetric(12), scaledMetric(8));
        auto* title = new QLabel(heading);
        title->setObjectName("dialogHeading");
        title->setTextFormat(Qt::PlainText);
        title->setWordWrap(true);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        headingLayout->addWidget(title);
        const auto updateHeading = [heading, title](const QString& text) {
            title->setText(text);
            heading->setVisible(!text.trimmed().isEmpty());
        };
        connect(this, &QWidget::windowTitleChanged, this, updateHeading);
        updateHeading(windowTitle());
        outer->addWidget(heading);
        scroll_ = new QScrollArea(this);
        scroll_->setObjectName("dialogContentScrollArea");
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        body_ = new QtDialogFormBody(scroll_);
        body_->setObjectName("dialogFormContent");
        body_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        form_ = new QFormLayout(body_);
        form_->setContentsMargins(0, 0, 0, 0);
        form_->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        scroll_->setWidget(body_);
        outer->addWidget(scroll_, 1);
        auto* footer = new QWidget(this);
        footer->setObjectName("dialogFooter");
        footer->setAttribute(Qt::WA_StyledBackground, true);
        footerLayout_ = new QVBoxLayout(footer);
        footerLayout_->setContentsMargins(scaledMetric(8), scaledMetric(8), scaledMetric(8), scaledMetric(8));
        outer->addWidget(footer);
        setMinimumSize(minimumSize);
        resize(preferredSize);
    }

    QFormLayout* formLayout() const { return form_; }
    QWidget* bodyWidget() const { return body_; }
    QScrollArea* scrollArea() const { return scroll_; }
    QVBoxLayout* footerLayout() const { return footerLayout_; }

    int scaledMetric(int base) const {
        const double savedBase = qApp->property("forgeBasePointSize").toDouble();
        const double current = font().pointSizeF();
        const double scale = savedBase > 0.0 && current > 0.0
            ? std::clamp(current / savedBase, 0.9, 2.0) : 1.0;
        return std::max(1, int(std::lround(base * scale)));
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        // Secondary commands deliberately do not take over the default Save.
        // Return on a focused command must activate that command, rather than
        // propagating through QDialog to an unrelated default operation.
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier)) {
            auto* button = qobject_cast<QPushButton*>(focusWidget());
            if (button && button->isVisible() && button->isEnabled()) {
                event->accept();
                button->click();
                return;
            }
        }
        QDialog::keyPressEvent(event);
    }

    void showEvent(QShowEvent* event) override {
        for (const auto* layout : body_->findChildren<QFormLayout*>()) {
            for (int row = 0; row < layout->rowCount(); ++row) {
                const auto* item = layout->itemAt(row, QFormLayout::LabelRole);
                if (auto* label = item ? qobject_cast<QLabel*>(item->widget()) : nullptr)
                    label->setWordWrap(true);
            }
        }
        QDialog::showEvent(event);
        boundToScreen();
    }

private:
    void boundToScreen() {
        auto* screen = windowHandle() ? windowHandle()->screen() : nullptr;
        if (!screen && parentWidget()) screen = parentWidget()->screen();
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (!screen) return;
        const QRect available = screen->availableGeometry();
        const QSize frameExtra(std::max(0, frameGeometry().width() - width()),
            std::max(0, frameGeometry().height() - height()));
        const QSize limit(std::max(1, available.width() - frameExtra.width() - 16),
            std::max(1, available.height() - frameExtra.height() - 16));
        setMinimumSize(requestedMinimum_.boundedTo(limit));
        setMaximumSize(limit);
        resize(size().boundedTo(limit));
        const QRect frame = frameGeometry();
        const int x = std::clamp(frame.left(), available.left(),
            std::max(available.left(), available.right() - frame.width() + 1));
        const int y = std::clamp(frame.top(), available.top(),
            std::max(available.top(), available.bottom() - frame.height() + 1));
        move(pos() + QPoint(x - frame.left(), y - frame.top()));
    }

    QSize requestedMinimum_;
    QScrollArea* scroll_ = nullptr;
    QWidget* body_ = nullptr;
    QFormLayout* form_ = nullptr;
    QVBoxLayout* footerLayout_ = nullptr;
};

// Preset selectors and their commands share a row when there is enough space;
// narrow forms put them below one another without clipping button text.
class QtDialogAdaptiveRow : public QWidget {
public:
    explicit QtDialogAdaptiveRow(QWidget* parent = nullptr, int spacing = 8)
        : QWidget(parent), row_(new QBoxLayout(QBoxLayout::LeftToRight, this)) {
        row_->setContentsMargins(0, 0, 0, 0);
        row_->setSpacing(spacing);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void addWidget(QWidget* widget, int stretch = 0) { row_->addWidget(widget, stretch); }

    QSize minimumSizeHint() const override {
        int width = 0;
        for (int i = 0; i < row_->count(); ++i)
            width = std::max(width, row_->itemAt(i)->minimumSize().width());
        return QSize(width, QWidget::minimumSizeHint().height());
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        int wantedWidth = std::max(0, row_->count() - 1) * row_->spacing();
        for (int i = 0; i < row_->count(); ++i)
            wantedWidth += row_->itemAt(i)->sizeHint().width();
        const auto direction = wantedWidth <= contentsRect().width()
            ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
        if (row_->direction() != direction) {
            row_->setDirection(direction);
            updateGeometry();
        }
    }

private:
    QBoxLayout* row_;
};

// Secondary commands wrap individually: two commands that fit stay together,
// rather than turning the whole group into a vertical stack at one breakpoint.
class QtDialogFlowLayout final : public QLayout {
public:
    explicit QtDialogFlowLayout(QWidget* parent = nullptr,
        int horizontalSpacing = 8, int verticalSpacing = 4)
        : QLayout(parent), horizontalSpacing_(std::max(0, horizontalSpacing)),
          verticalSpacing_(std::max(0, verticalSpacing)) {
        setContentsMargins(0, 0, 0, 0);
    }

    ~QtDialogFlowLayout() override {
        while (auto* item = takeAt(0)) delete item;
    }

    void addItem(QLayoutItem* item) override {
        items_.append(item);
        invalidate();
    }
    int count() const override { return items_.size(); }
    QLayoutItem* itemAt(int index) const override { return items_.value(index, nullptr); }
    QLayoutItem* takeAt(int index) override {
        if (index < 0 || index >= items_.size()) return nullptr;
        auto* item = items_.takeAt(index);
        invalidate();
        return item;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        return doLayout(QRect(0, 0, width, 0), false);
    }
    QSize sizeHint() const override {
        // Prefer one row, but do not make its total width the minimum width.
        // The enclosing form can then measure the actual wrapped height.
        int width = 0;
        int height = 0;
        int visibleItems = 0;
        for (const auto* item : items_) {
            if (!participates(item)) continue;
            const auto hint = intrinsicSize(item);
            width += hint.width() + (visibleItems++ ? horizontalSpacing_ : 0);
            height = std::max(height, hint.height());
        }
        const auto margins = contentsMargins();
        return QSize(width + margins.left() + margins.right(),
            height + margins.top() + margins.bottom());
    }
    QSize minimumSize() const override {
        QSize result(0, 0);
        for (const auto* item : items_) {
            if (participates(item)) result = result.expandedTo(intrinsicSize(item));
        }
        const auto margins = contentsMargins();
        return result + QSize(margins.left() + margins.right(),
            margins.top() + margins.bottom());
    }
    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect);
        doLayout(rect, true);
    }

private:
    static bool participates(const QLayoutItem* item) {
        return !item->isEmpty() && (!item->widget() || !item->widget()->isHidden());
    }
    static QSize intrinsicSize(const QLayoutItem* item) {
        return item->sizeHint().expandedTo(item->minimumSize());
    }
    int doLayout(const QRect& rect, bool place) const {
        const auto margins = contentsMargins();
        const int availableWidth = std::max(0,
            rect.width() - margins.left() - margins.right());
        const int left = rect.x() + margins.left();
        const int top = rect.y() + margins.top();
        int x = left;
        int y = top;
        int rowWidth = 0;
        int rowHeight = 0;
        bool rowHasItems = false;
        for (auto* item : items_) {
            if (!participates(item)) continue;
            const QSize hint = intrinsicSize(item);
            const int gap = rowHasItems ? horizontalSpacing_ : 0;
            if (rowHasItems && rowWidth + gap + hint.width() > availableWidth) {
                y += rowHeight + verticalSpacing_;
                x = left;
                rowWidth = 0;
                rowHeight = 0;
                rowHasItems = false;
            }
            if (rowHasItems) {
                x += horizontalSpacing_;
                rowWidth += horizontalSpacing_;
            }
            const int height = item->hasHeightForWidth()
                ? std::max(hint.height(), item->heightForWidth(hint.width())) : hint.height();
            if (place) item->setGeometry(QRect(x, y, hint.width(), height));
            x += hint.width();
            rowWidth += hint.width();
            rowHeight = std::max(rowHeight, height);
            rowHasItems = true;
        }
        return y - rect.y() + rowHeight + margins.bottom();
    }

    QList<QLayoutItem*> items_;
    int horizontalSpacing_;
    int verticalSpacing_;
};

class QtDialogFlowRow final : public QWidget {
public:
    explicit QtDialogFlowRow(QWidget* parent = nullptr,
        int horizontalSpacing = 8, int verticalSpacing = 4)
        : QWidget(parent), flow_(new QtDialogFlowLayout(this, horizontalSpacing, verticalSpacing)) {
        auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }

    void addWidget(QWidget* widget) { flow_->addWidget(widget); }
    QSize minimumSizeHint() const override { return flow_->minimumSize(); }
    int heightForWidth(int width) const override { return flow_->heightForWidth(width); }

private:
    QtDialogFlowLayout* flow_;
};
