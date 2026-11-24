// io/search_checkpoint.py's SearchCheckpoint for a native search's states: full saves at transitions and repair patches
// against the last one, written by the native store with no Python call. The saved document is the Python saver's:
// dict(metadata, windows=[stored windows], searchState=dict(outer, dfs=state with its windows replaced by theirs)).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pkc/io/store.hpp"
#include "pkc/value.hpp"

namespace pkc {

// Where the program of the repair a checkpoint saver follows sits in a published state (paths from the state's root),
// and where each extra target sits (a decision check's node entry): until the next full save, the saver writes the
// program's repairState and each target whole there. Mirrors io/search_checkpoint.py's identity search: the active
// anchor-leg program by default, the program and targets a repair began with.
struct RepairTargets {
    std::int64_t token = 0;  // the full save these paths belong to
    bool patch = false;      // a repair's progress: patch that save rather than saving in full
    std::vector<Array> program;
    std::vector<std::vector<Array>> targets;
};

class SearchSaver {
public:
    // `outer` is the event search's state, whose dfs entry the saver fills in place (null when the search state is
    // saved alone); `windows` the windows the Python saver already stored, in order.
    SearchSaver(std::shared_ptr<CheckpointStore> store, Value metadata, Value outer, std::vector<std::string> windows);

    void save(const Value& state, const RepairTargets& repair);      // __call__: a full save
    void progress(const Value& state, const RepairTargets& repair);  // repair_progress: a patch when it can
    // finish(result, validation): the final report dict(metadata, windows, result=dict(result, windows, searchState
    // with its dfs's windows stored), validation), saved, then the store compacted.
    void finish(const Value& result, const Value& validation);
    const std::vector<std::string>& windows() const { return windows_; }

    double seconds = 0, max_seconds = 0;  // spent saving, in all and in the longest save or patch
    std::int64_t saves = 0, patches = 0;

private:
    void store_windows(const Value& dfs);  // each audit window once, in order
    // The event state `outer` with `dfs` in its dfs entry, the dfs's windows replaced by the stored ones.
    std::string search_state(CheckpointEncoder& e, const Value& outer, const Value& dfs) const;
    // dict(metadata, **entries): each entry in the metadata's place when it has one, else appended in order.
    std::string document(CheckpointEncoder& e, const std::vector<std::pair<std::string, std::string>>& entries) const;

    std::shared_ptr<CheckpointStore> store_;
    Value metadata_, outer_;
    Array dfs_path_;
    std::vector<std::string> windows_;
    std::optional<std::string> base_;
    std::int64_t token_ = 0;
    std::vector<Array> paths_;
    std::vector<std::vector<Array>> extra_;
};

}  // namespace pkc
