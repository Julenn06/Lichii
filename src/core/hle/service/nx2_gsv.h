// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The nx2 graphics system services Switch 2 titles open through ISelfController 2000,
// one header per session under nx2_gsv/. Each part includes the one before it, so
// this order is also the declaration order.

#include "core/hle/service/nx2_gsv/syncpt.h"
#include "core/hle/service/nx2_gsv/gpu.h"
#include "core/hle/service/nx2_gsv/native_objects.h"
#include "core/hle/service/nx2_gsv/vi2.h"
#include "core/hle/service/nx2_gsv/video.h"
#include "core/hle/service/nx2_gsv/graphics_system.h"
