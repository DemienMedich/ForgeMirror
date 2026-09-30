#pragma once

#include "QtActionIcons.h"

#include <QAbstractAnimation>
#include <QEvent>
#include <QEasingCurve>
#include <QHideEvent>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStyleOptionToolButton>
#include <QToolButton>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

// A normal tool button whose disclosure chevron alone follows checked state.
// Content visibility and accessibility remain the caller's immediate toggled
// handling; this class never changes layout, focus, or the button's hit area.
class QtDisclosureButton final : public QToolButton {
public:
    using MotionPolicy = std::function<bool()>;

    explicit QtDisclosureButton(QWidget* parent = nullptr, MotionPolicy motionPolicy = {})
        : QToolButton(parent), motionAllowed_(std::move(motionPolicy)),
          indicatorAnimation_(new QVariantAnimation(this)) {
        setCheckable(true);
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        // Reserve the ordinary icon slot once. Per-frame drawing only replaces
        // the style option's icon, so it does not invalidate native size hints.
        setIcon(CreateQtActionIcon(QtActionIcon::ChevronRight, palette()));
        indicatorAnimation_->setObjectName("disclosureIndicatorAnimation");
        connect(this, &QToolButton::toggled, this, [this](bool expanded) {
            retargetIndicator(expanded);
        });
        connect(indicatorAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            if (!motionAllowed() || !isVisible() || !isEnabled()) {
                snapToState();
                return;
            }
            setIndicatorAngle(value.toReal());
        });
        connect(indicatorAnimation_, &QAbstractAnimation::finished, this, [this] {
            setIndicatorAngle(isChecked() ? 90.0 : 0.0);
        });
    }

    // Replacing a policy must not replay a transition. The owning window also
    // calls snapToState() after applying its persisted display preferences.
    void setMotionPolicy(MotionPolicy policy) {
        motionAllowed_ = std::move(policy);
        snapToState();
    }

    void snapToState() {
        indicatorAnimation_->stop();
        setIndicatorAngle(isChecked() ? 90.0 : 0.0);
    }

    qreal indicatorAngle() const { return indicatorAngle_; }
    QVariantAnimation* indicatorAnimation() const { return indicatorAnimation_; }

protected:
    void initStyleOption(QStyleOptionToolButton* option) const override {
        if (!option) return;
        QToolButton::initStyleOption(option);
        const auto paletteKey = option->palette.cacheKey();
        if (cachedIcon_.isNull() || cachedPaletteKey_ != paletteKey ||
            !qFuzzyCompare(cachedAngle_ + 1.0, indicatorAngle_ + 1.0)) {
            cachedIcon_ = CreateQtDisclosureIcon(indicatorAngle_, option->palette);
            cachedPaletteKey_ = paletteKey;
            cachedAngle_ = indicatorAngle_;
        }
        option->icon = cachedIcon_;
    }

    void showEvent(QShowEvent* event) override {
        QToolButton::showEvent(event);
        snapToState();
    }

    void hideEvent(QHideEvent* event) override {
        snapToState();
        QToolButton::hideEvent(event);
    }

    void changeEvent(QEvent* event) override {
        QToolButton::changeEvent(event);
        if (event->type() == QEvent::EnabledChange && !isEnabled()) snapToState();
    }

private:
    bool motionAllowed() const { return motionAllowed_ && motionAllowed_(); }

    void setIndicatorAngle(qreal angle) {
        angle = std::clamp(angle, qreal(0.0), qreal(90.0));
        if (qFuzzyCompare(indicatorAngle_ + 1.0, angle + 1.0)) return;
        indicatorAngle_ = angle;
        update();
    }

    void retargetIndicator(bool expanded) {
        const qreal target = expanded ? 90.0 : 0.0;
        const qreal current = indicatorAngle_;
        indicatorAnimation_->stop();
        if (!motionAllowed() || !isVisible() || !isEnabled() ||
            qFuzzyCompare(current + 1.0, target + 1.0)) {
            setIndicatorAngle(target);
            return;
        }

        const int fullDuration = expanded ? 180 : 150;
        const int remainingDuration = std::max(1, int(std::lround(fullDuration * std::abs(target - current) / 90.0)));
        // QVariantAnimation emits valueChanged while endpoints are assigned.
        // Preserve the current visual angle until both endpoints are ready.
        {
            const QSignalBlocker block(indicatorAnimation_);
            indicatorAnimation_->setDuration(remainingDuration);
            indicatorAnimation_->setEasingCurve(expanded ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
            indicatorAnimation_->setStartValue(current);
            indicatorAnimation_->setEndValue(target);
            indicatorAnimation_->setCurrentTime(0);
        }
        indicatorAnimation_->start();
    }

    MotionPolicy motionAllowed_;
    QVariantAnimation* indicatorAnimation_;
    qreal indicatorAngle_ = 0.0;
    mutable QIcon cachedIcon_;
    mutable qint64 cachedPaletteKey_ = 0;
    mutable qreal cachedAngle_ = -1.0;
};
