// CPython math-module functions whose results differ from the obvious libm
// expression in the last bit.
#pragma once

#include <cfloat>
#include <cmath>
#include <initializer_list>

namespace pkc {

inline double radians(double degrees) { return degrees * (M_PI / 180.0); }  // math.radians
inline double degrees(double radians) { return radians * (180.0 / M_PI); }  // math.degrees

// CPython 3.12.2 vector_norm (math.hypot and math.dist, with compensated rounding) on n >= 2 coordinates; libm hypot
// can differ by one ULP and change a travel decision. See native/cpp/LICENSE.cpython and Modules/mathmodule.c at
// v3.12.2.
inline double python_norm(const double* coordinates, std::size_t n) {
    double values[8];
    double maximum = 0.;
    bool infinite = false, nan = false;
    for (std::size_t i = 0; i < n; ++i) {
        values[i] = std::fabs(coordinates[i]);
        infinite = infinite || std::isinf(values[i]);
        nan = nan || std::isnan(values[i]);
        if (values[i] > maximum) maximum = values[i];
    }
    if (infinite) return INFINITY;
    if (nan) return NAN;
    if (maximum == 0.) return maximum;
    int exponent;
    std::frexp(maximum, &exponent);
    if (exponent < -1023) {
        double scaled[8];
        for (std::size_t i = 0; i < n; ++i) scaled[i] = values[i] / DBL_MIN;
        return DBL_MIN * python_norm(scaled, n);
    }
    double scale = std::ldexp(1., -exponent), sum = 1., frac1 = 0., frac2 = 0.;
    for (std::size_t i = 0; i < n; ++i) {
        double x = values[i] * scale;
        double product = x * x, product_error = std::fma(x, x, -product);
        double combined = sum + product, error = (sum - combined) + product;
        sum = combined;
        frac1 += product_error;
        frac2 += error;
    }
    double h = std::sqrt(sum - 1. + (frac1 + frac2));
    double product = -h * h, product_error = std::fma(-h, h, -product);
    double combined = sum + product, error = (sum - combined) + product;
    sum = combined;
    frac1 += product_error;
    frac2 += error;
    double correction = sum - 1. + (frac1 + frac2);
    h += correction / (2. * h);
    return h / scale;
}

inline double python_hypot(double a, double b) {  // math.hypot(a, b)
    const double values[2] = {a, b};
    return python_norm(values, 2);
}

// CPython 3.12 sum() over floats (started from the int 0): Neumaier-compensated, so it can differ from a
// plain left-to-right addition in the last bit.
inline double python_sum(std::initializer_list<double> values) {
    double result = 0., c = 0.;
    for (double x : values) {
        const double t = result + x;
        c += std::fabs(result) >= std::fabs(x) ? (result - t) + x : (x - t) + result;
        result = t;
    }
    if (c != 0. && std::isfinite(c)) result += c;
    return result;
}

template <class Range>
double python_sum(const Range& values) {
    double result = 0., c = 0.;
    for (double x : values) {
        const double t = result + x;
        c += std::fabs(result) >= std::fabs(x) ? (result - t) + x : (x - t) + result;
        result = t;
    }
    if (c != 0. && std::isfinite(c)) result += c;
    return result;
}

}  // namespace pkc
