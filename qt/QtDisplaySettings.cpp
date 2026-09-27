#include "QtDisplaySettings.h"
#include "QtDeadlineAgent.h"
#include "QtTheme.h"
#include <QtWidgets>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {
QString pathFor(const std::filesystem::path& directory) { return QString::fromUtf8((directory / "meta/ui.ini").u8string()); }
const std::array<QString, 18>& legacyBackgroundNames() {
    static const std::array<QString, 18> names = {
        QString::fromUtf8("Главное меню"), QString::fromUtf8("Профиль"), QString::fromUtf8("Навыки"),
        QString::fromUtf8("Профессии"), QString::fromUtf8("Пайплайн"), QString::fromUtf8("Правила"),
        QString::fromUtf8("Настройки"), QString::fromUtf8("3D просмотр"), QString::fromUtf8("3D настройки"),
        QString::fromUtf8("Статистика"), QString::fromUtf8("Логи"), QString::fromUtf8("Задачи"),
        QString::fromUtf8("Проекты"), QString::fromUtf8("Помодоро"), QString::fromUtf8("Ярлыки"),
        QString::fromUtf8("О программе"), QString::fromUtf8("Баннер"), QString::fromUtf8("Хранилище")};
    return names;
}
const std::array<int, 18>& legacyBackgroundPageMap() {
    static const std::array<int, 18> map = {1, 11, 12, 2, 4, 3, 9, -1, 13, 5, 17, 14, 16, 6, 7, 8, 10, 9};
    return map;
}
QString normalizeBackgroundPath(QString value) {
    value = QDir::fromNativeSeparators(value.trimmed());
    if (!value.startsWith(QStringLiteral("ui/backgrounds/"), Qt::CaseSensitive) ||
        value.contains("..") || value.contains(':') || value.contains('\\') || value.contains('\r') || value.contains('\n') ||
        value.mid(QStringLiteral("ui/backgrounds/").size()).isEmpty() || value.mid(QStringLiteral("ui/backgrounds/").size()).contains('/') ||
        QFileInfo(value).suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) return {};
    return value;
}
int normalizedScale(int value) { for (int allowed : {90, 100, 110, 125}) if (value == allowed) return value; return 100; }
int nearestValue(int value, std::initializer_list<int> values) {
    return *std::min_element(values.begin(), values.end(), [value](int a, int b) {
        return std::abs(a - value) < std::abs(b - value);
    });
}
QString presetPath(const std::filesystem::path& directory) {
    return QString::fromUtf8((directory / "meta/qt-layout-presets.json").u8string());
}
QString legacyPresetDirectory(const std::filesystem::path& directory) {
    return QString::fromUtf8((directory / "meta/ui-presets").u8string());
}
bool readQtPresets(const std::filesystem::path& directory, QJsonArray* presets, QString* error = nullptr) {
    if (error) error->clear();
    if (!presets) return false;
    *presets = QJsonArray{};
    const QString filePath = presetPath(directory);
    const QFileInfo fileInfo(filePath);
    if (fileInfo.isSymLink() || QFileInfo(fileInfo.absolutePath()).isSymLink()) {
        if (error) *error = QString::fromUtf8("Путь к пресетам является ссылкой.");
        return false;
    }
    if (!fileInfo.exists()) return true;
    if (!fileInfo.isFile() || fileInfo.size() < 0 || fileInfo.size() > 65536) {
        if (error) *error = QString::fromUtf8("Файл пресетов повреждён или слишком велик.");
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QString::fromUtf8("Не удалось прочитать пресеты.");
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value("version").toInt() != 1 || !document.object().value("presets").isArray()) {
        if (error) *error = QString::fromUtf8("Файл пресетов повреждён; он оставлен без изменений.");
        return false;
    }
    *presets = document.object().value("presets").toArray();
    if (presets->size() > 20) {
        if (error) *error = QString::fromUtf8("В файле больше 20 пресетов.");
        *presets = QJsonArray{};
        return false;
    }
    QSet<QString> names;
    for (const auto& value : *presets) {
        if (!value.isObject()) { if (error) *error = QString::fromUtf8("В списке есть некорректный пресет."); *presets = QJsonArray{}; return false; }
        const auto name = value.toObject().value("name").toString();
        if (name.trimmed().isEmpty() || name.size() > 40 || names.contains(name)) {
            if (error) *error = QString::fromUtf8("Имена пресетов некорректны или повторяются.");
            *presets = QJsonArray{}; return false;
        }
        names.insert(name);
    }
    return true;
}
QString safePresetName(QString name) {
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_ -]")), QString());
    name = name.simplified();
    if (name.size() > 40) name.truncate(40);
    return name.trimmed();
}
QJsonObject presetObject(const QtLayoutPreset& preset) {
    QJsonArray backgrounds;
    for (const auto& path : preset.windowBackgrounds) backgrounds.append(normalizeBackgroundPath(path));
    return {{"name", preset.name}, {"scalePercent", normalizedScale(preset.scalePercent)},
        {"spacingPercent", nearestValue(preset.spacingPercent, {80, 90, 100, 110, 120})},
        {"cornerRadius", nearestValue(preset.cornerRadius, {0, 4, 8, 12})},
        {"compactRows", preset.compactRows}, {"fullscreen", preset.fullscreen}, {"decorated", preset.decorated},
        {"windowBackgrounds", backgrounds}, {"backgroundAlpha", std::clamp(preset.backgroundAlpha, 0.0, 1.0)},
        {"backgroundTiled", preset.backgroundTiled}, {"backgroundTileScale", std::clamp(preset.backgroundTileScale, 0.25, 3.0)}};
}
QtLayoutPreset presetFromObject(const QJsonObject& object) {
    QtLayoutPreset preset;
    preset.name = object.value("name").toString();
    preset.scalePercent = normalizedScale(object.value("scalePercent").toInt(100));
    preset.spacingPercent = nearestValue(object.value("spacingPercent").toInt(100), {80, 90, 100, 110, 120});
    preset.cornerRadius = nearestValue(object.value("cornerRadius").toInt(4), {0, 4, 8, 12});
    preset.compactRows = object.value("compactRows").toBool();
    preset.fullscreen = object.value("fullscreen").toBool();
    preset.decorated = object.value("decorated").toBool(true);
    const auto backgrounds = object.value("windowBackgrounds").toArray();
    for (int i = 0; i < int(preset.windowBackgrounds.size()) && i < backgrounds.size(); ++i)
        preset.windowBackgrounds[size_t(i)] = normalizeBackgroundPath(backgrounds.at(i).toString());
    preset.backgroundAlpha = std::clamp(object.value("backgroundAlpha").toDouble(0.25), 0.0, 1.0);
    preset.backgroundTiled = object.value("backgroundTiled").toBool();
    preset.backgroundTileScale = std::clamp(object.value("backgroundTileScale").toDouble(1.0), 0.25, 3.0);
    return preset;
}
bool saveQtPresets(const std::filesystem::path& directory, const QJsonArray& presets, QString* error) {
    const QString filePath = presetPath(directory);
    const QFileInfo fileInfo(filePath);
    if (fileInfo.isSymLink() || QFileInfo(fileInfo.absolutePath()).isSymLink()) {
        if (error) *error = QString::fromUtf8("Путь к пресетам является ссылкой.");
        return false;
    }
    QDir().mkpath(fileInfo.absolutePath());
    QSaveFile file(filePath);
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"presets", presets}}).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QString::fromUtf8("Не удалось атомарно сохранить пресеты компоновки.");
        return false;
    }
    if (error) error->clear();
    return true;
}
bool readLegacyLayoutPreset(const std::filesystem::path& directory, const QString& name, QtLayoutPreset* preset) {
    if (!preset) return false;
    const QString legacyDirectory = legacyPresetDirectory(directory);
    if (QFileInfo(legacyDirectory).isSymLink()) return false;
    const QString path = QDir(legacyDirectory).filePath(name + ".ini");
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || info.size() < 0 || info.size() > 1024 * 1024) return false;
    QSettings source(path, QSettings::IniFormat);
    if (source.status() != QSettings::NoError) return false;
    QtLayoutPreset result; result.name = name;
    source.beginGroup("style");
    bool ok = false;
    const double fontScale = source.value("fontScale", 1.0).toDouble(&ok);
    if (ok && std::isfinite(fontScale)) result.scalePercent = nearestValue(int(std::lround(std::clamp(fontScale, 0.6, 2.0) * 100.0)), {90, 100, 110, 125});
    const QString spacing = source.value("itemSpacing").toString();
    const auto pair = spacing.split(QRegularExpression(QStringLiteral("[ ,\\t]+")), Qt::SkipEmptyParts);
    if (pair.size() == 2) {
        bool xOk = false, yOk = false;
        const double x = pair[0].toDouble(&xOk), y = pair[1].toDouble(&yOk);
        if (xOk && yOk && std::isfinite(x) && std::isfinite(y))
            result.spacingPercent = nearestValue(int(std::lround(std::clamp((x + y) / 2.0 / 8.0 * 100.0, 70.0, 130.0))), {80, 90, 100, 110, 120});
    }
    const double rounding = source.value("frameRounding", 4.0).toDouble(&ok);
    if (ok && std::isfinite(rounding)) result.cornerRadius = nearestValue(int(std::lround(std::clamp(rounding, 0.0, 12.0))), {0, 4, 8, 12});
    result.fullscreen = source.value("windowFullscreen", false).toBool();
    result.decorated = source.value("windowDecorated", true).toBool();
    result.backgroundAlpha = std::clamp(source.value("backgroundAlpha", 0.25).toDouble(), 0.0, 1.0);
    result.backgroundTiled = source.value("backgroundTiled", false).toBool();
    result.backgroundTileScale = std::clamp(source.value("backgroundTileScale", 1.0).toDouble(), 0.25, 3.0);
    source.endGroup();
    source.beginGroup("backgrounds");
    const auto& legacyNames = legacyBackgroundNames();
    const auto& pageMap = legacyBackgroundPageMap();
    for (size_t page = 0; page < result.windowBackgrounds.size(); ++page)
        if (pageMap[page] >= 0) result.windowBackgrounds[page] = normalizeBackgroundPath(source.value(legacyNames[size_t(pageMap[page])]).toString());
    source.endGroup();
    result.compactRows = result.spacingPercent <= 90;
    *preset = result;
    return true;
}
}
QStringList QtBackgroundPageNames() {
    return {QString::fromUtf8("Профиль"), QString::fromUtf8("Задачи"), QString::fromUtf8("Проекты"),
        QString::fromUtf8("Навыки"), QString::fromUtf8("Пайплайн"), QString::fromUtf8("Профессии"),
        QString::fromUtf8("Отчёты"), QString::fromUtf8("Аудит"), QString::fromUtf8("Pomodoro"),
        QString::fromUtf8("Правила"), QString::fromUtf8("Хранилище"), QString::fromUtf8("Ярлыки"),
        QString::fromUtf8("Баннер"), QString::fromUtf8("Облако"), QString::fromUtf8("3D просмотр"),
        QString::fromUtf8("Настройки 3D"), QString::fromUtf8("Логи"), QString::fromUtf8("Статистика профилей")};
}
QStringList ListQtBackgroundImages(const std::filesystem::path& directory) {
    QStringList result;
    const QString workspace = QDir::cleanPath(QString::fromStdWString(directory.wstring()));
    const QString imageDirectory = QDir(workspace).filePath(QStringLiteral("ui/backgrounds"));
    const QFileInfo dirInfo(imageDirectory);
    if (dirInfo.isSymLink() || !dirInfo.isDir()) return result;
    QDir dir(imageDirectory);
    const auto entries = dir.entryInfoList({QStringLiteral("*.png")}, QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const auto& entry : entries) {
        if (entry.isSymLink() || !entry.isFile() || entry.size() <= 0 || entry.size() > 16 * 1024 * 1024) continue;
        const auto relative = QStringLiteral("ui/backgrounds/") + entry.fileName();
        if (LoadQtBackgroundImage(directory, relative).isNull()) continue;
        result.push_back(relative);
    }
    return result;
}
QImage LoadQtBackgroundImage(const std::filesystem::path& directory, const QString& relativePath) {
    const QString safe = normalizeBackgroundPath(relativePath);
    if (safe.isEmpty()) return {};
    const QString workspace = QDir::cleanPath(QString::fromStdWString(directory.wstring()));
    const QString base = QDir(workspace).filePath(QStringLiteral("ui/backgrounds"));
    if (QFileInfo(base).isSymLink()) return {};
    const QString filePath = QDir(workspace).filePath(safe);
    const QFileInfo info(filePath);
    if (info.isSymLink() || !info.isFile() || info.size() <= 0 || info.size() > 16 * 1024 * 1024) return {};
    const QString canonicalBase = QDir::fromNativeSeparators(QFileInfo(base).canonicalFilePath());
    const QString canonicalFile = QDir::fromNativeSeparators(info.canonicalFilePath());
    const QString canonicalWorkspace = QDir::fromNativeSeparators(QFileInfo(workspace).canonicalFilePath());
    if (canonicalWorkspace.isEmpty() || canonicalBase.compare(canonicalWorkspace + QStringLiteral("/ui/backgrounds"), Qt::CaseInsensitive) != 0 ||
        canonicalFile.isEmpty() ||
        !canonicalFile.startsWith(canonicalBase + QLatin1Char('/'), Qt::CaseInsensitive)) return {};
    QImageReader reader(canonicalFile, "png");
    reader.setAutoTransform(true);
    const QSize dimensions = reader.size();
    if (!dimensions.isValid() || dimensions.width() > 8192 || dimensions.height() > 8192 ||
        qint64(dimensions.width()) * dimensions.height() > 32ll * 1024 * 1024) return {};
    return reader.read();
}
QStringList ListQtLayoutPresets(const std::filesystem::path& directory) {
    QStringList names;
    QJsonArray custom;
    if (readQtPresets(directory, &custom))
        for (const auto& value : custom) names.push_back(value.toObject().value("name").toString());
    const QString legacyPath = legacyPresetDirectory(directory);
    if (!QFileInfo(legacyPath).isSymLink()) {
        const QDir legacy(legacyPath);
        const auto entries = legacy.entryInfoList({"*.ini"}, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto& entry : entries) {
            if (!entry.isSymLink() && !names.contains(entry.completeBaseName())) names.push_back(entry.completeBaseName());
        }
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}
bool ShowQtBackgroundSettings(QWidget* parent, const std::filesystem::path& directory, QtDisplaySettings& settings) {
    QDialog dialog(parent);
    dialog.setObjectName("qtBackgroundSettings");
    dialog.setWindowTitle(QString::fromUtf8("Фоны разделов"));
    dialog.setMinimumSize(560, 560);
    QtDisplaySettings draft = settings;
    auto* outer = new QVBoxLayout(&dialog);
    auto* notice = new QLabel(QString::fromUtf8("Выберите PNG из папки ui/backgrounds. Цветовая палитра интерфейса не меняется."));
    notice->setWordWrap(true);
    outer->addWidget(notice);
    auto* images = new QComboBox;
    images->setObjectName("qtBackgroundBulkChoice");
    images->setAccessibleName(QString::fromUtf8("Фон для назначения всем разделам"));
    images->addItem(QString::fromUtf8("Без фона"), QString());
    for (const auto& image : ListQtBackgroundImages(directory)) images->addItem(QFileInfo(image).fileName(), image);
    auto* applyAll = new QPushButton(QString::fromUtf8("Назначить всем"));
    applyAll->setObjectName("qtBackgroundApplyAll");
    applyAll->setMinimumHeight(40);
    auto* bulk = new QHBoxLayout;
    bulk->addWidget(images, 1); bulk->addWidget(applyAll);
    outer->addLayout(bulk);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* rowsWidget = new QWidget;
    auto* rows = new QFormLayout(rowsWidget);
    rows->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    rows->setContentsMargins(8, 8, 8, 8);
    rows->setHorizontalSpacing(14);
    rows->setVerticalSpacing(8);
    const auto pageNames = QtBackgroundPageNames();
    const auto available = ListQtBackgroundImages(directory);
    std::array<QComboBox*, 18> choices{};
    for (int i = 0; i < int(choices.size()); ++i) {
        auto* choice = new QComboBox(rowsWidget);
        choice->setObjectName(QStringLiteral("qtBackgroundPage%1").arg(i));
        choice->setAccessibleName(QString::fromUtf8("Фон раздела «%1»").arg(pageNames.value(i)));
        choice->addItem(QString::fromUtf8("Без фона"), QString());
        for (const auto& image : available) choice->addItem(QFileInfo(image).fileName(), image);
        const auto current = normalizeBackgroundPath(draft.windowBackgrounds[size_t(i)]);
        int index = choice->findData(current);
        if (!current.isEmpty() && index < 0) {
            choice->addItem(QString::fromUtf8("Файл недоступен · %1").arg(QFileInfo(current).fileName()), current);
            index = choice->count() - 1;
        }
        choice->setCurrentIndex(std::max(0, index));
        choices[size_t(i)] = choice;
        rows->addRow(pageNames.value(i), choice);
    }
    scroll->setWidget(rowsWidget);
    outer->addWidget(scroll, 1);
    auto* options = new QHBoxLayout;
    auto* alpha = new QSlider(Qt::Horizontal);
    alpha->setObjectName("qtBackgroundAlpha"); alpha->setRange(0, 100);
    alpha->setValue(qRound(std::clamp(draft.backgroundAlpha, 0.0, 1.0) * 100.0));
    alpha->setAccessibleName(QString::fromUtf8("Непрозрачность фонового изображения"));
    auto* alphaLabel = new QLabel;
    auto updateAlphaLabel = [alpha, alphaLabel] { alphaLabel->setText(QStringLiteral("%1%").arg(alpha->value())); };
    updateAlphaLabel(); QObject::connect(alpha, &QSlider::valueChanged, &dialog, updateAlphaLabel);
    options->addWidget(new QLabel(QString::fromUtf8("Непрозрачность")));
    options->addWidget(alpha, 1); options->addWidget(alphaLabel);
    outer->addLayout(options);
    auto* tileRow = new QHBoxLayout;
    auto* tiled = new QCheckBox(QString::fromUtf8("Замостить изображение"));
    tiled->setObjectName("qtBackgroundTiled"); tiled->setChecked(draft.backgroundTiled);
    auto* tileScale = new QComboBox;
    tileScale->setObjectName("qtBackgroundTileScale");
    for (double value : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0})
        tileScale->addItem(QStringLiteral("%1×").arg(value, 0, 'f', 2).remove(QRegularExpression(QStringLiteral("0+$"))).remove(QRegularExpression(QStringLiteral("\\.$"))), value);
    int scaleIndex = 0;
    for (int i = 0; i < tileScale->count(); ++i)
        if (std::abs(tileScale->itemData(i).toDouble() - std::clamp(draft.backgroundTileScale, 0.25, 3.0)) < 0.001) scaleIndex = i;
    tileScale->setCurrentIndex(scaleIndex);
    tileScale->setEnabled(tiled->isChecked());
    QObject::connect(tiled, &QCheckBox::toggled, tileScale, &QWidget::setEnabled);
    tileRow->addWidget(tiled); tileRow->addStretch(); tileRow->addWidget(new QLabel(QString::fromUtf8("Масштаб плитки"))); tileRow->addWidget(tileScale);
    outer->addLayout(tileRow);
    QObject::connect(applyAll, &QPushButton::clicked, &dialog, [&] {
        const QString selected = images->currentData().toString();
        for (auto* choice : choices) {
            int index = choice->findData(selected);
            if (!selected.isEmpty() && index < 0) {
                choice->addItem(QFileInfo(selected).fileName(), selected);
                index = choice->count() - 1;
            }
            choice->setCurrentIndex(std::max(0, index));
        }
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Применить"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Save)->setMinimumSize(88, 40);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    buttons->button(QDialogButtonBox::Cancel)->setMinimumSize(88, 40);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        for (size_t i = 0; i < choices.size(); ++i)
            draft.windowBackgrounds[i] = normalizeBackgroundPath(choices[i]->currentData().toString());
        draft.backgroundAlpha = alpha->value() / 100.0;
        draft.backgroundTiled = tiled->isChecked();
        draft.backgroundTileScale = tileScale->currentData().toDouble();
        settings = draft;
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
bool LoadQtLayoutPreset(const std::filesystem::path& directory, const QString& name, QtLayoutPreset* preset) {
    if (!preset) return false;
    QJsonArray custom;
    if (!readQtPresets(directory, &custom)) return false;
    for (const auto& value : custom) {
        const auto object = value.toObject();
        if (object.value("name").toString() == name) { *preset = presetFromObject(object); return true; }
    }
    return readLegacyLayoutPreset(directory, name, preset);
}
bool SaveQtLayoutPreset(const std::filesystem::path& directory, const QtLayoutPreset& preset, QString* error) {
    const QString name = safePresetName(preset.name);
    if (name.isEmpty()) { if (error) *error = QString::fromUtf8("Введите имя пресета."); return false; }
    QJsonArray presets;
    if (!readQtPresets(directory, &presets, error)) return false;
    int found = -1;
    for (int i = 0; i < presets.size(); ++i)
        if (presets.at(i).toObject().value("name").toString() == name) { found = i; break; }
    if (found < 0 && presets.size() >= 20) {
        if (error) *error = QString::fromUtf8("Можно сохранить не более 20 Qt-пресетов.");
        return false;
    }
    auto saved = preset; saved.name = name;
    if (found < 0) presets.append(presetObject(saved)); else presets[found] = presetObject(saved);
    return saveQtPresets(directory, presets, error);
}
bool DeleteQtLayoutPreset(const std::filesystem::path& directory, const QString& name, QString* error) {
    QJsonArray presets;
    if (!readQtPresets(directory, &presets, error)) return false;
    for (int i = 0; i < presets.size(); ++i) {
        if (presets.at(i).toObject().value("name").toString() == name) {
            presets.removeAt(i);
            return saveQtPresets(directory, presets, error);
        }
    }
    if (error) *error = QString::fromUtf8("Удаляются только пресеты, созданные в Qt.");
    return false;
}
bool IsQtLayoutPresetDeletable(const std::filesystem::path& directory, const QString& name) {
    QJsonArray presets;
    if (!readQtPresets(directory, &presets)) return false;
    for (const auto& value : presets)
        if (value.toObject().value("name").toString() == name) return true;
    return false;
}
QtDisplaySettings LoadQtDisplaySettings(const std::filesystem::path& directory) {
    QtDisplaySettings out; QFile file(pathFor(directory)); if (!file.open(QIODevice::ReadOnly)) return out;
    auto bytes = file.readAll(); if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3); QString section;
    for (const auto& raw : QString::fromUtf8(bytes).split('\n')) {
        const auto line = raw.trimmed(); if (line.startsWith('[') && line.endsWith(']')) { section = line.mid(1, line.size() - 2); continue; }
        const auto key = line.section('=', 0, 0).trimmed(), value = line.section('=', 1).trimmed();
        if (section == "profile" && key == "lastProfileId") out.lastProfileId = value;
        if (section == "ui" && key == "windowDecorated") out.decorated = value != "0";
        if (section == "style") {
            if (key == "backgroundAlpha") { bool ok = false; const double alpha = value.toDouble(&ok); if (ok && std::isfinite(alpha)) out.backgroundAlpha = std::clamp(alpha, 0.0, 1.0); }
            else if (key == "backgroundTiled") out.backgroundTiled = value == "1" || value.compare("true", Qt::CaseInsensitive) == 0;
            else if (key == "backgroundTileScale") { bool ok = false; const double scale = value.toDouble(&ok); if (ok && std::isfinite(scale)) out.backgroundTileScale = std::clamp(scale, 0.25, 3.0); }
        }
        if (section == "backgrounds") {
            const auto& names = legacyBackgroundNames();
            const auto& pageMap = legacyBackgroundPageMap();
            for (size_t page = 0; page < out.windowBackgrounds.size(); ++page)
                if (pageMap[page] >= 0 && key == names[size_t(pageMap[page])]) out.windowBackgrounds[page] = normalizeBackgroundPath(value);
        }
        if (section == "projects") {
            if (key == "sortMode") { bool ok = false; const int index = value.toInt(&ok); out.projectSortMode = ok ? std::clamp(index, 0, 3) : 0; }
            else if (key == "overdueOnly") out.projectsOverdueOnly = value == "1";
            else if (key == "xpPendingOnly") out.projectsXpPendingOnly = value == "1";
        }
        if (section == "qt") {
            if (key == "scalePercent") out.scalePercent = normalizedScale(value.toInt()); else if (key == "spacingPercent") out.spacingPercent = nearestValue(value.toInt(), {80, 90, 100, 110, 120}); else if (key == "cornerRadius") out.cornerRadius = nearestValue(value.toInt(), {0, 4, 8, 12}); else if (key == "compactRows") out.compactRows = value == "1"; else if (key == "fullscreen") out.fullscreen = value == "1"; else if (key == "decorated") out.decorated = value != "0"; else if (key == "minimizeToTray") out.minimizeToTray = value == "1"; else if (key == "deadlineNotificationsWhenClosed") out.deadlineNotificationsWhenClosed = value == "1";
            else if (key == "backgroundAlpha") { bool ok = false; const double alpha = value.toDouble(&ok); if (ok && std::isfinite(alpha)) out.backgroundAlpha = std::clamp(alpha, 0.0, 1.0); }
            else if (key == "backgroundTiled") out.backgroundTiled = value == "1";
            else if (key == "backgroundTileScale") { bool ok = false; const double scale = value.toDouble(&ok); if (ok && std::isfinite(scale)) out.backgroundTileScale = std::clamp(scale, 0.25, 3.0); }
            else if (key.startsWith("windowBackground")) { bool ok = false; const int index = key.mid(QStringLiteral("windowBackground").size()).toInt(&ok); if (ok && index >= 0 && index < int(out.windowBackgrounds.size())) out.windowBackgrounds[size_t(index)] = normalizeBackgroundPath(value); }
            else if (key == "lastProfileId") out.lastProfileId = value;
            else if (key == "lastPage") { bool ok = false; const int page = value.toInt(&ok); out.lastPage = ok ? std::clamp(page, 0, 17) : 0; }
            else if (key == "profileViewMode") { bool ok = false; const int index = value.toInt(&ok); out.profileViewMode = ok ? std::clamp(index, 0, 3) : 1; }
            else if (key == "profileSkillSort") { bool ok = false; const int index = value.toInt(&ok); out.profileSkillSort = ok ? std::clamp(index, 0, 3) : 0; }
            else if (key == "profileSkillWeightCategory") { bool ok = false; const int index = value.toInt(&ok); out.profileSkillWeightCategory = ok ? std::clamp(index, 0, 5) : 0; }
            else if (key == "profileSkillWeightMin") { bool ok = false; const double weight = value.toDouble(&ok); if (ok && std::isfinite(weight)) out.profileSkillWeightMin = std::clamp(weight, 0.0, 2.0); }
            else if (key == "profileSkillWeightMax") { bool ok = false; const double weight = value.toDouble(&ok); if (ok && std::isfinite(weight)) out.profileSkillWeightMax = std::clamp(weight, 0.0, 2.0); }
            else if (key == "taskStatusFilter") { bool ok = false; const int index = value.toInt(&ok); out.taskStatusFilter = ok ? std::clamp(index, 0, 3) : 0; }
            else if (key == "taskPriorityFilter") { bool ok = false; const int index = value.toInt(&ok); out.taskPriorityFilter = ok ? std::clamp(index, 0, 4) : 0; }
            else if (key == "taskQuickFilter") { bool ok = false; const int index = value.toInt(&ok); out.taskQuickFilter = ok ? std::clamp(index, 0, 13) : 0; }
            else if (key == "taskCreatedRange") { bool ok = false; const int index = value.toInt(&ok); out.taskCreatedRange = ok ? std::clamp(index, 0, 4) : 0; }
            else if (key == "taskSortMode") { bool ok = false; const int index = value.toInt(&ok); out.taskSortMode = ok ? std::clamp(index, 0, 2) : 0; }
            else if (key == "taskAssigneeProfileId") out.taskAssigneeProfileId = value;
            else if (key == "taskProjectId") out.taskProjectId = value;
            else if (key == "taskPipelineStepId") out.taskPipelineStepId = value;
            else if (key == "catalogProfessionId") out.catalogProfessionId = value;
            else if (key == "reportView") { bool ok = false; const int index = value.toInt(&ok); out.reportView = ok ? std::clamp(index, 0, 6) : 0; }
            else if (key == "reportDateRange") { bool ok = false; const int index = value.toInt(&ok); out.reportDateRange = ok ? std::clamp(index, 0, 4) : 0; }
            else if (key == "reportComparePrevious") out.reportComparePrevious = value == "1";
            else if (key == "reportDateFrom") out.reportDateFrom = QDate::fromString(value, Qt::ISODate);
            else if (key == "reportDateTo") out.reportDateTo = QDate::fromString(value, Qt::ISODate);
            else if (key == "projectSortMode") { bool ok = false; const int index = value.toInt(&ok); out.projectSortMode = ok ? std::clamp(index, 0, 3) : 0; }
            else if (key == "projectsOverdueOnly") out.projectsOverdueOnly = value == "1";
            else if (key == "projectsXpPendingOnly") out.projectsXpPendingOnly = value == "1";
            else if (key == "auditSourceFilter") { bool ok = false; const int index = value.toInt(&ok); out.auditSourceFilter = ok ? std::clamp(index, 0, 5) : 0; }
            else if (key == "logShowInfo") out.logShowInfo = value != "0";
            else if (key == "logShowWarning") out.logShowWarning = value != "0";
            else if (key == "logShowError") out.logShowError = value != "0";
            else if (key == "logSourceFilter") out.logSourceFilter = value;
            else if (key == "logAutoScroll") out.logAutoScroll = value != "0";
            else if (key == "logCompactView") out.logCompactView = value == "1";
            else if (key == "adminStatsSearch") out.adminStatsSearch = value;
            else if (key == "adminStatsIncludeArchived") out.adminStatsIncludeArchived = value != "0";
            else if (key == "adminStatsRankFilter") { bool ok = false; const int index = value.toInt(&ok); out.adminStatsRankFilter = ok ? std::clamp(index, 0, 16) : 0; }
            else if (key == "adminStatsView") { bool ok = false; const int index = value.toInt(&ok); out.adminStatsView = ok ? std::clamp(index, 0, 7) : 0; }
            else if (key == "adminStatsAutoRefresh") out.adminStatsAutoRefresh = value != "0";
            else if (key == "adminStatsRefreshSeconds") { bool ok = false; const int seconds = value.toInt(&ok); out.adminStatsRefreshSeconds = ok ? std::clamp(seconds, 5, 120) : 30; }
            else if (key == "adminStatsInactivityDays") { bool ok = false; const int days = value.toInt(&ok); out.adminStatsInactivityDays = ok ? std::clamp(days, 1, 365) : 30; }
        }
    }
    const auto today = QDate::currentDate();
    out.profileSkillWeightMax = std::max(out.profileSkillWeightMin, out.profileSkillWeightMax);
    if (!out.reportDateFrom.isValid()) out.reportDateFrom = today.addDays(-29);
    if (!out.reportDateTo.isValid()) out.reportDateTo = today;
    if (out.reportDateFrom > out.reportDateTo) out.reportDateFrom = out.reportDateTo;
    return out;
}
bool SaveQtDisplaySettings(const std::filesystem::path& directory, const QtDisplaySettings& settings) {
    const auto path = pathFor(directory), meta = QFileInfo(path).absolutePath();
    if (QFileInfo(meta).isSymLink() || QFileInfo(path).isSymLink()) return false;
    QFile input(path); QByteArray original; if (input.open(QIODevice::ReadOnly)) original = input.readAll(); input.close();
    const bool bom = original.startsWith("\xEF\xBB\xBF"); if (bom) original.remove(0, 3);
    auto lines = QString::fromUtf8(original).split('\n'); int begin = -1, end = lines.size();
    for (int i = 0; i < lines.size(); ++i) { const auto line = lines[i].trimmed(); if (line == "[qt]") { begin = i; continue; } if (begin >= 0 && i > begin && line.startsWith('[')) { end = i; break; } }
    if (begin < 0) { if (!lines.isEmpty() && !lines.back().isEmpty()) lines << ""; begin = lines.size(); lines << "[qt]"; end = lines.size(); }
    auto set = [&](const QString& key, const QString& value) { for (int i = begin + 1; i < end; ++i) if (lines[i].section('=', 0, 0).trimmed() == key) { lines[i] = key + '=' + value; return; } lines.insert(end++, key + '=' + value); };
    set("scalePercent", QString::number(normalizedScale(settings.scalePercent))); set("spacingPercent", QString::number(nearestValue(settings.spacingPercent, {80, 90, 100, 110, 120}))); set("cornerRadius", QString::number(nearestValue(settings.cornerRadius, {0, 4, 8, 12}))); set("compactRows", settings.compactRows ? "1" : "0"); set("fullscreen", settings.fullscreen ? "1" : "0"); set("decorated", settings.decorated ? "1" : "0"); set("minimizeToTray", settings.minimizeToTray ? "1" : "0"); set("deadlineNotificationsWhenClosed", settings.deadlineNotificationsWhenClosed ? "1" : "0");
    set("backgroundAlpha", QString::number(std::isfinite(settings.backgroundAlpha) ? std::clamp(settings.backgroundAlpha, 0.0, 1.0) : 0.25, 'f', 2));
    set("backgroundTiled", settings.backgroundTiled ? "1" : "0");
    set("backgroundTileScale", QString::number(std::isfinite(settings.backgroundTileScale) ? std::clamp(settings.backgroundTileScale, 0.25, 3.0) : 1.0, 'f', 2));
    for (size_t i = 0; i < settings.windowBackgrounds.size(); ++i)
        set(QStringLiteral("windowBackground%1").arg(i), normalizeBackgroundPath(settings.windowBackgrounds[i]));
    auto profileId = settings.lastProfileId; profileId.remove('\r'); profileId.remove('\n');
    set("lastProfileId", profileId); set("lastPage", QString::number(std::clamp(settings.lastPage, 0, 17)));
    set("profileViewMode", QString::number(std::clamp(settings.profileViewMode, 0, 3)));
    set("profileSkillSort", QString::number(std::clamp(settings.profileSkillSort, 0, 3)));
    set("profileSkillWeightCategory", QString::number(std::clamp(settings.profileSkillWeightCategory, 0, 5)));
    const double profileSkillWeightMin = std::isfinite(settings.profileSkillWeightMin)
        ? std::clamp(settings.profileSkillWeightMin, 0.0, 2.0) : 0.0;
    const double profileSkillWeightMax = std::isfinite(settings.profileSkillWeightMax)
        ? std::clamp(settings.profileSkillWeightMax, profileSkillWeightMin, 2.0) : 2.0;
    set("profileSkillWeightMin", QString::number(profileSkillWeightMin, 'f', 2));
    set("profileSkillWeightMax", QString::number(profileSkillWeightMax, 'f', 2));
    set("taskStatusFilter", QString::number(std::clamp(settings.taskStatusFilter, 0, 3)));
    set("taskPriorityFilter", QString::number(std::clamp(settings.taskPriorityFilter, 0, 4)));
    set("taskQuickFilter", QString::number(std::clamp(settings.taskQuickFilter, 0, 13)));
    set("taskCreatedRange", QString::number(std::clamp(settings.taskCreatedRange, 0, 4)));
    set("taskSortMode", QString::number(std::clamp(settings.taskSortMode, 0, 2)));
    auto taskAssigneeProfileId = settings.taskAssigneeProfileId; taskAssigneeProfileId.remove('\r'); taskAssigneeProfileId.remove('\n');
    set("taskAssigneeProfileId", taskAssigneeProfileId);
    auto projectId = settings.taskProjectId; projectId.remove('\r'); projectId.remove('\n');
    auto pipelineId = settings.taskPipelineStepId; pipelineId.remove('\r'); pipelineId.remove('\n');
    set("taskProjectId", projectId); set("taskPipelineStepId", pipelineId);
    auto catalogProfessionId = settings.catalogProfessionId; catalogProfessionId.remove('\r'); catalogProfessionId.remove('\n');
    set("catalogProfessionId", catalogProfessionId);
    set("reportView", QString::number(std::clamp(settings.reportView, 0, 6)));
    set("reportDateRange", QString::number(std::clamp(settings.reportDateRange, 0, 4)));
    set("reportComparePrevious", settings.reportComparePrevious ? "1" : "0");
    const auto today = QDate::currentDate();
    const auto reportFrom = settings.reportDateFrom.isValid() ? settings.reportDateFrom : today.addDays(-29);
    const auto reportTo = settings.reportDateTo.isValid() ? settings.reportDateTo : today;
    set("reportDateFrom", (reportFrom <= reportTo ? reportFrom : reportTo).toString(Qt::ISODate));
    set("reportDateTo", reportTo.toString(Qt::ISODate));
    set("projectSortMode", QString::number(std::clamp(settings.projectSortMode, 0, 3)));
    set("projectsOverdueOnly", settings.projectsOverdueOnly ? "1" : "0");
    set("projectsXpPendingOnly", settings.projectsXpPendingOnly ? "1" : "0");
    set("auditSourceFilter", QString::number(std::clamp(settings.auditSourceFilter, 0, 5)));
    set("logShowInfo", settings.logShowInfo ? "1" : "0");
    set("logShowWarning", settings.logShowWarning ? "1" : "0");
    set("logShowError", settings.logShowError ? "1" : "0");
    auto logSourceFilter = settings.logSourceFilter; logSourceFilter.remove('\r'); logSourceFilter.remove('\n');
    set("logSourceFilter", logSourceFilter);
    set("logAutoScroll", settings.logAutoScroll ? "1" : "0");
    set("logCompactView", settings.logCompactView ? "1" : "0");
    auto adminStatsSearch = settings.adminStatsSearch; adminStatsSearch.remove('\r'); adminStatsSearch.remove('\n');
    set("adminStatsSearch", adminStatsSearch);
    set("adminStatsIncludeArchived", settings.adminStatsIncludeArchived ? "1" : "0");
    set("adminStatsRankFilter", QString::number(std::clamp(settings.adminStatsRankFilter, 0, 16)));
    set("adminStatsView", QString::number(std::clamp(settings.adminStatsView, 0, 7)));
    set("adminStatsAutoRefresh", settings.adminStatsAutoRefresh ? "1" : "0");
    set("adminStatsRefreshSeconds", QString::number(std::clamp(settings.adminStatsRefreshSeconds, 5, 120)));
    set("adminStatsInactivityDays", QString::number(std::clamp(settings.adminStatsInactivityDays, 1, 365)));
    const auto bytes = (bom ? QByteArray("\xEF\xBB\xBF") : QByteArray()) + lines.join('\n').toUtf8();
    QDir().mkpath(meta); QSaveFile output(path); output.setDirectWriteFallback(false);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}
void ApplyQtDisplaySettings(QApplication& app, const QtDisplaySettings& settings) {
    double base = app.property("forgeBasePointSize").toDouble();
    if (base <= 0.0) { base = app.font().pointSizeF(); app.setProperty("forgeBasePointSize", base); }
    auto font = app.font(); font.setPointSizeF(base * normalizedScale(settings.scalePercent) / 100.0); app.setFont(font);
    ApplyQtLayoutMetrics(app, settings.spacingPercent, settings.cornerRadius);
}
bool ShowQtDisplaySettings(QWidget* parent, const std::filesystem::path& directory, QtDisplaySettings& settings) {
    QDialog dialog(parent); dialog.setObjectName("qtDisplaySettings"); dialog.setWindowTitle(QString::fromUtf8("Настройки интерфейса Qt")); dialog.setMinimumWidth(420);
    QtDisplaySettings backgroundDraft = settings;
    auto* form = new QFormLayout(&dialog); auto* scale = new QComboBox; scale->setObjectName("qtScale");
    for (int value : {90, 100, 110, 125}) scale->addItem(QString::number(value) + "%", value);
    scale->setCurrentIndex(std::max(0, scale->findData(normalizedScale(settings.scalePercent))));
    auto* spacing = new QComboBox; spacing->setObjectName("qtSpacing");
    for (int value : {80, 90, 100, 110, 120}) spacing->addItem(QString::number(value) + "%", value);
    spacing->setCurrentIndex(std::max(0, spacing->findData(nearestValue(settings.spacingPercent, {80, 90, 100, 110, 120}))));
    auto* rounding = new QComboBox; rounding->setObjectName("qtCornerRadius");
    for (int value : {0, 4, 8, 12}) rounding->addItem(QString::number(value) + QString::fromUtf8(" px"), value);
    rounding->setCurrentIndex(std::max(0, rounding->findData(nearestValue(settings.cornerRadius, {0, 4, 8, 12}))));
    auto* compact = new QCheckBox(QString::fromUtf8("Компактные строки таблиц")); compact->setObjectName("qtCompactRows"); compact->setChecked(settings.compactRows);
    auto* fullscreen = new QCheckBox(QString::fromUtf8("Полноэкранный режим (F11)")); fullscreen->setObjectName("qtFullscreen"); fullscreen->setChecked(settings.fullscreen);
    auto* decorated = new QCheckBox(QString::fromUtf8("Показывать рамку окна")); decorated->setObjectName("qtDecorated"); decorated->setChecked(settings.decorated);
    auto* tray = new QCheckBox(QString::fromUtf8("При закрытии сворачивать в трей и продолжать напоминания"));
    tray->setObjectName("qtMinimizeToTray");
    tray->setChecked(settings.minimizeToTray);
    tray->setEnabled(QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages());
    tray->setToolTip(tray->isEnabled() ? QString::fromUtf8("Окно скроется, но приложение останется запущенным. Выход доступен из меню значка в трее.")
        : QString::fromUtf8("Системный трей недоступен в этой среде."));
    auto* background = new QCheckBox(QString::fromUtf8("Напоминать о дедлайнах, когда программа закрыта"));
    background->setObjectName("qtDeadlineNotificationsWhenClosed");
    background->setChecked(settings.deadlineNotificationsWhenClosed);
    const auto defaultWorkspace = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/workspace"));
    const auto selectedWorkspace = QDir::cleanPath(QString::fromStdWString(directory.wstring()));
#ifdef _WIN32
    const bool supported = selectedWorkspace.compare(defaultWorkspace, Qt::CaseInsensitive) == 0 &&
        QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages();
#else
    const bool supported = false;
#endif
    background->setEnabled(supported || settings.deadlineNotificationsWhenClosed);
    background->setToolTip(supported
        ? QString::fromUtf8("Планировщик Windows запускает проверку каждые 15 минут в текущем сеансе пользователя. Тексты задач не показываются.")
        : QString::fromUtf8("Доступно в Windows для стандартного изолированного рабочего пространства при поддержке уведомлений системного трея."));
    auto* backgroundsButton = new QPushButton;
    backgroundsButton->setObjectName("qtBackgroundSettingsButton");
    backgroundsButton->setMinimumHeight(40);
    auto updateBackgroundsButton = [backgroundsButton, &backgroundDraft] {
        const auto count = std::count_if(backgroundDraft.windowBackgrounds.begin(), backgroundDraft.windowBackgrounds.end(),
            [](const QString& path) { return !normalizeBackgroundPath(path).isEmpty(); });
        backgroundsButton->setText(QString::fromUtf8("Фоны разделов… (%1 назначено)").arg(count));
    };
    updateBackgroundsButton();
    QObject::connect(backgroundsButton, &QPushButton::clicked, &dialog, [&] {
        if (ShowQtBackgroundSettings(&dialog, directory, backgroundDraft)) updateBackgroundsButton();
    });
    auto* presets = new QComboBox; presets->setObjectName("qtLayoutPresetList");
    auto refreshPresets = [directory, presets] {
        const auto selected = presets->currentData().toString();
        presets->clear();
        for (const auto& name : ListQtLayoutPresets(directory)) presets->addItem(name, name);
        const int index = presets->findData(selected);
        if (index >= 0) presets->setCurrentIndex(index);
    };
    refreshPresets();
    auto* presetName = new QLineEdit; presetName->setObjectName("qtLayoutPresetName");
    presetName->setAccessibleName(QString::fromUtf8("Имя пользовательского пресета компоновки"));
    presetName->setMaxLength(40);
    auto* applyPreset = new QPushButton(QString::fromUtf8("Загрузить"));
    applyPreset->setObjectName("qtLayoutPresetApply");
    auto* savePreset = new QPushButton(QString::fromUtf8("Сохранить пресет"));
    savePreset->setObjectName("qtLayoutPresetSave");
    auto* deletePreset = new QPushButton(QString::fromUtf8("Удалить Qt-пресет"));
    deletePreset->setObjectName("qtLayoutPresetDelete");
    presets->setAccessibleName(QString::fromUtf8("Выбрать или импортировать пресет компоновки"));
    applyPreset->setAccessibleName(QString::fromUtf8("Применить выбранный пресет компоновки"));
    savePreset->setAccessibleName(QString::fromUtf8("Сохранить или обновить Qt-пресет компоновки"));
    deletePreset->setAccessibleName(QString::fromUtf8("Удалить выбранный Qt-пресет"));
    presets->setToolTip(QString::fromUtf8("Старые пресеты доступны только для чтения. Палитра и параметры профиля не импортируются; старые PNG-фоны разделов доступны отдельно."));
    auto* presetRow = new QWidget;
    auto* presetRowLayout = new QHBoxLayout(presetRow);
    presetRowLayout->setContentsMargins(0, 0, 0, 0);
    presetRowLayout->addWidget(presets, 1); presetRowLayout->addWidget(applyPreset); presetRowLayout->addWidget(deletePreset);
    auto* presetNameRow = new QWidget;
    auto* presetNameLayout = new QHBoxLayout(presetNameRow);
    presetNameLayout->setContentsMargins(0, 0, 0, 0);
    presetNameLayout->addWidget(presetName, 1); presetNameLayout->addWidget(savePreset);
    auto* notice = new QLabel; notice->setObjectName("qtSettingsNotice"); notice->setWordWrap(true);
    QString presetError;
    form->addRow(QString::fromUtf8("Масштаб текста"), scale);
    form->addRow(QString::fromUtf8("Интервалы интерфейса"), spacing);
    form->addRow(QString::fromUtf8("Скругление карточек и акцентных кнопок"), rounding);
    form->addRow(compact); form->addRow(fullscreen); form->addRow(decorated); form->addRow(tray); form->addRow(background);
    form->addRow(QString(), backgroundsButton);
    form->addRow(QString(), presetRow);
    form->addRow(QString::fromUtf8("Новый/обновляемый пресет"), presetNameRow);
    QObject::connect(applyPreset, &QPushButton::clicked, &dialog, [&] {
        QtLayoutPreset preset;
        if (!LoadQtLayoutPreset(directory, presets->currentData().toString(), &preset)) return;
        scale->setCurrentIndex(std::max(0, scale->findData(preset.scalePercent)));
        spacing->setCurrentIndex(std::max(0, spacing->findData(preset.spacingPercent)));
        rounding->setCurrentIndex(std::max(0, rounding->findData(preset.cornerRadius)));
        compact->setChecked(preset.compactRows);
        fullscreen->setChecked(preset.fullscreen);
        decorated->setChecked(preset.decorated);
        backgroundDraft.windowBackgrounds = preset.windowBackgrounds;
        backgroundDraft.backgroundAlpha = preset.backgroundAlpha;
        backgroundDraft.backgroundTiled = preset.backgroundTiled;
        backgroundDraft.backgroundTileScale = preset.backgroundTileScale;
        updateBackgroundsButton();
        presetName->setText(preset.name);
        notice->setText(QString::fromUtf8("Параметры компоновки загружены; палитра не меняется."));
    });
    QObject::connect(savePreset, &QPushButton::clicked, &dialog, [&] {
        QtLayoutPreset preset;
        preset.name = presetName->text();
        preset.scalePercent = scale->currentData().toInt();
        preset.spacingPercent = spacing->currentData().toInt();
        preset.cornerRadius = rounding->currentData().toInt();
        preset.compactRows = compact->isChecked();
        preset.fullscreen = fullscreen->isChecked();
        preset.decorated = decorated->isChecked();
        preset.windowBackgrounds = backgroundDraft.windowBackgrounds;
        preset.backgroundAlpha = backgroundDraft.backgroundAlpha;
        preset.backgroundTiled = backgroundDraft.backgroundTiled;
        preset.backgroundTileScale = backgroundDraft.backgroundTileScale;
        if (!SaveQtLayoutPreset(directory, preset, &presetError)) { notice->setText(presetError); return; }
        presetName->setText(preset.name);
        refreshPresets();
        presets->setCurrentIndex(presets->findData(preset.name));
        notice->setText(QString::fromUtf8("Пресет компоновки сохранён вместе с настройками фонов; палитра не затрагивается."));
    });
    QObject::connect(deletePreset, &QPushButton::clicked, &dialog, [&] {
        const auto name = presets->currentData().toString();
        if (!IsQtLayoutPresetDeletable(directory, name)) {
            notice->setText(QString::fromUtf8("Импортированный пресет доступен только для чтения; исходный файл сохранён."));
            return;
        }
        if (QMessageBox::question(&dialog, QString::fromUtf8("Удаление пресета"),
            QString::fromUtf8("Удалить Qt-пресет «%1»?").arg(name), QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) return;
        if (!DeleteQtLayoutPreset(directory, name, &presetError)) { notice->setText(presetError); return; }
        refreshPresets();
        notice->setText(QString::fromUtf8("Qt-пресет удалён."));
    });
    QObject::connect(presets, &QComboBox::currentIndexChanged, &dialog, [directory, presets, deletePreset] {
        deletePreset->setEnabled(IsQtLayoutPresetDeletable(directory, presets->currentData().toString()));
    });
    deletePreset->setEnabled(IsQtLayoutPresetDeletable(directory, presets->currentData().toString()));
    auto* hint = new QLabel(QString::fromUtf8("Цветовая схема зафиксирована для миграции и здесь не меняется. Старые пресеты доступны только для чтения; они переносят компоновку и фоны окон, не меняя палитру и данные профилей."));
    hint->setWordWrap(true); form->addRow(hint);
    form->addRow(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить")); buttons->button(QDialogButtonBox::Save)->setProperty("primary", true); buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена")); form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto next = settings; next.scalePercent = scale->currentData().toInt(); next.spacingPercent = spacing->currentData().toInt();
        next.cornerRadius = rounding->currentData().toInt(); next.compactRows = compact->isChecked();
        next.fullscreen = fullscreen->isChecked(); next.decorated = decorated->isChecked();
        next.windowBackgrounds = backgroundDraft.windowBackgrounds;
        next.backgroundAlpha = backgroundDraft.backgroundAlpha;
        next.backgroundTiled = backgroundDraft.backgroundTiled;
        next.backgroundTileScale = backgroundDraft.backgroundTileScale;
        next.minimizeToTray = tray->isEnabled() && tray->isChecked();
        next.deadlineNotificationsWhenClosed = background->isEnabled() && background->isChecked();
        QString scheduleError;
        if (next.deadlineNotificationsWhenClosed != settings.deadlineNotificationsWhenClosed &&
            !ConfigureQtDeadlineSchedule(next.deadlineNotificationsWhenClosed, &scheduleError)) {
            notice->setText(scheduleError); return;
        }
        if (!SaveQtDisplaySettings(directory, next)) {
            if (next.deadlineNotificationsWhenClosed != settings.deadlineNotificationsWhenClosed)
                ConfigureQtDeadlineSchedule(settings.deadlineNotificationsWhenClosed, nullptr);
            notice->setText(QString::fromUtf8("Не удалось атомарно сохранить настройки.")); return;
        }
        settings = next; dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
