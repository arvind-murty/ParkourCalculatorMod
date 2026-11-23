// The contact-branch repair loop of one node solve: search/repair.py, with
// repair_state's resume context and branch keys, validation/scene.check_geometry
// and constraints/contacts.contact_conflicts. Wall-hit options (max_wall_hits > 0)
// and routes with passages are left to Python.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "pkc/constraints/variants.hpp"
#include "pkc/search/ordered_route.hpp"
#include "pkc/value.hpp"

namespace pkc {

struct RepairOptions {
    double seconds = 0, candidate_seconds = 0;
    std::int64_t retries = 0;
    bool improve = false, rank_equal_cost = false, optimize_goal = false;
    std::optional<bool> stop_on_feasible;
    bool oracle_rejection = false, climb_families = false, retry_unresolved = true;
    Value geometry, solid_blocks, resume_state, repair_scope, validation_scope;  // each may be null
    NumericalSolve numerical, confirm;  // may be empty: numerics.solver.solve, no confirmation walk
    std::function<Value(const Value& plan, const Value& candidate, const Value& vertical)> candidate_filter;  // may be empty
    std::function<void(const Value& state)> on_progress;  // may be empty
};

// solve_contact_branches(route, profiles, checks, compiler, ...). `initial_variants` (may be null) is the caller's
// generator; the loop may replace it with conflict-driven successors, which it owns.
Value solve_contact_branches(const Value& route, const Value& profiles, const Value& checks, const SolveHooks& hooks,
                             const RepairOptions& options, ConstraintVariants* initial_variants);

// node_alternatives(accepted, incumbent, objective): a decision check's held alternatives, best first. A null
// `incumbent` is None.
Array node_alternatives(const Array& accepted, const Value& incumbent, const Value& objective);

// repair_state.branch_key(rows, passages, hits).
std::string branch_key(const Array& rows, const Array& passages, const Array& hits);

}  // namespace pkc
