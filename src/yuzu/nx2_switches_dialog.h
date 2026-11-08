// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <QDialog>

class QLabel;
class QLineEdit;
class QTableWidget;

/// Edits the user's nx2_switches.txt (the EDEN_NX2_* environment switches read at start) without
/// opening the file by hand. Every switch the sources read is listed, with its value in the file,
/// the value in effect now and, for the documented ones, what it does.
class Nx2SwitchesDialog : public QDialog {
    Q_OBJECT

public:
    explicit Nx2SwitchesDialog(QWidget* parent = nullptr);

    /// The nx2_switches.txt the emulator reads last (the user's copy).
    static std::filesystem::path UserSwitchFile();

private:
    void Populate();
    void ApplyFilter(const QString& text);
    bool Save();

    QLineEdit* filter = nullptr;
    QTableWidget* table = nullptr;
    QLabel* status = nullptr;
};
