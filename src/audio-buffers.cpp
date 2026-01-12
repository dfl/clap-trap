/**
 * clap-headless-host: Audio Buffers Implementation
 */

#include "clap-headless-host/audio-buffers.h"
#include <algorithm>
#include <cmath>

namespace clap_headless {

//-----------------------------------------------------------------------------
// StereoAudioBuffers
//-----------------------------------------------------------------------------

StereoAudioBuffers::StereoAudioBuffers(uint32_t blockSize) : blockSize_(blockSize) {
    for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
        inputData_[ch].resize(blockSize, 0.0f);
        outputData_[ch].resize(blockSize, 0.0f);
        inputPtrs_[ch] = inputData_[ch].data();
        outputPtrs_[ch] = outputData_[ch].data();
    }

    inputBuffer_.data32 = inputPtrs_;
    inputBuffer_.data64 = nullptr;
    inputBuffer_.channel_count = NUM_CHANNELS;
    inputBuffer_.latency = 0;
    inputBuffer_.constant_mask = 0;

    outputBuffer_.data32 = outputPtrs_;
    outputBuffer_.data64 = nullptr;
    outputBuffer_.channel_count = NUM_CHANNELS;
    outputBuffer_.latency = 0;
    outputBuffer_.constant_mask = 0;
}

void StereoAudioBuffers::fillInputWithSine(float frequency, float sampleRate, float amplitude) {
    constexpr float PI = 3.14159265358979323846f;
    for (uint32_t i = 0; i < blockSize_; ++i) {
        float sample = amplitude * std::sin(2.0f * PI * frequency * static_cast<float>(i) / sampleRate);
        for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
            inputData_[ch][i] = sample;
        }
    }
}

void StereoAudioBuffers::clearInput() {
    for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
        std::fill(inputData_[ch].begin(), inputData_[ch].end(), 0.0f);
    }
}

void StereoAudioBuffers::clearOutput() {
    for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
        std::fill(outputData_[ch].begin(), outputData_[ch].end(), 0.0f);
    }
}

bool StereoAudioBuffers::outputHasNonZero() const {
    for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
        for (const auto& sample : outputData_[ch]) {
            if (sample != 0.0f) return true;
        }
    }
    return false;
}

bool StereoAudioBuffers::outputIsValid() const {
    for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
        for (const auto& sample : outputData_[ch]) {
            if (std::isnan(sample) || std::isinf(sample)) return false;
        }
    }
    return true;
}

float StereoAudioBuffers::outputPeakAmplitude() const {
    float peak = 0.0f;
    for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
        for (const auto& sample : outputData_[ch]) {
            peak = std::max(peak, std::abs(sample));
        }
    }
    return peak;
}

//-----------------------------------------------------------------------------
// AudioBuffers
//-----------------------------------------------------------------------------

AudioBuffers::AudioBuffers(uint32_t blockSize, uint32_t inputChannels, uint32_t outputChannels)
    : blockSize_(blockSize), inputChannels_(inputChannels), outputChannels_(outputChannels) {

    inputData_.resize(inputChannels);
    inputPtrs_.resize(inputChannels);
    for (uint32_t ch = 0; ch < inputChannels; ++ch) {
        inputData_[ch].resize(blockSize, 0.0f);
        inputPtrs_[ch] = inputData_[ch].data();
    }

    outputData_.resize(outputChannels);
    outputPtrs_.resize(outputChannels);
    for (uint32_t ch = 0; ch < outputChannels; ++ch) {
        outputData_[ch].resize(blockSize, 0.0f);
        outputPtrs_[ch] = outputData_[ch].data();
    }

    inputBuffer_.data32 = inputPtrs_.data();
    inputBuffer_.data64 = nullptr;
    inputBuffer_.channel_count = inputChannels;
    inputBuffer_.latency = 0;
    inputBuffer_.constant_mask = 0;

    outputBuffer_.data32 = outputPtrs_.data();
    outputBuffer_.data64 = nullptr;
    outputBuffer_.channel_count = outputChannels;
    outputBuffer_.latency = 0;
    outputBuffer_.constant_mask = 0;
}

void AudioBuffers::clearInput() {
    for (auto& ch : inputData_) {
        std::fill(ch.begin(), ch.end(), 0.0f);
    }
}

void AudioBuffers::clearOutput() {
    for (auto& ch : outputData_) {
        std::fill(ch.begin(), ch.end(), 0.0f);
    }
}

bool AudioBuffers::outputHasNonZero() const {
    for (const auto& ch : outputData_) {
        for (const auto& sample : ch) {
            if (sample != 0.0f) return true;
        }
    }
    return false;
}

bool AudioBuffers::outputIsValid() const {
    for (const auto& ch : outputData_) {
        for (const auto& sample : ch) {
            if (std::isnan(sample) || std::isinf(sample)) return false;
        }
    }
    return true;
}

} // namespace clap_headless
