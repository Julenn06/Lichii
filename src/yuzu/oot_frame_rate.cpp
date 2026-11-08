// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>

#include "common/game_presets.h"
#include "common/settings.h"
#include "yuzu/oot_frame_rate.h"

OotFrameRateControl::OotFrameRateControl(QWidget* window_, QStatusBar* status_bar,
                                         std::function<void()> save_)
    : QObject(window_), window{window_}, button{new QPushButton()}, save{std::move(save_)} {
    button->setObjectName(QStringLiteral("OotFpsStatusBarButton"));
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tr("Ocarina of Time frame rate. Click to switch between 60 FPS and "
                          "30 FPS (lower-end PCs); it takes effect at once."));
    connect(button, &QPushButton::clicked, this, &OotFrameRateControl::Toggle);
    Refresh();
    status_bar->insertPermanentWidget(0, button);
    button->setVisible(false); // shown while Ocarina of Time runs

    // Clicking back and forth writes the configuration once, a second after the last click.
    save_timer.setSingleShot(true);
    save_timer.setInterval(1000);
    connect(&save_timer, &QTimer::timeout, this, [this] { save(); });
}

void OotFrameRateControl::OnGameBoot(u64 title_id) {
    const bool ocarina = title_id == Settings::OcarinaOfTimeProgramId;
    // Asked the first time the game boots (it used to be asked at the first start of the
    // emulator, also from players without the game).
    if (ocarina && !Settings::values.oot_fps_asked.GetValue()) {
        Ask();
    }
    button->setVisible(ocarina);
}

void OotFrameRateControl::OnGameStopped() {
    button->setVisible(false);
}

void OotFrameRateControl::Ask() {
    QMessageBox box(window);
    box.setWindowTitle(tr("Ocarina of Time frame rate"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("Which frame rate should Ocarina of Time run at?"));
    box.setInformativeText(
        tr("60 FPS needs a fast, recent CPU.\n"
           "30 FPS is for lower-end PCs. The game keeps its normal speed either way.\n\n"
           "Switch any time with the 60 FPS / 30 FPS button at the bottom of the window."));
    QPushButton* const fps60 = box.addButton(tr("60 FPS"), QMessageBox::AcceptRole);
    QPushButton* const fps30 = box.addButton(tr("30 FPS (lower-end PCs)"), QMessageBox::AcceptRole);
    box.setDefaultButton(fps60);
    box.exec();
    Settings::values.oot_30fps.SetValue(box.clickedButton() == fps30);
    Settings::values.oot_fps_asked.SetValue(true);
    Refresh();
    save();
}

void OotFrameRateControl::Toggle() {
    Settings::values.oot_30fps.SetValue(!Settings::values.oot_30fps.GetValue());
    Settings::values.oot_fps_asked.SetValue(true);
    Refresh();
    save_timer.start();
}

void OotFrameRateControl::Refresh() {
    const bool fps30 = Settings::values.oot_30fps.GetValue();
    button->setChecked(fps30);
    button->setText(fps30 ? tr("30 FPS") : tr("60 FPS"));
}
