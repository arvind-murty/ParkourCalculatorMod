#include "pkc/io/checkpoint_store.hpp"

#include <string_view>
#include <variant>

#include "../value_ops.hpp"
#include "pkc/canonical.hpp"

namespace pkc {
namespace {

constexpr std::size_t kChunkBytes = 2048;  // checkpoint_store.CHUNK_BYTES

std::string json(const Value& value) { return dumps(value, false, true); }

// len(str) in Python: code points.
std::size_t code_points(const std::string& text) {
    std::size_t n = 0;
    for (unsigned char c : text) n += (c & 0xC0) != 0x80;
    return n;
}

bool is_container(const Value& value) { return is_object(value) || is_list(value); }

bool short_text(const Value& value) {
    const auto* s = std::get_if<std::string>(&value.data);
    return !s || code_points(*s) < kChunkBytes;
}

// flat(item): a scalar (a short string), or a container of at most 16 such scalars.
bool flat(const Value& item) {
    if (!is_container(item)) return short_text(item);
    std::size_t size = 0;
    bool scalars = true;
    const auto visit = [&](const Value& child) {
        ++size;
        scalars = scalars && !is_container(child) && short_text(child);
    };
    if (is_object(item)) {
        for (const auto& [_, child] : fields(item)) visit(child);
    } else {
        for (const Value& child : items(item)) visit(child);
    }
    return size <= 16 && scalars;
}

}  // namespace

const std::string* EncodeCache::find(const void* identity) {
    const auto found = entries_.find(identity);
    if (found == entries_.end()) return nullptr;
    if (found->second.container.expired()) {  // a new container at a dead one's address
        entries_.erase(found);
        return nullptr;
    }
    found->second.epoch = epoch_;
    return &found->second.data;
}

void EncodeCache::store(const void* identity, const Value& container, const std::string& data) {
    std::weak_ptr<const void> watched;
    if (is_object(container)) watched = std::get<std::shared_ptr<const Object>>(container.data);
    if (is_list(container)) watched = std::get<std::shared_ptr<const Array>>(container.data);
    entries_.insert_or_assign(identity, Entry{std::move(watched), data, epoch_});
}

void EncodeCache::begin_snapshot() {
    ++epoch_;
    for (auto it = entries_.begin(); it != entries_.end();)
        it = it->second.epoch + 1 < epoch_ || it->second.container.expired() ? entries_.erase(it) : std::next(it);
}

CheckpointEncoder::CheckpointEncoder(std::function<bool(const std::string& key)> known, EncodeCache* cache)
    : known_(std::move(known)), cache_(cache) {}

std::string CheckpointEncoder::reference(const std::string& key) const { return "[\"r\",\"" + key + "\"]"; }

std::string CheckpointEncoder::remember(const std::string& data) {
    std::string key = sha256_hex(data);
    if (!known_(key)) {
        if (const auto found = pending_index_.find(key); found != pending_index_.end()) {
            pending[found->second].second = data;
        } else {
            pending_index_.emplace(key, pending.size());
            pending.emplace_back(key, data);
        }
    }
    return key;
}

std::string CheckpointEncoder::encode(const Value& value) {
    const bool container = is_container(value);
    const void* identity = nullptr;
    if (is_object(value)) identity = std::get<std::shared_ptr<const Object>>(value.data).get();
    if (is_list(value)) identity = std::get<std::shared_ptr<const Array>>(value.data).get();
    if (identity) {
        if (const auto found = memo_.find(identity); found != memo_.end()) return found->second;
        if (cache_)
            if (const std::string* cached = cache_->find(identity)) return memo_.emplace(identity, *cached).first->second;
    }
    std::string data;
    bool all_flat = container;
    if (is_object(value)) {
        for (const auto& [_, child] : fields(value)) all_flat = all_flat && flat(child);
    } else if (is_list(value)) {
        for (const Value& child : items(value)) all_flat = all_flat && flat(child);
    }
    if (all_flat) {
        // The C JSON encoder handles dense numerical arrays and small constraint rows together.
        data = "[\"j\"," + json(value) + "]";
    } else if (is_object(value)) {
        data = "[\"d\",[";
        bool first = true;
        for (const auto& [key, child] : fields(value)) {
            if (!first) data += ',';
            first = false;
            data += '[';
            data += json(Value{key});
            data += ',';
            data += encode(child);
            data += ']';
        }
        data += "]]";
    } else if (is_list(value)) {
        data = "[\"l\",[";
        bool first = true;
        for (const Value& child : items(value)) {
            if (!first) data += ',';
            first = false;
            data += encode(child);
        }
        data += "]]";
    } else {
        data = json(value);
    }
    const bool stored = data.size() >= kChunkBytes;
    data = chunk(std::move(data));
    if (identity) {
        memo_.emplace(identity, data);
        // Only a stored object's reference: a smaller encoding sits inside its parent's, so keeping it too would hold
        // the same text twice, and it costs little to redo.
        if (cache_ && stored) cache_->store(identity, value, data);
    }
    return data;
}

std::string CheckpointEncoder::chunk(std::string data) {
    return data.size() >= kChunkBytes ? reference(remember(data)) : data;
}

std::string CheckpointEncoder::encode_replacing(const Value& object, const std::string& key,
                                                const std::vector<std::string>& stored) {
    // A list of references is never flat; an empty one is.
    std::string replaced;
    if (stored.empty()) {
        replaced = "[\"j\",[]]";
    } else {
        replaced = "[\"l\",[";
        for (std::size_t i = 0; i < stored.size(); ++i) replaced += (i ? "," : "") + reference(stored[i]);
        replaced += "]]";
        replaced = chunk(std::move(replaced));
    }
    bool all_flat = true;
    for (const auto& [k, child] : fields(object)) all_flat = all_flat && (k == key ? stored.empty() : flat(child));
    if (all_flat) {
        Object copy = fields(object);
        put(copy, key, array({}));
        return chunk("[\"j\"," + json(pkc::object(std::move(copy))) + "]");
    }
    std::string data = "[\"d\",[";
    bool first = true;
    for (const auto& [k, child] : fields(object)) {
        if (!first) data += ',';
        first = false;
        data += '[' + json(Value{k}) + ',' + (k == key ? replaced : encode(child)) + ']';
    }
    return chunk(data + "]]");
}

}  // namespace pkc
