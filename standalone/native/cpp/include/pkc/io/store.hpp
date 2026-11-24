// io/checkpoint_store.py's CheckpointStore for native callers: the same manifest, SQLite companion database and
// objects (keys are the SHA-256 of an object's uncompressed encoding, data zlib level 1), so either side reads the
// other's reports. Objects commit in one transaction before the manifest is replaced, which a failed save leaves as it
// was. read_report reads either side's reports.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "pkc/io/checkpoint_store.hpp"
#include "pkc/value.hpp"

struct sqlite3;

namespace pkc {

class CheckpointStore {
public:
    // `path` is the manifest's; the database is `<path>.store.sqlite3` beside it. Creates both as needed.
    explicit CheckpointStore(std::string path);
    ~CheckpointStore();
    CheckpointStore(const CheckpointStore&) = delete;
    CheckpointStore& operator=(const CheckpointStore&) = delete;

    const std::string& identity() const { return identity_; }
    const std::string& path() const { return path_; }
    bool known(const std::string& key) const { return known_.count(key) > 0; }
    std::shared_ptr<EncodeCache> cache() const { return cache_; }
    std::size_t size() const { return known_.size(); }

    struct Archived {
        std::string root;
        std::int64_t new_objects = 0, new_bytes = 0;
    };
    // archive's commit: the encoding `encode` builds through an encoder over this store's objects and cache, stored
    // with every new object in one transaction. A failure empties the encode cache and stores nothing.
    Archived archive(const std::function<std::string(CheckpointEncoder&)>& encode);
    // The objects a Python walk collected, (key, uncompressed data) not yet stored, in one transaction.
    void insert(const std::vector<std::pair<std::string, std::string>>& objects);
    void publish(const std::string& root);  // the manifest naming `root`, replaced atomically
    // patch's object ['p', [base, changes]] once stored and published; `changes` is the archived change list's root.
    std::string publish_patch(const std::string& base, const std::string& changes);
    void compact();  // drops every object the published root does not reach, then VACUUM
    void close();

private:
    void execute(const char* sql);

    std::string path_, database_, identity_;
    sqlite3* db_ = nullptr;
    std::unordered_set<std::string> known_;
    std::shared_ptr<EncodeCache> cache_ = std::make_shared<EncodeCache>();
};

// read_report(path): a manifest's report decoded from its companion database (read only), or a legacy plain or
// gzipped JSON report as it is; the Python reader's checks and messages (std::invalid_argument).
Value read_report(const std::string& path);

}  // namespace pkc
