// Python bindings for the native core. Conversion happens once per call at
// this boundary; native units keep their state in C++ between calls.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include "pkc/canonical.hpp"
#include "pkc/coefficients.hpp"
#include "pkc/constraints/variants.hpp"
#include "pkc/envelopes.hpp"
#include "pkc/gate.hpp"
#include "pkc/movement/units.hpp"
#include "pkc/movement/vertical_profiles.hpp"
#include "pkc/numerics/builders.hpp"
#include "pkc/numerics/constraints.hpp"
#include "pkc/numerics/disk.hpp"
#include "pkc/numerics/solver.hpp"
#include "pkc/numerics/model.hpp"
#include "pkc/program.hpp"
#include "pkc/movement/generation.hpp"
#include "pkc/reachability/service.hpp"
#include "pkc/search/anchors/adapter.hpp"
#include "pkc/search/anchors/policy.hpp"
#include "pkc/search/bounds/remaining.hpp"
#include "pkc/search/comparison.hpp"
#include "pkc/search/dfs.hpp"
#include "pkc/io/checkpoint_store.hpp"
#include "pkc/search/dfs_search.hpp"
#include "pkc/search/dfs_state.hpp"
#include "pkc/search/engine.hpp"
#include "pkc/search/feasibility.hpp"
#include "pkc/search/goals.hpp"
#include "pkc/search/node.hpp"
#include "pkc/search/ordered_route.hpp"
#include "pkc/search/repair.hpp"
#include "pkc/reachability/viability.hpp"
#include "pkc/scene/geometry.hpp"
#include "pkc/scene/ladders.hpp"
#include "pkc/scene/normalize.hpp"
#include "pkc/scene/route.hpp"
#include "pkc/scene/static_collision.hpp"
#include "pkc/screen.hpp"
#include "pkc/spatial/cells.hpp"
#include "pkc/spatial/regions.hpp"
#include "pkc/spatial/surfaces.hpp"
#include "pkc/validation/numerical.hpp"
#include "pkc/validation/vertical.hpp"

namespace py = pybind11;

namespace {

// Raw CPython calls: this boundary converts every program a native unit sees,
// and pybind11's generic isinstance/cast per node cost more than the unit.
std::string utf8(PyObject* text) {
    Py_ssize_t size = 0;
    const char* data = PyUnicode_AsUTF8AndSize(text, &size);
    if (!data) throw py::error_already_set();
    return std::string(data, size);
}

pkc::Value convert(PyObject* object) {
    if (object == Py_None) return {nullptr};
    if (PyBool_Check(object)) return {object == Py_True};
    if (PyLong_Check(object)) {
        int overflow = 0;
        long long value = PyLong_AsLongLongAndOverflow(object, &overflow);
        if (overflow) throw py::type_error("integer outside the 64-bit range");
        return {static_cast<std::int64_t>(value)};
    }
    if (PyFloat_Check(object)) return {PyFloat_AS_DOUBLE(object)};
    if (PyUnicode_Check(object)) return {utf8(object)};
    if (PyList_Check(object) || PyTuple_Check(object)) {
        bool list = PyList_Check(object);
        Py_ssize_t size = list ? PyList_GET_SIZE(object) : PyTuple_GET_SIZE(object);
        auto array = std::make_shared<pkc::Array>();
        array->reserve(size);
        for (Py_ssize_t i = 0; i < size; ++i)
            array->push_back(convert(list ? PyList_GET_ITEM(object, i) : PyTuple_GET_ITEM(object, i)));
        return {std::shared_ptr<const pkc::Array>(std::move(array))};
    }
    if (PyDict_Check(object)) {
        auto map = std::make_shared<pkc::Object>();
        map->reserve(PyDict_GET_SIZE(object));
        PyObject *key, *item;
        Py_ssize_t position = 0;
        while (PyDict_Next(object, &position, &key, &item)) {
            // json.dumps would stringify non-str keys after sorting them by their
            // own type; canonical identity only uses str keys, so reject the rest.
            if (!PyUnicode_Check(key)) throw py::type_error("canonical keys must be str");
            map->emplace_back(utf8(key), convert(item));
        }
        return {std::shared_ptr<const pkc::Object>(std::move(map))};
    }
    throw py::type_error(std::string("Object of type ") + Py_TYPE(object)->tp_name + " is not canonical JSON");
}

pkc::Value convert(py::handle object) { return convert(object.ptr()); }

// Output keys come from the schema (a gate result repeats about 115 keys some
// 2,600 times), so each is created and interned once and its hash is cached.
PyObject* key_reference(const std::string& key) {
    static std::unordered_map<std::string, PyObject*> keys;  // holds one reference each; GIL-guarded
    auto found = keys.find(key);
    if (found == keys.end()) {
        PyObject* name = PyUnicode_FromStringAndSize(key.data(), key.size());
        if (!name) return nullptr;
        PyUnicode_InternInPlace(&name);
        found = keys.emplace(key, name).first;
    }
    Py_INCREF(found->second);
    return found->second;
}

PyObject* new_reference(const pkc::Value& value) {
    return std::visit([](const auto& held) -> PyObject* {
        using T = std::decay_t<decltype(held)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            Py_RETURN_NONE;
        } else if constexpr (std::is_same_v<T, bool>) {
            return PyBool_FromLong(held);
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            return PyLong_FromLongLong(held);
        } else if constexpr (std::is_same_v<T, double>) {
            return PyFloat_FromDouble(held);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return PyUnicode_FromStringAndSize(held.data(), held.size());
        } else if constexpr (std::is_same_v<T, std::shared_ptr<const pkc::Array>>) {
            PyObject* list = PyList_New(held->size());
            if (!list) return nullptr;
            for (std::size_t i = 0; i < held->size(); ++i) {
                PyObject* item = new_reference((*held)[i]);
                if (!item) {
                    Py_DECREF(list);
                    return nullptr;
                }
                PyList_SET_ITEM(list, i, item);
            }
            return list;
        } else {
            PyObject* dict = PyDict_New();
            if (!dict) return nullptr;
            for (const auto& [key, item] : *held) {
                PyObject* name = key_reference(key);
                PyObject* converted = name ? new_reference(item) : nullptr;
                int failed = !converted || PyDict_SetItem(dict, name, converted) < 0;
                Py_XDECREF(name);
                Py_XDECREF(converted);
                if (failed) {
                    Py_DECREF(dict);
                    return nullptr;
                }
            }
            return dict;
        }
    }, value.data);
}

// Gate results kept native (InteractionGate under PKC_NATIVE_CORE). Each builds
// its Python dict once, on the first request, so repeated reads return the same
// object as the Python gate's cache does.
struct NativeCoefficients {
    pkc::Value value;
    py::object dict;
};

// A typed program: a context's parent, or a child built natively that only the
// gate's screens read.
struct NativeProgram {
    std::shared_ptr<const pkc::Program> program;
    std::string physics_key;  // computed on first request
};

struct NativePrepared {
    std::shared_ptr<const pkc::Program> program;
    pkc::Value result;  // {route, profiles, plan, vertical}
    std::string key;
    py::object dict;
};

struct NativeAnalysis {
    std::shared_ptr<NativePrepared> prepared;
    std::shared_ptr<NativeCoefficients> coefficients;
    pkc::GateAnalysis analysis;
    py::object dict;
};

py::object to_python(const pkc::Value& value);

std::shared_ptr<const pkc::Program> program_arg(py::handle program) {  // a NativeProgram handle or a program dict
    if (py::isinstance<NativeProgram>(program)) return program.cast<std::shared_ptr<NativeProgram>>()->program;
    return std::make_shared<const pkc::Program>(pkc::program_from_value(convert(program)));
}

py::object memoized(py::object& slot, const std::function<py::object()>& build) {
    if (!slot) slot = build();
    return slot;
}

py::object to_python(const pkc::Value& value) {
    PyObject* result = new_reference(value);
    if (!result) throw py::error_already_set();
    return py::reinterpret_steal<py::object>(result);
}

// A generator context dict as the native gate and generators read it; its
// feetAllowed and checkBudget callables are called back.
pkc::GenerationContext generation_context(py::dict context) {
    pkc::GenerationContext out;
    py::object program = context["program"];
    out.program = std::make_shared<const pkc::Program>(pkc::program_from_value(convert(program)));
    out.state = pkc::boundary_state(*out.program);
    if (context.contains("maxInputs")) out.max_inputs = context["maxInputs"].cast<std::int64_t>();
    if (context.contains("threshold")) {
        py::handle t = context["threshold"];
        out.threshold = {t.cast<double>(), py::isinstance<py::int_>(t)};
    }
    if (context.contains("runTickChoices")) out.run_tick_choices = context["runTickChoices"].cast<bool>();
    if (context.contains("launchFace") && !context["launchFace"].is_none())
        out.launch_face = context["launchFace"].cast<std::string>();
    if (context.contains("cases") && !context["cases"].is_none()) {
        out.cases.emplace();
        for (py::handle c : context["cases"]) out.cases->insert(c.cast<std::string>());
    }
    if (context.contains("forward") && !context["forward"].is_none()) {
        py::dict f = context["forward"];
        auto point = f["point"].cast<std::vector<double>>();
        std::set<std::pair<std::string, std::int64_t>> allowed;
        for (py::handle move : f["allowed"]) allowed.insert(move.cast<std::pair<std::string, std::int64_t>>());
        py::handle margin = f["margin"];
        out.forward = pkc::Forward{{point.at(0), point.at(1)}, std::move(allowed),
                                   {margin.cast<double>(), py::isinstance<py::int_>(margin)}};
    }
    if (context.contains("frameId")) out.frame_id = convert(context["frameId"]);
    if (context.contains("decision") && !context["decision"].is_none()) {
        py::object point = context["decision"].attr("get")("decisionPoint");
        if (!point.is_none()) out.decision_point = convert(point);
    }
    if (context.contains("regionItems")) out.region_items = context["regionItems"].cast<bool>();
    if (context.contains("feetAllowed") && !context["feetAllowed"].is_none()) {
        py::object allowed = context["feetAllowed"];
        out.feet_allowed_fn = [allowed](const std::string& name) { return allowed(name).cast<bool>(); };
    }
    if (context.contains("checkBudget") && !context["checkBudget"].is_none()) {
        py::object budget = context["checkBudget"];
        out.check_budget = [budget] { budget(); };
    }
    return out;
}

py::object optional_text(const std::optional<std::string>& value) {
    return value ? py::object(py::str(*value)) : py::object(py::none());
}

py::dict visit_state_dict(const pkc::VisitState& state) {  // visit_state's dict
    py::dict out;
    out["version"] = 1;
    out["current"] = optional_text(state.current);
    out["departed"] = state.departed;
    out["valid"] = state.valid;
    return out;
}


// Runs a Python hook for native code, mapping the exceptions the Python callers catch onto the native ones: a
// TimeoutError throws pkc::Timeout, ValueError std::invalid_argument, RuntimeError and OSError std::runtime_error.
template <class Call>
auto python_hook(Call&& call) -> decltype(call()) {
    try {
        return call();
    } catch (py::error_already_set& error) {
        const std::string message = py::str(error.value());
        if (error.matches(PyExc_TimeoutError)) throw pkc::Timeout(message);
        if (error.matches(PyExc_ValueError)) throw std::invalid_argument(message);
        if (error.matches(PyExc_RuntimeError) || error.matches(PyExc_OSError)) throw std::runtime_error(message);
        throw;
    }
}

// A Python numerical callable (numerics.solver.solve's signature) as a native one.
// numerics.solver._command's native worker, built beside this module.
const std::string& worker_path() {
    static const std::string path = [] {
        const std::string file = py::module_::import("_pkc_native").attr("__file__").cast<std::string>();
        return file.substr(0, file.rfind('/') + 1) + "pkc_numerics_worker";
    }();
    return path;
}

std::function<bool()> cancelled_hook(py::object cancel_event) {
    if (cancel_event.is_none()) return {};
    return [cancel_event] { return python_hook([&] { return cancel_event.attr("is_set")().cast<bool>(); }); };
}

// numerics.solver.solve natively: python_executable only chose the Python worker.
pkc::NumericalSolve native_solve(py::object cancel_event) {
    return [cancelled = cancelled_hook(cancel_event)](const pkc::Value& problem, double seconds, bool stop_on_feasible,
                                                       const std::function<void(const pkc::Value&)>& on_incumbent) {
        return pkc::solve_numerical(problem, seconds, stop_on_feasible, worker_path(), cancelled, on_incumbent);
    };
}

pkc::NumericalSolve numerical_hook(py::object solve, py::object python_executable, py::object cancel_event) {
    if (solve.is_none()) return {};
    if (solve.is(py::module_::import("parkour_solver.numerics.solver").attr("solve"))) return native_solve(cancel_event);
    return [solve, python_executable, cancel_event](const pkc::Value& problem, double seconds, bool stop_on_feasible,
                                                     const std::function<void(const pkc::Value&)>& on_incumbent) {
        return python_hook([&] {
            py::object incumbent = py::none();
            if (on_incumbent)
                incumbent = py::cpp_function([&on_incumbent](py::handle candidate) { on_incumbent(convert(candidate)); });
            return convert(solve(to_python(problem), py::arg("seconds") = seconds,
                                 py::arg("stop_on_feasible") = stop_on_feasible,
                                 py::arg("python_executable") = python_executable, py::arg("cancel_event") = cancel_event,
                                 py::arg("on_incumbent") = incumbent));
        });
    };
}

// The node solve's Python services: the compiler, the default numerical solve, the work budget and the cancel event.
pkc::SolveHooks solve_hooks(py::object compiler, py::object solve, py::object work, py::object cancel_event,
                            py::object python_executable) {
    pkc::SolveHooks hooks;
    hooks.compile = [compiler](const pkc::Value& route, const pkc::Value& plan, double seconds) {
        return python_hook([&] { return convert(compiler.attr("compile")(to_python(route), to_python(plan), seconds)); });
    };
    if (py::hasattr(compiler, "derive_footprints")) {
        hooks.derive_footprints = [compiler](const pkc::Value& route, double seconds) {
            return python_hook([&] { return convert(compiler.attr("derive_footprints")(to_python(route), seconds)); });
        };
        hooks.derive_standing_footprints = [compiler](const pkc::Value& route, double seconds) {
            return python_hook([&] { return convert(compiler.attr("derive_standing_footprints")(to_python(route), seconds)); });
        };
    }
    hooks.check_scene = [compiler](const pkc::Value& geometry, const pkc::Value& positions, const pkc::Value& movement,
                                   const pkc::Value& ground, double seconds, const pkc::Value& contacts,
                                   const pkc::Object& contract) {
        return python_hook([&] {
            py::dict kwargs;
            if (!std::holds_alternative<std::nullptr_t>(contacts.data)) kwargs["contacts"] = to_python(contacts);
            for (const auto& [key, value] : contract) kwargs[py::str(key)] = to_python(value);
            return convert(compiler.attr("check_scene")(to_python(geometry), to_python(positions), to_python(movement),
                                                        to_python(ground), seconds, **kwargs));
        });
    };
    hooks.solve = numerical_hook(solve, python_executable, cancel_event);
    if (!work.is_none()) {
        hooks.work_check = [work] { python_hook([&] { work.attr("check")(); }); };
        hooks.work_proposal = [work](const pkc::Value& problem) { python_hook([&] { work.attr("proposal")(to_python(problem)); }); };
        hooks.work_before_solve = [work](const pkc::Value& problem) {
            python_hook([&] { work.attr("before_solve")(to_python(problem)); });
        };
        hooks.work_accepted = [work](const pkc::Value& best) { python_hook([&] { work.attr("accepted")(to_python(best)); }); };
    }
    hooks.cancelled = cancelled_hook(cancel_event);
    return hooks;
}

// RelaxedDispatch's services for a gate's native core: the native IPOPT solve, the core's confirmations and
// numerics/disk's settings in force.
pkc::RelaxedServices relaxed_services(std::shared_ptr<pkc::InteractionGate> core, py::object cancel_event) {
    py::module_ disk = py::module_::import("parkour_solver.numerics.disk");
    pkc::RelaxedServices services;
    services.full = native_solve(cancel_event);
    services.cancelled = cancelled_hook(cancel_event);
    services.confirmation = [core](const std::string& fingerprint) { return core->confirmation(fingerprint); };
    services.remember_confirmation = [core](const std::string& fingerprint, const pkc::Value& result) {
        core->remember_confirmation(fingerprint, result);
    };
    services.limits = pkc::DiskLimits{disk.attr("CAP").cast<double>(), disk.attr("MAX_ITER").cast<unsigned>(),
                                      disk.attr("RETRY_CAP").cast<double>(), disk.attr("RETRY_MAX_ITER").cast<unsigned>()};
    services.alarm_fraction = disk.attr("ALARM_FRACTION").cast<double>();
    services.material_gap = disk.attr("MATERIAL_GAP").cast<double>();
    return services;
}

// The node solve's Python services (engine.evaluate_program's): `services` maps work, numerical, confirm,
// analyze_model, reachable_faces, candidate, check_generation, record_counts, accepted and work_summary to callables
// (numerical and confirm may be None), and gate to the gate's native core or None; a core replaces analyze_model,
// reachable_faces and the relaxed dispatch. on_progress is left to the caller.
pkc::EvaluateHooks evaluate_hooks(py::object compiler, py::object solve, py::object cancel_event,
                                  py::object python_executable, const py::object& services) {
    auto service = [&](const char* name) { return py::object(services[py::str(name)]); };
    py::object analyze_model = service("analyze_model"), reachable_faces = service("reachable_faces"),
               candidate = service("candidate"), check_generation = service("check_generation"),
               record_counts = service("record_counts"), accepted = service("accepted"),
               work_summary = service("work_summary");
    pkc::EvaluateHooks hooks;
    hooks.solve = solve_hooks(compiler, solve, service("work"), cancel_event, python_executable);
    hooks.numerical = numerical_hook(service("numerical"), python_executable, cancel_event);
    hooks.confirm = numerical_hook(service("confirm"), python_executable, cancel_event);
    if (py::object gate = service("gate"); !gate.is_none()) {  // the gate's native core serves its own caches
        auto core = gate.cast<std::shared_ptr<pkc::InteractionGate>>();
        if (hooks.numerical) {  // the relaxed dispatch, with its confirmations in the core
            const pkc::RelaxedServices relaxed = relaxed_services(core, cancel_event);
            hooks.numerical = [relaxed](const pkc::Value& problem, double seconds, bool stop_on_feasible,
                                        const std::function<void(const pkc::Value&)>& on_incumbent) {
                return pkc::relaxed_dispatch(problem, seconds, stop_on_feasible, false, on_incumbent, relaxed);
            };
            hooks.confirm = [relaxed](const pkc::Value& problem, double seconds, bool stop_on_feasible,
                                      const std::function<void(const pkc::Value&)>& on_incumbent) {
                return pkc::relaxed_dispatch(problem, seconds, stop_on_feasible, true, on_incumbent, relaxed);
            };
        }
        hooks.analyze_model = [core](const pkc::Value& route, const pkc::Value& plan, const pkc::Value& vertical,
                                     const pkc::Value& scope) { return core->analyze_model(route, plan, vertical, scope); };
        hooks.reachable_faces = [core](const std::string& face) {
            const pkc::FaceTable& table = core->reachable_faces(face);
            pkc::Array solids;
            for (std::int64_t solid : table.solids) solids.push_back(pkc::Value{solid});
            return pkc::Value{std::make_shared<const pkc::Object>(pkc::Object{
                {"ticks", table.ticks ? pkc::Value{*table.ticks} : pkc::Value{nullptr}},
                {"solids", pkc::Value{std::make_shared<const pkc::Array>(std::move(solids))}}})};
        };
    } else {
        hooks.analyze_model = [analyze_model](const pkc::Value& route, const pkc::Value& plan,
                                              const pkc::Value& vertical, const pkc::Value& scope) {
            return python_hook([&] {
                return convert(analyze_model(to_python(route), to_python(plan), to_python(vertical), to_python(scope)));
            });
        };
        hooks.reachable_faces = [reachable_faces](const std::string& face) {
            return python_hook([&] { return convert(reachable_faces(face)); });
        };
    }
    if (!candidate.is_none())  // None for the native search, whose adapter checks candidates
        hooks.candidate = [candidate](const pkc::Value& plan, const pkc::Value& value, const pkc::Value& vertical,
                                      const pkc::Value& scope) {
            return python_hook([&] {
                return convert(candidate(to_python(plan), to_python(value), to_python(vertical), to_python(scope)));
            });
        };
    hooks.check_generation = [check_generation] { python_hook([&] { check_generation(); }); };
    hooks.record_counts = [record_counts](const pkc::Value& counts) {
        python_hook([&] { record_counts(to_python(counts)); });
    };
    hooks.accepted = [accepted](const pkc::Value& best) { python_hook([&] { accepted(to_python(best)); }); };
    hooks.work_summary = [work_summary] { return python_hook([&] { return convert(work_summary()); }); };
    return hooks;
}
const pkc::Object& object_fields(const pkc::Value& value) {
    return *std::get<std::shared_ptr<const pkc::Object>>(value.data);
}

const pkc::Value* field(const pkc::Object& object, const std::string& key) {
    for (const auto& [k, v] : object)
        if (k == key) return &v;
    return nullptr;
}

// A published DFS state for Python: fields convert on first read.
struct NativeSnapshot {
    pkc::Snapshot snapshot;
    std::map<std::string, py::object> converted;

    const pkc::Object& fields() const { return object_fields(snapshot->state); }
    pkc::Value at(py::handle path) const {
        pkc::Value node = snapshot->state;
        for (py::handle part : path) {
            if (py::isinstance<py::int_>(part)) {
                node = std::get<std::shared_ptr<const pkc::Array>>(node.data)->at(part.cast<std::size_t>());
            } else {
                const pkc::Value* child = field(object_fields(node), part.cast<std::string>());
                if (!child) throw py::key_error(part.cast<std::string>());
                node = *child;
            }
        }
        return node;
    }
    py::object item(const std::string& key) {
        if (auto found = converted.find(key); found != converted.end()) return found->second;
        const pkc::Value* value = field(fields(), key);
        if (!value) throw py::key_error(key);
        return converted.emplace(key, to_python(*value)).first->second;
    }
};

py::list paths_to_python(const std::vector<pkc::Array>& paths) {
    py::list out;
    for (const pkc::Array& path : paths) out.append(to_python(pkc::Value{std::make_shared<const pkc::Array>(path)}));
    return out;
}

// checkpoint_store's encode for a native value: (its encoding, the new objects [(key, data)]), skipping objects in
// the store's `known` set.
py::tuple checkpoint_encode(const std::function<std::string(pkc::CheckpointEncoder&)>& encode, py::handle known) {
    pkc::CheckpointEncoder encoder([&](const std::string& key) {
        const int found = PySet_Contains(known.ptr(), py::str(key).ptr());
        if (found < 0) throw py::error_already_set();
        return found == 1;
    });
    const std::string data = encode(encoder);
    py::list objects;
    for (const auto& [key, object] : encoder.pending) objects.append(py::make_tuple(key, py::bytes(object)));
    return py::make_tuple(py::bytes(data), objects);
}

}  // namespace

PYBIND11_MODULE(_pkc_native, m) {
    m.doc() = "Native core for parkour_solver (canonical identity, typed programs, gate preparation).";
    py::register_exception<std::invalid_argument>(m, "NativeValueError", PyExc_ValueError);
    py::register_exception<pkc::Unsupported>(m, "NativeUnsupported", PyExc_NotImplementedError);
    py::register_exception<pkc::Timeout>(m, "NativeTimeout", PyExc_TimeoutError);
    m.def("canonical_json", [](py::handle value) { return py::bytes(pkc::canonical_json(convert(value))); },
          "json.dumps(value, sort_keys=True, allow_nan=False).encode()");
    m.def("digest", [](py::handle value) { return pkc::digest(convert(value)); },
          "parkour_solver.canonical.digest");
    // Python -> typed struct -> Python, for parity checks of the program model.
    m.def("round_trip_program", [](py::handle value) {
        return to_python(pkc::to_value(pkc::program_from_value(convert(value))));
    });
    m.def("round_trip_choice", [](py::handle value) {
        return to_python(pkc::to_value(pkc::choice_from_value(convert(value))));
    });
    m.def("gate_prepare", [](py::handle scene, py::handle program, bool require_complete) -> py::object {
        auto prepared = pkc::gate_prepare(convert(scene), pkc::program_from_value(convert(program)), require_complete);
        return prepared ? to_python(*prepared) : py::none();
    }, "InteractionGate.prepare's build: {route, profiles, plan, vertical} or None");
    m.def("compile_coefficients", [](py::handle request) {
        return to_python(pkc::compile_coefficients(convert(request)));
    }, "physics/coefficients.compile_request for one coefficient_request message");
    m.def("interaction_reachability", [](py::handle envelopes, py::handle interaction, bool refine, py::handle objective) {
        const pkc::Value goal = convert(objective);
        return to_python(pkc::interaction_reachability(convert(envelopes), convert(interaction), refine,
                                                       objective.is_none() ? nullptr : &goal));
    }, py::arg("envelopes"), py::arg("interaction"), py::arg("refine") = false, py::arg("objective") = py::none(),
       "reachability/intervals.interaction_reachability");
    m.def("interaction_reachability_many", [](py::handle envelopes, py::handle interactions) {
        return to_python(pkc::interaction_reachability_many(convert(envelopes), convert(interactions)));
    }, "reachability/intervals.interaction_reachability_many");
    m.def("reachable_envelopes", [](py::handle problem, py::handle y, py::handle parent_ticks) {
        std::optional<std::int64_t> parent;
        if (!parent_ticks.is_none()) parent = parent_ticks.cast<std::int64_t>();
        return to_python(pkc::reachable_envelopes(convert(problem), convert(y), parent));
    }, py::arg("problem"), py::arg("y") = py::none(), py::arg("parent_ticks") = py::none(),
       "reachability/intervals.reachable_envelopes without the `model` entry");
    py::class_<NativeCoefficients, std::shared_ptr<NativeCoefficients>>(m, "NativeCoefficients")
        .def("to_python", [](NativeCoefficients& self) {
            return memoized(self.dict, [&] { return to_python(self.value); });
        });
    m.def("coefficients", [](py::dict value) {
        return std::make_shared<NativeCoefficients>(NativeCoefficients{convert(value), value});
    }, "A coefficient reply the Python bridge compiled, for a native analysis");
    py::class_<NativePrepared, std::shared_ptr<NativePrepared>>(m, "NativePrepared")
        .def_property_readonly("key", [](NativePrepared& self) { return self.key; })
        .def("to_python", [](NativePrepared& self) {
            return memoized(self.dict, [&] {
                py::dict out;
                out["key"] = self.key;
                for (const auto& [name, field] : *std::get<std::shared_ptr<const pkc::Object>>(self.result.data))
                    out[py::str(name)] = to_python(field);
                return py::object(out);
            });
        }, "InteractionGate.prepare's result dict")
        .def("coefficient_digest", [](NativePrepared& self) {
            return pkc::digest(pkc::coefficient_request(self.result));
        }, "digest(coefficient_request(route, plan)), the gate's coefficient cache key")
        .def("compile_coefficients", [](NativePrepared& self) {
            return std::make_shared<NativeCoefficients>(
                NativeCoefficients{pkc::compile_coefficients(pkc::coefficient_request(self.result)), {}});
        })
        .def("analyze", [](std::shared_ptr<NativePrepared> self, std::shared_ptr<NativeCoefficients> coefficients,
                           bool open_terminal, py::handle parent_ticks, std::string key) {
            std::optional<std::int64_t> parent;
            if (!parent_ticks.is_none()) parent = parent_ticks.cast<std::int64_t>();
            auto analysis = pkc::analyze_prepared(self->result, coefficients->value, open_terminal, parent, std::move(key));
            return std::make_shared<NativeAnalysis>(NativeAnalysis{self, coefficients, std::move(analysis), {}});
        }, "InteractionGate.analyze_model's build under the gate's analysis key");
    py::class_<NativeAnalysis, std::shared_ptr<NativeAnalysis>>(m, "NativeAnalysis")
        .def_property_readonly("key", [](NativeAnalysis& self) { return self.analysis.key; })
        .def("required", [](NativeAnalysis& self) {
            return to_python(pkc::required_proof(self.analysis, *self.prepared->program));
        }, "InteractionGate.required's proof, or None")
        .def("to_python", [](NativeAnalysis& self) {
            return memoized(self.dict, [&] {
                py::object model = to_python(self.analysis.model);
                py::dict envelopes = py::reinterpret_borrow<py::dict>(to_python(self.analysis.envelopes));
                py::object model_key = envelopes.attr("pop")("modelKey");
                envelopes["model"] = model;  // the Python result aliases its model
                envelopes["modelKey"] = model_key;
                py::dict out;
                out["key"] = self.analysis.key;
                out["model"] = model;
                out["envelopes"] = envelopes;
                out["coefficient"] = memoized(self.coefficients->dict, [&] { return to_python(self.coefficients->value); });
                out["y"] = to_python(self.analysis.y);
                return py::object(out);
            });
        }, "InteractionGate.analyze_model's result dict");
    py::class_<NativeProgram, std::shared_ptr<NativeProgram>>(m, "NativeProgram")
        .def("append_choice", [](NativeProgram& self, py::handle choice, std::int64_t max_inputs) -> py::object {
            auto child = pkc::append_choice(*self.program, pkc::choice_from_value(convert(choice)), max_inputs);
            if (!child) return py::none();
            return py::cast(std::make_shared<NativeProgram>(
                NativeProgram{std::make_shared<const pkc::Program>(std::move(*child)), {}}));
        }, py::arg("choice"), py::arg("max_inputs") = 200,
           "movement/program.append_choice; raises NativeUnsupported for the appends it rebuilds")
        .def("physics_key", [](NativeProgram& self) {
            if (self.physics_key.empty()) self.physics_key = pkc::program_physics_key(*self.program);
            return self.physics_key;
        }, "movement/program.program_physics_key")
        .def_property_readonly("parent_ticks", [](NativeProgram& self) -> py::object {
            if (!self.program->parent_ticks) return py::none();
            return py::int_(*self.program->parent_ticks);
        })
        .def("to_python", [](NativeProgram& self) { return to_python(pkc::to_value(*self.program)); });
    m.def("program", [](py::handle value) {
        return std::make_shared<NativeProgram>(
            NativeProgram{std::make_shared<const pkc::Program>(pkc::program_from_value(convert(value))), {}});
    }, "A program dict as a typed program");
    m.def("prepare", [](py::handle scene, std::shared_ptr<NativeProgram> program, bool require_complete,
                        std::string key) -> py::object {
        auto result = pkc::gate_prepare(convert(scene), *program->program, require_complete);
        if (!result) return py::none();
        return py::cast(std::make_shared<NativePrepared>(
            NativePrepared{program->program, std::move(*result), std::move(key), {}}));
    }, "prepare for a typed program (tried before the dict overload, whose handle accepts anything)");
    m.def("prepare", [](py::handle scene, py::handle program, bool require_complete, std::string key) -> py::object {
        auto typed = std::make_shared<const pkc::Program>(pkc::program_from_value(convert(program)));
        auto result = pkc::gate_prepare(convert(scene), *typed, require_complete);
        if (!result) return py::none();
        return py::cast(std::make_shared<NativePrepared>(
            NativePrepared{std::move(typed), std::move(*result), std::move(key), {}}));
    }, "InteractionGate.prepare kept native under the gate's preparation key, or None");
    m.def("check_scene", [](py::handle request, py::handle seconds) {
        std::optional<double> budget;
        if (!seconds.is_none()) budget = seconds.cast<double>();
        return to_python(pkc::check_scene(convert(request), budget));
    }, py::arg("request"), py::arg("seconds") = py::none(),
       "scene/static_collision.check_scene; `seconds` is the budget left (None: unlimited)");
    m.def("normalize_geometry", [](py::handle scene) { return to_python(pkc::normalize_geometry(convert(scene))); },
          "scene/geometry.normalize_geometry, returning the normalized scene");
    m.def("route_footprints", [](py::handle blocks, bool standing) {
        return to_python(pkc::route_footprints(convert(blocks), standing));
    }, py::arg("blocks"), py::arg("standing") = false, "scene/geometry.route_footprints");
    m.def("support_center_interval", &pkc::support_center_interval, "scene/geometry.support_center_interval");
    m.def("prepare_scene", [](py::handle request) { return to_python(pkc::prepare_scene(convert(request))); },
          "scene/normalize.prepare_scene; raises NativeUnsupported for scenes with climbable cells");
    m.def("prepare_route", [](py::handle request) { return to_python(pkc::prepare_route(convert(request))); },
          "scene/route.prepare_route; raises NativeUnsupported for routes with ladder events");
    m.def("schedule", [](py::handle route, std::vector<std::int64_t> allocation, std::string stage) {
        return to_python(pkc::schedule(convert(route), allocation, stage));
    }, py::arg("route"), py::arg("allocation"), py::arg("stage") = "jump", "scene/route.schedule");
    m.def("initial_guess", [](py::handle route, py::handle plan, py::handle parent) {
        pkc::Value parent_value = convert(parent);
        return to_python(pkc::initial_guess(convert(route), convert(plan), parent.is_none() ? nullptr : &parent_value));
    }, py::arg("route"), py::arg("plan"), py::arg("parent") = py::none(), "scene/route.initial_guess");
    m.def("prepare_problem", [](py::handle request) { return to_python(pkc::prepare_problem(convert(request))); },
          "numerics/model.prepare");
    m.def("fingerprint", [](py::handle problem) { return pkc::fingerprint(convert(problem)); },
          "numerics/model.fingerprint");
    m.def("numerical_problem", [](py::handle route, py::handle plan, py::handle coefficients, py::handle parent) {
        pkc::Value parent_value = convert(parent);
        return to_python(pkc::numerical_problem(convert(route), convert(plan), convert(coefficients),
                                                parent.is_none() ? nullptr : &parent_value));
    }, py::arg("route"), py::arg("plan"), py::arg("coefficients"), py::arg("parent") = py::none(),
       "numerics/builders.numerical_problem");
    m.def("disk_spec", [](py::handle problem, py::handle extra_rows, py::handle objective, bool strict) {
        pkc::Value objective_value = convert(objective);
        return to_python(pkc::disk_spec(convert(problem), convert(extra_rows),
                                        objective.is_none() ? nullptr : &objective_value, strict));
    }, py::arg("problem"), py::arg("extra_rows"), py::arg("objective"), py::arg("strict"), "numerics/disk.spec");
    m.def("disk_solve", [](py::handle content, double time_limit, unsigned max_iter) {
        return to_python(pkc::disk_solve(convert(content), time_limit, max_iter));
    }, "numerics/disk._solve with disk.MAX_ITER");
    m.def("disk_deficits", [](py::handle problem, py::handle controls) {
        return to_python(pkc::disk_deficits(convert(problem), convert(controls)));
    }, "numerics/disk.deficits as a list");
    m.def("ipopt_seed", [](py::handle yaw) { return to_python(pkc::ipopt_seed(convert(yaw))); }, "numerics/disk.ipopt_seed");
    m.def("alarm_ticks", [](py::handle problem, py::handle shortfall, double fraction) {
        return to_python(pkc::alarm_ticks(convert(problem), convert(shortfall), fraction));
    }, "numerics/disk.alarm_ticks with disk.ALARM_FRACTION");
    m.def("reduce_walls", [](py::handle walls) { return to_python(pkc::reduce_walls(convert(walls))); },
          "numerics/constraints.reduce_walls");
    m.def("validate", [](py::handle problem, py::handle yaw) {
        return to_python(pkc::validate(convert(problem), convert(yaw)));
    }, "validation/numerical.validate");
    m.def("validate_vertical_schedule", [](py::handle route, py::handle profiles, py::handle plan) {
        return to_python(pkc::validate_vertical_schedule(convert(route), convert(profiles), convert(plan)));
    }, "validation/vertical.validate_vertical_schedule");
    m.def("validate_controls", [](py::handle problem, py::handle controls) {
        return to_python(pkc::validate_controls(convert(problem), convert(controls)));
    }, "validation/numerical.validate_controls");
    py::class_<pkc::SceneGeometry, std::shared_ptr<pkc::SceneGeometry>>(m, "SceneGeometry")
        .def(py::init([](py::handle solids) { return std::make_shared<pkc::SceneGeometry>(convert(solids)); }))
        .def("cluster_groups", &pkc::SceneGeometry::cluster_groups, "spatial/surfaces.SceneGeometry._cluster_map's groups")
        .def("sides", [](const pkc::SceneGeometry& g, std::vector<std::int64_t> members, std::vector<std::int64_t> active) {
            return to_python(g.sides(members, active));
        }, "spatial/surfaces.SceneGeometry.sides as plain lists")
        .def("contact_options", [](const pkc::SceneGeometry& g, int axis, std::int64_t sign, py::handle clearance,
                                   std::vector<std::int64_t> members) {
            return to_python(g.contact_options(axis, sign, convert(clearance), members));
        }, "spatial/surfaces.SceneGeometry.contact_options as [surface, plane, members, regions] rows");
    m.def("decompose", [](py::handle scene, std::int64_t max_cells, py::handle seconds, bool refine_support) {
        std::optional<double> budget;
        if (!seconds.is_none()) budget = seconds.cast<double>();
        return to_python(pkc::decompose(convert(scene), max_cells, budget, refine_support));
    }, "spatial/cells.decompose as rows; `seconds` is the deadline's remaining budget (None: unlimited)");
    m.def("support_components", [](py::handle decomposition) {
        return to_python(pkc::support_components(convert(decomposition)));
    }, "spatial/regions.support_components on decompose() rows");
    m.def("build_geometry_graph", [](py::handle scene, py::handle decomposition, py::handle supports, py::handle task_goal) {
        return to_python(pkc::build_geometry_graph(convert(scene), convert(decomposition), convert(supports), convert(task_goal)));
    }, "spatial/regions.build_geometry_graph on decompose() and support_components() rows");
    m.def("free_rectangles", [](py::handle boxes) {
        return to_python(pkc::free_rectangles(convert(boxes)));
    }, "spatial/surfaces.free_rectangles as lists");
    m.def("trace_ladder_schedule", [](py::handle initial, py::handle ladder, py::handle sneak, py::handle hit,
                                      py::handle jump, py::handle target_top, py::handle min_landing_state,
                                      py::handle contacts, py::handle occupied_y_bounds, bool stop_on_landing,
                                      py::handle threshold) {
        return to_python(pkc::trace_ladder_schedule(convert(initial), convert(ladder), convert(sneak), convert(hit),
                                                    convert(jump), convert(target_top), convert(min_landing_state),
                                                    convert(contacts), convert(occupied_y_bounds), stop_on_landing,
                                                    convert(threshold)));
    }, py::arg("initial"), py::arg("ladder"), py::arg("sneak"), py::arg("hit"), py::kw_only(), py::arg("jump") = py::none(),
       py::arg("target_top") = py::none(), py::arg("min_landing_state") = 0, py::arg("contacts") = py::none(),
       py::arg("occupied_y_bounds") = py::none(), py::arg("stop_on_landing") = true, py::arg("threshold") = .005,
       "movement/vertical_profiles.trace_ladder_schedule");
    // movement/units.py's ladder parts, for the native ladder code and its replay checks
    m.def("ladder_units", [](py::handle schedule, py::handle region_id) {
        const pkc::Value value = convert(schedule);
        return to_python(pkc::Value{std::make_shared<const pkc::Array>(
            pkc::ladder_units(*std::get<std::shared_ptr<const pkc::Object>>(value.data), convert(region_id)))});
    }, py::arg("schedule"), py::arg("region_id") = py::none());
    m.def("movement_program", [](std::int64_t ticks, py::handle events) {
        const pkc::Value value = convert(events);
        return to_python(pkc::Value{std::make_shared<const pkc::Array>(
            pkc::movement_program(ticks, *std::get<std::shared_ptr<const pkc::Array>>(value.data)))});
    });
    m.def("exit_matches", [](std::string kind, double y, py::handle region) {
        const pkc::Value value = convert(region);
        return pkc::exit_matches(kind, y, *std::get<std::shared_ptr<const pkc::Object>>(value.data));
    });
    // scene/ladders.py, for the native ladder code and its replay checks
    m.def("ladder_columns", [](py::handle cells) { return to_python(pkc::ladder_columns(convert(cells))); });
    m.def("ladder_regions", [](py::handle cells) { return to_python(pkc::ladder_regions(convert(cells))); });
    m.def("region_rectangles", [](py::handle column, py::handle y) {
        return to_python(pkc::region_rectangles(convert(column), convert(y)));
    });
    m.def("region_contains", [](py::handle column, py::handle position) {
        return pkc::region_contains(convert(column), convert(position));
    });
    m.def("region_axis", [](py::handle column, std::string axis) {
        py::list v = to_python(pkc::region_axis(convert(column), axis));
        return py::tuple(v);
    });
    m.def("climb_walls", [](py::handle column, py::handle solids) {
        return to_python(pkc::climb_walls(convert(column), convert(solids)));
    });
    m.def("movement_contexts", [](py::handle column, py::handle solids, bool include_fall) {
        return to_python(pkc::movement_contexts(convert(column), convert(solids), include_fall));
    }, py::arg("column"), py::arg("solids"), py::kw_only(), py::arg("include_fall") = true);
    m.def("entry_channels", [](py::handle column, py::handle wall) {
        return to_python(pkc::entry_channels(convert(column), convert(wall)));
    });
    m.def("outside_row", [](py::handle face, py::handle state, py::handle name) {
        return to_python(pkc::outside_row(convert(face), convert(state), convert(name)));
    });
    m.def("schedule_constraints", [](py::handle schedule, py::handle column, py::handle wall, py::handle entry_face,
                                     py::handle exit_face, py::handle start) {
        py::list v = to_python(pkc::schedule_constraints(convert(schedule), convert(column), convert(wall),
                                                         convert(entry_face), convert(exit_face), convert(start)));
        return py::tuple(v);
    }, py::arg("schedule"), py::arg("column"), py::arg("wall"), py::arg("entry_face"), py::arg("exit_face"),
       py::kw_only(), py::arg("start") = 0);
    m.def("exit_faces", [](py::handle column, py::handle target) {
        return to_python(pkc::exit_faces(convert(column), convert(target)));
    });
    py::class_<pkc::ConstraintVariants>(m, "ConstraintVariants")
        .def(py::init([](py::handle route, py::handle profile, py::handle geometry, py::handle envelopes, py::handle scope,
                         py::handle checks, py::handle state, py::handle reference,
                         py::handle owners, py::object check, py::handle segment_solids, py::handle objective,
                         std::int64_t settled, py::handle obstacles, py::handle walk_offs) {
                 pkc::VariantInputs inputs;
                 inputs.route = convert(route);
                 inputs.profile = convert(profile);
                 inputs.geometry = convert(geometry);
                 inputs.envelopes = convert(envelopes);
                 inputs.scope = convert(scope);
                 auto listed_values = [](py::handle values) {
                     return *std::get<std::shared_ptr<const pkc::Array>>(convert(py::list(py::reinterpret_borrow<py::object>(values))).data);
                 };
                 inputs.checks = listed_values(checks);
                 inputs.walk_offs = listed_values(walk_offs);
                 inputs.reference = convert(reference);
                 inputs.owners = convert(owners);
                 inputs.objective = convert(objective);
                 inputs.settled = settled;
                 if (!segment_solids.is_none()) {
                     inputs.segment_solids.emplace();
                     for (auto item : py::reinterpret_borrow<py::dict>(segment_solids))
                         inputs.segment_solids->emplace_back(item.first.cast<std::int64_t>(),
                                                             item.second.cast<std::vector<std::int64_t>>());
                     std::sort(inputs.segment_solids->begin(), inputs.segment_solids->end());
                 }
                 if (!check.is_none()) inputs.check = [check] { check(); };
                 std::optional<pkc::Obstacles> listed;
                 if (!obstacles.is_none()) listed = obstacles.cast<pkc::Obstacles>();
                 return std::make_unique<pkc::ConstraintVariants>(std::move(inputs), std::move(listed), convert(state));
             }),
             py::arg("route"), py::arg("profile"), py::arg("geometry"), py::arg("envelopes"), py::arg("scope"),
             py::arg("checks"), py::arg("state"), py::arg("reference"),
             py::arg("owners"), py::arg("check"), py::arg("segment_solids"), py::arg("objective"), py::arg("settled"),
             py::arg("obstacles"), py::arg("walk_offs"))
        .def("__next__", [](pkc::ConstraintVariants& variants) {
            auto variant = variants.next();
            if (!variant) throw py::stop_iteration();
            return to_python(*variant);
        })
        .def_property_readonly("state", [](const pkc::ConstraintVariants& variants) { return to_python(variants.state()); })
        .def_property_readonly("conflict_driven", &pkc::ConstraintVariants::conflict_driven)
        .def_property_readonly("obstacles", [](const pkc::ConstraintVariants& variants) -> py::object {
            if (!variants.obstacles()) return py::none();
            py::list out;
            for (const auto& [begin, solid] : *variants.obstacles()) out.append(py::make_tuple(begin, solid));
            return out;
        })
        .def("obstacle_key", [](const pkc::ConstraintVariants& variants, std::int64_t solid, std::int64_t tick) {
            const auto key = variants.obstacle_key(solid, tick);
            return py::make_tuple(key[0], key[1]);
        })
        .def("with_obstacles", [](const pkc::ConstraintVariants& variants, const pkc::Obstacles& obstacles) {
            return variants.with_obstacles(obstacles);
        })
        .def_property_readonly("item_count", &pkc::ConstraintVariants::item_count);
    m.def("solve_contact_branches",
          [](py::handle route, py::handle profiles, py::handle checks, py::object compiler, py::object solve,
             double seconds, double candidate_seconds, std::int64_t retries, bool improve, bool rank_equal_cost,
             bool optimize_goal, py::object stop_on_feasible, py::object python_executable, py::object cancel_event,
             py::handle geometry, py::handle solid_blocks, std::int64_t max_wall_hits, py::object candidate_filter,
             py::object work, py::handle resume_state, py::handle repair_scope, py::object on_progress,
             py::handle validation_scope, pkc::ConstraintVariants* initial_variants, bool retry_unresolved,
             bool oracle_rejection, bool climb_families, py::object numerical, py::object confirm) {
              if (max_wall_hits) throw pkc::Unsupported("wall-hit repairs run in Python");
              const pkc::SolveHooks hooks = solve_hooks(compiler, solve, work, cancel_event, python_executable);
              pkc::RepairOptions options;
              options.seconds = seconds;
              options.candidate_seconds = candidate_seconds;
              options.retries = retries;
              options.improve = improve;
              options.rank_equal_cost = rank_equal_cost;
              options.optimize_goal = optimize_goal;
              if (!stop_on_feasible.is_none()) options.stop_on_feasible = stop_on_feasible.cast<bool>();
              options.oracle_rejection = oracle_rejection;
              options.climb_families = climb_families;
              options.retry_unresolved = retry_unresolved;
              options.geometry = convert(geometry);
              options.solid_blocks = convert(solid_blocks);
              options.resume_state = convert(resume_state);
              options.repair_scope = convert(repair_scope);
              options.validation_scope = convert(validation_scope);
              options.numerical = numerical_hook(numerical, python_executable, cancel_event);
              options.confirm = numerical_hook(confirm, python_executable, cancel_event);
              if (!candidate_filter.is_none())
                  options.candidate_filter = [candidate_filter](const pkc::Value& plan, const pkc::Value& candidate,
                                                                const pkc::Value& vertical) {
                      return python_hook([&] {
                          return convert(candidate_filter(to_python(plan), to_python(candidate), to_python(vertical)));
                      });
                  };
              if (!on_progress.is_none())
                  options.on_progress = [on_progress](const pkc::Value& state) {
                      python_hook([&] { on_progress(to_python(state)); });
                  };
              return to_python(pkc::solve_contact_branches(convert(route), convert(profiles), convert(checks), hooks,
                                                           options, initial_variants));
          },
          py::arg("route"), py::arg("profiles"), py::arg("checks"), py::arg("compiler"), py::arg("solve"), py::kw_only(),
          py::arg("seconds"), py::arg("candidate_seconds"), py::arg("retries"), py::arg("improve") = false,
          py::arg("rank_equal_cost") = false, py::arg("optimize_goal") = false, py::arg("stop_on_feasible") = py::none(),
          py::arg("python_executable") = py::none(), py::arg("cancel_event") = py::none(), py::arg("geometry") = py::none(),
          py::arg("solid_blocks") = py::none(), py::arg("max_wall_hits") = 0, py::arg("candidate_filter") = py::none(),
          py::arg("work") = py::none(), py::arg("resume_state") = py::none(), py::arg("repair_scope") = py::none(),
          py::arg("on_progress") = py::none(), py::arg("validation_scope") = py::none(),
          py::arg("initial_variants") = nullptr, py::arg("retry_unresolved") = true, py::arg("oracle_rejection") = false,
          py::arg("climb_families") = false, py::arg("numerical") = py::none(), py::arg("confirm") = py::none(),
          "search/repair.solve_contact_branches; `solve` is numerics.solver.solve, called when `numerical` is None");
    m.def("evaluate_node",
          [](py::handle scene, py::handle program, py::object compiler, py::object solve, py::object cancel_event,
             py::object python_executable, py::object prepare, py::object begin, py::object begin_solve,
             py::object assign, py::object progress, py::object settle, py::object observe, py::object replay,
             py::object store, py::handle saved, py::handle guide_plan, std::int64_t span, std::int64_t solve_interval,
             std::string numerical, py::handle frame, double candidate_seconds, bool retry_unresolved) -> py::object {
              pkc::NodeHooks hooks;
              hooks.prepare = [prepare](const pkc::Value& value) {
                  return python_hook([&] { return convert(prepare(to_python(value))); });
              };
              hooks.begin = [begin] { python_hook([&] { begin(); }); };
              hooks.begin_solve = [=] {
                  return python_hook([&] {
                      py::object services = begin_solve();
                      pkc::NodeSolve out;
                      out.hooks = evaluate_hooks(compiler, solve, cancel_event, python_executable, services);
                      out.seconds = services["seconds"].cast<double>();
                      out.objective = convert(services["objective"]);
                      out.adapter_identity = convert(services["adapter_identity"]);
                      return out;
                  });
              };
              hooks.assign = [assign](const pkc::Value& repair) { python_hook([&] { assign(to_python(repair)); }); };
              if (!progress.is_none())
                  hooks.progress = [progress](const pkc::Value& repair) {
                      python_hook([&] { progress(to_python(repair)); });
                  };
              hooks.settle = [settle](const pkc::Value& repair) { python_hook([&] { settle(to_python(repair)); }); };
              hooks.observe = [observe](const pkc::Value& model, const std::string& status, double elapsed,
                                        const pkc::Value& metrics) {
                  python_hook([&] { observe(to_python(model), status, elapsed, to_python(metrics)); });
              };
              hooks.replay = [replay](const pkc::Value& positions, const pkc::Value& model) {
                  python_hook([&] { replay(to_python(positions), to_python(model)); });
              };
              hooks.store = [store](const std::string& identity, const pkc::Value& record, const pkc::Value& node_result,
                                    const pkc::Value& recovery) {
                  python_hook([&] { store(identity, to_python(record), to_python(node_result), to_python(recovery)); });
              };
              pkc::NodeOptions options;
              options.saved = convert(saved);
              options.guide_plan = convert(guide_plan);
              options.span = span;
              options.solve_interval = solve_interval;
              options.numerical = numerical;
              options.frame = convert(frame);
              options.candidate_seconds = candidate_seconds;
              options.retry_unresolved = retry_unresolved;
              const auto evaluated = pkc::evaluate_node(convert(scene), convert(program), hooks, options);
              if (!evaluated) return py::none();
              return py::make_tuple(to_python(evaluated->result), evaluated->input_program);
          },
          py::arg("scene"), py::arg("program"), py::arg("compiler"), py::arg("solve"), py::kw_only(),
          py::arg("cancel_event"), py::arg("python_executable"), py::arg("prepare"), py::arg("begin"),
          py::arg("begin_solve"), py::arg("assign"), py::arg("progress"), py::arg("settle"), py::arg("observe"),
          py::arg("replay"), py::arg("store"), py::arg("saved"), py::arg("guide_plan"), py::arg("span"),
          py::arg("solve_interval"), py::arg("numerical"), py::arg("frame"), py::arg("candidate_seconds"),
          py::arg("retry_unresolved"),
          "search/dfs.evaluate_node with the search state it records into as Python callables: (result, whether the "
          "result's program is the caller's), or None for a route the native node solve leaves to Python");
    m.def("commit_arrival", [](py::handle program, py::handle best, py::handle source_program, bool keep_region_choices) {
        return to_python(pkc::commit_arrival(convert(program), convert(best), convert(source_program), keep_region_choices));
    }, py::arg("program"), py::arg("best"), py::kw_only(), py::arg("source_program") = py::none(),
       py::arg("keep_region_choices") = false, "search/dfs.commit_arrival");
    m.def("candidate_key", [](py::handle program) { return pkc::candidate_key(convert(program)); },
          "search/dfs.candidate_key");
    m.def("round_trip_dfs_state", [](py::handle state) {
        return to_python(pkc::to_value(pkc::dfs_state_from_value(convert(state))));
    }, "search_goals' state through its typed form (search/dfs_state.hpp)");
    m.def("checkpoint_encode", [](py::handle value, py::handle known) {
        const pkc::Value v = convert(value);
        return checkpoint_encode([&](pkc::CheckpointEncoder& e) { return e.encode(v); }, known);
    }, "io/checkpoint_store's encode of a JSON value: (its encoding, new objects [(key, data)] not in `known`)");
    py::class_<NativeSnapshot, std::shared_ptr<NativeSnapshot>>(m, "DfsSnapshot")
        .def("__getitem__", &NativeSnapshot::item)
        .def("get", [](NativeSnapshot& s, const std::string& key, py::object fallback) {
            return field(s.fields(), key) ? s.item(key) : fallback;
        }, py::arg("key"), py::arg("default") = py::none())
        .def("__contains__", [](const NativeSnapshot& s, const std::string& key) { return field(s.fields(), key) != nullptr; })
        .def("keys", [](const NativeSnapshot& s) {
            py::list out;
            for (const auto& [key, _] : s.fields()) out.append(key);
            return out;
        })
        .def("__iter__", [](const NativeSnapshot& s) {
            py::list out;
            for (const auto& [key, _] : s.fields()) out.append(key);
            return py::iter(out);
        })
        .def("__len__", [](const NativeSnapshot& s) { return s.fields().size(); })
        .def("to_python", [](const NativeSnapshot& s) { return to_python(s.snapshot->state); },
             "the whole state as a new dict")
        .def("value", [](const NativeSnapshot& s, py::handle path) { return to_python(s.at(path)); },
             "the value at a path of keys and indexes")
        .def("length", [](const NativeSnapshot& s, py::handle path) {
            const pkc::Value node = s.at(path);
            if (const auto* list = std::get_if<std::shared_ptr<const pkc::Array>>(&node.data)) return (*list)->size();
            return object_fields(node).size();
        }, "len() of the list or dict at a path")
        .def_property_readonly("repair", [](const NativeSnapshot& s) {
            const pkc::RepairTargets& r = s.snapshot->repair;
            py::list targets;
            for (const auto& paths : r.targets) targets.append(paths_to_python(paths));
            py::dict out;
            out["token"] = r.token;
            out["patch"] = r.patch;
            out["program"] = paths_to_python(r.program);
            out["targets"] = targets;
            return out;
        }, "where the saver's repair program and extra targets sit (see search/dfs_search.hpp RepairTargets)")
        .def("checkpoint_encode", [](const NativeSnapshot& s, py::handle known,
                                     std::optional<std::vector<std::string>> windows) {
            return checkpoint_encode([&](pkc::CheckpointEncoder& e) {
                return windows ? e.encode_replacing(s.snapshot->state, "windows", *windows) : e.encode(s.snapshot->state);
            }, known);
        }, py::arg("known"), py::arg("windows") = py::none(),
           "checkpoint_store's encode of the state, or of dict(state, windows=[stored windows by key]): (encoding, new "
           "objects)");
    py::class_<pkc::DfsSearch, std::shared_ptr<pkc::DfsSearch>>(m, "DfsSearch")
        .def(py::init([](py::handle state, py::dict options, py::object hooks, std::shared_ptr<pkc::InteractionGate> gate,
                         std::shared_ptr<pkc::AnchorScene> anchors, std::shared_ptr<pkc::AnchorAdapter> adapter) {
            pkc::DfsOptions o;
            auto value = [&](const char* key) { return convert(options[key]); };
            o.scene = value("scene");
            o.cfg = value("cfg");
            o.motion = value("motion");
            o.guide_plan = value("guidePlan");
            o.anchor_policy = value("anchorPolicy");
            o.policy_settings = value("policySettings");
            o.domain_id = value("domainId");
            for (py::handle root : options["roots"]) o.roots.push_back(convert(root));
            o.task_block = options["taskBlock"].cast<std::string>();
            o.initial_jump_inputs = value("initialJumpInputs");
            o.caps = *std::get<std::shared_ptr<const pkc::Object>>(value("caps").data);
            o.minimum_run_pruning = options["minimumRunPruning"].cast<bool>();
            o.retry_unknown = options["retryUnknown"].cast<bool>();
            o.deadline = options["deadline"].cast<double>();
            o.started = options["started"].cast<double>();
            o.candidate_seconds = options["candidateSeconds"].cast<double>();
            o.score_seconds = options["scoreSeconds"].cast<double>();
            o.batch_size = options["batchSize"].cast<std::int64_t>();
            o.numerical_backend = options["numericalBackend"].cast<std::string>();
            o.prior_calls = options["priorCalls"].cast<std::int64_t>();
            o.initial_expanded = options["initialExpanded"].cast<std::int64_t>();
            o.comparison_policy = options["comparisonPolicy"].cast<std::int64_t>();
            pkc::DfsHooks h;
            auto hook = [&](const char* name) { return py::object(hooks.attr(name)); };
            h.monotonic = [f = hook("monotonic")] { return python_hook([&] { return f().cast<double>(); }); };
            h.cancelled = [f = hook("cancelled")] { return python_hook([&] { return f().cast<bool>(); }); };
            h.progress = hooks.attr("progress").cast<bool>();
            const auto snapshot_hook = [](py::object f) {
                return [f](const pkc::Snapshot& state) {
                    python_hook([&] { f(std::make_shared<NativeSnapshot>(NativeSnapshot{state, {}})); });
                };
            };
            h.publish = snapshot_hook(hook("publish"));
            h.begin_repair = snapshot_hook(hook("begin_repair"));
            h.repair_progress = snapshot_hook(hook("repair_progress"));
            h.check = [f = hook("check")] { python_hook([&] { f(); }); };
            h.solver_calls = [f = hook("solver_calls")] { return python_hook([&] { return f().cast<std::int64_t>(); }); };
            h.count = [f = hook("count")](const std::string& name) { python_hook([&] { f(name); }); };
            h.accepted = [f = hook("accepted")](const pkc::Value& candidate) { python_hook([&] { f(to_python(candidate)); }); };
            h.termination = [f = hook("termination")] { return python_hook([&] { return convert(f()); }); };
            h.evaluate = [f = hook("evaluate")](std::int64_t frame, const pkc::Value& request, const pkc::Value& cursor,
                                                const pkc::Value& program, const pkc::Value& options) {
                return python_hook([&] {
                    return convert(f(frame, to_python(request), to_python(cursor), to_python(program), to_python(options)));
                });
            };
            h.transfer_bounds = [f = hook("transfer_bounds")](const pkc::Value& saved) {
                return python_hook([&] { return convert(f(to_python(saved))); });
            };
            h.prepare = [f = hook("prepare")](const pkc::Value& program) {
                return python_hook([&] { return convert(f(to_python(program))); });
            };
            auto node_solve = [compiler = hook("compiler"), solve = hook("solve"), cancel_event = hook("cancel_event"),
                               python_executable = hook("python_executable")](py::object services) {
                pkc::NodeSolve out;
                out.hooks = evaluate_hooks(compiler, solve, cancel_event, python_executable, services);
                out.seconds = services["seconds"].cast<double>();
                return out;  // the search sets the stage's objective, identity and candidate check
            };
            h.begin_solve = [f = hook("begin_solve"), node_solve](const pkc::Value& program) {
                return python_hook([&] { return node_solve(f(to_python(program))); });
            };
            h.prepare_leg = [f = hook("prepare_leg")](const pkc::Value& program, const pkc::Value& landing) {
                return python_hook([&] { return convert(f(to_python(program), to_python(landing))); });
            };
            h.begin_leg = [f = hook("begin_leg"), node_solve](const pkc::Value& program, const std::string& numerical,
                                                              double seconds) {
                return python_hook([&] { return node_solve(f(to_python(program), numerical, seconds)); });
            };
            h.solve_route = [f = hook("solve_route")](std::int64_t frame, const pkc::Value& request,
                                                      const pkc::Value& cursor, const pkc::Value& program,
                                                      const std::function<void(const pkc::Value&)>& progress) {
                return python_hook([&] {
                    py::object callback = py::none();
                    if (progress)
                        callback = py::cpp_function([progress](py::handle repair) { progress(convert(repair)); });
                    return convert(f(frame, to_python(request), to_python(cursor), to_python(program), callback));
                });
            };
            return std::make_shared<pkc::DfsSearch>(pkc::dfs_state_from_value(convert(state)), std::move(o), std::move(h),
                                                    std::move(gate), std::move(anchors), std::move(adapter));
        }))
        .def("run", &pkc::DfsSearch::run, "the main loop; its termination")
        .def("finalize_best", &pkc::DfsSearch::finalize_best)
        .def("state", [](const pkc::DfsSearch& search) { return to_python(search.state()); })
        .def("viability", [](const pkc::DfsSearch& search) {
            py::dict out;
            out["checked"] = search.viability_checked();
            out["excluded"] = search.viability_excluded();
            return out;
        })
        .def("score_progress", [](pkc::DfsSearch& search, py::handle repair) { search.score_progress(convert(repair)); });
    py::class_<pkc::InteractionGate, std::shared_ptr<pkc::InteractionGate>>(m, "InteractionGate")
        .def(py::init([](py::handle scene) { return std::make_shared<pkc::InteractionGate>(convert(scene)); }))
        .def("reachable_faces", [](pkc::InteractionGate& gate, std::optional<std::string> name) {
            const auto& t = gate.reachable_faces(name);
            py::dict out;
            out["blocks"] = t.blocks;
            out["solids"] = t.solids;
            out["ticks"] = t.ticks ? py::object(py::int_(*t.ticks)) : py::object(py::none());
            out["landing"] = t.landing;
            out["stepping"] = t.stepping;
            out["ceiling"] = t.ceiling;
            out["ceilingSolids"] = t.ceiling_solids;
            return out;
        }, "InteractionGate.reachable_faces with sets for frozensets")
        .def("required", [](pkc::InteractionGate& gate, py::handle program) {
            return to_python(gate.required(program_arg(program)));
        }, "InteractionGate.required for a program dict or NativeProgram")
        .def("prepare", [](pkc::InteractionGate& gate, py::handle program, bool require_complete) -> py::object {
            auto prepared = gate.prepared(program_arg(program), require_complete);
            if (!prepared) return py::none();
            return py::cast(std::make_shared<NativePrepared>(NativePrepared{prepared->program, prepared->result, prepared->key, {}}));
        }, py::arg("program"), py::arg("require_complete") = false, "InteractionGate._prepared on the session cache")
        .def("analysis", [](pkc::InteractionGate& gate, py::handle program) -> py::object {
            auto [prepared, analysis] = gate.analyzed(program_arg(program));
            if (!analysis) return py::none();
            return py::cast(std::make_shared<NativeAnalysis>(NativeAnalysis{
                std::make_shared<NativePrepared>(NativePrepared{prepared->program, prepared->result, prepared->key, {}}),
                std::make_shared<NativeCoefficients>(NativeCoefficients{analysis->coefficient, {}}), *analysis, {}}));
        }, "InteractionGate._analysis (open terminal, no scope) on the session cache")
        .def("approach", [](pkc::InteractionGate& gate, py::dict context, std::int64_t tick, bool jumping) {
            return to_python(gate.approach(generation_context(context), tick, jumping));
        }, "InteractionGate.approach's envelopes without their model alias")
        .def("query_many", [](pkc::InteractionGate& gate, py::dict context, py::list bounds, std::int64_t tick,
                              py::dict label, bool jumping, std::vector<std::string> phases) {
            std::vector<pkc::Array> boxes;
            for (py::handle b : bounds) boxes.push_back(std::get<std::shared_ptr<const pkc::Array>>(convert(b).data) ? *std::get<std::shared_ptr<const pkc::Array>>(convert(b).data) : pkc::Array{});
            return to_python(pkc::Value{std::make_shared<const pkc::Array>(
                gate.query_many(generation_context(context), boxes, tick,
                                *std::get<std::shared_ptr<const pkc::Object>>(convert(label).data), jumping, phases))});
        })
        .def("walk_off", [](pkc::InteractionGate& gate, py::dict context, py::handle landing_boxes, py::dict label) {
            return gate.walk_off(generation_context(context), convert(landing_boxes),
                                 *std::get<std::shared_ptr<const pkc::Object>>(convert(label).data));
        })
        .def("sequence", [](pkc::InteractionGate& gate, py::dict context, py::handle choice) {
            return to_python(gate.sequence(generation_context(context), pkc::choice_from_value(convert(choice))));
        })
        .def("check_program", [](pkc::InteractionGate& gate, py::handle program, py::handle objective) {
            auto [proof, selection] = gate.check_program(program_arg(program), convert(objective));
            return py::make_tuple(to_python(proof), to_python(selection));
        }, "InteractionGate.check_program: (proof, walk-off selection to append to program['walkOffs'])")
        .def("generate_choices", [](std::shared_ptr<pkc::InteractionGate> gate, const std::string& kind, py::dict context) {
            auto native = generation_context(context);
            native.gate = gate.get();
            py::list out;
            for (const auto& choice : pkc::generate_choices(kind, native)) out.append(to_python(pkc::to_value(choice)));
            return out;
        }, "movement/generation.generate_choices for an open context, with this gate as its interactionGate")
        .def_property_readonly("checked", [](pkc::InteractionGate& gate) { return gate.checked; })
        .def_property_readonly("exclusions", [](pkc::InteractionGate& gate) {
            return to_python(pkc::Value{std::make_shared<const pkc::Array>(gate.exclusions)});
        })
        .def_property_readonly("excluded", [](pkc::InteractionGate& gate) { return gate.exclusions.size(); })
        .def("exclusions_since", [](pkc::InteractionGate& gate, std::size_t start) {
            return to_python(pkc::Value{std::make_shared<const pkc::Array>(
                gate.exclusions.begin() + static_cast<std::ptrdiff_t>(std::min(start, gate.exclusions.size())),
                gate.exclusions.end())});
        }, "The exclusion log from entry `start` on")
        .def_property_readonly("metrics", [](pkc::InteractionGate& gate) {
            return to_python(pkc::Value{std::make_shared<const pkc::Object>(gate.metrics())});
        });
    py::class_<pkc::AnchorAdapter, std::shared_ptr<pkc::AnchorAdapter>>(m, "AnchorAdapter")
        .def(py::init([](py::handle data, py::handle graph, py::handle anchor_policy, py::handle guide_plan,
                         py::handle policy_settings, std::shared_ptr<pkc::AnchorScene> scene) {
            return std::make_shared<pkc::AnchorAdapter>(convert(data), convert(graph), convert(anchor_policy),
                                                        convert(guide_plan), convert(policy_settings), std::move(scene));
        }))
        .def("initial_cursor", [](const pkc::AnchorAdapter& a, py::handle roots) {
            const pkc::Value listed = convert(roots);
            return to_python(a.initial_cursor(*std::get<std::shared_ptr<const pkc::Array>>(listed.data)));
        })
        .def("next_goals", [](const pkc::AnchorAdapter& a, py::handle cursor, py::handle parents, py::handle continuation) {
            const pkc::Value listed = convert(parents);
            py::object reply = to_python(a.next_goals(convert(cursor), *std::get<std::shared_ptr<const pkc::Array>>(listed.data),
                                                    convert(continuation), [] {}));
            return py::make_tuple(reply["requests"], reply["continuation"], reply["report"]);
        })
        .def("stage", [](const pkc::AnchorAdapter& a, py::handle request, py::handle cursor) {
            return to_python(a.stage(convert(request), convert(cursor)));
        })
        .def("advance", [](const pkc::AnchorAdapter& a, py::handle cursor, py::handle child, py::handle witness,
                           py::handle request) {
            return to_python(a.advance(convert(cursor), convert(child), convert(witness), convert(request)));
        })
        .def("candidate", [](pkc::AnchorAdapter& a, py::handle program, py::handle request, py::handle cursor,
                             bool node_query) {
            auto check = a.candidate(convert(program), convert(request), convert(cursor), node_query);
            return py::cpp_function([check](py::handle plan, py::handle candidate, py::handle vertical, py::handle scope) {
                return to_python(check(convert(plan), convert(candidate), convert(vertical), convert(scope)));
            });
        }, "the stage view's bound candidate(plan, candidate, vertical, scope) check")
        .def_property_readonly("counts", [](const pkc::AnchorAdapter& a) {
            py::dict out;
            out["eventAudits"] = a.event_audits();
            out["requiredEventRejections"] = a.required_event_rejections();
            return out;
        });
    py::class_<pkc::AnchorScene, std::shared_ptr<pkc::AnchorScene>>(m, "AnchorScene")
        .def(py::init([](py::handle scene) { return std::make_shared<pkc::AnchorScene>(convert(scene)); }))
        .def("visits", [](const pkc::AnchorScene& s, py::handle program) { return s.visits(*program_arg(program)); })
        .def("launch", [](const pkc::AnchorScene& s, py::handle program, std::optional<std::int64_t> begin,
                          std::optional<std::int64_t> end) { return s.launch(*program_arg(program), begin, end); },
             py::arg("program"), py::arg("begin") = py::none(), py::arg("end") = py::none())
        .def("feet_touches", [](const pkc::AnchorScene& s, py::handle program) { return s.feet_touches(*program_arg(program)); })
        .def("visit_state", [](const pkc::AnchorScene& s, py::handle program) {
            return visit_state_dict(s.visit_state(*program_arg(program)));
        })
        .def("audit", [](const pkc::AnchorScene& s, py::handle program, std::vector<std::string> anchors,
                         std::optional<std::int64_t> reached) {
            const auto a = s.audit(*program_arg(program), anchors, reached);
            py::dict out;
            out["accepted"] = !a.reason;
            out["reason"] = optional_text(a.reason);
            out["visits"] = visit_state_dict(a.visits);
            out["reached"] = a.reached;
            out["ticks"] = a.ticks;
            return out;
        }, py::arg("program"), py::arg("anchors"), py::arg("reached") = py::none())
        .def("next_anchor", [](const pkc::AnchorScene& s, py::handle program, std::vector<std::string> anchors,
                               const std::string& task) { return s.next_anchor(*program_arg(program), anchors, task); })
        .def("advantageous_targets", [](const pkc::AnchorScene& s, py::handle base, py::handle child,
                                        const std::string& next_owner) {
            py::set out;
            for (const auto& [kind, name] : s.advantageous_targets(*program_arg(base), *program_arg(child), next_owner))
                out.add(py::make_tuple(kind, optional_text(name)));
            return out;
        })
        .def("transfer_structure", [](const pkc::AnchorScene& s, py::handle program, std::int64_t start,
                                      const std::string& source, const std::string& target, bool complete,
                                      bool until_arrival) {
            return to_python(s.transfer_structure(*program_arg(program), start, source, target, complete, until_arrival));
        })
        .def("within_caps", [](const pkc::AnchorScene& s, py::handle request, py::handle program) {
            return s.within_caps(*std::get<std::shared_ptr<const pkc::Object>>(convert(request).data), *program_arg(program));
        })
        .def("decision_point", [](const pkc::AnchorScene& s, py::handle program, py::handle guides) {
            return to_python(s.decision_point(*program_arg(program), convert(guides)));
        })
        .def("program_objective", [](const pkc::AnchorScene& s, py::handle program, py::handle guides,
                                     std::vector<std::string> anchors) {
            return to_python(s.program_objective(*program_arg(program), convert(guides), anchors));
        })
        .def("owner", &pkc::AnchorScene::owner)
        .def("support_face", &pkc::AnchorScene::support_face);
    m.def("comparison_label", [](py::handle program, std::int64_t start, std::optional<std::int64_t> through,
                                 const std::string& identity, py::handle mode, py::handle context, py::handle evidence) {
        const std::string m = py::isinstance<py::bool_>(mode) ? (mode.cast<bool>() ? "turnaround" : "ordinary")
                                                              : mode.cast<std::string>();
        auto object_of = [](py::handle h) {
            return h.is_none() ? pkc::Object{} : *std::get<std::shared_ptr<const pkc::Object>>(convert(h).data);
        };
        return to_python(pkc::comparison_label(*program_arg(program), start, through, identity, m, object_of(context),
                                               object_of(evidence)));
    }, "search/comparison.comparison_label");
    m.def("compare_candidates", [](py::handle candidate, py::handle peer, py::handle context, py::handle incumbent) {
        const pkc::Object ctx = context.is_none() ? pkc::Object{} : *std::get<std::shared_ptr<const pkc::Object>>(convert(context).data);
        return to_python(pkc::compare_candidates(convert(candidate), convert(peer), ctx, convert(incumbent)));
    }, "search/comparison.compare_candidates");
    m.def("assess", [](py::handle label, py::handle peers, py::handle context) {
        return to_python(pkc::assess(convert(label), convert(peers), *std::get<std::shared_ptr<const pkc::Object>>(convert(context).data)));
    }, "search/deferrals.assess");
    m.def("remaining_bound", [](py::handle guides, std::int64_t span, py::handle program, py::handle motion,
                                py::handle transfers, py::handle hard_latches) {
        return to_python(pkc::remaining_bound(convert(guides), span, *program_arg(program), convert(motion),
                                              convert(transfers), convert(hard_latches)));
    }, "search/bounds/remaining.remaining_bound");
    m.def("program_search_key", [](py::handle program) { return pkc::program_search_key(*program_arg(program)); },
          "movement/program.program_search_key");
    m.def("truncate_program", [](py::handle program, std::int64_t ticks) {
        return to_python(pkc::to_value(pkc::truncate_program(*program_arg(program), ticks)));
    }, "movement/program.truncate_program");
    m.def("node_identity", [](py::handle program, py::handle context) {
        return pkc::node_identity(*program_arg(program), convert(context));
    }, "search/feasibility.node_identity");
    m.def("meaningful_choice", [](py::handle choice) { return pkc::meaningful_choice(pkc::choice_from_value(convert(choice))); },
          "search/feasibility.meaningful_choice");
    m.def("landing_prefixes", [](py::handle scene, py::handle program, const std::string& block, bool reuse_full) {
        py::list out;
        for (const auto& prefix : pkc::landing_prefixes(convert(scene), *program_arg(program), block, reuse_full))
            out.append(py::make_tuple(to_python(pkc::to_value(prefix.program)), prefix.reused));
        return out;
    }, "search/goals._landing_prefixes: (program, reused) pairs");
    m.def("prefix_viability", [](py::handle scene, py::handle program, std::vector<std::string> required,
                                 std::int64_t terminal_tick, std::optional<std::vector<std::string>> types) {
        return to_python(pkc::prefix_viability(convert(scene), *program_arg(program), required, terminal_tick, types));
    }, "reachability/viability.prefix_viability");
    m.def("classify", [](py::handle guides, std::int64_t span, py::handle point) {
        auto p = point.cast<std::vector<double>>();
        return to_python(pkc::classify(convert(guides), span, p.at(0), p.at(1)));
    }, "spatial/anchor_guides.classify");
    m.def("decision_objective", [](py::handle guides, std::int64_t reached, py::handle point, py::handle fallback) {
        return to_python(pkc::decision_objective(convert(guides), reached, convert(point), convert(fallback)));
    }, "spatial/anchor_guides.decision_objective");
    m.def("advantageous_candidates", [](std::shared_ptr<pkc::InteractionGate> gate, std::shared_ptr<pkc::AnchorScene> anchors,
                                        std::optional<std::string> launch, py::handle top, const std::string& next_owner,
                                        std::vector<std::string> types, py::handle threshold) {
        auto number = [](py::handle h) { return pkc::Num{h.cast<double>(), py::isinstance<py::int_>(h)}; };
        return to_python(pkc::Value{std::make_shared<const pkc::Array>(
            pkc::advantageous_candidates(*gate, *anchors, launch, number(top), next_owner, types, number(threshold)))});
    }, "anchors/policy.advantageous_candidates on the gate's face tables");
    m.def("disk_cached_solve", [](py::handle content, double seconds, const std::string& kind, double cap,
                                  unsigned max_iter, double retry_cap, unsigned retry_max_iter) {
        return to_python(pkc::disk_cached_solve(convert(content), seconds, kind,
                                                pkc::DiskLimits{cap, max_iter, retry_cap, retry_max_iter}));
    }, "numerics/disk.solve on the native decided-result cache the native gate's screens share");
    m.def("solve_numerical",
          [](py::handle request, std::optional<double> seconds, std::optional<bool> stop_on_feasible,
             py::object cancel_event, py::object on_incumbent, std::optional<std::string> worker) {
              std::function<void(const pkc::Value&)> incumbent;
              if (!on_incumbent.is_none())
                  incumbent = [on_incumbent](const pkc::Value& candidate) {
                      python_hook([&] { on_incumbent(to_python(candidate)); });
                  };
              return to_python(pkc::solve_numerical(convert(request), seconds, stop_on_feasible,
                                                    worker ? *worker : worker_path(), cancelled_hook(cancel_event),
                                                    incumbent));
          },
          py::arg("request"), py::kw_only(), py::arg("seconds") = py::none(), py::arg("stop_on_feasible") = py::none(),
          py::arg("cancel_event") = py::none(), py::arg("on_incumbent") = py::none(), py::arg("worker") = py::none(),
          "numerics.solver.solve with the native worker (or `worker`)");
    m.def("relaxed_dispatch",
          [](std::shared_ptr<pkc::InteractionGate> core, py::handle problem, double seconds, bool stop_on_feasible,
             bool confirming, py::object cancel_event, py::object on_incumbent) {
              std::function<void(const pkc::Value&)> incumbent;
              if (!on_incumbent.is_none())
                  incumbent = [on_incumbent](const pkc::Value& candidate) {
                      python_hook([&] { on_incumbent(to_python(candidate)); });
                  };
              return to_python(pkc::relaxed_dispatch(convert(problem), seconds, stop_on_feasible, confirming, incumbent,
                                                     relaxed_services(core, cancel_event)));
          },
          py::arg("core"), py::arg("problem"), py::kw_only(), py::arg("seconds"), py::arg("stop_on_feasible"),
          py::arg("confirming"), py::arg("cancel_event"), py::arg("on_incumbent"),
          "numerics/disk.RelaxedDispatch's dispatch with its confirmations in the gate's native core");
    m.def("disk_stats", [] { return to_python(pkc::disk_stats()); }, "numerics/disk.STATS of the native decided cache");
    m.def("float_repr", &pkc::float_repr, "repr(float) for a finite float");
    m.def("sha256_hex", [](py::bytes data) { return pkc::sha256_hex(std::string(data)); });
}
