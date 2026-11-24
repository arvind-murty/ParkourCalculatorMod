// work.py's SearchWork (a search's call budget, deadline and counters) and search/engine.py's ScopedWork (a node
// solve's budget inside its parent's), for the native solve entry (search/application.hpp).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

class SearchWork {
public:
    explicit SearchWork(std::optional<std::int64_t> max_solver_calls = std::nullopt,
                        std::optional<double> deadline = std::nullopt);

    void check();  // the deadline, then the call budget: throws Timeout(termination) once either ends the work
    void proposal();
    void before_solve();
    void accepted();
    void count(const std::string& name, std::int64_t by = 1);  // counts[name] += by
    void update(const Value& counts);                          // counts.update(counts)
    std::int64_t counts(const std::string& name) const;        // counts[name], 0 when absent
    Value summary() const;

    std::optional<double> deadline;
    std::optional<std::string> termination;

private:
    std::optional<std::int64_t> max_solver_calls_;
    double started_;
    std::vector<std::pair<std::string, std::int64_t>> counts_;  // a Counter, in insertion order
    std::optional<double> first_proposal_, first_accepted_;
};

class ScopedWork {
public:
    ScopedWork(std::shared_ptr<SearchWork> parent, std::optional<double> deadline);

    std::optional<double> deadline() const;  // the earlier of the local and the parent's
    std::optional<std::string> termination() const;
    void check();
    void proposal();
    void before_solve();
    void accepted();

    std::shared_ptr<SearchWork> parent;
    SearchWork local;
};

}  // namespace pkc
