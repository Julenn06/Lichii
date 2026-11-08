// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Native objects shared by the sessions, chat and camera stubs.

#include "core/hle/service/nx2_gsv/gpu.h"

namespace Service {

inline bool Nx2VideoTraceEnabled() {
    static const bool enabled = std::getenv("EDEN_NX2_VDEC_TRACE") != nullptr;
    return enabled;
}

// Native memory and images handed out by the Switch 2 graphics server (gsv 111/114/423/429/315),
// shared with the vi2 session so presents can find an image's memory. Command meanings follow
// the shared changes.patch (IGraphicsSupervisor), re-checked against title 4's SDK.
struct Nx2NativeObjects {
    struct VideoTrace {
        u32 syncpoint{};
        u64 decoded_frames{};
        u32 image{};
        u64 gpu_address{};
        u64 device_address{};
        u64 pts{};
        std::string codec;
    };
    struct Memory {
        u64 size{};
        u64 address{}; // CPU (== GPU) address once bound by 114/423
        u32 parent{};
        u64 parent_offset{};
    };
    struct Image {
        u32 memory{};
        std::array<u32, 16> preference{};
        u64 size{};
        u32 count{1}; // images in the pool; handle id + i is image i
        u64 offset{}; // into its memory (images of a decoder pool share one)
        bool window_pool{};
        bool decoder{}; // a video decoder output image (gsv 911)
        // A decoder image the client reads on the GPU (gsv 428/426): written block-linear.
        bool gpu_read{};
        u32 gpu_maps{};     // outstanding 426 maps
        bool mapped_here{}; // 426 added its GPU mapping (no other mapping covered it)
    };
    std::mutex mutex;
    std::map<u32, Memory> memories;
    std::map<u32, Image> images;
    std::optional<VideoTrace> last_video_write;
    u32 next_memory{1};
    u32 next_image{1};
};

// GameChat's graphics side (nn::chat::gsv), handed out by gsv 1202. Read from the SDK's
// nn::chat::gsv::Initialize: TIPC 1() returns a u32 id at word 3 (it aborts with 2162-0001 if
// the id is over 0x3ff), then TIPC 11(u32 id) returns an event (copy handle at word 3, result at
// word 4) that it attaches as a system event. Everything else falls through to the nx2 auto stub.
class INx2ChatGsvSession final : public ServiceFramework<INx2ChatGsvSession> {
public:
    explicit INx2ChatGsvSession(Core::System& system_)
        : ServiceFramework{system_, "nx2:chat:gsv"}, m_context{system_, "nx2:chat:gsv"} {
        RegisterHandlersTipc({
            {1, &INx2ChatGsvSession::GetEventId, "GetEventId (guessed)"},
            {11, &INx2ChatGsvSession::GetEvent, "GetEvent (guessed)"},
        });
    }

    ~INx2ChatGsvSession() override {
        for (auto& [id, event] : m_events) {
            m_context.CloseEvent(event);
        }
    }

private:
    void GetEventId(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:chat:gsv TIPC 1, returning id 0");
        TipcReplyWords(ctx, {0});
    }

    void GetEvent(HLERequestContext& ctx) {
        const u32 id = ctx.CommandBuffer()[2];
        LOG_WARNING(Service, "(STUBBED) nx2:chat:gsv TIPC 11 id={}, returning an event", id);
        auto*& event = m_events[id];
        if (event == nullptr) {
            event = m_context.CreateEvent(fmt::format("nx2:chat:gsv:Event{}", id));
        }
        IPC::ResponseBuilder rb{ctx, 2, 1, 0};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, event->GetReadableEvent());
    }

    KernelHelpers::ServiceContext m_context;
    std::map<u32, Kernel::KEvent*> m_events;
};

// nn::camera::sdk camera context (gsv 602). GameChat builds its local camera video storage from it
// and polls it every frame. Nothing about the commands is known beyond the arguments seen (3 and
// 21 take the camera name "Default", 29 a u32 0x180 = 384, the width of the 384x216 camera images),
// and the game runs on an empty success, so they answer that without a log line per call.
class INx2CameraSession final : public ServiceFramework<INx2CameraSession> {
public:
    explicit INx2CameraSession(Core::System& system_) : ServiceFramework{system_, "nx2:camera"} {
        RegisterHandlersTipc({
            {3, &INx2CameraSession::Stub, "Unknown3 (camera name \"Default\")"},
            {21, &INx2CameraSession::Stub, "Unknown21 (camera name \"Default\")"},
            {29, &INx2CameraSession::Stub, "Unknown29 (u32 0x180, polled every frame)"},
        });
    }

private:
    void Stub(HLERequestContext& ctx) {
        const u32 command = ctx.GetCommand();
        if (m_logged.insert(command).second) {
            LOG_WARNING(Service, "(STUBBED) nx2:camera TIPC {} [2]={:#x} [3]={:#x}, returning success",
                        command, ctx.CommandBuffer()[2], ctx.CommandBuffer()[3]);
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    std::set<u32> m_logged;
};

// Video decoder output images (nn::codec image pools, gsv 991/911/961). The preference
// (MakeDecodeImagePreference) holds an output layout code at word 1 and (width - 1) |
// (height - 1) << 16 at word 2. Codes come in pairs, one per CodecMode: 1/4 NV12 (format 3, what
// the movie players use), 2/5 BGRA, 3/6 RGBA; the HDR codes (10-13) are written as RGBA. The
// server picks the layout and describes it to the client with gsv 961: here pitch-linear rows,
// NV12's half-height chroma plane below the luma plane, each plane padded to 128 rows.
struct Nx2DecoderImageLayout {
    u32 code;
    u32 width;
    u32 height;
    Tegra::Host1x::Nx2PictureFormat format;
    u64 pitch;
    u64 chroma_offset;
    u64 size;
};

inline Nx2DecoderImageLayout Nx2CalculateDecoderImageLayout(const u32* preference) {
    using Tegra::Host1x::Nx2PictureFormat;
    Nx2DecoderImageLayout layout{};
    layout.code = preference[1];
    layout.width = (preference[2] & 0xFFFF) + 1;
    layout.height = (preference[2] >> 16) + 1;
    switch (layout.code) {
    case 1:
    case 4:
        layout.format = Nx2PictureFormat::Nv12;
        break;
    case 2:
    case 5:
        layout.format = Nx2PictureFormat::Bgra8;
        break;
    default:
        layout.format = Nx2PictureFormat::Rgba8;
        break;
    }
    const bool nv12 = layout.format == Nx2PictureFormat::Nv12;
    constexpr u64 BlockRows = 128;
    layout.pitch = Common::AlignUp(u64{layout.width} * (nv12 ? 1 : 4), 64);
    layout.chroma_offset = layout.pitch * Common::AlignUp(u64{layout.height}, BlockRows);
    layout.size = layout.chroma_offset +
                  (nv12 ? layout.pitch * Common::AlignUp((u64{layout.height} + 1) / 2, BlockRows) : 0);
    return layout;
}

} // namespace Service
