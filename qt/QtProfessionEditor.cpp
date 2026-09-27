#include "QtProfessionEditor.h"
#include "AppProfessionService.h"
#include "AppTaskCompletionService.h"
#include "AppUtils.h"
#include "Profile.h"
#include "SkillCatalog.h"
#include <QtWidgets>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace {
QString q(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }
std::string u(const QString& value) { return value.toUtf8().toStdString(); }
bool safe(const QString& value) {
    for (const auto ch : value) if (ch == '|' || ch.category() == QChar::Other_Control ||
        ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator) return false;
    return true;
}
bool sameCatalog(const SkillCatalog& a, const SkillCatalog& b) {
    if (a.skills().size() != b.skills().size()) return false;
    for (const auto& key : a.skills()) if (!b.contains_id(key) || a.display_name(key) != b.display_name(key) ||
        a.description(key) != b.description(key) || a.category(key) != b.category(key) ||
        std::abs(a.weight(key) - b.weight(key)) > 0.000001 || a.professions(key) != b.professions(key)) return false;
    return true;
}
}

QString MergeQtProfessions(QtWorkspace& workspace, const std::string& restoreProfileId,
                           const std::string& fromId, const std::string& toId,
                           const QString& destinationDescription) {
    auto failText = [](const char* value) { return QString::fromUtf8(value); };
    const auto source = std::find_if(workspace.data.professions.begin(), workspace.data.professions.end(),
        [&](const auto& p) { return p.id == fromId; });
    const auto destination = std::find_if(workspace.data.professions.begin(), workspace.data.professions.end(),
        [&](const auto& p) { return p.id == toId; });
    if (fromId.empty() || toId.empty() || fromId == toId || source == workspace.data.professions.end() ||
        destination == workspace.data.professions.end()) return failText("Исходная или целевая профессия больше не существует.");
    const auto description = destinationDescription.trimmed();
    if (!safe(description)) return failText("Описание содержит неподдерживаемые символы.");
    if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return failText("Сначала завершите восстановление данных через обновление.");

    const auto professionsPath = q((workspace.directory / "meta/professions.txt").u8string());
    QFile originalProfessions(professionsPath);
    if (!originalProfessions.open(QIODevice::ReadOnly)) return failText("Не удалось проверить список профессий.");
    const auto originalProfessionBytes = originalProfessions.readAll();
    if (originalProfessions.error() != QFileDevice::NoError) return failText("Ошибка чтения списка профессий.");
    originalProfessions.close();
    const auto onDiskProfessions = LoadProfessionsData(workspace.directory);
    if (onDiskProfessions.size() != workspace.data.professions.size())
        return failText("Список профессий изменился на диске. Обновите данные.");
    for (size_t i = 0; i < onDiskProfessions.size(); ++i)
        if (onDiskProfessions[i].id != workspace.data.professions[i].id ||
            onDiskProfessions[i].name != workspace.data.professions[i].name ||
            onDiskProfessions[i].description != workspace.data.professions[i].description)
            return failText("Список профессий изменился на диске. Обновите данные.");
    QTemporaryDir staging;
    if (!staging.isValid()) return failText("Не удалось создать временный каталог.");
    const auto stagedRoot = std::filesystem::u8path(u(staging.path()));
    auto candidateProfessions = workspace.data.professions;
    const auto targetIndex = std::find_if(candidateProfessions.begin(), candidateProfessions.end(),
        [&](const auto& p) { return p.id == toId; });
    targetIndex->description = u(description);
    candidateProfessions.erase(std::remove_if(candidateProfessions.begin(), candidateProfessions.end(),
        [&](const auto& p) { return p.id == fromId; }), candidateProfessions.end());
    if (!AppSaveProfessionsData(stagedRoot, candidateProfessions)) return failText("Не удалось подготовить объединённый список профессий.");
    const auto verifiedProfessions = LoadProfessionsData(stagedRoot);
    if (verifiedProfessions.size() != candidateProfessions.size() ||
        std::none_of(verifiedProfessions.begin(), verifiedProfessions.end(), [&](const auto& p) { return p.id == toId && p.description == u(description); }) ||
        std::any_of(verifiedProfessions.begin(), verifiedProfessions.end(), [&](const auto& p) { return p.id == fromId; }))
        return failText("Проверка сериализации профессий не пройдена.");
    QFile stagedProfessions(q((stagedRoot / "meta/professions.txt").u8string()));
    if (!stagedProfessions.open(QIODevice::ReadOnly)) return failText("Не удалось прочитать объединённый список профессий.");
    const auto replacementProfessionBytes = stagedProfessions.readAll();
    if (stagedProfessions.error() != QFileDevice::NoError) return failText("Ошибка чтения объединённого списка профессий.");

    const auto skillsPath = q((workspace.directory / "skills.txt").u8string());
    QFile originalSkills(skillsPath);
    if (!originalSkills.open(QIODevice::ReadOnly)) return failText("Не удалось проверить каталог навыков.");
    const auto originalSkillBytes = originalSkills.readAll();
    if (originalSkills.error() != QFileDevice::NoError) return failText("Ошибка чтения каталога навыков.");
    originalSkills.close();
    if (!QFile::copy(skillsPath, staging.path() + "/skills.txt")) return failText("Не удалось подготовить каталог навыков для проверки.");
    SkillCatalog candidateCatalog(stagedRoot);
    if (!sameCatalog(candidateCatalog, workspace.catalog)) return failText("Каталог навыков изменился. Обновите данные.");
    for (const auto& skillId : candidateCatalog.skills()) {
        auto bindings = candidateCatalog.professions(skillId);
        bool changed = false;
        for (auto& binding : bindings) if (binding == fromId) { binding = toId; changed = true; }
        if (!changed) continue;
        std::vector<std::string> unique;
        for (const auto& binding : bindings)
            if (std::find(unique.begin(), unique.end(), binding) == unique.end()) unique.push_back(binding);
        if (!candidateCatalog.update_skill(skillId, candidateCatalog.display_name(skillId), candidateCatalog.weight(skillId),
            candidateCatalog.description(skillId), candidateCatalog.category(skillId), unique))
            return failText("Не удалось перенести связи навыков.");
    }
    SkillCatalog verifiedCatalog(stagedRoot);
    if (!sameCatalog(candidateCatalog, verifiedCatalog)) return failText("Проверка сериализации связей навыков не пройдена.");
    QFile stagedSkills(staging.path() + "/skills.txt");
    if (!stagedSkills.open(QIODevice::ReadOnly)) return failText("Не удалось прочитать проверочный каталог навыков.");
    const auto replacementSkillBytes = stagedSkills.readAll();
    if (stagedSkills.error() != QFileDevice::NoError) return failText("Ошибка чтения проверочного каталога навыков.");

    const auto profiles = workspace.storage->list_profiles();
    std::set<std::string> uniqueIds;
    std::vector<std::string> journalPaths;
    for (const auto& info : profiles) {
        if (info.id.empty() || !uniqueIds.insert(info.id).second) return failText("Список профилей содержит пустые или повторяющиеся ID.");
        journalPaths.push_back((info.archived ? "archive/" : "") + info.id);
    }
    bool prepared = false;
    try {
        PrepareProfessionDeletionRecovery(workspace.directory, journalPaths);
        prepared = true;
        for (const auto& info : profiles) {
            if (info.archived && !workspace.storage->set_archived(info.id, false)) throw std::runtime_error(u8"Не удалось временно открыть архивный профиль.");
            if (!workspace.storage->set_active_profile(info.id)) throw std::runtime_error(u8"Не удалось выбрать профиль для переноса профессии.");
            auto profile = workspace.storage->load_profile();
            if (!profile) throw std::runtime_error(u8"Не удалось загрузить профиль для переноса профессии.");
            if (profile->profession_id() == fromId) {
                profile->set_profession_id(toId);
                if (!workspace.storage->save_profile(*profile)) throw std::runtime_error(u8"Не удалось переназначить профессию профиля.");
                const auto checked = workspace.storage->load_profile();
                if (!checked || checked->profession_id() != toId) throw std::runtime_error(u8"Проверка переназначения профессии не пройдена.");
            }
            if (info.archived && !workspace.storage->set_archived(info.id, true)) throw std::runtime_error(u8"Не удалось вернуть профиль в архив.");
        }
        if (!restoreProfileId.empty() && !workspace.storage->set_active_profile(restoreProfileId)) throw std::runtime_error(u8"Не удалось восстановить выбранный профиль.");

        QFile currentProfessions(professionsPath);
        if (!currentProfessions.open(QIODevice::ReadOnly) || currentProfessions.readAll() != originalProfessionBytes || currentProfessions.error() != QFileDevice::NoError)
            throw std::runtime_error(u8"Список профессий изменился во время объединения.");
        currentProfessions.close();
        QFile currentSkills(skillsPath);
        if (!currentSkills.open(QIODevice::ReadOnly) || currentSkills.readAll() != originalSkillBytes || currentSkills.error() != QFileDevice::NoError)
            throw std::runtime_error(u8"Каталог навыков изменился во время объединения.");
        currentSkills.close();
        auto atomicReplace = [](const QString& path, const QByteArray& bytes, const char* error) {
            QSaveFile output(path); output.setDirectWriteFallback(false);
            if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) throw std::runtime_error(error);
        };
        atomicReplace(skillsPath, replacementSkillBytes, u8"Не удалось атомарно сохранить каталог навыков.");
        atomicReplace(professionsPath, replacementProfessionBytes, u8"Не удалось атомарно сохранить список профессий.");
        CommitQtRecoveryTransaction(workspace.directory);
        prepared = false;
        workspace.reload();
        return {};
    } catch (const std::exception& error) {
        std::string message = error.what();
        if (prepared) {
            try {
                RecoverTaskCompletion(workspace.directory);
                if (!restoreProfileId.empty()) workspace.storage->set_active_profile(restoreProfileId);
                workspace.reload();
                message += u8" Все изменения отменены.";
            } catch (const std::exception&) { message += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt."; }
        }
        return q(message);
    }
}
bool ShowProfessionEditor(QWidget* parent, QtWorkspace& workspace, const std::string& id,
                          const std::string& restoreProfileId) {
    const auto found = std::find_if(workspace.data.professions.begin(), workspace.data.professions.end(),
        [&](const auto& p) { return p.id == id; });
    if (!id.empty() && found == workspace.data.professions.end()) return false;
    QDialog dialog(parent);
    dialog.setObjectName("professionEditor");
    dialog.setWindowTitle(QString::fromUtf8(id.empty() ? "Новая профессия" : "Редактирование профессии"));
    dialog.setMinimumWidth(480);
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(id.empty() ? QString() : q(found->name));
    name->setObjectName("professionName");
    auto* description = new QLineEdit(id.empty() ? QString() : q(found->description));
    description->setObjectName("professionDescription");
    form->addRow(QString::fromUtf8("Название"), name);
    form->addRow(QString::fromUtf8("Описание"), description);
    auto* notice = new QLabel;
    notice->setObjectName("professionNotice");
    notice->setWordWrap(true);
    notice->setTextFormat(Qt::PlainText);
    form->addRow(notice);
    QObject::connect(name, &QLineEdit::textChanged, notice, &QLabel::clear);
    QObject::connect(description, &QLineEdit::textChanged, notice, &QLabel::clear);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto title = name->text().trimmed(), desc = description->text().trimmed();
        if (title.isEmpty() || !safe(title) || !safe(desc)) {
            notice->setText(QString::fromUtf8("Укажите название. Переносы строк, управляющие символы и | не поддерживаются.")); return;
        }
        if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction")) {
            notice->setText(QString::fromUtf8("Сначала завершите восстановление данных.")); return;
        }
        auto candidate = workspace.data.professions;
        for (const auto& p : candidate) if (id.empty() && q(p.name).compare(title, Qt::CaseInsensitive) == 0) {
            notice->setText(QString::fromUtf8("Профессия с таким названием уже существует.")); return;
        }
        const auto current = std::find_if(candidate.begin(), candidate.end(), [&](const auto& p) { return p.id == id; });
        if (!id.empty() && current == candidate.end()) { notice->setText(QString::fromUtf8("Профессия больше не существует.")); return; }
        if (!id.empty()) {
            const auto duplicate = std::find_if(candidate.begin(), candidate.end(), [&](const auto& p) {
                return p.id != id && q(p.name).compare(title, Qt::CaseInsensitive) == 0;
            });
            if (duplicate != candidate.end()) {
                QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Объединить профессии"),
                    QString::fromUtf8("Назначения активных и архивных профилей, а также связи навыков с профессией «%1» будут перенесены на «%2». Исходная профессия будет удалена, описание целевой заменено введённым.")
                        .arg(q(current->name), q(duplicate->name)), QMessageBox::Yes | QMessageBox::Cancel, &dialog);
                confirm.setObjectName("professionMergeConfirm");
                confirm.setDefaultButton(QMessageBox::Cancel);
                confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Объединить"));
                confirm.button(QMessageBox::Cancel)->setText(QString::fromUtf8("Отмена"));
                if (confirm.exec() != QMessageBox::Yes) return;
                const auto error = MergeQtProfessions(workspace, restoreProfileId, id, duplicate->id, desc);
                if (!error.isEmpty()) { notice->setText(error); return; }
                dialog.accept();
                return;
            }
        }
        const int index = id.empty() ? -1 : int(std::distance(candidate.begin(), current));
        QTemporaryDir staging;
        if (!staging.isValid()) { notice->setText(QString::fromUtf8("Не удалось создать временный каталог.")); return; }
        const auto directory = std::filesystem::u8path(u(staging.path()));
        const auto result = AppSaveProfessionEntry(directory, candidate, index, u(title), u(desc));
        const auto checked = LoadProfessionsData(directory);
        bool same = checked.size() == candidate.size();
        for (size_t i = 0; same && i < candidate.size(); ++i)
            same = candidate[i].id == checked[i].id && candidate[i].name == checked[i].name && candidate[i].description == checked[i].description;
        if (!result.ok || !same) { notice->setText(QString::fromUtf8("Проверка сохранения не пройдена. Исходные данные не изменены.")); return; }
        QFile file(staging.path() + "/meta/professions.txt");
        if (!file.open(QIODevice::ReadOnly)) { notice->setText(QString::fromUtf8("Ошибка чтения временного файла.")); return; }
        const auto bytes = file.readAll();
        if (file.error() != QFileDevice::NoError) { notice->setText(QString::fromUtf8("Ошибка чтения временного файла.")); return; }
        const auto meta = q((workspace.directory / "meta").u8string());
        QSaveFile output(meta + "/professions.txt");
        output.setDirectWriteFallback(false);
        if (!QDir().mkpath(meta) || !output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            notice->setText(QString::fromUtf8("Не удалось записать профессии. Исходный файл сохранён.")); return;
        }
        workspace.data.professions = std::move(candidate);
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
