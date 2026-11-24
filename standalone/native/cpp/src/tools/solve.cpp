// pkc_solve: cli/solve.py over the native solve entry (search/application.hpp), with no Python. The report is a
// checkpoint store at --output, read by io/checkpoint_store.read_report as the Python CLI's is; --resume reads either
// CLI's report (io/store.hpp read_report).
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "../value_ops.hpp"
#include "pkc/canonical.hpp"
#include "pkc/search/application.hpp"

namespace {

std::atomic<bool> cancel{false};

extern "C" void on_signal(int) { cancel = true; }

std::string executable_path(const char* argv0) {
#ifdef __APPLE__
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) == 0) return std::filesystem::canonical(path.c_str()).string();
#else
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) return path.string();
#endif
    return std::filesystem::canonical(argv0).string();
}

int usage(const std::string& message) {
    std::cerr << "usage: pkc_solve --input CASE.json --output REPORT.json.gz [--resume REPORT] [--save-progress] "
                 "[--seconds S] [--max-solver-calls N]\n"
              << message << "\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::optional<std::string> input, output, resume;
    pkc::SolveCaseOptions options;
    const std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto next = [&]() -> std::optional<std::string> {
            if (i + 1 >= args.size()) return std::nullopt;
            return args[++i];
        };
        if (arg == "--input") {
            input = next();
        } else if (arg == "--output") {
            output = next();
        } else if (arg == "--save-progress") {
            options.save_progress = true;
        } else if (arg == "--seconds") {
            const auto v = next();
            if (!v) return usage("--seconds needs a value");
            options.seconds = std::stod(*v);
        } else if (arg == "--max-solver-calls") {
            const auto v = next();
            if (!v) return usage("--max-solver-calls needs a value");
            options.max_solver_calls = std::stoll(*v);
        } else if (arg == "--resume") {
            resume = next();
            if (!resume) return usage("--resume needs a value");
        } else if (arg == "--compiler") {
            return usage("--compiler: the Java bridge runs in the Python CLI only");
        } else {
            return usage("unknown argument " + arg);
        }
    }
    if (!input || !output) return usage("--input and --output are required");
    std::ifstream in(*input, std::ios::binary);
    if (!in) return usage("cannot read " + *input);
    std::stringstream text;
    text << in.rdbuf();
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    try {
        const pkc::Value fixture = pkc::loads(text.str());
        if (resume) {
            const pkc::Value prior = pkc::read_report(*resume);
            const pkc::Value* result = pkc::is_object(prior) ? pkc::get(pkc::fields(prior), "result") : nullptr;
            const pkc::Value* state =
                result && pkc::is_object(*result) ? pkc::get(pkc::fields(*result), "searchState") : nullptr;
            if (!state || !pkc::py_truthy(*state))
                state = pkc::is_object(prior) ? pkc::get(pkc::fields(prior), "searchState") : nullptr;
            if (!state || !pkc::py_truthy(*state)) return usage("Resume report has no saved automatic search state");
            options.resume_state = *state;
        }
        options.worker = (std::filesystem::path(executable_path(argv[0])).parent_path() / "pkc_numerics_worker").string();
        options.cancelled = [] { return cancel.load(); };
        options.store = std::make_shared<pkc::CheckpointStore>(*output);
        options.metadata = pkc::object({{"fixture", fixture}, {"bridge", pkc::object({{"bridge", pkc::text("native")}})}});
        const pkc::Value solved = pkc::solve_case(fixture, options);
        options.store->close();
        const pkc::Object& r = pkc::fields(pkc::at(pkc::fields(solved), "result"));
        const pkc::Value& best = pkc::at(r, "best");
        const pkc::Value& generation = pkc::get_or_null(r, "routeGeneration");
        const pkc::Value* routes = pkc::is_null(generation) ? nullptr : pkc::get(pkc::fields(generation), "routes");
        const pkc::Value summary = pkc::object(
            {{"status", pkc::at(r, "status")},
             {"termination", pkc::at(r, "termination")},
             {"work", pkc::at(r, "work")},
             {"routes", routes ? *routes : pkc::integer_value(0)},
             {"ticks", pkc::is_null(best) ? pkc::kNull : pkc::at(pkc::fields(pkc::at(pkc::fields(best), "plan")), "ticks")},
             {"objective",
              pkc::is_null(best) ? pkc::kNull : pkc::at(pkc::fields(pkc::at(pkc::fields(best), "candidate")), "objective")},
             {"validation", pkc::at(pkc::fields(solved), "validation")}});
        std::cout << pkc::dumps(summary, false, false) << std::endl;
    } catch (const std::exception& error) {
        std::cerr << "pkc_solve: " << error.what() << "\n";
        return 1;
    }
    return cancel ? 130 : 0;
}
