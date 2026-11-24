// Port of io/checkpoint_store.py's CheckpointStore (writing side).
#include "pkc/io/store.hpp"

#include <sqlite3.h>
#include <zlib.h>

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>

#include "../value_ops.hpp"
#include "pkc/canonical.hpp"

namespace pkc {
namespace {

constexpr std::int64_t kVersion = 1;  // checkpoint_store.VERSION

[[noreturn]] void sqlite_fail(sqlite3* db, const std::string& what) {
    throw std::runtime_error(what + ": " + (db ? sqlite3_errmsg(db) : "no database"));
}

struct Statement {
    sqlite3_stmt* stmt = nullptr;
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) sqlite_fail(db, sql);
    }
    ~Statement() { sqlite3_finalize(stmt); }
};

std::string uuid4_hex() {  // uuid.uuid4().hex
    std::random_device device;
    unsigned char bytes[16];
    for (auto& b : bytes) b = static_cast<unsigned char>(device() & 0xFF);
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (unsigned char b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 0xF];
    }
    return out;
}

std::string deflate(const std::string& data, int level, int window_bits) {
    z_stream z{};
    if (deflateInit2(&z, level, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error("zlib deflateInit2 failed");
    std::string out(deflateBound(&z, data.size()), '\0');
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    z.avail_in = static_cast<uInt>(data.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = static_cast<uInt>(out.size());
    const int status = ::deflate(&z, Z_FINISH);
    deflateEnd(&z);
    if (status != Z_STREAM_END) throw std::runtime_error("zlib deflate failed");
    out.resize(z.total_out);
    return out;
}

std::string inflate(const void* data, int size) {
    z_stream z{};
    if (inflateInit(&z) != Z_OK) throw std::runtime_error("zlib inflateInit failed");
    std::string out;
    char buffer[1 << 16];
    z.next_in = reinterpret_cast<Bytef*>(const_cast<void*>(data));
    z.avail_in = static_cast<uInt>(size);
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        z.next_out = reinterpret_cast<Bytef*>(buffer);
        z.avail_out = sizeof(buffer);
        status = ::inflate(&z, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            inflateEnd(&z);
            throw std::runtime_error("zlib inflate failed");
        }
        out.append(buffer, sizeof(buffer) - z.avail_out);
    }
    inflateEnd(&z);
    return out;
}

void put_le32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
}

// gzip.compress(data, mtime=0): its header (level 9, unknown OS), raw deflate, CRC-32 and size.
std::string gzip(const std::string& data) {
    std::string out("\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff", 10);
    out += deflate(data, 9, -15);
    put_le32(out, static_cast<std::uint32_t>(
                      crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()))));
    put_le32(out, static_cast<std::uint32_t>(data.size()));
    return out;
}

std::string gunzip(const std::string& data) {
    z_stream z{};
    if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK) throw std::runtime_error("zlib inflateInit2 failed");
    std::string out;
    char buffer[1 << 16];
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    z.avail_in = static_cast<uInt>(data.size());
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        z.next_out = reinterpret_cast<Bytef*>(buffer);
        z.avail_out = sizeof(buffer);
        status = ::inflate(&z, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            inflateEnd(&z);
            throw std::runtime_error("gzip inflate failed");
        }
        out.append(buffer, sizeof(buffer) - z.avail_out);
    }
    inflateEnd(&z);
    return out;
}

bool gz(const std::string& path) { return std::filesystem::path(path).extension() == ".gz"; }

// A file operation's failure, raised in Python as the OSError its own file operations raise (python/module.cpp).
[[noreturn]] void file_fail(const char* what, const std::string& path) {
    throw std::filesystem::filesystem_error(what, path, std::error_code(errno, std::generic_category()));
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) file_fail("cannot read", path);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// _atomic_json(path, value): the bytes (gzipped for a .gz path) to `<path>.tmp`, then renamed over `path`.
void atomic_write(const std::string& path, const std::string& data) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) file_fail("cannot write", temporary);
        const std::string bytes = gz(path) ? gzip(data) : data;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) file_fail("cannot write", temporary);
    }
    std::filesystem::rename(temporary, path);
}

}  // namespace

CheckpointStore::CheckpointStore(std::string path) : path_(std::move(path)) {
    const std::filesystem::path manifest(path_);
    if (manifest.has_parent_path()) std::filesystem::create_directories(manifest.parent_path());
    database_ = path_ + ".store.sqlite3";
    if (sqlite3_open_v2(database_.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        sqlite_fail(db_, "open " + database_);
    execute("BEGIN");
    execute("CREATE TABLE IF NOT EXISTS metadata (version INTEGER, identity TEXT)");
    execute("CREATE TABLE IF NOT EXISTS objects (key TEXT PRIMARY KEY, data BLOB NOT NULL)");
    std::int64_t version = kVersion;
    {
        Statement select(db_, "SELECT version, identity FROM metadata");
        if (sqlite3_step(select.stmt) == SQLITE_ROW) {
            version = sqlite3_column_int64(select.stmt, 0);
            identity_ = reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 1));
        } else {
            identity_ = uuid4_hex();
            Statement insert(db_, "INSERT INTO metadata VALUES (?, ?)");
            sqlite3_bind_int64(insert.stmt, 1, kVersion);
            sqlite3_bind_text(insert.stmt, 2, identity_.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(insert.stmt) != SQLITE_DONE) sqlite_fail(db_, "insert metadata");
        }
    }
    execute("COMMIT");
    if (version != kVersion) {
        close();
        fail("Unsupported checkpoint store version");
    }
    Statement keys(db_, "SELECT key FROM objects");
    while (sqlite3_step(keys.stmt) == SQLITE_ROW)
        known_.emplace(reinterpret_cast<const char*>(sqlite3_column_text(keys.stmt, 0)));
}

CheckpointStore::~CheckpointStore() { close(); }

void CheckpointStore::close() {
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
}

void CheckpointStore::execute(const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : "unknown";
        sqlite3_free(error);
        throw std::runtime_error(std::string(sql) + ": " + message);
    }
}

void CheckpointStore::insert(const std::vector<std::pair<std::string, std::string>>& objects) {
    if (objects.empty()) return;
    execute("BEGIN");
    try {
        Statement insert(db_, "INSERT INTO objects VALUES (?, ?)");
        for (const auto& [key, data] : objects) {
            const std::string compressed = deflate(data, 1, MAX_WBITS);
            sqlite3_bind_text(insert.stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_blob(insert.stmt, 2, compressed.data(), static_cast<int>(compressed.size()), SQLITE_TRANSIENT);
            if (sqlite3_step(insert.stmt) != SQLITE_DONE) sqlite_fail(db_, "insert object");
            sqlite3_reset(insert.stmt);
        }
        execute("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
    for (const auto& [key, _] : objects) known_.insert(key);
}

CheckpointStore::Archived CheckpointStore::archive(const std::function<std::string(CheckpointEncoder&)>& encode) {
    try {
        CheckpointEncoder encoder([this](const std::string& key) { return known(key); }, cache_.get());
        Archived out;
        out.root = encoder.remember(encode(encoder));
        insert(encoder.pending);
        out.new_objects = static_cast<std::int64_t>(encoder.pending.size());
        for (const auto& [_, data] : encoder.pending) out.new_bytes += static_cast<std::int64_t>(data.size());
        return out;
    } catch (...) {
        cache_->clear();  // its new encodings may name objects that were never stored
        throw;
    }
}

void CheckpointStore::publish(const std::string& root) {
    const std::string manifest = dumps(
        object({{"format", text("pkc-checkpoint-store")}, {"version", integer_value(kVersion)},
                {"store", text(std::filesystem::path(database_).filename().string())}, {"storeId", text(identity_)},
                {"root", text(root)}}),
        false, true);
    atomic_write(path_, manifest);
}

std::string CheckpointStore::publish_patch(const std::string& base, const std::string& changes) {
    const std::string data = dumps(array({text("p"), array({text(base), text(changes)})}), false, true);
    const std::string key = sha256_hex(data);
    if (!known(key)) insert({{key, data}});
    publish(key);
    return key;
}

void CheckpointStore::compact() {
    cache_->clear();  // its encodings may name dropped objects
    const std::string raw = read_file(path_);
    const Value manifest = loads(gz(path_) ? gunzip(raw) : raw);
    std::unordered_set<std::string> reachable;
    Statement select(db_, "SELECT data FROM objects WHERE key=?");
    std::function<void(const std::string&)> visit;
    std::function<void(const Value&)> walk = [&](const Value& node) {
        if (!is_list(node)) return;
        const Array& pair = items(node);
        const std::string& tag = as_text(pair.at(0));
        const Value& content = pair.at(1);
        if (tag == "r") {
            visit(as_text(content));
        } else if (tag == "p") {
            for (const auto& key : items(content)) visit(as_text(key));
        } else if (tag == "d") {
            for (const auto& entry : items(content)) walk(items(entry).at(1));
        } else if (tag == "l") {
            for (const auto& value : items(content)) walk(value);
        }
    };
    visit = [&](const std::string& key) {
        if (!reachable.insert(key).second) return;
        sqlite3_bind_text(select.stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(select.stmt) != SQLITE_ROW) sqlite_fail(db_, "missing object " + key);
        const std::string data =
            inflate(sqlite3_column_blob(select.stmt, 0), sqlite3_column_bytes(select.stmt, 0));
        sqlite3_reset(select.stmt);
        walk(loads(data));
    };
    visit(as_text(at(fields(manifest), "root")));
    execute("BEGIN");
    try {
        Statement remove(db_, "DELETE FROM objects WHERE key=?");
        for (const auto& key : known_) {
            if (reachable.count(key)) continue;
            sqlite3_bind_text(remove.stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(remove.stmt) != SQLITE_DONE) sqlite_fail(db_, "delete object");
            sqlite3_reset(remove.stmt);
        }
        execute("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
    execute("VACUUM");
    known_ = std::move(reachable);
}

}  // namespace pkc
