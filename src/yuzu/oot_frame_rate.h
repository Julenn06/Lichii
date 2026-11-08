// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

#include <QObject>
#include <QTimer>

#include "common/common_types.h"

class QPushButton;
class QStatusBar;
class QWidget;

/// Ocarina of Time's 60 / 30 FPS choice: asked the first time the game boots, then a status bar
/// button switches it at once while the game runs (Settings::values.oot_30fps).
class OotFrameRateControl : public QObject {
    Q_OBJECT

public:
    /// `save` writes the configuration; toggles are coalesced into one call.
    OotFrameRateControl(QWidget* window, QStatusBar* status_bar, std::function<void()> save);

    /// A title is booting: asks on Ocarina's first boot and shows the button while it runs.
    void OnGameBoot(u64 title_id);
    void OnGameStopped();

    /// Shows the current setting (it can also change in the configuration dialogs).
    void Refresh();

private:
    void Ask();
    void Toggle();

    QWidget* window;
    QPushButton* button;
    QTimer save_timer;
    std::function<void()> save;
};
