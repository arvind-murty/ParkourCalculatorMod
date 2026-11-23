#include "pkc/search/frontier.hpp"

#include <set>
#include <string>
#include <utility>

#include "../schema.hpp"

namespace pkc {

bool restore_alternatives(Value& state_value, const std::vector<Value>& extra_live) {
    Object state = fields(state_value);
    std::set<std::string> succeeded;
    if (const Value* nodes = get(state, "succeededNodes"))
        for (const Value& node : items(*nodes)) succeeded.insert(as_text(node));
    const Value* held_value = get(state, "alternatives");
    Object alternatives = held_value ? fields(*held_value) : Object{};
    std::vector<std::size_t> waiting;
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
        const Object& held = fields(alternatives[i].second);
        if (as_int(at(held, "next")) < static_cast<std::int64_t>(items(at(held, "programs")).size()) &&
            !succeeded.count(alternatives[i].first))
            waiting.push_back(i);
    }
    if (waiting.empty()) return false;
    const Object& parents = fields(at(state, "decisionParents"));
    std::set<std::string> alive;
    const auto live = [&](const Value& program) {
        if (!py_truthy(program)) return;
        Value node = get_or_null(fields(program), "decisionNode");
        while (py_truthy(node) && !alive.count(as_text(node))) {
            alive.insert(as_text(node));
            node = get_or_null(parents, as_text(node));
        }
    };
    for (const char* group : {"pending", "ready"})
        for (const Value& program : items(at(state, group))) live(program);
    if (const Value* deferred = get(state, "deferredNodes"))
        for (const Value& program : items(*deferred)) live(program);
    live(get_or_null(state, "nodeInFlight"));
    const Value& expansion = at(state, "expansion");
    if (py_truthy(expansion)) live(get_or_null(fields(expansion), "program"));
    for (const Value& program : extra_live) live(program);
    Array pending = items(at(state, "pending"));
    bool restored = false;
    for (std::size_t i : waiting) {
        const std::string& node = alternatives[i].first;
        Object held = fields(alternatives[i].second);
        bool keep = false;
        if (const Value* watch = get(held, "watch")) {
            for (const Value& n : items(*watch))
                keep = keep || alive.count(as_text(n)) || succeeded.count(as_text(n));
        } else {
            keep = alive.count(node) || succeeded.count(node);
        }
        for (const Value& r : items(at(held, "restored"))) keep = keep || alive.count(as_text(r));
        if (keep) continue;
        const std::int64_t next = as_int(at(held, "next"));
        const Value program = items(at(held, "programs")).at(next);
        put(held, "next", integer_value(next + 1));
        Array restored_nodes = items(at(held, "restored"));
        restored_nodes.push_back(at(fields(program), "decisionNode"));
        put(held, "restored", array(std::move(restored_nodes)));
        alternatives[i].second = object(std::move(held));
        pending.push_back(program);
        // Entries after this one see it alive: a held launch-face jump waits for a run-tick reserve restored now.
        live(program);
        restored = true;
    }
    if (!restored) return false;
    put(state, "pending", array(std::move(pending)));
    put(state, "alternatives", object(std::move(alternatives)));
    state_value = object(std::move(state));
    return true;
}

}  // namespace pkc
