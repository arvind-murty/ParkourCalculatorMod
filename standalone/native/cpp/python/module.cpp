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
#include "pkc/envelopes.hpp"
#include "pkc/gate.hpp"
#include "pkc/movement/vertical_profiles.hpp"
#include "pkc/numerics/builders.hpp"
#include "pkc/numerics/constraints.hpp"
#include "pkc/numerics/disk.hpp"
#include "pkc/numerics/model.hpp"
#include "pkc/program.hpp"
#include "pkc/scene/geometry.hpp"
#include "pkc/scene/normalize.hpp"
#include "pkc/scene/route.hpp"
#include "pkc/scene/static_collision.hpp"
#include "pkc/screen.hpp"
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

py::object memoized(py::object& slot, const std::function<py::object()>& build) {
    if (!slot) slot = build();
    return slot;
}

py::object to_python(const pkc::Value& value) {
    PyObject* result = new_reference(value);
    if (!result) throw py::error_already_set();
    return py::reinterpret_steal<py::object>(result);
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
    m.def("interaction_reachability", [](py::handle envelopes, py::handle interaction, bool refine) {
        return to_python(pkc::interaction_reachability(convert(envelopes), convert(interaction), refine));
    }, py::arg("envelopes"), py::arg("interaction"), py::arg("refine") = false,
       "reachability/intervals.interaction_reachability; raises NativeUnsupported for a refined screen with a model");
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
    m.def("float_repr", &pkc::float_repr, "repr(float) for a finite float");
    m.def("sha256_hex", [](py::bytes data) { return pkc::sha256_hex(std::string(data)); });
}
