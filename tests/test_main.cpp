/**
 * clap-trap unit tests
 */

#include <catch2/catch_test_macros.hpp>
#include "clap-trap/clap-trap.h"

using namespace clap_trap;

//-----------------------------------------------------------------------------
// TestHost tests
//-----------------------------------------------------------------------------

TEST_CASE("TestHost basic functionality", "[host]") {
    TestHost host;

    SECTION("clap_host_t is valid") {
        const auto* h = host.clapHost();
        REQUIRE(h != nullptr);
        REQUIRE(h->name != nullptr);
        REQUIRE(h->vendor != nullptr);
        REQUIRE(h->version != nullptr);
        REQUIRE(clap_version_is_compatible(h->clap_version));
    }

    SECTION("Request flags start false") {
        REQUIRE_FALSE(host.restartRequested());
        REQUIRE_FALSE(host.processRequested());
        REQUIRE_FALSE(host.callbackRequested());
    }

    SECTION("Request callbacks set flags") {
        const auto* h = host.clapHost();
        h->request_restart(h);
        REQUIRE(host.restartRequested());

        h->request_process(h);
        REQUIRE(host.processRequested());

        h->request_callback(h);
        REQUIRE(host.callbackRequested());
    }

    SECTION("Reset clears flags") {
        const auto* h = host.clapHost();
        h->request_restart(h);
        h->request_process(h);
        h->request_callback(h);

        host.resetRequests();
        REQUIRE_FALSE(host.restartRequested());
        REQUIRE_FALSE(host.processRequested());
        REQUIRE_FALSE(host.callbackRequested());
    }
}

//-----------------------------------------------------------------------------
// Event list tests
//-----------------------------------------------------------------------------

TEST_CASE("EmptyInputEvents", "[events]") {
    EmptyInputEvents events;

    REQUIRE(events.get()->size(events.get()) == 0);
    REQUIRE(events.get()->get(events.get(), 0) == nullptr);
}

TEST_CASE("DiscardOutputEvents", "[events]") {
    DiscardOutputEvents events;

    clap_event_note_t note{};
    note.header.size = sizeof(note);
    note.header.type = CLAP_EVENT_NOTE_ON;

    // Should accept events
    REQUIRE(events.get()->try_push(events.get(), &note.header));
}

TEST_CASE("SimpleInputEvents", "[events]") {
    SimpleInputEvents events;

    SECTION("Empty by default") {
        REQUIRE(events.get()->size(events.get()) == 0);
    }

    SECTION("Add note-on") {
        events.addNoteOn(0, 0, 0, 60, 1, 0.8);
        REQUIRE(events.get()->size(events.get()) == 1);

        const auto* header = events.get()->get(events.get(), 0);
        REQUIRE(header != nullptr);
        REQUIRE(header->type == CLAP_EVENT_NOTE_ON);

        const auto* note = reinterpret_cast<const clap_event_note_t*>(header);
        REQUIRE(note->key == 60);
        REQUIRE(note->velocity == 0.8);
    }

    SECTION("Add multiple events") {
        events.addNoteOn(0, 0, 0, 60, 1, 0.8);
        events.addNoteOff(100, 0, 0, 60, 1, 0.5);
        events.addParamValue(50, 1, 0.5);

        REQUIRE(events.get()->size(events.get()) == 3);
    }

    SECTION("Clear events") {
        events.addNoteOn(0, 0, 0, 60, 1, 0.8);
        events.clear();
        REQUIRE(events.get()->size(events.get()) == 0);
    }
}

//-----------------------------------------------------------------------------
// Audio buffer tests
//-----------------------------------------------------------------------------

TEST_CASE("StereoAudioBuffers", "[buffers]") {
    StereoAudioBuffers buffers(256);

    SECTION("Initial state is zeroed") {
        REQUIRE_FALSE(buffers.outputHasNonZero());
        REQUIRE(buffers.outputIsValid());
    }

    SECTION("Fill with sine produces non-zero") {
        buffers.fillInputWithSine(440.0f, 48000.0f);
        // Check input has data
        bool hasNonZero = false;
        for (uint32_t i = 0; i < buffers.blockSize(); ++i) {
            if (buffers.inputData(0)[i] != 0.0f) {
                hasNonZero = true;
                break;
            }
        }
        REQUIRE(hasNonZero);
    }

    SECTION("CLAP buffers are valid") {
        REQUIRE(buffers.inputBuffer() != nullptr);
        REQUIRE(buffers.outputBuffer() != nullptr);
        REQUIRE(buffers.inputBuffer()->channel_count == 2);
        REQUIRE(buffers.outputBuffer()->channel_count == 2);
    }

    SECTION("Clear resets to zero") {
        buffers.fillInputWithSine(440.0f, 48000.0f);
        buffers.clearInput();

        bool allZero = true;
        for (uint32_t ch = 0; ch < 2; ++ch) {
            for (uint32_t i = 0; i < buffers.blockSize(); ++i) {
                if (buffers.inputData(ch)[i] != 0.0f) {
                    allZero = false;
                    break;
                }
            }
        }
        REQUIRE(allZero);
    }
}

TEST_CASE("AudioBuffers multi-channel", "[buffers]") {
    AudioBuffers buffers(128, 4, 6);

    REQUIRE(buffers.inputChannels() == 4);
    REQUIRE(buffers.outputChannels() == 6);
    REQUIRE(buffers.blockSize() == 128);
    REQUIRE(buffers.inputBuffer()->channel_count == 4);
    REQUIRE(buffers.outputBuffer()->channel_count == 6);
}

//-----------------------------------------------------------------------------
// PluginLoader tests (without actual plugin)
//-----------------------------------------------------------------------------

TEST_CASE("PluginLoader error handling", "[loader]") {
    SECTION("Non-existent file") {
        auto loader = PluginLoader::load("/nonexistent/path/plugin.clap");
        REQUIRE(loader->entry() == nullptr);
        REQUIRE_FALSE(loader->getError().empty());
    }
}
