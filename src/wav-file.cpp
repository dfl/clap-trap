/**
 * clap-trap: Simple WAV file I/O
 *
 * Minimal implementation for reading/writing PCM WAV files.
 * Supports 16-bit and 32-bit float formats.
 */

#include "clap-trap/wav-file.h"
#include <cstring>
#include <fstream>
#include <cmath>

namespace clap_trap {

namespace {

struct RiffChunk {
    char id[4];
    uint32_t size;
};

struct WavFmtChunk {
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};

template<typename T>
T readLE(std::ifstream& file) {
    T value;
    file.read(reinterpret_cast<char*>(&value), sizeof(T));
    return value;
}

template<typename T>
void writeLE(std::ofstream& file, T value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

} // anonymous namespace

//-----------------------------------------------------------------------------
// WavFile
//-----------------------------------------------------------------------------

std::unique_ptr<WavFile> WavFile::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        auto wav = std::make_unique<WavFile>();
        wav->error_ = "Could not open file: " + path;
        return wav;
    }

    // Read RIFF header
    char riffId[4];
    file.read(riffId, 4);
    if (std::memcmp(riffId, "RIFF", 4) != 0) {
        auto wav = std::make_unique<WavFile>();
        wav->error_ = "Not a RIFF file";
        return wav;
    }

    uint32_t fileSize = readLE<uint32_t>(file);
    (void)fileSize;

    char waveId[4];
    file.read(waveId, 4);
    if (std::memcmp(waveId, "WAVE", 4) != 0) {
        auto wav = std::make_unique<WavFile>();
        wav->error_ = "Not a WAVE file";
        return wav;
    }

    auto wav = std::make_unique<WavFile>();
    bool foundFmt = false;
    bool foundData = false;
    WavFmtChunk fmt{};

    // Read chunks
    while (file && !foundData) {
        char chunkId[4];
        file.read(chunkId, 4);
        if (!file) break;

        uint32_t chunkSize = readLE<uint32_t>(file);

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            fmt.audioFormat = readLE<uint16_t>(file);
            fmt.numChannels = readLE<uint16_t>(file);
            fmt.sampleRate = readLE<uint32_t>(file);
            fmt.byteRate = readLE<uint32_t>(file);
            fmt.blockAlign = readLE<uint16_t>(file);
            fmt.bitsPerSample = readLE<uint16_t>(file);

            // Skip any extra format bytes
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }

            wav->sampleRate_ = fmt.sampleRate;
            wav->channels_ = fmt.numChannels;
            foundFmt = true;
        }
        else if (std::memcmp(chunkId, "data", 4) == 0) {
            if (!foundFmt) {
                wav->error_ = "Data chunk before fmt chunk";
                return wav;
            }

            uint32_t numSamples = chunkSize / fmt.blockAlign;
            wav->samples_.resize(numSamples * fmt.numChannels);

            if (fmt.audioFormat == 1) { // PCM
                if (fmt.bitsPerSample == 16) {
                    for (uint32_t i = 0; i < numSamples * fmt.numChannels; ++i) {
                        int16_t sample = readLE<int16_t>(file);
                        wav->samples_[i] = sample / 32768.0f;
                    }
                }
                else if (fmt.bitsPerSample == 24) {
                    for (uint32_t i = 0; i < numSamples * fmt.numChannels; ++i) {
                        uint8_t bytes[3];
                        file.read(reinterpret_cast<char*>(bytes), 3);
                        int32_t sample = (bytes[0] << 8) | (bytes[1] << 16) | (bytes[2] << 24);
                        sample >>= 8; // Sign extend
                        wav->samples_[i] = sample / 8388608.0f;
                    }
                }
                else if (fmt.bitsPerSample == 32) {
                    for (uint32_t i = 0; i < numSamples * fmt.numChannels; ++i) {
                        int32_t sample = readLE<int32_t>(file);
                        wav->samples_[i] = sample / 2147483648.0f;
                    }
                }
                else {
                    wav->error_ = "Unsupported bit depth: " + std::to_string(fmt.bitsPerSample);
                    return wav;
                }
            }
            else if (fmt.audioFormat == 3) { // IEEE float
                if (fmt.bitsPerSample == 32) {
                    for (uint32_t i = 0; i < numSamples * fmt.numChannels; ++i) {
                        wav->samples_[i] = readLE<float>(file);
                    }
                }
                else {
                    wav->error_ = "Unsupported float bit depth: " + std::to_string(fmt.bitsPerSample);
                    return wav;
                }
            }
            else {
                wav->error_ = "Unsupported audio format: " + std::to_string(fmt.audioFormat);
                return wav;
            }

            foundData = true;
        }
        else {
            // Skip unknown chunk
            file.seekg(chunkSize, std::ios::cur);
        }
    }

    if (!foundFmt || !foundData) {
        wav->error_ = "Missing fmt or data chunk";
        return wav;
    }

    return wav;
}

bool WavFile::save(const std::string& path, const std::vector<float>& samples,
                   uint32_t sampleRate, uint32_t channels, WavFormat format) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    uint32_t numFrames = static_cast<uint32_t>(samples.size() / channels);
    uint16_t bitsPerSample = (format == WavFormat::Float32) ? 32 : 16;
    uint16_t audioFormat = (format == WavFormat::Float32) ? 3 : 1;
    uint16_t blockAlign = channels * (bitsPerSample / 8);
    uint32_t byteRate = sampleRate * blockAlign;
    uint32_t dataSize = numFrames * blockAlign;
    uint32_t fileSize = 36 + dataSize;

    // RIFF header
    file.write("RIFF", 4);
    writeLE<uint32_t>(file, fileSize);
    file.write("WAVE", 4);

    // fmt chunk
    file.write("fmt ", 4);
    writeLE<uint32_t>(file, 16); // chunk size
    writeLE<uint16_t>(file, audioFormat);
    writeLE<uint16_t>(file, static_cast<uint16_t>(channels));
    writeLE<uint32_t>(file, sampleRate);
    writeLE<uint32_t>(file, byteRate);
    writeLE<uint16_t>(file, blockAlign);
    writeLE<uint16_t>(file, bitsPerSample);

    // data chunk
    file.write("data", 4);
    writeLE<uint32_t>(file, dataSize);

    if (format == WavFormat::Float32) {
        for (float sample : samples) {
            writeLE<float>(file, sample);
        }
    }
    else {
        for (float sample : samples) {
            // Clamp and convert to 16-bit
            float clamped = std::fmax(-1.0f, std::fmin(1.0f, sample));
            int16_t intSample = static_cast<int16_t>(clamped * 32767.0f);
            writeLE<int16_t>(file, intSample);
        }
    }

    return true;
}

} // namespace clap_trap
