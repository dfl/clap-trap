/**
 * clap-trap: MIDI File Reader Implementation
 */

#include "clap-trap/midi-file.h"
#include <algorithm>
#include <cstring>
#include <fstream>

namespace clap_trap {

// Read big-endian values
static uint32_t readBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static uint16_t readBE16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

// Read variable-length quantity
static uint32_t readVLQ(const uint8_t*& ptr, const uint8_t* end) {
    uint32_t value = 0;
    while (ptr < end) {
        uint8_t byte = *ptr++;
        value = (value << 7) | (byte & 0x7F);
        if ((byte & 0x80) == 0) break;
    }
    return value;
}

std::unique_ptr<MidiFile> MidiFile::load(const char* path) {
    auto midi = std::unique_ptr<MidiFile>(new MidiFile());

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        midi->error_ = "Could not open file";
        return midi;
    }

    size_t size = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    if (!midi->parse(data)) {
        return midi;
    }

    return midi;
}

bool MidiFile::parse(const std::vector<uint8_t>& data) {
    const uint8_t* ptr = data.data();
    const uint8_t* end = ptr + data.size();

    // Parse header
    if (!parseHeader(ptr, end)) {
        return false;
    }

    // Parse tracks
    for (uint16_t i = 0; i < numTracks_; ++i) {
        if (!parseTrack(ptr, end)) {
            return false;
        }
    }

    // Sort events by time
    std::stable_sort(events_.begin(), events_.end(),
                     [](const MidiEvent& a, const MidiEvent& b) {
                         return a.tickTime < b.tickTime;
                     });

    // Compute absolute times in seconds
    computeTimes();

    return true;
}

bool MidiFile::parseHeader(const uint8_t*& ptr, const uint8_t* end) {
    if (end - ptr < 14) {
        error_ = "File too small for MIDI header";
        return false;
    }

    // Check "MThd" magic
    if (memcmp(ptr, "MThd", 4) != 0) {
        error_ = "Not a MIDI file (missing MThd)";
        return false;
    }
    ptr += 4;

    uint32_t headerLen = readBE32(ptr);
    ptr += 4;

    if (headerLen < 6) {
        error_ = "Invalid header length";
        return false;
    }

    format_ = readBE16(ptr);
    ptr += 2;

    numTracks_ = readBE16(ptr);
    ptr += 2;

    uint16_t division = readBE16(ptr);
    ptr += 2;

    // Check division format
    if (division & 0x8000) {
        // SMPTE format - not commonly used, convert to approximate ticks
        error_ = "SMPTE time format not supported";
        return false;
    } else {
        ticksPerQuarter_ = division;
    }

    // Skip any extra header bytes
    if (headerLen > 6) {
        ptr += (headerLen - 6);
    }

    return true;
}

bool MidiFile::parseTrack(const uint8_t*& ptr, const uint8_t* end) {
    if (end - ptr < 8) {
        error_ = "Unexpected end of file (track header)";
        return false;
    }

    // Check "MTrk" magic
    if (memcmp(ptr, "MTrk", 4) != 0) {
        error_ = "Invalid track header (missing MTrk)";
        return false;
    }
    ptr += 4;

    uint32_t trackLen = readBE32(ptr);
    ptr += 4;

    if (end - ptr < trackLen) {
        error_ = "Track length exceeds file size";
        return false;
    }

    const uint8_t* trackEnd = ptr + trackLen;
    uint32_t absoluteTick = 0;
    uint8_t runningStatus = 0;

    while (ptr < trackEnd) {
        // Delta time
        uint32_t delta = readVLQ(ptr, trackEnd);
        absoluteTick += delta;

        if (ptr >= trackEnd) break;

        uint8_t status = *ptr;

        // Running status
        if (status < 0x80) {
            status = runningStatus;
        } else {
            ptr++;
            if (status < 0xF0) {
                runningStatus = status;
            }
        }

        uint8_t type = status & 0xF0;
        uint8_t channel = status & 0x0F;

        if (status == 0xFF) {
            // Meta event
            if (ptr >= trackEnd) break;
            uint8_t metaType = *ptr++;
            uint32_t len = readVLQ(ptr, trackEnd);

            if (metaType == 0x51 && len == 3 && ptr + 3 <= trackEnd) {
                // Tempo change
                uint32_t uspq = (static_cast<uint32_t>(ptr[0]) << 16) |
                               (static_cast<uint32_t>(ptr[1]) << 8) |
                               static_cast<uint32_t>(ptr[2]);
                tempoMap_.push_back({absoluteTick, uspq});

                // Update tempo (first one sets the global tempo)
                if (tempoMap_.size() == 1) {
                    tempo_ = 60000000.0 / uspq;
                }
            }

            ptr += len;
        } else if (status == 0xF0 || status == 0xF7) {
            // SysEx
            uint32_t len = readVLQ(ptr, trackEnd);
            ptr += len;
        } else if (type >= 0x80 && type <= 0xE0) {
            // Channel message
            MidiEvent event{};
            event.tickTime = absoluteTick;
            event.type = type;
            event.channel = channel;

            if (ptr >= trackEnd) break;
            event.data1 = *ptr++;

            // Two-byte messages
            if (type != 0xC0 && type != 0xD0) {
                if (ptr >= trackEnd) break;
                event.data2 = *ptr++;
            }

            events_.push_back(event);
        }
    }

    ptr = trackEnd;
    return true;
}

void MidiFile::computeTimes() {
    // Default tempo if no tempo events
    if (tempoMap_.empty()) {
        tempoMap_.push_back({0, 500000}); // 120 BPM
    }

    // Sort tempo map
    std::sort(tempoMap_.begin(), tempoMap_.end(),
              [](const TempoChange& a, const TempoChange& b) {
                  return a.tick < b.tick;
              });

    // Build a cumulative time map for efficient lookup
    // Each entry: {tick, seconds_at_tick, tempo_after_tick}
    struct TimePoint {
        uint32_t tick;
        double seconds;
        uint32_t microsecondsPerQuarter;
    };
    std::vector<TimePoint> timeMap;

    double cumulativeSeconds = 0.0;
    uint32_t lastTick = 0;
    uint32_t currentTempo = 500000; // Default 120 BPM

    // If first tempo is at tick 0, use it
    if (!tempoMap_.empty() && tempoMap_[0].tick == 0) {
        currentTempo = tempoMap_[0].microsecondsPerQuarter;
    }

    timeMap.push_back({0, 0.0, currentTempo});

    for (const auto& tempo : tempoMap_) {
        if (tempo.tick == 0) {
            // Already handled above
            timeMap[0].microsecondsPerQuarter = tempo.microsecondsPerQuarter;
            currentTempo = tempo.microsecondsPerQuarter;
            continue;
        }

        // Add time for segment before this tempo change
        uint32_t deltaTicks = tempo.tick - lastTick;
        cumulativeSeconds += (deltaTicks * static_cast<double>(currentTempo)) /
                            (ticksPerQuarter_ * 1000000.0);

        lastTick = tempo.tick;
        currentTempo = tempo.microsecondsPerQuarter;
        timeMap.push_back({tempo.tick, cumulativeSeconds, currentTempo});
    }

    // Helper to convert tick to seconds
    auto tickToSeconds = [&](uint32_t tick) -> double {
        // Find the tempo region containing this tick
        size_t idx = 0;
        for (size_t i = 1; i < timeMap.size(); ++i) {
            if (timeMap[i].tick > tick) break;
            idx = i;
        }

        const auto& tp = timeMap[idx];
        uint32_t deltaTicks = tick - tp.tick;
        return tp.seconds + (deltaTicks * static_cast<double>(tp.microsecondsPerQuarter)) /
                           (ticksPerQuarter_ * 1000000.0);
    };

    // Compute time for each event
    for (auto& event : events_) {
        event.secondTime = tickToSeconds(event.tickTime);
    }

    // Compute total duration from the maximum tick time
    uint32_t maxTick = 0;
    for (const auto& event : events_) {
        maxTick = std::max(maxTick, event.tickTime);
    }
    durationSeconds_ = tickToSeconds(maxTick);

    // Update reported tempo to the initial tempo
    if (!tempoMap_.empty()) {
        tempo_ = 60000000.0 / tempoMap_[0].microsecondsPerQuarter;
    }
}

std::vector<MidiEvent> MidiFile::noteEvents() const {
    std::vector<MidiEvent> notes;
    for (const auto& e : events_) {
        if (e.isNoteOn() || e.isNoteOff()) {
            notes.push_back(e);
        }
    }
    return notes;
}

// Write big-endian values
static void writeBE32(std::vector<uint8_t>& data, uint32_t value) {
    data.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    data.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(value & 0xFF));
}

static void writeBE16(std::vector<uint8_t>& data, uint16_t value) {
    data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(value & 0xFF));
}

// Write variable-length quantity
static void writeVLQ(std::vector<uint8_t>& data, uint32_t value) {
    std::vector<uint8_t> bytes;
    bytes.push_back(value & 0x7F);
    value >>= 7;
    while (value > 0) {
        bytes.push_back((value & 0x7F) | 0x80);
        value >>= 7;
    }
    // Write in reverse order
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it) {
        data.push_back(*it);
    }
}

bool MidiFile::save(const char* path,
                    const std::vector<MidiEvent>& events,
                    double tempo,
                    uint16_t ticksPerQuarter) {
    std::vector<uint8_t> data;

    // Header chunk
    data.push_back('M'); data.push_back('T');
    data.push_back('h'); data.push_back('d');
    writeBE32(data, 6);           // Header length
    writeBE16(data, 0);           // Format 0 (single track)
    writeBE16(data, 1);           // Number of tracks
    writeBE16(data, ticksPerQuarter);

    // Build track data
    std::vector<uint8_t> trackData;

    // Tempo meta event at start
    uint32_t microsecondsPerQuarter = static_cast<uint32_t>(60000000.0 / tempo);
    writeVLQ(trackData, 0);       // Delta time 0
    trackData.push_back(0xFF);    // Meta event
    trackData.push_back(0x51);    // Tempo
    trackData.push_back(0x03);    // Length
    trackData.push_back(static_cast<uint8_t>((microsecondsPerQuarter >> 16) & 0xFF));
    trackData.push_back(static_cast<uint8_t>((microsecondsPerQuarter >> 8) & 0xFF));
    trackData.push_back(static_cast<uint8_t>(microsecondsPerQuarter & 0xFF));

    // Convert events to ticks and sort
    // Sort by tick, then by type (note-on before note-off at same tick)
    std::vector<std::pair<uint32_t, const MidiEvent*>> sortedEvents;
    for (const auto& event : events) {
        uint32_t tick = static_cast<uint32_t>(event.secondTime * ticksPerQuarter * tempo / 60.0);
        sortedEvents.push_back({tick, &event});
    }
    std::stable_sort(sortedEvents.begin(), sortedEvents.end(),
              [](const auto& a, const auto& b) {
                  if (a.first != b.first) return a.first < b.first;
                  // At same tick: note-on (0x90) before note-off (0x80)
                  return a.second->type > b.second->type;
              });

    // Write events
    uint32_t lastTick = 0;
    for (const auto& [tick, event] : sortedEvents) {
        uint32_t delta = tick - lastTick;
        lastTick = tick;

        writeVLQ(trackData, delta);

        uint8_t status = event->type | (event->channel & 0x0F);
        trackData.push_back(status);
        trackData.push_back(event->data1);

        // Two-byte messages (not program change or channel pressure)
        if (event->type != MidiEvent::ProgramChange &&
            event->type != MidiEvent::ChannelPressure) {
            trackData.push_back(event->data2);
        }
    }

    // End of track
    writeVLQ(trackData, 0);
    trackData.push_back(0xFF);
    trackData.push_back(0x2F);
    trackData.push_back(0x00);

    // Track chunk
    data.push_back('M'); data.push_back('T');
    data.push_back('r'); data.push_back('k');
    writeBE32(data, static_cast<uint32_t>(trackData.size()));
    data.insert(data.end(), trackData.begin(), trackData.end());

    // Write to file
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return true;
}

} // namespace clap_trap
