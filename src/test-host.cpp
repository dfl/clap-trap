/**
 * clap-headless-host: Test Host Implementation
 */

#include "clap-headless-host/test-host.h"
#include <cstring>

namespace clap_headless {

//-----------------------------------------------------------------------------
// TestHost
//-----------------------------------------------------------------------------

TestHost::TestHost(const char* name, const char* vendor, const char* version) {
    host_.clap_version = CLAP_VERSION;
    host_.host_data = this;
    host_.name = name;
    host_.vendor = vendor;
    host_.url = "https://github.com/WebCLAP/clap-headless-host";
    host_.version = version;
    host_.get_extension = hostGetExtension;
    host_.request_restart = hostRequestRestart;
    host_.request_process = hostRequestProcess;
    host_.request_callback = hostRequestCallback;
}

void TestHost::resetRequests() {
    restartRequested_ = false;
    processRequested_ = false;
    callbackRequested_ = false;
}

const void* TestHost::hostGetExtension(const clap_host_t* host, const char* id) {
    auto* self = static_cast<TestHost*>(host->host_data);
    if (self->extensionCallback_) {
        return self->extensionCallback_(id);
    }
    return nullptr;
}

void TestHost::hostRequestRestart(const clap_host_t* host) {
    auto* self = static_cast<TestHost*>(host->host_data);
    self->restartRequested_ = true;
}

void TestHost::hostRequestProcess(const clap_host_t* host) {
    auto* self = static_cast<TestHost*>(host->host_data);
    self->processRequested_ = true;
}

void TestHost::hostRequestCallback(const clap_host_t* host) {
    auto* self = static_cast<TestHost*>(host->host_data);
    self->callbackRequested_ = true;
}

//-----------------------------------------------------------------------------
// EmptyInputEvents
//-----------------------------------------------------------------------------

EmptyInputEvents::EmptyInputEvents() {
    events_.ctx = this;
    events_.size = [](const clap_input_events_t*) -> uint32_t { return 0; };
    events_.get = [](const clap_input_events_t*, uint32_t) -> const clap_event_header_t* {
        return nullptr;
    };
}

//-----------------------------------------------------------------------------
// DiscardOutputEvents
//-----------------------------------------------------------------------------

DiscardOutputEvents::DiscardOutputEvents() {
    events_.ctx = this;
    events_.try_push = [](const clap_output_events_t*, const clap_event_header_t*) -> bool {
        return true; // Accept but discard
    };
}

//-----------------------------------------------------------------------------
// SimpleInputEvents
//-----------------------------------------------------------------------------

SimpleInputEvents::SimpleInputEvents() {
    events_.ctx = this;
    events_.size = size;
    events_.get = getEvent;
}

void SimpleInputEvents::addNoteOn(uint32_t time, int16_t port, int16_t channel,
                                   int16_t key, int32_t noteId, double velocity) {
    size_t offset = eventData_.size();
    eventData_.resize(offset + sizeof(clap_event_note_t));

    auto* event = reinterpret_cast<clap_event_note_t*>(eventData_.data() + offset);
    event->header.size = sizeof(clap_event_note_t);
    event->header.time = time;
    event->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event->header.type = CLAP_EVENT_NOTE_ON;
    event->header.flags = 0;
    event->note_id = noteId;
    event->port_index = port;
    event->channel = channel;
    event->key = key;
    event->velocity = velocity;

    eventPtrs_.push_back(&event->header);
}

void SimpleInputEvents::addNoteOff(uint32_t time, int16_t port, int16_t channel,
                                    int16_t key, int32_t noteId, double velocity) {
    size_t offset = eventData_.size();
    eventData_.resize(offset + sizeof(clap_event_note_t));

    auto* event = reinterpret_cast<clap_event_note_t*>(eventData_.data() + offset);
    event->header.size = sizeof(clap_event_note_t);
    event->header.time = time;
    event->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event->header.type = CLAP_EVENT_NOTE_OFF;
    event->header.flags = 0;
    event->note_id = noteId;
    event->port_index = port;
    event->channel = channel;
    event->key = key;
    event->velocity = velocity;

    eventPtrs_.push_back(&event->header);
}

void SimpleInputEvents::addParamValue(uint32_t time, clap_id paramId, double value) {
    size_t offset = eventData_.size();
    eventData_.resize(offset + sizeof(clap_event_param_value_t));

    auto* event = reinterpret_cast<clap_event_param_value_t*>(eventData_.data() + offset);
    event->header.size = sizeof(clap_event_param_value_t);
    event->header.time = time;
    event->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event->header.type = CLAP_EVENT_PARAM_VALUE;
    event->header.flags = 0;
    event->param_id = paramId;
    event->cookie = nullptr;
    event->note_id = -1;
    event->port_index = -1;
    event->channel = -1;
    event->key = -1;
    event->value = value;

    eventPtrs_.push_back(&event->header);
}

void SimpleInputEvents::clear() {
    eventData_.clear();
    eventPtrs_.clear();
}

uint32_t SimpleInputEvents::size(const clap_input_events_t* list) {
    auto* self = static_cast<SimpleInputEvents*>(list->ctx);
    return static_cast<uint32_t>(self->eventPtrs_.size());
}

const clap_event_header_t* SimpleInputEvents::getEvent(const clap_input_events_t* list, uint32_t index) {
    auto* self = static_cast<SimpleInputEvents*>(list->ctx);
    if (index >= self->eventPtrs_.size()) return nullptr;
    return self->eventPtrs_[index];
}

} // namespace clap_headless
