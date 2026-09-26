#include "QtSkillEditor.h"
#include "AppTaskCompletionService.h"
#include "AppTaskProjectService.h"
#include "AppUtils.h"
#include "Profile.h"
#include <QtWidgets>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cmath>
#include <algorithm>
#include <limits>
#include <set>

namespace {
std::string u(const QString& value) { return value.toUtf8().toStdString(); }
QString q(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }
bool equalCatalogs(const SkillCatalog& a, const SkillCatalog& b) {
    if (a.skills().size() != b.skills().size()) return false;
    for (const auto& id : a.skills()) {
        if (!b.contains_id(id) || a.display_name(id) != b.display_name(id) ||
            std::abs(a.weight(id) - b.weight(id)) > 0.000001 ||
            a.description(id) != b.description(id) || a.category(id) != b.category(id) ||
            a.professions(id) != b.professions(id)) return false;
    }
    return true;
}
bool safeField(const QString& text) {
    for (const auto ch : text)
        if (ch == '|' || ch.category() == QChar::Other_Control ||
            ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator) return false;
    return true;
}

bool totalSkillXp(const Skill& skill, int& total) {
    if (skill.level < 1 || skill.xp < 0) return false;
    std::int64_t value = skill.xp;
    for (int level = 2; level <= skill.level; ++level) {
        const int needed = Skill::required_xp_for(level);
        if (needed <= 0 || value > std::numeric_limits<int>::max() - needed) return false;
        value += needed;
    }
    total = int(value);
    return true;
}

bool mergedSkill(const Skill& first, const Skill& second, Skill& output) {
    int firstTotal = 0, secondTotal = 0;
    if (!totalSkillXp(first, firstTotal) || !totalSkillXp(second, secondTotal) ||
        firstTotal > std::numeric_limits<int>::max() - secondTotal) return false;
    output = second;
    output.level = 1;
    output.xp = firstTotal + secondTotal;
    output.xpToNext = Skill::required_xp_for(2);
    while (output.xp >= output.xpToNext) {
        output.xp -= output.xpToNext;
        if (output.level == std::numeric_limits<int>::max()) return false;
        ++output.level;
        output.xpToNext = Skill::required_xp_for(output.level + 1);
        if (output.xpToNext <= 0) return false;
    }
    return true;
}

bool sameSkills(const std::vector<Skill>& a, const std::vector<Skill>& b) {
    if (a.size() != b.size()) return false;
    for (const auto& skill : a) {
        const auto found = std::find_if(b.begin(), b.end(), [&](const auto& other) { return other.name == skill.name; });
        if (found == b.end() || found->level != skill.level || found->xp != skill.xp ||
            found->xpToNext != skill.xpToNext || std::abs(found->weight - skill.weight) > 0.000001) return false;
    }
    return true;
}

bool sameAchievements(const std::vector<Achievement>& a, const std::vector<Achievement>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].title != b[i].title || a[i].skill != b[i].skill ||
            std::abs(a[i].bonusPercent - b[i].bonusPercent) > 0.000001 ||
            a[i].awardedAt != b[i].awardedAt || a[i].expiresAt != b[i].expiresAt || a[i].icon != b[i].icon) return false;
    }
    return true;
}
}

QString SaveQtSkill(QtWorkspace& workspace, const std::string& id, const QString& name,
                    double weight, const QString& description, const QString& category,
                    const std::optional<std::vector<std::string>>& professions) {
    const auto title = name.trimmed(), desc = description.trimmed(), cat = category.trimmed();
    if (title.isEmpty() || desc.isEmpty()) return QString::fromUtf8("Название и описание обязательны.");
    if (!safeField(title) || !safeField(desc) || !safeField(cat))
        return QString::fromUtf8("Формат каталога не поддерживает переносы строк, символ | и управляющие символы.");
    if (!std::isfinite(weight) || weight < 0.5 || weight > 1.6)
        return QString::fromUtf8("Вес должен быть от 0,5 до 1,6.");
    if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return QString::fromUtf8("Сначала завершите восстановление XP через обновление данных.");
    if (!id.empty() && !workspace.catalog.contains_id(id)) return QString::fromUtf8("Навык больше не существует.");
    const auto duplicate = workspace.catalog.id_for_name(u(title));
    if (duplicate && *duplicate != id) return QString::fromUtf8("Навык с таким названием уже существует.");
    const auto bindings = professions.value_or(workspace.catalog.professions(id));
    for (const auto& binding : bindings) {
        if (binding.empty() || !safeField(q(binding)) || binding.find_first_of(",;") != std::string::npos)
            return QString::fromUtf8("Некорректный идентификатор профессии.");
        const auto previous = workspace.catalog.professions(id);
        if (std::find(previous.begin(), previous.end(), binding) == previous.end() &&
            std::none_of(workspace.data.professions.begin(), workspace.data.professions.end(),
                [&](const auto& p) { return p.id == binding; }))
            return QString::fromUtf8("Выбранная профессия больше не существует.");
    }

    // The legacy writer returns void. Confine it to a disposable directory and
    // verify every record after serialization before touching the live catalog.
    const auto path = q((workspace.directory / "skills.txt").u8string());
    const bool existed = QFileInfo::exists(path);
    QByteArray original;
    if (existed) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) return QString::fromUtf8("Не удалось прочитать каталог.");
        original = input.readAll();
        if (input.error() != QFileDevice::NoError) return QString::fromUtf8("Ошибка чтения каталога.");
    }
    QTemporaryDir staging;
    if (!staging.isValid()) return QString::fromUtf8("Не удалось создать временный каталог.");
    const auto stagedPath = staging.path() + "/skills.txt";
    if (existed && !QFile::copy(path, stagedPath)) return QString::fromUtf8("Не удалось скопировать каталог для проверки.");
    SkillCatalog candidate(std::filesystem::u8path(u(staging.path())));
    if (!equalCatalogs(candidate, workspace.catalog))
        return QString::fromUtf8("Каталог изменился на диске. Обновите данные и повторите действие.");
    if (id.empty()) candidate.add_skill(u(title), weight, u(desc), u(cat), bindings);
    else candidate.update_skill(id, u(title), weight, u(desc), u(cat), bindings);
    SkillCatalog verified(std::filesystem::u8path(u(staging.path())));
    const auto savedId = verified.id_for_name(u(title));
    if (!savedId || (!id.empty() && *savedId != id) || !equalCatalogs(candidate, verified) ||
        verified.description(*savedId) != u(desc) || verified.category(*savedId) != u(cat) ||
        std::abs(verified.weight(*savedId) - weight) > 0.000001)
        return QString::fromUtf8("Проверка сохранённого каталога не пройдена. Исходные данные не изменены.");
    QFile staged(stagedPath);
    if (!staged.open(QIODevice::ReadOnly)) return QString::fromUtf8("Не удалось прочитать результат сохранения.");
    const auto bytes = staged.readAll();
    if (staged.error() != QFileDevice::NoError) return QString::fromUtf8("Ошибка чтения результата сохранения.");
    QFile current(path);
    if (QFileInfo::exists(path) != existed ||
        (existed && (!current.open(QIODevice::ReadOnly) || current.readAll() != original || current.error() != QFileDevice::NoError)))
        return QString::fromUtf8("Каталог изменился во время сохранения. Обновите данные.");
    current.close();
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        return QString::fromUtf8("Не удалось записать каталог. Исходный файл сохранён.");
    workspace.catalog.reload();
    return {};
}

QString MergeQtSkills(QtWorkspace& workspace, const std::string& restoreProfileId,
                      const std::string& fromId, const std::string& toId,
                      const QString& name, double weight, const QString& description,
                      const QString& category, const std::vector<std::string>& professions) {
    const auto title = name.trimmed(), desc = description.trimmed(), cat = category.trimmed();
    if (fromId.empty() || toId.empty() || fromId == toId || !workspace.catalog.contains_id(fromId) ||
        !workspace.catalog.contains_id(toId)) return QString::fromUtf8("Исходный или целевой навык больше не существует.");
    if (title.isEmpty() || desc.isEmpty() || !safeField(title) || !safeField(desc) || !safeField(cat) ||
        !std::isfinite(weight) || weight < 0.5 || weight > 1.6)
        return QString::fromUtf8("Проверьте название, описание и вес целевого навыка.");
    if (auto duplicate = workspace.catalog.id_for_name(u(title)); !duplicate || *duplicate != toId)
        return QString::fromUtf8("Целевое название больше не соответствует выбранному навыку.");
    if (std::filesystem::exists(workspace.directory / "meta/qt-xp-transaction"))
        return QString::fromUtf8("Сначала завершите восстановление данных через обновление.");

    const auto catalogPath = q((workspace.directory / "skills.txt").u8string());
    QFile originalCatalog(catalogPath);
    if (!originalCatalog.open(QIODevice::ReadOnly)) return QString::fromUtf8("Не удалось проверить каталог навыков.");
    const auto originalCatalogBytes = originalCatalog.readAll();
    if (originalCatalog.error() != QFileDevice::NoError) return QString::fromUtf8("Ошибка чтения каталога навыков.");
    originalCatalog.close();
    QTemporaryDir staging;
    if (!staging.isValid()) return QString::fromUtf8("Не удалось создать временный каталог.");
    const auto stagedCatalogPath = staging.path() + "/skills.txt";
    if (!QFile::copy(catalogPath, stagedCatalogPath)) return QString::fromUtf8("Не удалось подготовить слияние каталога.");
    SkillCatalog candidate(std::filesystem::u8path(u(staging.path())));
    if (!equalCatalogs(candidate, workspace.catalog)) return QString::fromUtf8("Каталог изменился на диске. Обновите данные.");
    if (!candidate.remove_skill(fromId)) return QString::fromUtf8("Не удалось убрать исходную запись из проверочной копии.");
    const bool changed = candidate.update_skill(toId, u(title), weight, u(desc), u(cat), professions);
    if (!candidate.contains_id(toId) || candidate.contains_id(fromId) ||
        candidate.display_name(toId) != u(title) || candidate.description(toId) != u(desc) ||
        candidate.category(toId) != u(cat) || candidate.professions(toId) != professions ||
        std::abs(candidate.weight(toId) - weight) > 0.001)
        return QString::fromUtf8("Проверочная копия каталога не соответствует выбранным данным.");
    (void)changed; // An unchanged destination is valid when only the source entry is removed.
    SkillCatalog verified(std::filesystem::u8path(u(staging.path())));
    if (!equalCatalogs(candidate, verified)) return QString::fromUtf8("Проверка сериализации каталога не пройдена.");
    QFile stagedCatalog(stagedCatalogPath);
    if (!stagedCatalog.open(QIODevice::ReadOnly)) return QString::fromUtf8("Не удалось прочитать проверочный каталог.");
    const auto replacementCatalogBytes = stagedCatalog.readAll();
    if (stagedCatalog.error() != QFileDevice::NoError) return QString::fromUtf8("Ошибка чтения проверочного каталога.");

    const auto profiles = workspace.storage->list_profiles();
    auto tasks = LoadTasksData(workspace.directory);
    auto audit = LoadTaskAuditData(workspace.directory);
    std::vector<std::string> profileIds;
    std::set<std::string> uniqueIds;
    profileIds.reserve(profiles.size());
    for (const auto& profile : profiles) {
        if (profile.id.empty() || !uniqueIds.insert(profile.id).second)
            return QString::fromUtf8("Список профилей содержит повторяющиеся или пустые ID.");
        profileIds.push_back(profile.id);
    }

    bool prepared = false;
    try {
        PrepareSkillMergeRecovery(workspace.directory, profileIds);
        prepared = true;
        for (const auto& info : profiles) {
            if (info.archived && !workspace.storage->set_archived(info.id, false))
                throw std::runtime_error(u8"Не удалось временно открыть архивный профиль.");
            if (!workspace.storage->set_active_profile(info.id)) {
                if (info.archived) workspace.storage->set_archived(info.id, true);
                throw std::runtime_error(u8"Не удалось выбрать профиль для переноса навыка.");
            }
            auto profile = workspace.storage->load_profile();
            if (!profile) throw std::runtime_error(u8"Не удалось загрузить профиль для переноса навыка.");
            auto profileSkills = profile->list_skills();
            int fromIndex = -1, toIndex = -1;
            for (int i = 0; i < int(profileSkills.size()); ++i) {
                if (profileSkills[size_t(i)].name == fromId) {
                    if (fromIndex >= 0) throw std::runtime_error(u8"В профиле найдено несколько копий исходного навыка; исправьте данные вручную.");
                    fromIndex = i;
                }
                if (profileSkills[size_t(i)].name == toId) {
                    if (toIndex >= 0) throw std::runtime_error(u8"В профиле найдено несколько копий целевого навыка; исправьте данные вручную.");
                    toIndex = i;
                }
            }
            bool profileChanged = false;
            if (fromIndex >= 0) {
                if (toIndex >= 0) {
                    Skill combined;
                    if (!mergedSkill(profileSkills[size_t(fromIndex)], profileSkills[size_t(toIndex)], combined))
                        throw std::runtime_error(u8"XP навыков выходит за безопасный диапазон.");
                    combined.name = toId;
                    profileSkills[size_t(toIndex)] = combined;
                    profileSkills.erase(profileSkills.begin() + fromIndex);
                } else {
                    profileSkills[size_t(fromIndex)].name = toId;
                }
                profileChanged = true;
            }
            auto achievements = profile->achievements();
            bool achievementChanged = false;
            for (auto& achievement : achievements) if (achievement.skill == fromId) {
                achievement.skill = toId;
                achievementChanged = true;
            }
            if (profileChanged || achievementChanged) {
                profile->set_skills(profileSkills);
                if (achievementChanged) profile->set_achievements(achievements);
                SyncProfileWithCatalog(*profile, candidate);
                if (!workspace.storage->save_profile(*profile))
                    throw std::runtime_error(u8"Не удалось сохранить изменённый профиль.");
                const auto checked = workspace.storage->load_profile();
                if (!checked || !sameSkills(profile->list_skills(), checked->list_skills()) ||
                    !sameAchievements(profile->achievements(), checked->achievements()))
                    throw std::runtime_error(u8"Проверка сохранённого профиля не пройдена.");
            }
            if (info.archived && !workspace.storage->set_archived(info.id, true))
                throw std::runtime_error(u8"Не удалось вернуть профиль в архив.");
        }
        if (!restoreProfileId.empty() && !workspace.storage->set_active_profile(restoreProfileId))
            throw std::runtime_error(u8"Не удалось восстановить выбранный профиль.");

        for (const auto& task : tasks) {
            std::vector<std::string> bindings;
            bool taskChanged = false;
            for (const auto& binding : task.skillIds) {
                const auto& resolved = binding == fromId ? toId : binding;
                taskChanged = taskChanged || binding == fromId;
                if (std::find(bindings.begin(), bindings.end(), resolved) == bindings.end()) bindings.push_back(resolved);
                else if (binding == fromId) taskChanged = true;
            }
            if (!taskChanged) continue;
            const auto result = AppUpdateTaskSkillIds(workspace.directory, tasks, task.id,
                bindings, "admin/qt", &audit);
            if (!result.ok) throw std::runtime_error(result.errorMessage.empty() ? u8"Не удалось перенести навык задачи." : result.errorMessage);
        }

        QFile currentCatalog(catalogPath);
        if (!currentCatalog.open(QIODevice::ReadOnly) || currentCatalog.readAll() != originalCatalogBytes ||
            currentCatalog.error() != QFileDevice::NoError)
            throw std::runtime_error(u8"Каталог навыков изменился во время слияния.");
        currentCatalog.close();
        QSaveFile output(catalogPath);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly) || output.write(replacementCatalogBytes) != replacementCatalogBytes.size() || !output.commit())
            throw std::runtime_error(u8"Не удалось атомарно сохранить объединённый каталог.");
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
            } catch (const std::exception&) {
                message += u8" Восстановление не завершено; журнал сохранён до перезапуска Qt.";
            }
        }
        return q(message);
    }
}

bool ShowSkillEditor(QWidget* parent, QtWorkspace& workspace, const std::string& id,
                     const std::string& restoreProfileId) {
    if (!id.empty() && !workspace.catalog.contains_id(id)) return false;
    QDialog dialog(parent);
    dialog.setObjectName("skillEditor");
    dialog.setWindowTitle(QString::fromUtf8(id.empty() ? "Новый навык" : "Редактирование навыка"));
    dialog.setMinimumWidth(480);
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(id.empty() ? QString() : q(workspace.catalog.display_name(id)));
    name->setObjectName("skillName");
    auto* description = new QLineEdit(id.empty() ? QString() : q(workspace.catalog.description(id)));
    description->setObjectName("skillDescription");
    auto* category = new QLineEdit(id.empty() ? QString() : q(workspace.catalog.category(id)));
    category->setObjectName("skillCategory");
    auto* weight = new QDoubleSpinBox;
    weight->setObjectName("skillWeight");
    weight->setRange(0.5, 1.6);
    weight->setDecimals(6);
    weight->setSingleStep(0.05);
    weight->setValue(id.empty() ? 1.0 : workspace.catalog.weight(id));
    form->addRow(QString::fromUtf8("Название"), name);
    form->addRow(QString::fromUtf8("Описание"), description);
    form->addRow(QString::fromUtf8("Категория"), category);
    form->addRow(QString::fromUtf8("Вес"), weight);
    auto* professionList = new QListWidget;
    professionList->setObjectName("skillProfessions");
    professionList->setMaximumHeight(120);
    const auto previousBindings = workspace.catalog.professions(id);
    auto addProfession = [&](const std::string& key, const QString& title, bool checked) {
        auto* item = new QListWidgetItem(title, professionList);
        item->setData(Qt::UserRole, q(key));
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    };
    for (const auto& binding : previousBindings) {
        const auto found = std::find_if(workspace.data.professions.begin(), workspace.data.professions.end(),
            [&](const auto& p) { return p.id == binding; });
        addProfession(binding, found == workspace.data.professions.end() ? q(binding) + QString::fromUtf8(" · недоступна") : q(found->name), true);
    }
    for (const auto& p : workspace.data.professions)
        if (std::find(previousBindings.begin(), previousBindings.end(), p.id) == previousBindings.end()) addProfession(p.id, q(p.name), false);
    form->addRow(QString::fromUtf8("Профессии"), professionList);
    auto* hint = new QLabel(QString::fromUtf8("Описание — одна строка. Накопленный XP не меняется. Недоступные связи сохраняются, пока вы сами их не снимете."));
    hint->setWordWrap(true);
    form->addRow(hint);
    auto* notice = new QLabel;
    notice->setObjectName("editorNotice");
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    form->addRow(notice);
    for (auto* field : {name, description, category})
        QObject::connect(field, &QLineEdit::textChanged, notice, &QLabel::clear);
    QObject::connect(weight, &QDoubleSpinBox::valueChanged, notice, &QLabel::clear);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        std::vector<std::string> selected;
        for (int i = 0; i < professionList->count(); ++i) if (professionList->item(i)->checkState() == Qt::Checked)
            selected.push_back(u(professionList->item(i)->data(Qt::UserRole).toString()));
        const auto duplicate = name->text().trimmed().isEmpty() ? std::optional<std::string>()
            : workspace.catalog.id_for_name(u(name->text().trimmed()));
        if (!id.empty() && duplicate && *duplicate != id) {
            QMessageBox confirm(QMessageBox::Warning, QString::fromUtf8("Объединить навыки"),
                QString::fromUtf8("Навык «%1» уже существует.\n\nXP и достижения исходного навыка будут перенесены по всем профилям, включая архивные; привязки задач тоже обновятся. Исходная запись будет удалена.")
                    .arg(q(workspace.catalog.display_name(*duplicate))), QMessageBox::Yes | QMessageBox::Cancel, &dialog);
            confirm.setObjectName("skillMergeConfirm");
            confirm.setDefaultButton(QMessageBox::Cancel);
            confirm.button(QMessageBox::Yes)->setText(QString::fromUtf8("Объединить"));
            confirm.button(QMessageBox::Cancel)->setText(QString::fromUtf8("Отмена"));
            if (confirm.exec() != QMessageBox::Yes) return;
            const auto error = MergeQtSkills(workspace, restoreProfileId, id, *duplicate,
                name->text(), weight->value(), description->text(), category->text(), selected);
            if (!error.isEmpty()) { notice->setText(error); return; }
            dialog.accept();
            return;
        }
        const auto error = SaveQtSkill(workspace, id, name->text(), weight->value(), description->text(), category->text(), selected);
        if (!error.isEmpty()) { notice->setText(error); return; }
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
