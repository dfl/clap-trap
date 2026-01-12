/**
 * clap-trap: Audio Buffers
 *
 * Helper classes for managing audio buffers in tests.
 */

#pragma once

#include <clap/clap.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace clap_trap {

/**
 * Manages stereo audio buffers for testing.
 */
class StereoAudioBuffers {
public:
    static constexpr uint32_t NUM_CHANNELS = 2;

    explicit StereoAudioBuffers(uint32_t blockSize);

    /// Get CLAP input buffer
    clap_audio_buffer_t* inputBuffer() { return &inputBuffer_; }

    /// Get CLAP output buffer
    clap_audio_buffer_t* outputBuffer() { return &outputBuffer_; }

    /// Fill input with a sine wave
    void fillInputWithSine(float frequency, float sampleRate, float amplitude = 0.5f);

    /// Fill input with silence
    void clearInput();

    /// Clear output buffer
    void clearOutput();

    /// Check if output contains non-zero samples
    bool outputHasNonZero() const;

    /// Check if output contains valid (non-NaN, non-Inf) samples
    bool outputIsValid() const;

    /// Get peak amplitude of output
    float outputPeakAmplitude() const;

    /// Get input data for a channel
    float* inputData(uint32_t channel) { return inputData_[channel].data(); }
    const float* inputData(uint32_t channel) const { return inputData_[channel].data(); }

    /// Get output data for a channel
    float* outputData(uint32_t channel) { return outputData_[channel].data(); }
    const float* outputData(uint32_t channel) const { return outputData_[channel].data(); }

    /// Get block size
    uint32_t blockSize() const { return blockSize_; }

private:
    uint32_t blockSize_;
    std::vector<float> inputData_[NUM_CHANNELS];
    std::vector<float> outputData_[NUM_CHANNELS];
    float* inputPtrs_[NUM_CHANNELS];
    float* outputPtrs_[NUM_CHANNELS];
    clap_audio_buffer_t inputBuffer_;
    clap_audio_buffer_t outputBuffer_;
};

/**
 * Manages multi-channel audio buffers for testing.
 */
class AudioBuffers {
public:
    AudioBuffers(uint32_t blockSize, uint32_t inputChannels, uint32_t outputChannels);

    clap_audio_buffer_t* inputBuffer() { return &inputBuffer_; }
    clap_audio_buffer_t* outputBuffer() { return &outputBuffer_; }

    void clearInput();
    void clearOutput();
    bool outputHasNonZero() const;
    bool outputIsValid() const;

    float* inputData(uint32_t channel) { return inputData_[channel].data(); }
    float* outputData(uint32_t channel) { return outputData_[channel].data(); }

    uint32_t blockSize() const { return blockSize_; }
    uint32_t inputChannels() const { return inputChannels_; }
    uint32_t outputChannels() const { return outputChannels_; }

private:
    uint32_t blockSize_;
    uint32_t inputChannels_;
    uint32_t outputChannels_;
    std::vector<std::vector<float>> inputData_;
    std::vector<std::vector<float>> outputData_;
    std::vector<float*> inputPtrs_;
    std::vector<float*> outputPtrs_;
    clap_audio_buffer_t inputBuffer_;
    clap_audio_buffer_t outputBuffer_;
};

} // namespace clap_trap
