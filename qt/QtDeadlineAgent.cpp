#include "QtDeadlineAgent.h"
#include "QtDisplaySettings.h"

#include "AppWorkspaceDataService.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>

#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

QString QtDeadlinePowerShellExecutable() {
#ifdef _WIN32
    std::vector<wchar_t> systemDirectory(MAX_PATH);
    for (;;) {
        const auto length = GetSystemDirectoryW(systemDirectory.data(), static_cast<UINT>(systemDirectory.size()));
        if (!length) return {};
        if (length < systemDirectory.size())
            return QDir(QString::fromWCharArray(systemDirectory.data(), static_cast<int>(length)))
                .filePath(QStringLiteral("WindowsPowerShell/v1.0/powershell.exe"));
        systemDirectory.resize(static_cast<std::size_t>(length) + 1);
    }
#else
    return {};
#endif
}

namespace {
constexpr auto kTaskName = "Pharos.ForgeMirrorQt.DeadlineReminders";
constexpr std::int64_t kDay = 24 * 60 * 60;

bool runTaskScript(const QString& script) {
    const auto executable = QtDeadlinePowerShellExecutable();
    if (executable.isEmpty()) return false;
    QProcess process;
    QByteArray utf16(reinterpret_cast<const char*>(script.utf16()), script.size() * int(sizeof(ushort)));
    process.start(executable, {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-EncodedCommand"), QString::fromLatin1(utf16.toBase64())});
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool readPreviousSignature(const QString& path, QByteArray& signature) {
    const QFileInfo info(path);
    if (!info.exists()) return true;
    if (info.isSymLink() || info.size() > 64 * 1024) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    signature = document.object().value(QStringLiteral("signature")).toString().toLatin1();
    return true;
}

bool writeSignature(const QString& path, const QByteArray& signature) {
    const auto info = QFileInfo(path);
    if (info.isSymLink() || QFileInfo(info.absolutePath()).isSymLink()) return false;
    QJsonObject object;
    object.insert(QStringLiteral("signature"), QString::fromLatin1(signature));
    object.insert(QStringLiteral("updatedAt"), QDateTime::currentSecsSinceEpoch());
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}

QtDeadlineSummary EvaluateQtDeadlines(const std::vector<TaskEntry>& tasks, std::int64_t now) {
    QtDeadlineSummary result;
    QStringList signatureParts;
    for (const auto& task : tasks) {
        if (task.deadlineAt <= 0 || task.status == 2) continue;
        if (task.deadlineAt <= now) {
            ++result.overdue;
            signatureParts << QStringLiteral("O:%1:%2").arg(QString::fromUtf8(task.id), QString::number(task.deadlineAt));
        } else if (task.deadlineAt <= now + kDay) {
            ++result.upcoming;
            signatureParts << QStringLiteral("U:%1:%2").arg(QString::fromUtf8(task.id), QString::number(task.deadlineAt));
        }
    }
    signatureParts.sort(Qt::CaseSensitive);
    result.signature = QCryptographicHash::hash(signatureParts.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex();
    return result;
}

bool ConfigureQtDeadlineSchedule(bool enabled, QString* error) {
#ifdef _WIN32
    const auto exe = QDir::toNativeSeparators(QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath());
    if (enabled && exe.isEmpty()) { if (error) *error = QString::fromUtf8("Не удалось определить путь программы."); return false; }
    const auto encodedExe = QString::fromLatin1(exe.toUtf8().toBase64());
    const auto enabledValue = enabled ? QStringLiteral("$true") : QStringLiteral("$false");
    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop'; $taskName='%1'; "
        "$exe=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('%2')); "
        "$existing=Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue; "
        "if($existing){ if($existing.Actions.Count -ne 1 -or "
        "[IO.Path]::GetFullPath($existing.Actions[0].Execute) -ine [IO.Path]::GetFullPath($exe) -or "
        "$existing.Actions[0].Arguments -ne '--deadline-agent'){ throw 'Task identity mismatch' }; "
        "if(%3){ exit 0 }; Unregister-ScheduledTask -TaskName $taskName -Confirm:$false; exit 0 }; "
        "if(-not (%3)){ exit 0 }; "
        "$action=New-ScheduledTaskAction -Execute $exe -Argument '--deadline-agent'; "
        "$trigger=New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) -RepetitionInterval (New-TimeSpan -Minutes 15); "
        "$settings=New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 2); "
        "$principal=New-ScheduledTaskPrincipal -UserId ($env:USERDOMAIN+'\\'+$env:USERNAME) -LogonType Interactive -RunLevel Limited; "
        "Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings "
        "-Principal $principal | Out-Null")
        .arg(QString::fromLatin1(kTaskName), encodedExe, enabledValue);
    if (!runTaskScript(script)) {
        if (error) *error = enabled
            ? QString::fromUtf8("Не удалось включить задание напоминаний Windows или его имя занято.")
            : QString::fromUtf8("Не удалось удалить собственное задание напоминаний Windows.");
        return false;
    }
    return true;
#else
    if (enabled) {
        if (error) *error = QString::fromUtf8("Напоминания при закрытой программе поддерживаются только в Windows.");
        return false;
    }
    return true;
#endif
}

int RunQtDeadlineAgent(const std::filesystem::path& workspace) {
    const auto metaPath = QString::fromStdWString((workspace / "meta").wstring());
    const auto uiPath = metaPath + QStringLiteral("/ui.ini");
    if (QFileInfo(QString::fromStdWString(workspace.wstring())).isSymLink() || QFileInfo(metaPath).isSymLink() ||
        QFileInfo(uiPath).isSymLink()) return 1;
    if (!LoadQtDisplaySettings(workspace).deadlineNotificationsWhenClosed) return 0;
    const auto tasksPath = QString::fromStdWString((workspace / "meta/tasks.json").wstring());
    const QFileInfo tasksInfo(tasksPath);
    if (!tasksInfo.exists() || tasksInfo.isSymLink() || tasksInfo.size() > 16 * 1024 * 1024) return 0;
    const auto tasks = LoadTasksDataReadOnly(workspace);
    const auto summary = EvaluateQtDeadlines(tasks, QDateTime::currentSecsSinceEpoch());
    const auto statePath = metaPath + QStringLiteral("/qt-deadline-agent-state.json");
    if (QFileInfo(metaPath).isSymLink()) return 1;
    QByteArray previous;
    if (!readPreviousSignature(statePath, previous)) return 1;
    if (previous == summary.signature) return 0;
    const bool hasDeadlines = summary.upcoming || summary.overdue;
    if (hasDeadlines && (!QSystemTrayIcon::isSystemTrayAvailable() || !QSystemTrayIcon::supportsMessages())) return 1;
    if (!writeSignature(statePath, summary.signature)) return 1;
    if (!hasDeadlines) return 0;
    QSystemTrayIcon tray;
    tray.setIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation));
    tray.show();
    QStringList lines;
    if (summary.overdue) lines << QString::fromUtf8("Просрочено задач: %1").arg(summary.overdue);
    if (summary.upcoming) lines << QString::fromUtf8("Дедлайн в ближайшие 24 часа: %1").arg(summary.upcoming);
    tray.showMessage(QStringLiteral("ForgeMirror"), lines.join(QLatin1Char('\n')),
                     QSystemTrayIcon::Information, 8000);
    QTimer::singleShot(8500, qApp, &QCoreApplication::quit);
    return qApp->exec();
}
