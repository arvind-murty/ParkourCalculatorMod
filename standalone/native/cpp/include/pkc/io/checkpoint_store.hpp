// io/checkpoint_store.py's object encoding: CheckpointStore.archive's encode for a value tree, producing the same
// tagged objects and content keys, so native state shares stored content with what Python writes. The store itself
// (SQLite, manifest, patches, reading) stays Python.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pkc/value.hpp"

namespace pkc {

class CheckpointEncoder {
public:
    // `known(key)`: whether the store already holds that object.
    explicit CheckpointEncoder(std::function<bool(const std::string& key)> known);

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
    std::unordered_map<const void*, std::string> memo_;  // containers already encoded in this archive
    std::unordered_map<std::string, std::size_t> pending_index_;
};

}  // namespace pkc
