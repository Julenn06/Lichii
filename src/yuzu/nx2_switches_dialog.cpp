// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "common/fs/path_util.h"
#include "common/nx2_env.h"
#include "yuzu/nx2_switches_dialog.h"

namespace {

enum Column { Name, Value, Active, Description, ColumnCount };

struct Documented {
    const char* name;
    const char* description;
};

// The switches players are expected to touch (dist/nx2_switches.txt, AGENTS.md); the rest are
// diagnostics and A/B experiments and are listed without a description.
constexpr Documented DocumentedSwitches[] = {
    {"EDEN_NX2_SPLIT", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                         "Runs GPU method and macro work on a second thread. 2 "
                                         "(default) is fastest; 0 turns it off.")},
    {"EDEN_NX2_FENCE_BATCH", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                               "Releases GPU fences in batches. 0 releases them "
                                               "one at a time (for comparisons).")},
    {"EDEN_NX2_FRAMES_AHEAD", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                                "Frames the GPU may run ahead. 2 smooths frame "
                                                "times but adds 1-2 frames of input latency.")},
    {"EDEN_NX2_BUFFER_DOWNLOADS", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                                    "1 copies GPU buffers back to the CPU. Slow "
                                                    "(38-63% fewer FPS); leave it off.")},
    {"EDEN_NX2_RESETFIX", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                            "Donkey Kong Bananza terrain fix (on by default). 0 "
                                            "leaves it to the GPU.")},
    {"EDEN_NX2_NO_REACTIVE", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                               "1 drops reactive-flushing read traps on Switch 2 "
                                               "games. Can lower FPS.")},
    {"EDEN_NX2_KEEP_ASTC", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                             "1 keeps ASTC textures compressed on the GPU when it "
                                             "supports them.")},
    {"EDEN_NX2_AUTO_STUB", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                             "1 answers unknown system calls with an empty "
                                             "success instead of stopping.")},
    {"EDEN_NX2_VSYNC_HZ", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                            "Rate of the emulated display's vsync (default 60).")},
    {"EDEN_NX2_PIPELINE_EVICT_ABOVE",
     QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                       "Graphics pipelines kept before the least used are evicted (default 8000).")},
    {"EDEN_NX2_BACKGROUND_PIPELINE_THREADS",
     QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Threads that build pipelines in the background.")},
    {"EDEN_NX2_TRANSCODE_THREADS",
     QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Threads that decode and transcode textures.")},
    {"EDEN_NX2_TC_EXPECTED_MIB", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                                   "Texture cache size (MiB) at which old "
                                                   "textures start to be collected.")},
    {"EDEN_NX2_TC_CRITICAL_MIB", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                                   "Texture cache size (MiB) at which textures "
                                                   "are collected aggressively.")},
    {"EDEN_NX2_VRAM_LIMIT_MIB",
     QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Caps the video memory the emulator uses (MiB).")},
    {"EDEN_NX2_GPU_PROFILE", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                               "1 logs GPU thread time per second by zone.")},
    {"EDEN_NX2_FRAME_DUMP", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                              "Folder to dump one frame's render passes into "
                                              "(diagnosing wrong images).")},
    {"EDEN_NX2_FLASH_WATCH", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                               "Folder for reports of 1-2 frame flashes.")},
    {"EDEN_NX2_SHADER_DUMP",
     QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Folder to dump native shader programs into.")},
    {"EDEN_NX2_REJECT_HASH", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                               "Comma-separated shader hashes to reject (A/B "
                                               "tests of a shader's effect).")},
    {"EDEN_NX2_IPC_TRACE", QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "1 logs every service call.")},
    {"EDEN_NX2_DEBUG_SERVER", QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                                                "host:port of the local debug server (loopback "
                                                "only), e.g. 127.0.0.1:6545.")},
};

const char* DescriptionOf(std::string_view name) {
    for (const auto& item : DocumentedSwitches) {
        if (name == item.name) {
            return item.description;
        }
    }
    return nullptr;
}

std::string Trim(std::string text) {
    constexpr const char* blank = " \t\r";
    text.erase(0, text.find_first_not_of(blank));
    text.erase(text.find_last_not_of(blank) + 1);
    return text;
}

/// NAME=VALUE entries of a switch file (main.cpp's ParseSwitchFile rules).
std::map<std::string, std::string> ReadEntries(const std::filesystem::path& path) {
    std::map<std::string, std::string> entries;
    std::ifstream file{path};
    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string value = Trim(line.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        entries.insert_or_assign(Trim(line.substr(0, eq)), std::move(value));
    }
    return entries;
}

} // Anonymous namespace

std::filesystem::path Nx2SwitchesDialog::UserSwitchFile() {
    // EdenDir is "user" next to the executable in portable installs and the roaming data folder
    // otherwise: the two copies main.cpp reads after the one shipped with the executable.
    return Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "nx2_switches.txt";
}

Nx2SwitchesDialog::Nx2SwitchesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Switch 2 Advanced Options"));
    resize(980, 620);

    auto* const layout = new QVBoxLayout(this);
    auto* const intro = new QLabel(
        tr("These options are read when Lichii starts: restart it after saving. Leave a value "
           "empty to use the default. Documented options are listed first; the rest are "
           "diagnostics for bug reports."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    filter = new QLineEdit();
    filter->setPlaceholderText(tr("Filter by name or description..."));
    filter->setClearButtonEnabled(true);
    connect(filter, &QLineEdit::textChanged, this, &Nx2SwitchesDialog::ApplyFilter);
    layout->addWidget(filter);

    table = new QTableWidget(0, ColumnCount);
    table->setHorizontalHeaderLabels(
        {tr("Option"), tr("Value in file"), tr("In effect now"), tr("Description")});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                           QAbstractItemView::AnyKeyPressed);
    table->horizontalHeader()->setSectionResizeMode(Name, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(Value, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(Active, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(Description, QHeaderView::Stretch);
    table->setColumnWidth(Value, 160);
    table->setWordWrap(true);
    layout->addWidget(table, 1);

    status = new QLabel();
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status);

    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
    QPushButton* const open_folder = buttons->addButton(tr("Open Folder"),
                                                        QDialogButtonBox::ActionRole);
    connect(open_folder, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QString::fromStdU16String(UserSwitchFile().parent_path().u16string())));
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (Save()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    Populate();
}

void Nx2SwitchesDialog::Populate() {
    const auto entries = ReadEntries(UserSwitchFile());

    std::vector<std::string_view> names;
    for (const auto& item : DocumentedSwitches) {
        names.push_back(item.name);
    }
    for (const auto& known : Common::Nx2Env::KnownSwitches()) {
        if (DescriptionOf(known.name) == nullptr) {
            names.push_back(known.name);
        }
    }

    table->setRowCount(static_cast<int>(names.size()));
    int row = 0;
    for (const std::string_view name : names) {
        const std::string key{name};
        auto* const name_item = new QTableWidgetItem(QString::fromStdString(key));
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);

        const auto in_file = entries.find(key);
        auto* const value_item = new QTableWidgetItem(
            in_file != entries.end() ? QString::fromStdString(in_file->second) : QString{});

        const char* const raw = Common::Nx2Env::Raw(key.c_str());
        auto* const active_item =
            new QTableWidgetItem(raw != nullptr ? QString::fromUtf8(raw) : tr("(default)"));
        active_item->setFlags(active_item->flags() & ~Qt::ItemIsEditable);

        const char* const description = DescriptionOf(name);
        auto* const description_item = new QTableWidgetItem(
            description != nullptr ? tr(description) : tr("Diagnostic or experiment"));
        description_item->setFlags(description_item->flags() & ~Qt::ItemIsEditable);
        if (description == nullptr) {
            description_item->setForeground(palette().placeholderText());
        }

        table->setItem(row, Name, name_item);
        table->setItem(row, Value, value_item);
        table->setItem(row, Active, active_item);
        table->setItem(row, Description, description_item);
        ++row;
    }
    status->setText(tr("File: %1").arg(QString::fromStdU16String(UserSwitchFile().u16string())));
}

void Nx2SwitchesDialog::ApplyFilter(const QString& text) {
    for (int row = 0; row < table->rowCount(); ++row) {
        const bool match = text.isEmpty() ||
                           table->item(row, Name)->text().contains(text, Qt::CaseInsensitive) ||
                           table->item(row, Description)->text().contains(text, Qt::CaseInsensitive);
        table->setRowHidden(row, !match);
    }
}

bool Nx2SwitchesDialog::Save() {
    std::map<std::string, std::string> wanted;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString value = table->item(row, Value)->text().trimmed();
        if (!value.isEmpty()) {
            wanted.emplace(table->item(row, Name)->text().toStdString(), value.toStdString());
        }
    }

    // Keep the file's comments and lines for switches this build does not list; rewrite the
    // listed ones in place and append new ones.
    const auto path = UserSwitchFile();
    std::vector<std::string> lines;
    {
        std::ifstream file{path};
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
    }
    std::map<std::string, bool> written;
    std::vector<std::string> output;
    for (const std::string& line : lines) {
        const std::string trimmed = Trim(line);
        const size_t eq = trimmed.find('=');
        if (trimmed.empty() || trimmed[0] == '#' || eq == std::string::npos) {
            output.push_back(line);
            continue;
        }
        const std::string name = Trim(trimmed.substr(0, eq));
        const bool listed = table->findItems(QString::fromStdString(name), Qt::MatchExactly)
                                .size() > 0;
        if (!listed) {
            output.push_back(line);
            continue;
        }
        if (const auto it = wanted.find(name); it != wanted.end() && !written[name]) {
            output.push_back(name + "=" + it->second);
            written[name] = true;
        }
    }
    if (lines.empty()) {
        output.push_back("# Lichii: EDEN_NX2_* options (Tools > Switch 2 Advanced Options).");
        output.push_back("# One NAME=VALUE per line; restart Lichii after editing.");
    }
    for (const auto& [name, value] : wanted) {
        if (!written[name]) {
            output.push_back(name + "=" + value);
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file{temporary, std::ios::binary | std::ios::trunc};
        for (const std::string& line : output) {
            file << line << '\n';
        }
        if (!file.good()) {
            QMessageBox::warning(this, tr("Switch 2 Advanced Options"),
                                 tr("Could not write %1.")
                                     .arg(QString::fromStdU16String(temporary.u16string())));
            return false;
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        QMessageBox::warning(this, tr("Switch 2 Advanced Options"),
                             tr("Could not save %1: %2")
                                 .arg(QString::fromStdU16String(path.u16string()),
                                      QString::fromStdString(ec.message())));
        return false;
    }
    QMessageBox::information(this, tr("Switch 2 Advanced Options"),
                             tr("Saved. Restart Lichii to apply the changes."));
    return true;
}
