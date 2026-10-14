// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Audio output through XAudio2, for the UWP app (Xbox Dev Mode, XBOX_PORT.md): SDL's audio
// drivers import desktop DLLs the Xbox does not have. One source voice per guest port, in the
// port's own format; XAudio2 mixes them and downmixes to the console's output.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>
#include <xaudio2.h>

#include "common/logging/log.h"
#include "core/libraries/audio/audioout.h"
#include "core/libraries/audio/audioout_backend.h"

namespace Libraries::AudioOut {

namespace {

// The engine and its mastering voice, made on the first port and kept for the app's life.
struct Engine {
    IXAudio2* xaudio = nullptr;
    IXAudio2MasteringVoice* master = nullptr;

    bool Start() {
        std::scoped_lock lk{mutex};
        if (xaudio) {
            return master != nullptr;
        }
        HRESULT hr = XAudio2Create(&xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(hr)) {
            LOG_ERROR(Lib_AudioOut, "XAudio2Create failed: {:#x}", static_cast<u32>(hr));
            xaudio = nullptr;
            return false;
        }
        hr = xaudio->CreateMasteringVoice(&master);
        if (FAILED(hr)) {
            LOG_ERROR(Lib_AudioOut, "XAudio2 mastering voice failed: {:#x}", static_cast<u32>(hr));
            master = nullptr;
            return false;
        }
        LOG_INFO(Lib_AudioOut, "XAudio2 started");
        return true;
    }

    std::mutex mutex;
};

Engine& GetEngine() {
    static Engine engine;
    return engine;
}

// PS4 volumes: 32768 is 0 dB.
constexpr float VolumeUnity = 32768.0f;

// From ksmedia.h, which is not otherwise needed.
constexpr GUID SubtypePcm{0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID SubtypeFloat{0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr DWORD MaskMono = 0x4;     // front centre
constexpr DWORD MaskStereo = 0x3;   // front left, front right
constexpr DWORD MaskSurround71 = 0x63F; // FL FR FC LFE BL BR SL SR

// Buffers are kept until XAudio2 has played them: a ring larger than the queue limit.
constexpr size_t RingBuffers = 8;
constexpr u32 MaxQueued = 6;

class XAudio2PortBackend : public PortBackend {
public:
    explicit XAudio2PortBackend(const PortOut& port)
        : buffer_size(port.BufferSize()), buffer_frames(port.buffer_frames),
          sample_rate(port.sample_rate) {
        if (!GetEngine().Start()) {
            return;
        }

        const u32 channels = port.format_info.num_channels;
        const bool is_float = port.format_info.is_float;
        const u16 bits = static_cast<u16>(port.format_info.sample_size * 8);

        WAVEFORMATEXTENSIBLE format{};
        format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format.Format.nChannels = static_cast<u16>(channels);
        format.Format.nSamplesPerSec = sample_rate;
        format.Format.wBitsPerSample = bits;
        format.Format.nBlockAlign = static_cast<u16>(channels * bits / 8);
        format.Format.nAvgBytesPerSec = sample_rate * format.Format.nBlockAlign;
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.Samples.wValidBitsPerSample = bits;
        format.dwChannelMask = channels == 1   ? MaskMono
                               : channels == 2 ? MaskStereo
                               : channels == 8 ? MaskSurround71
                                               : 0;
        format.SubFormat = is_float ? SubtypeFloat : SubtypePcm;

        const HRESULT hr =
            GetEngine().xaudio->CreateSourceVoice(&voice, &format.Format, 0, 2.0f);
        if (FAILED(hr)) {
            LOG_ERROR(Lib_AudioOut, "XAudio2 source voice failed: {:#x} ({} ch, {} Hz, {})",
                      static_cast<u32>(hr), channels, sample_rate, is_float ? "float" : "s16");
            voice = nullptr;
            return;
        }
        for (auto& buffer : ring) {
            buffer.resize(buffer_size);
        }
        voice->Start();
        LOG_INFO(Lib_AudioOut, "XAudio2 port: {} ch, {} Hz, {}, {} frames", channels, sample_rate,
                 is_float ? "float" : "s16", buffer_frames);
    }

    ~XAudio2PortBackend() override {
        if (voice) {
            voice->Stop();
            voice->FlushSourceBuffers();
            voice->DestroyVoice();
        }
    }

    void Output(void* ptr) override {
        if (!voice) {
            return;
        }
        // Keep the latency bounded: wait up to a few buffers' time for room in the queue.
        const auto period = std::chrono::microseconds(1'000'000ULL * buffer_frames / sample_rate);
        XAUDIO2_VOICE_STATE state{};
        for (int tries = 0; tries < 8; tries++) {
            voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued < MaxQueued) {
                break;
            }
            std::this_thread::sleep_for(period / 2);
        }
        if (state.BuffersQueued >= MaxQueued) {
            return; // still full: drop this buffer rather than overwrite one being played
        }

        auto& buffer = ring[next];
        next = (next + 1) % RingBuffers;
        std::memcpy(buffer.data(), ptr, buffer_size);

        XAUDIO2_BUFFER submit{};
        submit.AudioBytes = buffer_size;
        submit.pAudioData = buffer.data();
        voice->SubmitSourceBuffer(&submit);
    }

    void SetVolume(const std::array<int, 8>& ch_volumes) override {
        if (!voice) {
            return;
        }
        const int loudest = *std::max_element(ch_volumes.begin(), ch_volumes.end());
        voice->SetVolume(std::clamp(loudest / VolumeUnity, 0.0f, 2.0f));
    }

private:
    const u32 buffer_size;
    const u32 buffer_frames;
    const u32 sample_rate;
    IXAudio2SourceVoice* voice = nullptr;
    std::array<std::vector<u8>, RingBuffers> ring;
    size_t next = 0;
};

} // namespace

std::unique_ptr<PortBackend> XAudio2AudioOut::Open(PortOut& port) {
    return std::make_unique<XAudio2PortBackend>(port);
}

} // namespace Libraries::AudioOut
