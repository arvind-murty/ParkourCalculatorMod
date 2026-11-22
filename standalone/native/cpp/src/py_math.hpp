// CPython math-module functions whose results differ from the obvious libm
// expression in the last bit.
#pragma once

#include <cfloat>
#include <cmath>

namespace pkc {

inline double radians(double degrees) { return degrees * (M_PI / 180.0); }  // math.radians
inline double degrees(double radians) { return radians * (180.0 / M_PI); }  // math.degrees

// CPython 3.12.2 math.hypot (vector_norm with compensated rounding) for two
// coordinates; libm hypot can differ by one ULP and change a travel decision.
// See native/cpp/LICENSE.cpython and Modules/mathmodule.c at v3.12.2.
inline double python_hypot(double a, double b) {
    double values[2] = {std::fabs(a), std::fabs(b)};
    double maximum = values[1] > values[0] ? values[1] : values[0];
    if (std::isinf(values[0]) || std::isinf(values[1])) return INFINITY;
    if (std::isnan(values[0]) || std::isnan(values[1])) return NAN;
    if (maximum == 0.) return maximum;
    int exponent;
    std::frexp(maximum, &exponent);
    if (exponent < -1023) return DBL_MIN * python_hypot(values[0] / DBL_MIN, values[1] / DBL_MIN);
    double scale = std::ldexp(1., -exponent), sum = 1., frac1 = 0., frac2 = 0.;
    for (double value : values) {
        double x = value * scale;
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

}  // namespace pkc
