// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <QDialog>
#include "common/net/net.h"

class QRadioButton;
namespace Ui {
class UpdateDialog;
}

class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    explicit UpdateDialog(const Common::Net::Release& release, QWidget* parent = nullptr);
    ~UpdateDialog();

    /// True once the installer is waiting for Lichii to close; the caller must then close the
    /// main window so the files can be replaced.
    bool InstallStarted() const {
        return m_install_started;
    }

private slots:
    void Download();

private:
    /// Downloads this fork's package, checks it and hands it to the installer script.
    void Install();
    bool DownloadTo(const QString& filename);

    Ui::UpdateDialog* ui;
    QList<QRadioButton*> m_buttons;
    Common::Net::Asset m_asset;
    QString m_release_url;
    bool m_install_started = false;
};
