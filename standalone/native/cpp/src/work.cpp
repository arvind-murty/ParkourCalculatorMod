// Port of work.py's SearchWork and search/engine.py's ScopedWork.
#include "pkc/work.hpp"

#include <algorithm>
#include <cmath>

#include "pkc/numerics/disk.hpp"
#include "pkc/scene/static_collision.hpp"
#include "value_ops.hpp"

namespace pkc {

namespace {
bool work_only_mode = false;
}  // namespace

bool work_only() { return work_only_mode; }

void set_work_only(bool enabled) {
    if (enabled == work_only_mode) return;
    work_only_mode = enabled;
    disk_clear_decided();
}

SearchWork::SearchWork(std::optional<std::int64_t> max_solver_calls, std::optional<double> deadline)
    : deadline(deadline), max_solver_calls_(max_solver_calls), started_(seconds_now()) {
    if (max_solver_calls && *max_solver_calls < 1) throw std::invalid_argument("max_solver_calls must be a positive integer");
}

void SearchWork::check() {
    if (deadline && seconds_now() >= *deadline) termination = "deadline";
    if (max_solver_calls_ && counts("solverCalls") >= *max_solver_calls_) termination = "numerical_call_limit";
    if (termination) throw Timeout(*termination);
}

void SearchWork::proposal() {
    count("numericalProposals");
    if (!first_proposal_) first_proposal_ = seconds_now() - started_;
}

void SearchWork::before_solve() {
    check();
    count("solverCalls");
}

void SearchWork::accepted() {
    count("acceptedBranches");
    if (!first_accepted_) first_accepted_ = seconds_now() - started_;
}

void SearchWork::count(const std::string& name, std::int64_t by) {
    for (auto& [key, value] : counts_)
        if (key == name) {
            value += by;
            return;
        }
    counts_.emplace_back(name, by);
}

void SearchWork::update(const Value& counts) {
    for (const auto& [name, value] : fields(counts)) {
        const auto* flag = std::get_if<bool>(&value.data);
        count(name, flag ? static_cast<std::int64_t>(*flag) : std::get<std::int64_t>(value.data));
    }
}

std::int64_t SearchWork::counts(const std::string& name) const {
    for (const auto& [key, value] : counts_)
        if (key == name) return value;
    return 0;
}

Value SearchWork::summary() const {
    const auto seconds = [](const std::optional<double>& v) { return v ? real(*v) : kNull; };
    Object out;
    for (const auto& [key, value] : counts_) out.emplace_back(key, integer_value(value));
    // dict(self.counts, ...): a counter named like a summary field takes the field's value in the counter's place.
    put(out, "uniqueCompletePrograms", integer_value(0));
    put(out, "unitsByType", object({}));
    put(out, "firstProposalSeconds", seconds(first_proposal_));
    put(out, "firstAcceptedSeconds", seconds(first_accepted_));
    put(out, "maxSolverCalls", max_solver_calls_ ? integer_value(*max_solver_calls_) : kNull);
    put(out, "termination", termination ? text(*termination) : kNull);
    return object(std::move(out));
}

ScopedWork::ScopedWork(std::shared_ptr<SearchWork> parent, std::optional<double> deadline)
    : parent(std::move(parent)), local(std::nullopt, deadline) {}

std::optional<double> ScopedWork::deadline() const {
    const double local_deadline = local.deadline.value_or(INFINITY), parent_deadline = parent->deadline.value_or(INFINITY);
    const double earlier = std::min(local_deadline, parent_deadline);
    return std::isinf(earlier) ? std::nullopt : std::optional<double>(earlier);
}

std::optional<std::string> ScopedWork::termination() const { return parent->termination ? parent->termination : local.termination; }

void ScopedWork::check() {
    local.check();
    parent->check();
}

void ScopedWork::proposal() {
    parent->proposal();
    local.proposal();
}

void ScopedWork::before_solve() {
    check();
    parent->before_solve();
    local.before_solve();
}

void ScopedWork::accepted() { local.accepted(); }

}  // namespace pkc
