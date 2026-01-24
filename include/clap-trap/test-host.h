/**
 * clap-trap: Test Host
 *
 * Minimal CLAP host implementation for headless testing.
 */

#pragma once

#include <clap/clap.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace clap_trap {

/**
 * Minimal CLAP host implementation for testing.
 *
 * Provides the required clap_host_t interface with optional
 * callbacks for host requests.
 */
class TestHost {
public:
    TestHost(const char* name = "clap-trap",
             const char* vendor = "clap-trap",
             const char* version = "1.0.0");

    /// Get the clap_host_t pointer to pass to plugins
    const clap_host_t* clapHost() const { return &host_; }

    /// Check if restart was requested
    bool restartRequested() const { return restartRequested_; }

    /// Check if process was requested
    bool processRequested() const { return processRequested_; }

    /// Check if callback was requested
    bool callbackRequested() const { return callbackRequested_; }

    /// Reset all request flags
    void resetRequests();

    /// Optional: set callback for extension requests
    using ExtensionCallback = std::function<const void*(const char* id)>;
    void setExtensionCallback(ExtensionCallback cb) { extensionCallback_ = std::move(cb); }

private:
    clap_host_t host_;
    bool restartRequested_ = false;
    bool processRequested_ = false;
    bool callbackRequested_ = false;
    ExtensionCallback extensionCallback_;

    static const void* hostGetExtension(const clap_host_t* host, const char* id);
    static void hostRequestRestart(const clap_host_t* host);
    static void hostRequestProcess(const clap_host_t* host);
    static void hostRequestCallback(const clap_host_t* host);
};

/**
 * Empty input events - returns 0 events.
 */
class EmptyInputEvents {
public:
    EmptyInputEvents();
    const clap_input_events_t* get() const { return &events_; }

private:
    clap_input_events_t events_;
};

/**
 * Discarding output events - accepts but ignores all events.
 */
class DiscardOutputEvents {
public:
    DiscardOutputEvents();
    const clap_output_events_t* get() const { return &events_; }

private:
    clap_output_events_t events_;
};

/**
 * Captured event - stores event type and relevant data.
 */
struct CapturedEvent {
    uint32_t time;
    uint16_t type;

    // Note event data
    int16_t port;
    int16_t channel;
    int16_t key;
    int32_t noteId;
    double velocity;

    // Note expression data
    clap_note_expression expressionId;
    double expressionValue;

    // Parameter data
    clap_id paramId;
    double paramValue;

    // Utility methods
    bool isNoteOn() const { return type == CLAP_EVENT_NOTE_ON; }
    bool isNoteOff() const { return type == CLAP_EVENT_NOTE_OFF; }
    bool isNoteExpression() const { return type == CLAP_EVENT_NOTE_EXPRESSION; }
    bool isParamValue() const { return type == CLAP_EVENT_PARAM_VALUE; }
};

/**
 * Capturing output events - stores all events for inspection.
 */
class CaptureOutputEvents {
public:
    CaptureOutputEvents();

    const clap_output_events_t* get() const { return &events_; }

    /// Get all captured events
    const std::vector<CapturedEvent>& events() const { return captured_; }

    /// Clear captured events
    void clear() { captured_.clear(); }

    /// Get count of specific event types
    size_t countNoteOn() const;
    size_t countNoteOff() const;
    size_t countNoteExpression() const;
    size_t countParamValue() const;

private:
    clap_output_events_t events_;
    std::vector<CapturedEvent> captured_;

    static bool tryPush(const clap_output_events_t* list, const clap_event_header_t* event);
};

/**
 * Simple input events list that holds a vector of events.
 */
class SimpleInputEvents {
public:
    SimpleInputEvents();

    /// Add a note-on event
    void addNoteOn(uint32_t time, int16_t port, int16_t channel,
                   int16_t key, int32_t noteId, double velocity);

    /// Add a note-off event
    void addNoteOff(uint32_t time, int16_t port, int16_t channel,
                    int16_t key, int32_t noteId, double velocity);

    /// Add a parameter value event
    void addParamValue(uint32_t time, clap_id paramId, double value);

    /// Clear all events
    void clear();

    const clap_input_events_t* get() const { return &events_; }

private:
    clap_input_events_t events_;
    std::vector<uint8_t> eventData_;
    std::vector<size_t> eventOffsets_;  // Store offsets instead of pointers (buffer can reallocate!)

    static uint32_t size(const clap_input_events_t* list);
    static const clap_event_header_t* getEvent(const clap_input_events_t* list, uint32_t index);
};

} // namespace clap_trap
