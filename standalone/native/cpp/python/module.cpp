// Python bindings for the native core. Conversion happens once per call at
// this boundary; native units keep their state in C++ between calls.
#include <pybind11/pybind11.h>

#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include "pkc/canonical.hpp"
#include "pkc/coefficients.hpp"
#include "pkc/envelopes.hpp"
#include "pkc/gate.hpp"
#include "pkc/program.hpp"

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

py::object to_python(const pkc::Value& value) {
    PyObject* result = new_reference(value);
    if (!result) throw py::error_already_set();
    return py::reinterpret_steal<py::object>(result);
}

}  // namespace

PYBIND11_MODULE(_pkc_native, m) {
    m.doc() = "Native core for parkour_solver (canonical identity, typed programs, gate preparation).";
    py::register_exception<std::invalid_argument>(m, "NativeValueError", PyExc_ValueError);
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
    m.def("reachable_envelopes", [](py::handle problem, py::handle y, py::handle parent_ticks) {
        std::optional<std::int64_t> parent;
        if (!parent_ticks.is_none()) parent = parent_ticks.cast<std::int64_t>();
        return to_python(pkc::reachable_envelopes(convert(problem), convert(y), parent));
    }, py::arg("problem"), py::arg("y") = py::none(), py::arg("parent_ticks") = py::none(),
       "reachability/intervals.reachable_envelopes without the `model` entry");
    m.def("float_repr", &pkc::float_repr, "repr(float) for a finite float");
    m.def("sha256_hex", [](py::bytes data) {
        static const char* hex = "0123456789abcdef";
        std::string out;
        for (auto byte : pkc::sha256(std::string(data))) {
            out += hex[byte >> 4];
            out += hex[byte & 0xF];
        }
        return out;
    });
}
