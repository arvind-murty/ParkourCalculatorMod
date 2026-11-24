// The native solve entry: search/application.run(fixture, compiler) with the Python bridge's compiler, through
// find_checkpoint_route, the event search's sessions (search/events/engine.search_event_routes) and the native
// anchor search, with no Python. The services Python gives a Python-hosted native search run natively here: the work
// budget (work.hpp), the compiler (the coefficient tables, scene checks and footprints, counted), the interaction
// gate's preparation cache, the node solves' services, the transfer-time bounds and the final replay.
// Routes with wall hits or ladder events, which the native solves leave to Python, are rejected (neither occurs:
// the search forces maxWallHits=0 and runs without ladders).
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "pkc/io/store.hpp"
#include "pkc/value.hpp"

namespace pkc {

struct SolveCaseOptions {
    std::optional<double> seconds;                  // settings.seconds override
    std::optional<std::int64_t> max_solver_calls;   // settings.max_solver_calls override
    std::string worker;                             // the pkc_numerics_worker executable
    std::function<bool()> cancelled;                // the cancel event; empty never cancels
    // The checkpoint store the run saves into, as the solve CLI's SearchCheckpoint does: with `save_progress` every
    // transition and repair progress, then always the final report {metadata, windows, result, validation}. Null
    // saves nothing.
    std::shared_ptr<CheckpointStore> store;
    Value metadata;
    bool save_progress = false;
    Value resume_state;  // a saved event search state (report result.searchState) to continue; null starts fresh
};

// application.run(fixture): {result, validation}. Raises std::invalid_argument as the Python raises ValueError.
Value solve_case(const Value& fixture, const SolveCaseOptions& options);

// validation/witness.replay_witness(scene, best, compiler) with the native compiler.
Value replay_witness(const Value& scene, const Value& best);

// search/bounds/flight.transfer_bounds(scene, guides, motion, InteractionGate(scene, compiler), saved=saved) on a fresh
// gate over the native compiler: the port the entry's searches run.
Value transfer_bounds(const Value& scene, const Value& guides, const Value& motion, const Value& saved);

}  // namespace pkc
