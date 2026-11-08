// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include "common/fs/path_util.h"
#include "common/scm_rev.h"
#include "qt_common/util/zip_dir.h"
#include "yuzu/bug_report.h"
#include "yuzu/nx2_switches_dialog.h"

namespace BugReport {

namespace {

QString ToQString(const std::filesystem::path& path) {
    return QString::fromStdU16String(path.u16string());
}

/// The account name, as it appears in paths (C:/Users/<name>/...) and some log lines.
QString UserName() {
    for (const char* variable : {"USERNAME", "USER", "LOGNAME"}) {
        const QString value = qEnvironmentVariable(variable);
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString Censor(QString text) {
    const QString user = UserName();
    // A name of one or two letters would mangle unrelated words.
    if (user.size() >= 3) {
        text.replace(QRegularExpression(QStringLiteral("(?<=[\\\\/])%1(?=[\\\\/])")
                                            .arg(QRegularExpression::escape(user)),
                                        QRegularExpression::CaseInsensitiveOption),
                     QStringLiteral("user"));
    }
    return text;
}

/// Ini lines holding secrets: account tokens, API keys, passwords.
bool IsSecret(const QString& key) {
    static const QRegularExpression secret{
        QStringLiteral("(token|api_key|password|secret)"),
        QRegularExpression::CaseInsensitiveOption};
    return secret.match(key).hasMatch() && !key.contains(QStringLiteral("\\default"));
}

bool CopyText(const QString& from, const QString& to, bool is_ini) {
    QFile in{from};
    if (!in.open(QIODevice::ReadOnly)) {
        return false;
    }
    QString text = QString::fromUtf8(in.readAll());
    if (is_ini) {
        QStringList lines = text.split(QLatin1Char('\n'));
        for (QString& line : lines) {
            const qsizetype eq = line.indexOf(QLatin1Char('='));
            if (eq > 0 && IsSecret(line.left(eq))) {
                line = line.left(eq + 1) + QStringLiteral("<removed>");
            }
        }
        text = lines.join(QLatin1Char('\n'));
    }
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile out{to};
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    out.write(Censor(text).toUtf8());
    return true;
}

} // Anonymous namespace

void Create(QWidget* parent) {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm"));
    const QString suggested =
        QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
            .filePath(QStringLiteral("lichii-report-%1.zip").arg(stamp));
    const QString zip_path = QFileDialog::getSaveFileName(
        parent, QObject::tr("Create Bug Report"), suggested, QObject::tr("Zip archive (*.zip)"));
    if (zip_path.isEmpty()) {
        return;
    }

    QTemporaryDir staging;
    if (!staging.isValid()) {
        QMessageBox::warning(parent, QObject::tr("Create Bug Report"),
                             QObject::tr("Could not create a temporary folder."));
        return;
    }
    const QDir out{staging.path()};

    using Common::FS::EdenPath;
    const QDir log_dir{ToQString(Common::FS::GetEdenPath(EdenPath::LogDir))};
    const QDir config_dir{ToQString(Common::FS::GetEdenPath(EdenPath::ConfigDir))};

    QStringList included;
    const auto add = [&](const QString& from, const QString& name, bool is_ini) {
        if (QFileInfo::exists(from) && CopyText(from, out.filePath(name), is_ini)) {
            included << name;
        }
    };
    for (const QString& log : log_dir.entryList({QStringLiteral("eden_log.txt*")}, QDir::Files)) {
        add(log_dir.filePath(log), QStringLiteral("log/") + log, false);
    }
    add(config_dir.filePath(QStringLiteral("qt-config.ini")), QStringLiteral("config/qt-config.ini"),
        true);
    const QDir custom_dir{config_dir.filePath(QStringLiteral("custom"))};
    for (const QString& ini : custom_dir.entryList({QStringLiteral("*.ini")}, QDir::Files)) {
        add(custom_dir.filePath(ini), QStringLiteral("config/custom/") + ini, true);
    }
    add(ToQString(Nx2SwitchesDialog::UserSwitchFile()), QStringLiteral("nx2_switches.txt"), false);

    {
        QFile readme{out.filePath(QStringLiteral("README.txt"))};
        if (readme.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream{&readme};
            stream << "Lichii bug report\n"
                   << "Build: " << Common::g_build_version << " | " << Common::g_scm_branch << "-"
                   << Common::g_scm_desc << "\n"
                   << "Created: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n"
                   << "Included:\n  " << included.join(QStringLiteral("\n  ")) << "\n\n"
                   << "Not included: decryption keys, games, firmware, saves. Account tokens and "
                      "API keys in the configuration were replaced with <removed>, and the user "
                      "name in paths with \"user\".\n";
        }
    }

    QFile::remove(zip_path);
    if (!QtCommon::Compress::ZipDirectory(zip_path, staging.path())) {
        QMessageBox::warning(parent, QObject::tr("Create Bug Report"),
                             QObject::tr("Could not write %1.").arg(zip_path));
        return;
    }
    QMessageBox::information(
        parent, QObject::tr("Create Bug Report"),
        QObject::tr("Report saved to:\n%1\n\nIt contains the logs and settings, without keys, "
                    "account tokens or API keys. Check it before sharing.")
            .arg(QDir::toNativeSeparators(zip_path)));
}

} // namespace BugReport
