/**
 * clap-trap: Simple WAV file I/O
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace clap_trap {

/**
 * WAV output format
 */
enum class WavFormat {
    Int16,      // 16-bit PCM (most compatible)
    Float32     // 32-bit float
};

/**
 * Simple WAV file reader/writer
 *
 * Supports reading: 16-bit, 24-bit, 32-bit PCM and 32-bit float
 * Supports writing: 16-bit PCM and 32-bit float
 */
class WavFile {
public:
    /**
     * Load a WAV file
     * @param path Path to WAV file
     * @return WavFile instance (check hasError() for success)
     */
    static std::unique_ptr<WavFile> load(const std::string& path);

    /**
     * Save samples to a WAV file
     * @param path Output path
     * @param samples Interleaved samples
     * @param sampleRate Sample rate in Hz
     * @param channels Number of channels
     * @param format Output format (Int16 or Float32)
     * @return true on success
     */
    static bool save(const std::string& path, const std::vector<float>& samples,
                     uint32_t sampleRate, uint32_t channels,
                     WavFormat format = WavFormat::Int16);

    bool hasError() const { return !error_.empty(); }
    const std::string& getError() const { return error_; }

    uint32_t sampleRate() const { return sampleRate_; }
    uint32_t channels() const { return channels_; }
    uint32_t frameCount() const { return static_cast<uint32_t>(samples_.size() / channels_); }

    /**
     * Get interleaved samples as float [-1, 1]
     */
    const std::vector<float>& samples() const { return samples_; }
    std::vector<float>& samples() { return samples_; }

private:
    std::string error_;
    uint32_t sampleRate_ = 0;
    uint32_t channels_ = 0;
    std::vector<float> samples_;
};

} // namespace clap_trap
