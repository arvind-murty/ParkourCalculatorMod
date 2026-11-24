// Port of numerics/solver.py's solve. The worker's stdout and stderr are read on their own threads and the request
// is written on a third, as the Python does, so a full pipe never stalls the loop; the threads never touch Python.
#include "pkc/numerics/solver.hpp"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "../schema.hpp"
#include "../value_ops.hpp"
#include "pkc/canonical.hpp"
#include "pkc/numerics/model.hpp"
#include "pkc/validation/numerical.hpp"

extern char** environ;

namespace pkc {
namespace {

constexpr std::size_t kMaxLine = 2000000;  // readline(2000001): a longer response is an error

double seconds_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string os_error(int code) { return "[Errno " + std::to_string(code) + "] " + std::strerror(code); }

// The bounded message queue: at most 64, the oldest dropped when full (publish).
class Messages {
public:
    void publish(std::string kind, std::string value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= 64) queue_.pop_front();
        queue_.emplace_back(std::move(kind), std::move(value));
        ready_.notify_one();
    }
    bool get(double timeout, std::pair<std::string, std::string>& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!ready_.wait_for(lock, std::chrono::duration<double>(timeout), [&] { return !queue_.empty(); }))
            return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }
    bool empty() {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::pair<std::string, std::string>> queue_;
};

std::string rstrip(std::string text) {
    while (!text.empty() && std::strchr(" \t\n\r\v\f", text.back())) text.pop_back();
    return text;
}

// A spawned worker: its pid, the parent's pipe ends and its exit code once reaped.
struct Worker {
    pid_t pid = -1;
    int in = -1, out = -1, err = -1;
    bool reaped = false;
    int returncode = 0;

    bool poll() {  // Popen.poll() is None
        if (reaped) return false;
        int status = 0;
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == 0) return true;
        reaped = true;
        returncode = WIFSIGNALED(status) ? -WTERMSIG(status) : WEXITSTATUS(status);
        return false;
    }
    void wait() {
        if (reaped) return;
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        reaped = true;
        returncode = WIFSIGNALED(status) ? -WTERMSIG(status) : WEXITSTATUS(status);
    }
};

// subprocess.Popen(worker, stdin/stdout/stderr=PIPE) with OPENBLAS_NUM_THREADS and OMP_NUM_THREADS at 1; the child
// keeps only its three standard streams. Throws the Popen OSError's message.
Worker spawn(const std::string& path) {
    int in[2], out[2], err[2];
    if (pipe(in) != 0) throw std::runtime_error(os_error(errno));
    if (pipe(out) != 0) throw std::runtime_error(os_error(errno));
    if (pipe(err) != 0) throw std::runtime_error(os_error(errno));
    for (int fd : {in[1], out[0], err[0]}) fcntl(fd, F_SETFD, FD_CLOEXEC);
    std::vector<std::string> variables;
    for (char** e = environ; *e; ++e) {
        const std::string entry(*e);
        if (entry.rfind("OPENBLAS_NUM_THREADS=", 0) != 0 && entry.rfind("OMP_NUM_THREADS=", 0) != 0)
            variables.push_back(entry);
    }
    variables.push_back("OPENBLAS_NUM_THREADS=1");
    variables.push_back("OMP_NUM_THREADS=1");
    std::vector<char*> envp;
    for (auto& v : variables) envp.push_back(v.data());
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_adddup2(&actions, err[1], 2);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);  // close_fds=True
    std::string program = path;
    char* argv[] = {program.data(), nullptr};
    pid_t pid = -1;
    const int failed = posix_spawn(&pid, program.c_str(), &actions, &attributes, argv, envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    for (int fd : {in[0], out[1], err[1]}) close(fd);
    if (failed != 0) {
        for (int fd : {in[1], out[0], err[0]}) close(fd);
        throw std::runtime_error(os_error(failed) + ": '" + path + "'");
    }
    return Worker{pid, in[1], out[0], err[0]};
}

}  // namespace

Value solve_numerical(const Value& request, std::optional<double> seconds, std::optional<bool> stop_on_feasible,
                      const std::string& worker_path, const std::function<bool()>& cancelled_hook,
                      const std::function<void(const Value& candidate)>& on_incumbent) {
    Object overrides = fields(request);
    // An unlimited budget is null on the wire (JSON carries no infinity): the worker has no wall limit.
    if (seconds) put(overrides, "seconds", std::isfinite(*seconds) ? real(*seconds) : Value{nullptr});
    if (stop_on_feasible) put(overrides, "stopOnFeasible", boolean(*stop_on_feasible));
    const Value problem = prepare_problem(object(std::move(overrides)));
    const Object& p = fields(problem);
    const bool stop = py_truthy(at(p, "stopOnFeasible"));
    const bool maximize = as_text(at(fields(at(p, "objective")), "sense")) == "MAX";
    const double started = seconds_now();
    const double deadline = started + (is_null(at(p, "seconds")) ? INFINITY : num(at(p, "seconds")).value);
    auto is_cancelled = [&] { return cancelled_hook && cancelled_hook(); };
    Messages messages;
    std::mutex stderr_mutex;
    std::deque<std::string> stderr_lines;  // the last 20
    Array phases;
    Value best = kNull, first_feasible = kNull, last_rejection = kNull, worker_status = kNull, error = kNull;
    std::int64_t rejected = 0;
    std::string termination = "worker_finished";
    bool cancelled = false, launched = false;

    auto accept = [&](const Value& candidate) {
        if (!py_truthy(at(fields(candidate), "feasible"))) return;
        const double direction = maximize ? 1 : -1;
        if (is_null(best) || direction * num(at(fields(candidate), "objective")).value >
                                 direction * num(at(fields(best), "objective")).value) {
            if (is_null(first_feasible)) first_feasible = real(seconds_now() - started);
            best = candidate;
            if (on_incumbent) on_incumbent(best);
        }
    };

    if (!is_cancelled()) {
        Value seed = kNull;
        try {
            seed = validate(problem, at(p, "yaw"));
        } catch (const std::invalid_argument&) {
        }
        if (!is_null(seed)) accept(seed);
    }

    Worker worker;
    std::atomic<bool> output_alive{false}, errors_alive{false};
    std::thread output, errors, writer;
    auto finish = [&] {  // the finally: stop a running worker, then let the threads see its streams end
        if (!launched) return;
        if (worker.poll()) {
            kill(worker.pid, SIGTERM);
            const double until = seconds_now() + 0.2;
            while (worker.poll() && seconds_now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (worker.poll()) {
                kill(worker.pid, SIGKILL);
                worker.wait();
            }
        }
        for (std::thread* t : {&writer, &output, &errors})
            if (t->joinable()) t->join();
        for (int fd : {worker.out, worker.err}) close(fd);
    };
    try {
        if (is_cancelled()) {
            cancelled = true;
            termination = "cancelled";
        } else if (!is_null(best) && stop) {
            termination = "first_feasible";
        } else {
            worker = spawn(worker_path);
            launched = true;
            output_alive = errors_alive = true;
            output = std::thread([&] {
                std::string buffer;
                char chunk[65536];
                auto emit = [&](std::string line) {
                    if (line.size() > kMaxLine) {
                        messages.publish("io_error", "Oversized worker response");
                        return false;
                    }
                    messages.publish("message", std::move(line));
                    return true;
                };
                bool open = true;
                while (open) {
                    const ssize_t n = read(worker.out, chunk, sizeof chunk);
                    if (n < 0 && errno == EINTR) continue;
                    if (n < 0) {
                        messages.publish("io_error", os_error(errno));
                        break;
                    }
                    if (n == 0) {
                        if (!buffer.empty()) emit(std::move(buffer));
                        break;
                    }
                    buffer.append(chunk, static_cast<std::size_t>(n));
                    std::size_t start = 0, newline;
                    while ((newline = buffer.find('\n', start)) != std::string::npos) {
                        if (!emit(buffer.substr(start, newline + 1 - start))) {
                            open = false;
                            break;
                        }
                        start = newline + 1;
                    }
                    buffer.erase(0, start);
                    if (open && buffer.size() > kMaxLine) open = emit(std::move(buffer));
                }
                output_alive = false;
            });
            errors = std::thread([&] {
                std::string buffer;
                char chunk[4096];
                auto keep = [&](const std::string& line) {
                    std::lock_guard<std::mutex> lock(stderr_mutex);
                    stderr_lines.push_back(rstrip(line.size() > 400 ? line.substr(line.size() - 400) : line));
                    if (stderr_lines.size() > 20) stderr_lines.pop_front();
                };
                for (;;) {
                    const ssize_t n = read(worker.err, chunk, sizeof chunk);
                    if (n < 0 && errno == EINTR) continue;
                    if (n <= 0) break;
                    buffer.append(chunk, static_cast<std::size_t>(n));
                    std::size_t start = 0, newline;
                    while ((newline = buffer.find('\n', start)) != std::string::npos) {
                        keep(buffer.substr(start, newline + 1 - start));
                        start = newline + 1;
                    }
                    buffer.erase(0, start);
                }
                if (!buffer.empty()) keep(buffer);
                errors_alive = false;
            });
            const std::string payload = dumps(problem, false, true) + "\n";
            writer = std::thread([&, payload] {
                std::size_t written = 0;
                while (written < payload.size()) {
                    const ssize_t n = write(worker.in, payload.data() + written, payload.size() - written);
                    if (n < 0 && errno == EINTR) continue;
                    if (n < 0) {
                        messages.publish("io_error", "Could not write worker request: " + os_error(errno));
                        break;
                    }
                    written += static_cast<std::size_t>(n);
                }
                close(worker.in);
            });
            while (worker.poll() || output_alive || !messages.empty()) {
                if (is_cancelled()) {
                    cancelled = true;
                    termination = "cancelled";
                    break;
                }
                const double remaining = deadline - seconds_now();
                if (remaining <= 0) {
                    termination = "time_limit";
                    break;
                }
                std::pair<std::string, std::string> received;
                if (!messages.get(std::min(0.025, remaining), received)) continue;
                if (received.first == "io_error") {
                    error = text(received.second);
                    termination = "worker_error";
                    break;
                }
                std::string invalid;
                try {
                    const Value message = loads(received.second);
                    if (!is_object(message)) throw std::invalid_argument("expected a JSON object");
                    const Object& m = fields(message);
                    const Value* type = get(m, "type");
                    if (!type) throw std::invalid_argument("'type'");
                    const std::string& kind = as_text(*type);
                    if (kind == "candidate") {
                        const Value* yaw = get(m, "yaw");
                        if (!yaw) throw std::invalid_argument("'yaw'");
                        Value candidate;
                        try {
                            candidate = validate(problem, *yaw);
                        } catch (const std::invalid_argument& failure) {
                            ++rejected;
                            last_rejection = text(failure.what());
                            continue;
                        }
                        if (!py_truthy(at(fields(candidate), "feasible"))) {
                            ++rejected;
                            Object kept;
                            for (const auto& [key, value] : fields(candidate))
                                if (key != "yaw" && key != "positions" && key != "velocity" && key != "y" &&
                                    key != "vy" && key != "onGround" && key != "horizontalCollision" &&
                                    key != "ladderPattern")
                                    kept.emplace_back(key, value);
                            last_rejection = object(std::move(kept));
                            continue;
                        }
                        accept(candidate);
                        if (stop) {
                            termination = "first_feasible";
                            break;
                        }
                    } else if (kind == "phase") {
                        phases.push_back(message);
                    } else if (kind == "done") {
                        const Value* status = get(m, "status");
                        if (!status) throw std::invalid_argument("'status'");
                        worker_status = text(is_null(*status) ? "None" : std::holds_alternative<std::string>(status->data)
                                                                              ? as_text(*status)
                                                                              : dumps(*status, false, false));
                    } else if (kind == "error") {
                        const Value* text_value = get(m, "message");
                        if (!text_value) throw std::invalid_argument("'message'");
                        error = text(std::holds_alternative<std::string>(text_value->data)
                                         ? as_text(*text_value)
                                         : dumps(*text_value, false, false));
                        termination = "worker_error";
                        break;
                    } else {
                        throw std::invalid_argument("Unknown worker response type");
                    }
                } catch (const std::invalid_argument& failure) {
                    invalid = failure.what();
                }
                if (!invalid.empty()) {
                    error = text("Invalid worker response: " + invalid);
                    termination = "protocol_error";
                    break;
                }
            }
            const double until = seconds_now() + 0.1;  // errors.join(timeout=0.1)
            while (errors_alive && seconds_now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (!worker.poll() && worker.returncode != 0 && termination == "worker_finished") {
                error = text("Worker exited with code " + std::to_string(worker.returncode));
                termination = "worker_error";
            }
        }
    } catch (const std::runtime_error& failure) {  // Popen's OSError
        error = text(failure.what());
        termination = "runtime_error";
    } catch (...) {
        finish();
        throw;
    }
    finish();
    if (cancelled) best = kNull;
    const std::string status = cancelled ? "cancelled" : !is_null(best) ? "feasible" : !is_null(error) ? "error" : "unknown";
    Array kept_stderr;
    for (const auto& line : stderr_lines) kept_stderr.push_back(text(line));
    return object({{"version", integer_value(1)}, {"status", text(status)}, {"feasible", boolean(!is_null(best))},
                   {"candidate", best}, {"objectiveMode", at(p, "objectiveMode")},
                   {"problemHash", text(fingerprint(problem))}, {"elapsedSeconds", real(seconds_now() - started)},
                   {"firstFeasibleSeconds", first_feasible},
                   {"problem", object({{"ticks", at(p, "ticks")},
                                       {"constraints", integer_value(static_cast<std::int64_t>(items(at(p, "walls")).size()))},
                                       {"contacts", integer_value(static_cast<std::int64_t>(items(at(p, "contacts")).size()))},
                                       {"threshold", at(p, "threshold")}, {"objective", at(p, "objective")},
                                       {"source", get_or_null(p, "source")}})},
                   {"termination", text(termination)}, {"workerStatus", worker_status},
                   {"workerLaunched", boolean(launched)}, {"error", error}, {"tolerance", real(kReplayTolerance)},
                   {"phases", array(std::move(phases))},
                   {"diagnostics", object({{"rejectedCandidates", integer_value(rejected)},
                                           {"lastRejection", last_rejection},
                                           {"stderr", array(std::move(kept_stderr))}})}});
}

}  // namespace pkc
