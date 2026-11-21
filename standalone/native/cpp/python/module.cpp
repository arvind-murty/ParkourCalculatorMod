// Python bindings for the native core. Conversion happens once per call at
// this boundary; native units keep their state in C++ between calls.
#include <pybind11/pybind11.h>

#include <stdexcept>

#include "pkc/canonical.hpp"

namespace py = pybind11;

namespace {

pkc::Value convert(py::handle object) {
    if (object.is_none()) return {nullptr};
    if (py::isinstance<py::bool_>(object)) return {object.cast<bool>()};
    if (py::isinstance<py::int_>(object)) {
        int overflow = 0;
        long long value = PyLong_AsLongLongAndOverflow(object.ptr(), &overflow);
        if (overflow) throw py::type_error("integer outside the 64-bit range");
        return {static_cast<std::int64_t>(value)};
    }
    if (py::isinstance<py::float_>(object)) return {object.cast<double>()};
    if (py::isinstance<py::str>(object)) return {object.cast<std::string>()};
    if (py::isinstance<py::list>(object) || py::isinstance<py::tuple>(object)) {
        auto array = std::make_shared<pkc::Array>();
        for (auto item : object) array->push_back(convert(item));
        return {std::shared_ptr<const pkc::Array>(std::move(array))};
    }
    if (py::isinstance<py::dict>(object)) {
        auto map = std::make_shared<pkc::Object>();
        for (auto item : object.cast<py::dict>()) {
            // json.dumps would stringify non-str keys after sorting them by their
            // own type; canonical identity only uses str keys, so reject the rest.
            if (!py::isinstance<py::str>(item.first)) throw py::type_error("canonical keys must be str");
            map->emplace_back(item.first.cast<std::string>(), convert(item.second));
        }
        return {std::shared_ptr<const pkc::Object>(std::move(map))};
    }
    throw py::type_error("Object of type " + std::string(py::str(py::type::handle_of(object).attr("__name__"))) +
                         " is not canonical JSON");
}

}  // namespace

PYBIND11_MODULE(_pkc_native, m) {
    m.doc() = "Native core for parkour_solver (canonical identity).";
    py::register_exception<std::invalid_argument>(m, "CanonicalValueError", PyExc_ValueError);
    m.def("canonical_json", [](py::handle value) { return py::bytes(pkc::canonical_json(convert(value))); },
          "json.dumps(value, sort_keys=True, allow_nan=False).encode()");
    m.def("digest", [](py::handle value) { return pkc::digest(convert(value)); },
          "parkour_solver.canonical.digest");
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
