#include <QtWidgets>
#include "QtTheme.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
enum class QtShortcutRunState { Unknown, Running, NotRunning };
constexpr int Shortcuts = 11;
QString q(const std::string& text) { return QString::fromUtf8(text); }
struct Entry { std::string id, label, path; };
std::vector<Entry> LoadShortcutsData(const QString&) {
    const QString label = QString::fromUtf8("Очень длинное название & ярлыка безопасного файла для проверки подписи и полного контекста");
    return {{"running", (label + " A").toUtf8().toStdString(), "synthetic-running"},
        {"stopped", (label + " B").toUtf8().toStdString(), "synthetic-not-running"},
        {"unknown", (label + " C").toUtf8().toStdString(), "synthetic-document"}};
}
std::vector<QtShortcutRunState> qtShortcutRunStates(const std::vector<Entry>&) {
    return {QtShortcutRunState::Running, QtShortcutRunState::NotRunning, QtShortcutRunState::Unknown};
}
static QIcon qtShortcutStatusIcon(QtShortcutRunState state) {
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    const QColor color = state == QtShortcutRunState::Running ? QColor(70, 190, 105) :
        state == QtShortcutRunState::NotRunning ? QColor(220, 90, 90) : QColor(135, 140, 150);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QRect(2, 2, 10, 10));
    return QIcon(pixmap);
}

    class ShortcutMenu final : public QMenu {
    public:
        using QMenu::QMenu;
        QSize sizeHint() const override {
            const QScopedValueRollback<bool> measuring(painting_, false);
            return QMenu::sizeHint();
        }
    protected:
        void initStyleOption(QStyleOptionMenuItem* option, const QAction* action) const override {
            QMenu::initStyleOption(option, action);
            // QMenu adds the shared tab width after item sizing. Fusion also
            // adds its current reserved width, so do not count it twice in
            // geometry passes, including those outside sizeHint().
            if (!painting_ && option) option->reservedShortcutWidth = 0;
        }
        void paintEvent(QPaintEvent* event) override {
            sizeHint(); // Refresh dirty geometry under the sizing-only rule.
            const QScopedValueRollback<bool> painting(painting_, true);
            QMenu::paintEvent(event); // Painting retains the complete tab column.
        }
    private:
        mutable bool painting_ = false;
    };

class Harness final : public QWidget {
public:
    Harness() {
        navigation_ = new QListWidget(this);
        shortcutMenu_ = new ShortcutMenu(this);
        shortcutMenu_->setObjectName("quickShortcutMenu");
        shortcutMenu_->setToolTipsVisible(true);
    connect(shortcutMenu_, &QMenu::aboutToShow, this, [this] {
        shortcutMenu_->clear();
        const auto entries = LoadShortcutsData(workspace_.directory);
        const auto states = qtShortcutRunStates(entries);
        const auto* menuScreen = shortcutMenu_->screen();
        const int menuWidth = std::max(1, std::min(640,
            menuScreen ? menuScreen->availableGeometry().width() - 32 : 640));
        shortcutMenu_->setMaximumWidth(menuWidth);
        const QFontMetrics menuMetrics(shortcutMenu_->font());
        struct MenuLabel { QAction* action; QString label; QString status; };
        std::vector<MenuLabel> labels;
        int widestStatus = 0;
        for (size_t index = 0; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            const auto state = states[index];
            const QString status = state == QtShortcutRunState::Running ? QString::fromUtf8("Запущена") :
                state == QtShortcutRunState::NotRunning ? QString::fromUtf8("Не запущена") : QString::fromUtf8("Статус недоступен");
            const QString fullLabel = q(entry.label);
            widestStatus = std::max(widestStatus, menuMetrics.horizontalAdvance(status));
            auto* action = shortcutMenu_->addAction(qtShortcutStatusIcon(state), QStringLiteral("…\t") + status);
            labels.push_back({action, fullLabel, status});
            action->setObjectName("quickShortcutAction");
            action->setData(q(entry.id));
            action->setProperty("shortcutRunState", int(state));
            const auto path = q(entry.path);
            const auto context = QString::fromUtf8("%1\n%2\n%3").arg(fullLabel, status, QDir::toNativeSeparators(path));
            action->setToolTip(context); action->setStatusTip(context);
            connect(action, &QAction::triggered, this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        }
        if (entries.empty()) {
            const auto guidance = QString::fromUtf8("Сохранённых ярлыков пока нет");
            auto* empty = shortcutMenu_->addAction(QStringLiteral("…"));
            empty->setObjectName("quickShortcutEmpty"); empty->setEnabled(false);
            empty->setToolTip(guidance); labels.push_back({empty, guidance, {}});
        }
        if (!shortcutMenu_->actions().isEmpty()) shortcutMenu_->addSeparator();
        const auto manageTitle = QString::fromUtf8("Управление ярлыками…");
        auto* manage = shortcutMenu_->addAction(QStringLiteral("…"));
        manage->setObjectName("manageShortcutsAction");
        manage->setToolTip(manageTitle); manage->setStatusTip(manageTitle);
        labels.push_back({manage, manageTitle, {}});
        connect(manage, &QAction::triggered, this, [this] {
            navigation_->setCurrentRow(Shortcuts);
        });
        // QMenu allocates shared label/status columns. Its real styled size
        // includes icon/check gutters, tab spacing, frame and popup margins.
        const int ellipsisWidth = menuMetrics.horizontalAdvance(QStringLiteral("…"));
        const int styleOverhead = std::max(0, shortcutMenu_->sizeHint().width() - ellipsisWidth - widestStatus);
        int labelWidth = std::max(ellipsisWidth, menuWidth - widestStatus - styleOverhead);
        const auto applyLabels = [&] {
            for (const auto& row : labels) {
                auto visibleLabel = menuMetrics.elidedText(row.label, Qt::ElideRight, labelWidth);
                visibleLabel.replace('&', QStringLiteral("&&"));
                row.action->setText(row.status.isEmpty() ? visibleLabel : visibleLabel + '\t' + row.status);
            }
        };
        applyLabels();
        while (shortcutMenu_->sizeHint().width() > menuWidth && labelWidth > ellipsisWidth) {
            labelWidth = std::max(ellipsisWidth, labelWidth - (shortcutMenu_->sizeHint().width() - menuWidth));
            applyLabels();
        }
    });

    }
    QMenu* menu() const { return shortcutMenu_; }
private:
    struct { QString directory; } workspace_;
    QMenu* shortcutMenu_ = nullptr;
    QListWidget* navigation_ = nullptr;
};
int main(int argc, char** argv) {
    QApplication app(argc, argv); ApplyQtTheme(app);
    app.setProperty("forgeBasePointSize", 10.0);
    Harness harness; QMenu* menu = harness.menu();
    bool okay = true; int pass = 0;
    for (const int percent : {100, 200, 100, 200}) {
        ++pass; auto font = app.font(); font.setPointSizeF(10.0 * percent / 100.0); app.setFont(font);
        ApplyQtLayoutMetrics(app, 100, 4, 4.0, 4.0, 6.0, 4.0, 8.0, 0.0, 8.0, 6.0);
        QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
        menu->ensurePolished();
        const auto natural = menu->sizeHint();
        std::cout << "pass=" << pass << " scale=" << percent << " font=" << menu->font().pointSizeF()
            << " natural=" << natural.width() << 'x' << natural.height() << " maximum=" << menu->maximumWidth()
            << " minimum=" << menu->minimumWidth() << '\n';
        okay &= natural.width() <= menu->maximumWidth();
        QPixmap image(natural); image.fill(Qt::transparent); menu->resize(natural); menu->render(&image);
        okay &= menu->sizeHint().width() <= menu->maximumWidth();
        if (!image.save(QString("prototype-%1-%2.png").arg(pass).arg(percent))) okay = false;
        int shortcuts = 0;
        for (auto* action : menu->actions()) {
            if (action->isSeparator()) continue;
            const auto rect = menu->actionGeometry(action);
            const auto label = action->text().section('\t', 0, 0).replace("&&", "&");
            const auto status = action->text().section('\t', 1);
            const QFontMetrics metrics(action->font().resolve(menu->font()));
            const int iconWidth = action->icon().isNull() ? 0 : menu->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, menu);
            const int glyphWidth = metrics.horizontalAdvance(label) + menu->fontMetrics().horizontalAdvance(status) + iconWidth;
            const bool fits = rect.width() >= glyphWidth && rect.height() >= metrics.height()
                && rect.width() <= menu->maximumWidth() && menu->rect().contains(rect);
            okay &= fits;
            std::cout << "  action=" << action->objectName().toStdString() << " rect=" << rect.width() << 'x' << rect.height()
                << " labelGlyph=" << metrics.horizontalAdvance(label) << " statusGlyph=" << menu->fontMetrics().horizontalAdvance(status)
                << " glyphs=" << glyphWidth << " fits=" << fits << '\n';
            if (action->objectName() == "quickShortcutAction") {
                ++shortcuts;
                const QString expected = action->data().toString() == "running" ? QString::fromUtf8("Запущена") :
                    action->data().toString() == "stopped" ? QString::fromUtf8("Не запущена") : QString::fromUtf8("Статус недоступен");
                okay &= status == expected && !action->icon().isNull() && action->toolTip().contains(expected) && action->toolTip().contains(" & ");
            }
        }
        okay &= shortcuts == 3;
    }
    std::cout << "PrototypeOnly=" << (okay ? "PASS" : "FAIL") << " states=4 actions=12 no action triggered no product targets\n";
    return okay ? 0 : 1;
}