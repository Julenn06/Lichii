// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include "common/logging.h"
#include "frontend_common/update_checker.h"
#include "qt_common/abstract/frontend.h"
#include "ui_update_dialog.h"
#include "update_dialog.h"

#undef GetSaveFileName

namespace {

#ifdef _WIN32
// Runs detached once Lichii has asked to update: waits for this process to exit, unpacks the
// package and copies it over the program folder (user/ is never in a package, and an existing
// nx2_switches.txt is kept), restoring the previous files if anything fails, then starts Lichii.
constexpr char InstallerScript[] = R"ps(param(
    [Parameter(Mandatory = $true)][string]$Zip,
    [Parameter(Mandatory = $true)][string]$Dest,
    [Parameter(Mandatory = $true)][int]$WaitPid,
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Log
)
$ErrorActionPreference = 'Stop'
function Write-Log([string]$Text) {
    try { Add-Content -LiteralPath $Log -Value ('[{0:HH:mm:ss}] {1}' -f (Get-Date), $Text) } catch {}
}
# Antivirus scans and the closing process can hold a file for a moment: retry for ~10 s.
function Copy-WithRetry([string]$From, [string]$To) {
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        try { Copy-Item -LiteralPath $From -Destination $To -Force; return }
        catch { Start-Sleep -Milliseconds 250 }
    }
    throw "could not copy $From to $To"
}
$stage = Join-Path ([IO.Path]::GetTempPath()) ('lichii-update-' + [guid]::NewGuid().ToString('N'))
$backup = Join-Path $stage 'backup'
$replaced = New-Object System.Collections.Generic.List[string]
$created = New-Object System.Collections.Generic.List[string]
try {
    Write-Log "Waiting for Lichii (PID $WaitPid) to close"
    $process = Get-Process -Id $WaitPid -ErrorAction SilentlyContinue
    if ($process) { $process.WaitForExit() }
    Start-Sleep -Milliseconds 500
    Write-Log "Extracting $Zip"
    $unpacked = Join-Path $stage 'files'
    Expand-Archive -LiteralPath $Zip -DestinationPath $unpacked -Force
    $root = (Get-Item -LiteralPath $unpacked).FullName.TrimEnd('\')
    if (-not (Test-Path -LiteralPath (Join-Path $root 'lichii.exe'))) { throw 'the package has no lichii.exe' }
    $items = @(Get-ChildItem -LiteralPath $root -Recurse -File)
    foreach ($item in $items) {
        $rel = $item.FullName.Substring($root.Length).TrimStart('\')
        $target = Join-Path $Dest $rel
        if ($rel -ieq 'nx2_switches.txt' -and (Test-Path -LiteralPath $target)) { continue }
        $folder = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $folder)) { New-Item -ItemType Directory -Force -Path $folder | Out-Null }
        if (Test-Path -LiteralPath $target) {
            $saved = Join-Path $backup $rel
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $saved) | Out-Null
            Copy-WithRetry $target $saved
            $replaced.Add($rel)
        } else {
            $created.Add($rel)
        }
        Copy-WithRetry $item.FullName $target
    }
    Write-Log "Installed $($items.Count) files into $Dest"
    Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue
} catch {
    Write-Log "Update failed: $_ - restoring the previous files"
    foreach ($rel in $replaced) {
        try { Copy-WithRetry (Join-Path $backup $rel) (Join-Path $Dest $rel) }
        catch { Write-Log "Could not restore $rel" }
    }
    foreach ($rel in $created) { Remove-Item -LiteralPath (Join-Path $Dest $rel) -Force -ErrorAction SilentlyContinue }
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
Start-Process -FilePath $Exe -WorkingDirectory $Dest
)ps";
#endif

} // namespace

UpdateDialog::UpdateDialog(const Common::Net::Release& release, QWidget* parent)
    : QDialog(parent), ui(new Ui::UpdateDialog) {
    ui->setupUi(this);

    m_release_url = QString::fromStdString(release.html_url);
    ui->version->setText(
        tr("%1 is available for download.").arg(QString::fromStdString(release.title)));
    ui->url->setText(tr("<a href=\"%1\">View on GitHub</a>").arg(m_release_url));

    std::string text{release.body};
    if (auto pos = text.find("# Packages"); pos != std::string::npos) {
        text = text.substr(0, pos);
    }

    ui->body->setMarkdown(QString::fromStdString(text));

    // This fork's releases (vX.Y.Z with a Lichii-Frizti-v3-mejorado-*.zip): install in place.
    const auto package = UpdateChecker::GetPackage(release);
#ifdef _WIN32
    if (package && UpdateChecker::ParseVersion(release.tag)) {
        m_asset = *package;
        ui->groupBox->setHidden(true);
        const QString current = QString::fromUtf8(UpdateChecker::CurrentVersion().data(),
                                                  UpdateChecker::CurrentVersion().size());
        ui->version->setText(tr("<b>%1</b> is available. You have version %2.")
                                 .arg(QString::fromStdString(release.title).toHtmlEscaped(),
                                      current.isEmpty() ? tr("unknown")
                                                        : QStringLiteral("v") + current));
        ui->version->setTextFormat(Qt::RichText);
        ui->version->setWordWrap(true);
        ui->label_2->setText(
            tr("Lichii will download the update, close, replace its program files and start "
               "again. Your games, saves, keys and settings are kept."));
        ui->label_2->setWordWrap(true);
        if (QPushButton* yes = ui->buttonBox->button(QDialogButtonBox::Yes)) {
            yes->setText(tr("Install and restart"));
            yes->setDefault(true);
            // The theme gives buttons a fixed padding; leave room for the longer label.
            yes->setMinimumWidth(yes->fontMetrics().horizontalAdvance(yes->text()) + 56);
        }
        if (QPushButton* no = ui->buttonBox->button(QDialogButtonBox::No)) {
            no->setText(tr("Later"));
        }
        connect(this, &QDialog::accepted, this, &UpdateDialog::Install);
        return;
    }
#endif

    // TODO(crueter): Find a way to set default
    const auto assets = release.GetPlatformAssets();

    if (assets.empty()) {
        ui->groupBox->setHidden(true);
        connect(this, &QDialog::accepted, this, [release]() {
            QDesktopServices::openUrl(QUrl{QString::fromStdString(release.html_url)});
        });
    } else if (assets.size() == 1) {
        ui->groupBox->setHidden(true);
        m_asset = assets[0];

        connect(this, &QDialog::accepted, this, &UpdateDialog::Download);
    } else {
        u32 i = 0;
        for (const Common::Net::Asset& a : assets) {
            QRadioButton* r = new QRadioButton(tr(a.name.c_str()), this);
            connect(r, &QRadioButton::toggled, this, [a, this](bool checked) {
                if (checked)
                    m_asset = a;
            });

            if (i == 0)
                r->setChecked(true);
            ++i;

            ui->radioButtons->addWidget(r);
        }

        connect(this, &QDialog::accepted, this, &UpdateDialog::Download);
    }
}

UpdateDialog::~UpdateDialog() {
    delete ui;
}

bool UpdateDialog::DownloadTo(const QString& filename) {
    QSaveFile file(filename);
    if (!file.open(QIODevice::Truncate | QIODevice::WriteOnly)) {
        LOG_WARNING(Frontend, "Could not open file {}", filename.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to save file"),
                                     tr("Could not open file %1 for writing.").arg(filename));
        return false;
    }

    QProgressDialog progress(tr("Downloading %1...").arg(QString::fromStdString(m_asset.filename)),
                             tr("Cancel"), 0, 100, parentWidget());
    progress.setWindowTitle(tr("Downloading..."));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);
    QCoreApplication::processEvents();

    bool write_failed = false;
    const auto result = Common::Net::Download(
        m_asset.url, m_asset.path,
        [&](const char* chunk, std::size_t length) {
            if (file.write(chunk, static_cast<qint64>(length)) == -1) {
                write_failed = true;
                return false;
            }
            return true;
        },
        [&](u64 current, u64 total) {
            if (total != 0) {
                progress.setValue(static_cast<int>((current * 100) / total));
            }
            QCoreApplication::processEvents();
            return !progress.wasCanceled();
        });
    const bool canceled = progress.wasCanceled();
    progress.close();

    if (canceled) {
        file.cancelWriting();
        return false;
    }
    if (write_failed) {
        LOG_WARNING(Frontend, "Could not write to file {}", filename.toStdString());
        file.cancelWriting();
        QtCommon::Frontend::Critical(tr("Failed to save file"),
                                     tr("Could not write to file %1.").arg(filename));
        return false;
    }
    if (!result.ok) {
        file.cancelWriting();
        const QString reason = result.status != 0
                                   ? tr("Error code: %1").arg(result.status)
                                   : QString::fromStdString(result.error);
        QtCommon::Frontend::Critical(tr("Failed to download file"),
                                     tr("Could not download from %1%2\n%3")
                                         .arg(QString::fromStdString(m_asset.url),
                                              QString::fromStdString(m_asset.path), reason));
        return false;
    }
    if (!file.commit()) {
        LOG_WARNING(Frontend, "Could not commit to file {}", filename.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to save file"),
                                     tr("Could not commit to file %1.").arg(filename));
        return false;
    }
    return true;
}

void UpdateDialog::Download() {
    const auto filename = QtCommon::Frontend::GetSaveFileName(
        tr("New Version Location"),
        qApp->applicationDirPath() % QStringLiteral("/") % QString::fromStdString(m_asset.filename),
        tr("All Files (*.*)"));

    if (filename.isEmpty() || !DownloadTo(filename))
        return;

    // Download is complete. User may choose to open in the file manager.
    auto button =
        QtCommon::Frontend::Question(tr("Download Complete"),
                                     tr("Successfully downloaded %1. Would you like to open it?")
                                         .arg(QString::fromStdString(m_asset.filename)),
                                     QtCommon::Frontend::Yes | QtCommon::Frontend::No);

    if (button == QtCommon::Frontend::Yes) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
    }
}

void UpdateDialog::Install() {
#ifdef _WIN32
    const QString app_dir = QCoreApplication::applicationDirPath();
    QWidget* const owner = parentWidget();

    // Installed somewhere Lichii cannot write to (Program Files): send the user to the release.
    {
        QFile probe(app_dir + QStringLiteral("/.lichii-update-probe"));
        if (!probe.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(owner, tr("Update"),
                                 tr("Lichii cannot write to its folder:\n%1\n\nDownload the new "
                                    "version from GitHub and extract it over this one.")
                                     .arg(QDir::toNativeSeparators(app_dir)));
            QDesktopServices::openUrl(QUrl{m_release_url});
            return;
        }
        probe.close();
        probe.remove();
    }

    const QDir temp{QStandardPaths::writableLocation(QStandardPaths::TempLocation)};
    const QString zip = temp.filePath(QString::fromStdString(m_asset.filename));
    if (!DownloadTo(zip)) {
        return;
    }

    // GitHub publishes each asset's SHA-256: a truncated or altered download is not installed.
    constexpr std::string_view sha256_prefix = "sha256:";
    if (std::string_view{m_asset.digest}.starts_with(sha256_prefix)) {
        QFile file(zip);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        const bool read = file.open(QIODevice::ReadOnly) && hash.addData(&file);
        file.close();
        const QByteArray expected =
            QByteArray::fromStdString(m_asset.digest.substr(sha256_prefix.size())).toLower();
        if (!read || hash.result().toHex() != expected) {
            QFile::remove(zip);
            QMessageBox::critical(owner, tr("Update"),
                                  tr("The downloaded file is damaged (its checksum does not "
                                     "match). Try again later."));
            return;
        }
    }

    const QString script_path = temp.filePath(QStringLiteral("lichii-update.ps1"));
    QSaveFile script(script_path);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        script.write(InstallerScript, sizeof(InstallerScript) - 1) == -1 || !script.commit()) {
        QMessageBox::critical(owner, tr("Update"),
                              tr("Could not prepare the installer in %1.")
                                  .arg(QDir::toNativeSeparators(temp.path())));
        return;
    }

    const QString powershell =
        QDir{qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"))}.filePath(
            QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
    QProcess installer;
    installer.setProgram(QDir::toNativeSeparators(powershell));
    installer.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-WindowStyle"),
        QStringLiteral("Hidden"),
        QStringLiteral("-File"),
        QDir::toNativeSeparators(script_path),
        QStringLiteral("-Zip"),
        QDir::toNativeSeparators(zip),
        QStringLiteral("-Dest"),
        QDir::toNativeSeparators(app_dir),
        QStringLiteral("-WaitPid"),
        QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("-Exe"),
        QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
        QStringLiteral("-Log"),
        QDir::toNativeSeparators(temp.filePath(QStringLiteral("lichii-update.log"))),
    });
    installer.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        constexpr unsigned long create_no_window = 0x08000000; // CREATE_NO_WINDOW
        args->flags |= create_no_window;
    });
    if (!installer.startDetached()) {
        QMessageBox::critical(owner, tr("Update"),
                              tr("Could not start the installer (%1).")
                                  .arg(QDir::toNativeSeparators(powershell)));
        return;
    }
    LOG_INFO(Frontend, "Installing {} after Lichii closes", m_asset.filename);
    m_install_started = true;
#endif
}
