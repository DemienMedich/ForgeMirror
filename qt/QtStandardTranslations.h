#pragma once

#include <QTranslator>
#include <QCoreApplication>
#include <QVariant>

// Install before constructing widgets: changing translators while a picker is
// open sends LanguageChange and can reset its custom labels and editor state.
class QtStandardTranslations final : public QTranslator {
public:
    explicit QtStandardTranslations(QObject* parent) : QTranslator(parent) {}
    bool isEmpty() const override { return false; }
    QString translate(const char* context, const char* source, const char*, int) const override {
        const auto ctx = QLatin1String(context);
        const auto text = QLatin1String(source);
        if (ctx == QLatin1String("QFileSystemModel")) {
            if (text == QLatin1String("Name")) return QString::fromUtf8("Имя");
            if (text == QLatin1String("Size")) return QString::fromUtf8("Размер");
            if (text == QLatin1String("Type")) return QString::fromUtf8("Тип");
            if (text == QLatin1String("Date Modified")) return QString::fromUtf8("Дата изменения");
            if (text == QLatin1String("Computer")) return QString::fromUtf8("Компьютер");
        }
        if ((ctx == QLatin1String("QFileSystemModel") || ctx == QLatin1String("QFileDialog")) &&
            text == QLatin1String("My Computer")) return QString::fromUtf8("Мой компьютер");
        if (ctx == QLatin1String("QFileDialog") &&
            text == QLatin1String("%1 already exists.\nDo you want to replace it?"))
            return QString::fromUtf8("%1 уже существует.\nЗаменить его?");
        if (ctx == QLatin1String("QPlatformTheme")) {
            if (text == QLatin1String("&Yes")) return QString::fromUtf8("&Да");
            if (text == QLatin1String("&No")) return QString::fromUtf8("&Нет");
        }
        return {};
    }
};

inline void InstallQtStandardTranslations(QCoreApplication& app) {
    if (app.property("forgeStandardTranslationsInstalled").toBool()) return;
    auto* translation = new QtStandardTranslations(&app);
    if (app.installTranslator(translation)) app.setProperty("forgeStandardTranslationsInstalled", true);
}
