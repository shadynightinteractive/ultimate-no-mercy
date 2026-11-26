#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/lockstep.hpp"
#include <algorithm>
#include <cassert>

static uint32_t sample_rate = 48000;

static ultramodern::audio_callbacks_t audio_callbacks;

void ultramodern::set_audio_callbacks(const ultramodern::audio_callbacks_t& callbacks) {
    audio_callbacks = callbacks;
}

void ultramodern::init_audio() {
    // Pick an initial dummy sample rate; this will be set by the game later to the true sample rate.
    set_audio_frequency(48000);
}

void ultramodern::set_audio_frequency(uint32_t freq) {
    if (audio_callbacks.set_frequency) {
        audio_callbacks.set_frequency(freq);
    }
    sample_rate = freq;
}

// [replay 2026-11-14] The sound driver's view of the speakers in lockstep (ultramodern/lockstep.hpp): how much is still
// queued is worked out from the lockstep clock -- what the game has queued, less what a speaker at the game's rate would
// have played since -- instead of asking the PC's sound device, whose answer is the PC's timing. The sound itself still
// goes to the PC (queue_samples), and the game's driver sees the same numbers it would at its own pace.
static uint64_t lockstep_frames_queued = 0;      // stereo frames the game has queued and the "speaker" has not played
static uint64_t lockstep_last_ticks = 0;
static uint64_t lockstep_tick_rest = 0;           // (ticks x rate not yet a whole frame)
static void lockstep_play_out() {
    const uint64_t now = ultramodern::lockstep::now();
    lockstep_tick_rest += (now - lockstep_last_ticks) * sample_rate;
    lockstep_last_ticks = now;
    const uint64_t played = lockstep_tick_rest / ultramodern::lockstep::TICKS_PER_SECOND;
    lockstep_tick_rest %= ultramodern::lockstep::TICKS_PER_SECOND;
    lockstep_frames_queued -= std::min(played, lockstep_frames_queued);
}

void ultramodern::queue_audio_buffer(RDRAM_ARG PTR(int16_t) audio_data_, uint32_t byte_count) {
    // Ensure that the byte count is an integer multiple of samples.
    assert((byte_count & 1) == 0);

    // Calculate the number of samples from the number of bytes.
    uint32_t sample_count = byte_count / sizeof(int16_t);
    if (ultramodern::lockstep::on()) { lockstep_play_out(); lockstep_frames_queued += sample_count / 2; }   // [replay 2026-11-14]

    // Queue the swapped audio data.
    if (sample_count > 0 && audio_callbacks.queue_samples) {
        audio_callbacks.queue_samples(TO_PTR(int16_t, audio_data_), sample_count);
    }
}

// For SDL2
//uint32_t buffer_offset_frames = 1;
// For Godot
//float buffer_offset_frames = 0.5f;
// [wcw fix] WCW registers no AI event (verified: osSetEventMesg(OS_EVENT_AI) never called);
// its audio thread generates a burst once per ~33ms game frame, sized by osAiGetLength.
// On hardware osAiGetLength reports only the current DMA buffer, so the game stays buffers
// ahead; here it sees the whole host queue and keeps ~0 headroom → constant underruns
// (~22/s measured) against SDL's chunked consumption. Under-report by ~6 VIs worth
// (~50ms @ 22050 Hz) so the game builds real queue depth. Costs ~50ms audio latency.
float buffer_offset_frames = 6.0f;

// [wcw2k] AI backpressure model (see ultramodern.hpp). Threshold must sit ABOVE the
// buffer_offset_frames under-report below (6 VIs), so drivers that pace themselves by
// osAiGetLength (WCW/Revenge) still build their ~6-VI depth before FULL ever asserts —
// their behavior is unchanged. Drivers that pace by AI_STATUS_FULL (WM2000) bound the
// host queue at ~8 VIs (~130ms) instead of growing it forever.
bool ultramodern::is_audio_backlogged() {
    if (ultramodern::lockstep::on()) { lockstep_play_out(); return lockstep_frames_queued > 8ull * (sample_rate / 60); }   // [replay 2026-11-14]
    if (audio_callbacks.get_frames_remaining == nullptr) {
        return false;
    }
    uint32_t buffered_frames = (uint32_t)audio_callbacks.get_frames_remaining();
    uint32_t frames_per_vi = sample_rate / 60;
    return buffered_frames > 8 * frames_per_vi;
}

// If there's ever any audio popping, check here first. Some games are very sensitive to
// the remaining sample count and reporting a number that's too high here can lead to issues.
// Reporting a number that's too low can lead to audio lag in some games.
uint32_t ultramodern::get_remaining_audio_bytes() {
    // Get the number of remaining buffered audio bytes.
    uint32_t buffered_byte_count;
    if (ultramodern::lockstep::on()) {   // [replay 2026-11-14] (above: the lockstep clock's speaker)
        lockstep_play_out();
        buffered_byte_count = (uint32_t)std::min<uint64_t>(lockstep_frames_queued * 2 * sizeof(int16_t), 0x7FFFFFFFu);
    }
    else if (audio_callbacks.get_frames_remaining != nullptr) {
        buffered_byte_count = audio_callbacks.get_frames_remaining() * 2 * sizeof(int16_t);
    }
    else {
        buffered_byte_count = 100;
    }
    // Adjust the reported count to be some number of refreshes in the future, which helps ensure that
    // there are enough samples even if the audio thread experiences a small amount of lag. This prevents
    // audio popping on games that use the buffered audio byte count to determine how many samples
    // to generate.
    uint32_t samples_per_vi = (sample_rate / 60);
    if (buffered_byte_count > static_cast<uint32_t>(buffer_offset_frames * sizeof(int16_t) * samples_per_vi)) {
        buffered_byte_count -= static_cast<uint32_t>(buffer_offset_frames * sizeof(int16_t) * samples_per_vi);
    }
    else {
        buffered_byte_count = 0;
    }
    return buffered_byte_count;
}
