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
    // Asked once more from players who chose before the smooth 60 FPS option existed.
    if (ocarina && (!Settings::values.oot_fps_asked.GetValue() ||
                    (SmoothAvailable() && !Settings::values.oot_smooth_offered.GetValue()))) {
        Ask();
    }
    button->setVisible(ocarina);
}

bool OotFrameRateControl::SmoothAvailable() {
#ifdef HAS_LSFG
    return true;
#else
    return false;
#endif
}

OotFrameRateControl::Choices OotFrameRateControl::FillQuestion(QMessageBox& box) {
    box.setWindowTitle(tr("Ocarina of Time frame rate"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("Which frame rate should Ocarina of Time run at?"));
    QString text = tr("60 FPS needs a fast, recent CPU.\n"
                      "30 FPS is for lower-end PCs. The game keeps its normal speed either way.");
    if (SmoothAvailable()) {
        text += QStringLiteral("\n") +
                tr("Smooth 60 FPS runs the game at 30 FPS and shows a generated frame between "
                   "every two (FSR 3 frame generation): a steady 60 FPS on screen for PCs that "
                   "cannot hold 60. It adds a little input delay and can show artifacts in fast "
                   "motion.");
    }
    text += QStringLiteral("\n\n") +
            tr("Switch any time with the 60 FPS / 30 FPS button at the bottom of the window.");
    if (SmoothAvailable()) {
        text += QStringLiteral(" ") +
                tr("Frame generation is in Configure > Graphics and changes when the game is "
                   "started again.");
    }
    box.setInformativeText(text);
    Choices choices;
    QPushButton* const fps60 = box.addButton(tr("60 FPS"), QMessageBox::AcceptRole);
    choices.fps60 = fps60;
    if (SmoothAvailable()) {
        choices.smooth60 =
            box.addButton(tr("Smooth 60 FPS (30 + frame generation)"), QMessageBox::AcceptRole);
    }
    choices.fps30 = box.addButton(tr("30 FPS (lower-end PCs)"), QMessageBox::AcceptRole);
    box.setDefaultButton(fps60);
    return choices;
}

void OotFrameRateControl::OnGameStopped() {
    button->setVisible(false);
}

void OotFrameRateControl::Ask() {
    QMessageBox box(window);
    const Choices choices = FillQuestion(box);
    box.exec();
    const QAbstractButton* const clicked = box.clickedButton();
    if (choices.smooth60 != nullptr && clicked == choices.smooth60) {
        // Asked before the renderer is created, so frame generation takes effect on this boot.
        Settings::values.oot_30fps.SetValue(true);
        Settings::values.frame_gen.SetValue(true);
        Settings::values.frame_gen_type.SetValue(Settings::FrameGenType::Fsr3);
        Settings::values.frame_gen_multiplier.SetValue(2);
    } else if (clicked == choices.fps60 || clicked == choices.fps30) {
        // A dialog closed without a choice keeps what was set.
        Settings::values.oot_30fps.SetValue(clicked == choices.fps30);
    }
    Settings::values.oot_fps_asked.SetValue(true);
    Settings::values.oot_smooth_offered.SetValue(true);
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
    // With frame generation at 2x the screen gets twice the game's frames.
    const bool doubled =
        SmoothAvailable() && Settings::values.frame_gen.GetValue() &&
        Settings::values.frame_gen_multiplier.GetValue() == 2 &&
        Settings::values.frame_gen_target_rate.GetValue() == 0;
    if (doubled) {
        button->setText(fps30 ? tr("Smooth 60 FPS") : tr("60 FPS + frame generation"));
    } else {
        button->setText(fps30 ? tr("30 FPS") : tr("60 FPS"));
    }
}
