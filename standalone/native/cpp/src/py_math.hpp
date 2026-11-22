// CPython math-module functions whose results differ from the obvious libm
// expression in the last bit.
#pragma once

#include <cfloat>
#include <cmath>

namespace pkc {

inline double radians(double degrees) { return degrees * (M_PI / 180.0); }  // math.radians
inline double degrees(double radians) { return radians * (180.0 / M_PI); }  // math.degrees

// math.hypot(x, y): CPython's vector_norm (Modules/mathmodule.c), not libm hypot.
inline double hypot(double x, double y) {
    double max = std::fmax(std::fabs(x), std::fabs(y));
    if (std::isinf(x) || std::isinf(y)) return INFINITY;
    if (std::isnan(x) || std::isnan(y)) return NAN;
    if (max == 0.0) return max;
    int max_e;
    std::frexp(max, &max_e);
    if (max_e < -1023) return DBL_MIN * hypot(x / DBL_MIN, y / DBL_MIN);
    const double scale = std::ldexp(1.0, -max_e);
    double csum = 1.0, frac1 = 0.0, frac2 = 0.0;
    auto mul = [](double a, double b, double& lo) {  // dl_mul: lossless product
        double z = a * b;
        lo = std::fma(a, b, -z);
        return z;
    };
    auto fast_sum = [](double a, double b, double& lo) {  // dl_fast_sum: |a| >= |b|
        double sum = a + b;
        lo = b - (sum - a);
        return sum;
    };
    for (double v : {x, y}) {
        double lo, sum_lo;
        double hi = mul(v * scale, v * scale, lo);
        csum = fast_sum(csum, hi, sum_lo);
        frac1 += lo;
        frac2 += sum_lo;
    }
    double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
    double lo, sum_lo;
    double hi = mul(-h, h, lo);
    csum = fast_sum(csum, hi, sum_lo);
    frac1 += lo;
    frac2 += sum_lo;
    double correction = csum - 1.0 + (frac1 + frac2);
    h += correction / (2.0 * h);  // differential correction
    return h / scale;
}

}  // namespace pkc
