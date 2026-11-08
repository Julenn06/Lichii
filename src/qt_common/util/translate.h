// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCoreApplication>
#include <QString>

/// Declares tr() for namespace-scope Qt code. lupdate files each call under the enclosing
/// namespace (such as "QtCommon::Game"), so the lookup has to use that context too: through a
/// widget's tr() it searches "QWidget" and every message shows untranslated.
#define QTCOMMON_TRANSLATION_CONTEXT(context)                                                     \
    namespace {                                                                                    \
    [[maybe_unused]] QString tr(const char* text, const char* disambiguation = nullptr,            \
                                int n = -1) {                                                      \
        return QCoreApplication::translate(context, text, disambiguation, n);                     \
    }                                                                                              \
    }
