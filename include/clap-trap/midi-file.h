/**
 * clap-trap: MIDI File Reader
 *
 * Simple MIDI file parser for testing note effects.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace clap_trap {

/**
 * MIDI event from a file
 */
struct MidiEvent {
    enum Type {
        NoteOff = 0x80,
        NoteOn = 0x90,
        PolyPressure = 0xA0,
        ControlChange = 0xB0,
        ProgramChange = 0xC0,
        ChannelPressure = 0xD0,
        PitchBend = 0xE0,
        Meta = 0xFF
    };

    uint32_t tickTime;      // Absolute tick time
    double secondTime;      // Absolute time in seconds (computed after tempo map)
    uint8_t type;           // Event type (status byte with channel stripped)
    uint8_t channel;        // MIDI channel (0-15)
    uint8_t data1;          // First data byte (e.g., note number)
    uint8_t data2;          // Second data byte (e.g., velocity)

    bool isNoteOn() const { return type == NoteOn && data2 > 0; }
    bool isNoteOff() const { return type == NoteOff || (type == NoteOn && data2 == 0); }
};

/**
 * Simple MIDI file reader
 *
 * Supports Standard MIDI Files (Type 0 and 1).
 * Merges all tracks into a single event list sorted by time.
 */
class MidiFile {
public:
    /// Load a MIDI file
    static std::unique_ptr<MidiFile> load(const char* path);

    /// Check for errors
    bool hasError() const { return !error_.empty(); }
    const std::string& getError() const { return error_; }

    /// Get file properties
    uint16_t format() const { return format_; }
    uint16_t ticksPerQuarter() const { return ticksPerQuarter_; }
    double durationSeconds() const { return durationSeconds_; }

    /// Get all events (sorted by time)
    const std::vector<MidiEvent>& events() const { return events_; }

    /// Get note events only (note on/off)
    std::vector<MidiEvent> noteEvents() const;

    /// Get tempo in BPM (from first tempo event, or 120 default)
    double tempo() const { return tempo_; }

    /// Write a MIDI file from events
    /// @param path Output file path
    /// @param events Events to write (must include secondTime)
    /// @param tempo Tempo in BPM
    /// @param ticksPerQuarter Ticks per quarter note (default 480)
    /// @return true on success
    static bool save(const char* path,
                     const std::vector<MidiEvent>& events,
                     double tempo = 120.0,
                     uint16_t ticksPerQuarter = 480);

private:
    MidiFile() = default;

    bool parse(const std::vector<uint8_t>& data);
    bool parseHeader(const uint8_t*& ptr, const uint8_t* end);
    bool parseTrack(const uint8_t*& ptr, const uint8_t* end);
    void computeTimes();

    std::string error_;
    uint16_t format_ = 0;
    uint16_t numTracks_ = 0;
    uint16_t ticksPerQuarter_ = 480;
    double tempo_ = 120.0;
    double durationSeconds_ = 0.0;

    std::vector<MidiEvent> events_;

    // Tempo map for time conversion
    struct TempoChange {
        uint32_t tick;
        uint32_t microsecondsPerQuarter;
    };
    std::vector<TempoChange> tempoMap_;
};

} // namespace clap_trap
