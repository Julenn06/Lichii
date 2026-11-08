// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Includes and TIPC helpers shared by the nx2 graphics sessions.

#include <algorithm>
#include "common/nx2_env.h"
#include "common/nx2_timeline.h"
#include <array>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <atomic>
#include <deque>
#include <map>
#include <optional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/div_ceil.h"
#include "common/dkb_frame_trace.h"
#include "common/game_presets.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/arm/dynarmic/window_trace.h"
#include "core/device_memory.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/perf_stats.h"
#include "video_core/gpu.h"
#include "video_core/nx2_perf.h"
#include "video_core/framebuffer_config.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/k_system_resource.h"
#include "core/hle/kernel/k_memory_manager.h"
#include "core/hle/kernel/k_page_group.h"
#include "core/hle/kernel/k_resource_limit.h"
#include "core/hle/kernel/k_server_session.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_hardware_timer.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/kernel/svc_results.h"
#include "core/memory.h"
#include "video_core/host1x/host1x.h"
#include "video_core/host1x/nx2_video_decoder.h"
#include "video_core/nx2.h"
#include "video_core/textures/decoders.h"
#include "core/hle/service/hle_ipc.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/syncpoint_manager.h"
#include "core/hle/service/nvdrv/devices/nx2_stub_device.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nx2_stub_interface.h"
#include "core/hle/service/nx2_image_layout.h"
#include "core/hle/service/nx2_video_input_queue.h"
#include "core/hle/service/nx2_present_pacer.h"
#include "core/hle/service/nx2_video_ready.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service {

// Creates a session served by `iface` and returns its client end. Unlike PushIpcInterface this
// works from a domain too, for callers that want a raw session handle.
template <class T>
inline Kernel::KClientSession* CreateRawSession(Core::System& system, HLERequestContext& ctx,
                                                std::shared_ptr<T> iface) {
    auto& kernel = system.Kernel();
    ASSERT(Kernel::GetCurrentProcess(kernel).GetResourceLimit()->Reserve(
        kernel, Kernel::LimitableResource::SessionCountMax, 1));

    auto* session = Kernel::KSession::Create(kernel);
    session->Initialize(kernel, nullptr, 0);
    Kernel::KSession::Register(kernel, session);

    auto manager =
        std::make_shared<SessionRequestManager>(kernel, ctx.GetManager()->GetServerManager());
    manager->SetSessionHandler(std::move(iface));
    ctx.GetManager()->GetServerManager().RegisterSession(&session->GetServerSession(), manager);
    return &session->GetClientSession();
}

// Replies to a TIPC request with success and raw u32 data words. ResponseBuilder::Push(Result)
// writes the CMIF two-word result, which shifts TIPC data one word past where the client reads
// it (and past the copied size); TIPC has a one-word result followed directly by the data.
inline void TipcReplyWords(HLERequestContext& ctx, std::span<const u32> words) {
    IPC::ResponseBuilder rb{ctx, static_cast<u32>(2 + words.size())};
    rb.PushRaw<u32>(ResultSuccess.raw);
    for (const u32 w : words) {
        rb.Push<u32>(w);
    }
}

inline void TipcReplyWords(HLERequestContext& ctx, std::initializer_list<u32> words) {
    TipcReplyWords(ctx, std::span<const u32>{words.begin(), words.size()});
}

// TIPC 0(u64 token) on the Switch 2 sessions: a handshake newer SDKs send first; the reply echoes
// the token. (The auto stub's reply only echoed it by leaving the request's words in place.)
inline void TipcHandshake(HLERequestContext& ctx, std::string_view session) {
    const auto* w = ctx.CommandBuffer();
    LOG_INFO(Service, "{} TIPC 0 handshake {:#x}", session, u64{w[2]} | (u64{w[3]} << 32));
    TipcReplyWords(ctx, {w[2], w[3]});
}

// Replies with success and a copied handle to a new session served by `iface`.
template <class T>
inline void ReturnRawSession(Core::System& system, HLERequestContext& ctx, std::shared_ptr<T> iface) {
    auto* client = CreateRawSession(system, ctx, std::move(iface));
    IPC::ResponseBuilder rb{ctx, 2, 1, 0};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, *client);
}

} // namespace Service
