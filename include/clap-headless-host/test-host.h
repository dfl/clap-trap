/**
 * clap-headless-host: Test Host
 *
 * Minimal CLAP host implementation for headless testing.
 */

#pragma once

#include <clap/clap.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace clap_headless {

/**
 * Minimal CLAP host implementation for testing.
 *
 * Provides the required clap_host_t interface with optional
 * callbacks for host requests.
 */
class TestHost {
public:
    TestHost(const char* name = "clap-headless-host",
             const char* vendor = "clap-headless-host",
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
    std::vector<const clap_event_header_t*> eventPtrs_;

    static uint32_t size(const clap_input_events_t* list);
    static const clap_event_header_t* getEvent(const clap_input_events_t* list, uint32_t index);
};

} // namespace clap_headless
