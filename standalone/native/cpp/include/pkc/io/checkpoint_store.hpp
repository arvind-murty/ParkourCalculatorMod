// io/checkpoint_store.py's object encoding: CheckpointStore.archive's encode for a value tree, producing the same
// tagged objects and content keys, so native state shares stored content with what Python writes. The store itself
// (SQLite, manifest, patches, reading) stays Python.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

// One store's encodings of the containers it has encoded, kept across archives. A container value is immutable, so
// while it lives its address names the same content; an entry watches it without keeping it alive and is dropped once
// it dies. An encoding's references name objects the store already holds; the store clears the cache whenever that may
// no longer hold (a failed archive, compaction). A snapshot encode drops the entries neither it nor the one before
// touched.
class EncodeCache {
public:
    const std::string* find(const void* identity);
    void store(const void* identity, const Value& container, const std::string& data);
    void begin_snapshot();
    void clear() { entries_.clear(); }
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::weak_ptr<const void> container;
        std::string data;
        std::uint64_t epoch = 0;
    };
    std::unordered_map<const void*, Entry> entries_;
    std::uint64_t epoch_ = 1;
};

class CheckpointEncoder {
public:
    // `known(key)`: whether the store already holds that object; `cache`, when given, the store's encode cache.
    explicit CheckpointEncoder(std::function<bool(const std::string& key)> known, EncodeCache* cache = nullptr);

    std::string encode(const Value& value);  // a value's encoding; a reference once it reaches CHUNK_BYTES
    std::string remember(const std::string& data);  // stores `data` unless known; its key
    std::string reference(const std::string& key) const;  // ['r', key]
    // encode(dict(object, **{key: [StoredValue(k) for k in stored]})): the object with one entry replaced by
    // references to objects already in the store.
    std::string encode_replacing(const Value& object, const std::string& key, const std::vector<std::string>& stored);

    // New objects for the store, in first-seen order.
    std::vector<std::pair<std::string, std::string>> pending;

private:
    std::string chunk(std::string data);  // a reference once `data` reaches CHUNK_BYTES

    std::function<bool(const std::string&)> known_;
    EncodeCache* cache_ = nullptr;
    // Containers already encoded in this archive, each held so a temporary's address is never reused while it is named.
    std::unordered_map<const void*, std::pair<Value, std::string>> memo_;
    std::unordered_map<std::string, std::size_t> pending_index_;
};

}  // namespace pkc
