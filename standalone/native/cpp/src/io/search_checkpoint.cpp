// Port of io/search_checkpoint.py's SearchCheckpoint for native states (its snapshot branch).
#include "pkc/io/search_checkpoint.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "../value_ops.hpp"
#include "pkc/canonical.hpp"

namespace pkc {
namespace {

const Value& at_path(const Value& root, const Array& path) {
    const Value* node = &root;
    for (const Value& part : path) {
        if (const auto* index = std::get_if<std::int64_t>(&part.data))
            node = &items(*node).at(static_cast<std::size_t>(*index));
        else
            node = &at(fields(*node), as_text(part));
    }
    return *node;
}

Array joined(const Array& head, const Array& tail) {
    Array out = head;
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

struct Timer {
    double& total;
    double& longest;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~Timer() {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        total += elapsed;
        longest = std::max(longest, elapsed);
    }
};

}  // namespace

SearchSaver::SearchSaver(std::shared_ptr<CheckpointStore> store, Value metadata, Value outer,
                         std::vector<std::string> windows)
    : store_(std::move(store)),
      metadata_(std::move(metadata)),
      outer_(std::move(outer)),
      windows_(std::move(windows)) {
    dfs_path_ = is_null(outer_) ? Array{text("searchState")} : Array{text("searchState"), text("dfs")};
}

void SearchSaver::store_windows(const Value& dfs) {
    // Audit windows are immutable: each is stored once, and the saves name it.
    const Array& windows = items(at(fields(dfs), "windows"));
    if (windows.size() < windows_.size()) fail("Checkpoint audit windows cannot shrink");
    for (std::size_t i = windows_.size(); i < windows.size(); ++i)
        windows_.push_back(store_->archive([&](CheckpointEncoder& e) { return e.encode(windows[i]); }).root);
}

std::string SearchSaver::search_state(CheckpointEncoder& e, const Value& outer, const Value& dfs) const {
    const std::string stored = e.encode_replacing(dfs, "windows", windows_);
    if (is_null(outer)) return stored;
    std::vector<std::pair<std::string, std::string>> entries;
    bool placed = false;
    for (const auto& [key, value] : fields(outer)) {
        placed = placed || key == "dfs";
        entries.emplace_back(key, key == "dfs" ? stored : e.encode(value));
    }
    if (!placed) entries.emplace_back("dfs", stored);
    return e.entries(entries);
}

std::string SearchSaver::document(CheckpointEncoder& e,
                                  const std::vector<std::pair<std::string, std::string>>& entries) const {
    std::vector<std::pair<std::string, std::string>> saved;
    std::vector<bool> placed(entries.size(), false);
    for (const auto& [key, value] : fields(metadata_)) {
        std::size_t i = 0;
        while (i < entries.size() && entries[i].first != key) ++i;
        if (i < entries.size()) placed[i] = true;
        saved.emplace_back(key, i < entries.size() ? entries[i].second : e.encode(value));
    }
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (!placed[i]) saved.push_back(entries[i]);
    return e.entries(saved);
}

void SearchSaver::save(const Value& state, const RepairTargets& repair) {
    Timer timer{seconds, max_seconds};
    ++saves;
    store_windows(state);
    // dict(metadata, windows=windows, searchState=dict(outer, dfs=stored)): existing keys keep their place.
    const std::string root = store_->archive([&](CheckpointEncoder& e) {
        return document(e, {{"windows", e.references(windows_)}, {"searchState", search_state(e, outer_, state)}});
    }).root;
    store_->publish(root);
    base_ = root;
    token_ = repair.token;
    paths_.clear();
    extra_.clear();
    for (const Array& path : repair.program) paths_.push_back(joined(dfs_path_, path));
    for (const auto& paths : repair.targets) {
        std::vector<Array> full;
        for (const Array& path : paths) full.push_back(joined(dfs_path_, path));
        extra_.push_back(std::move(full));
    }
}

void SearchSaver::finish(const Value& result, const Value& validation) {
    const Object& r = fields(result);
    const Value* state = get(r, "searchState");
    const Value* dfs = state && is_object(*state) ? get(fields(*state), "dfs") : nullptr;
    const bool stored = dfs && !is_null(*dfs);
    if (stored) store_windows(*dfs);
    const std::string root = store_->archive([&](CheckpointEncoder& e) {
        const std::string references = e.references(windows_);
        std::vector<std::pair<std::string, std::string>> entries;
        bool windows = false;
        for (const auto& [key, value] : r) {
            windows = windows || key == "windows";
            if (key == "windows")
                entries.emplace_back(key, references);
            else if (key == "searchState" && stored)
                entries.emplace_back(key, search_state(e, object(with(fields(*state), "dfs", kNull)), *dfs));
            else
                entries.emplace_back(key, e.encode(value));
        }
        if (!windows) entries.emplace_back("windows", references);
        return document(e, {{"windows", references}, {"result", e.entries(entries)}, {"validation", e.encode(validation)}});
    }).root;
    store_->publish(root);
    store_->compact();
}

void SearchSaver::progress(const Value& state, const RepairTargets& repair) {
    if (!base_ || !repair.patch || repair.token != token_) {
        save(state, repair);
        return;
    }
    Timer timer{seconds, max_seconds};
    ++patches;
    const std::size_t inner = dfs_path_.size();
    const auto relative = [&](const Array& path) { return Array(path.begin() + static_cast<std::ptrdiff_t>(inner), path.end()); };
    // [(path, value), ...]: a pair holding a native part is never flat, so it encodes as a list of its two encodings.
    const auto changes = [&](CheckpointEncoder& e) {
        std::vector<std::string> pairs;
        const auto part = [&](const Array& path, const Value& value) {
            pairs.push_back(e.chunk("[\"l\",[" + e.encode(array(path)) + "," + e.encode(value) + "]]"));
        };
        for (const Array& path : paths_) {
            Array full = path;
            full.push_back(text("repairState"));
            part(full, at_path(state, relative(full)));
        }
        for (const auto& paths : extra_)
            for (const Array& path : paths) part(path, at_path(state, relative(path)));
        Array calls = dfs_path_;
        calls.push_back(text("cumulativeCalls"));
        pairs.push_back(e.encode(array({array(calls), at(fields(state), "cumulativeCalls")})));
        std::string data = "[\"l\",[";
        for (std::size_t i = 0; i < pairs.size(); ++i) data += (i ? "," : "") + pairs[i];
        return e.chunk(data + "]]");
    };
    store_->publish_patch(*base_, store_->archive(changes).root);
}

}  // namespace pkc
