#include "det_math.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace rwe
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr double HalfPi = 1.57079632679489661923;
        constexpr double QuarterPi = 0.78539816339744830962;
        constexpr double SixthPi = 0.52359877559829887308;
        constexpr double Sqrt3 = 1.73205080756887729353;
        // tan(pi/12): above this the series is mapped down by a rotation of pi/6.
        constexpr double Tan15 = 0.26794919243112270647;

        // pi/2 split into pieces whose products with a small integer are
        // exact (fdlibm's PIO2_1, PIO2_2 and PIO2_2T), so that the reduced
        // argument keeps its accuracy.
        constexpr double Pio2First = 1.57079632673412561417e+00;
        constexpr double Pio2Second = 6.07710050630396597660e-11;
        constexpr double Pio2Third = 2.02226624879595063154e-21;
        constexpr double TwoOverPi = 6.36619772367581382433e-01;

        /** Past this the argument is brought down by fmod, which is exact. */
        constexpr double MaxDirectReduction = 1.0e8;

        constexpr double nan()
        {
            return std::numeric_limits<double>::quiet_NaN();
        }

        // Taylor coefficients, built at compile time with IEEE division.
        template <std::size_t N>
        constexpr std::array<double, N> sinCoefficients()
        {
            // 1/(2n+1)! with alternating sign, n = 0..N-1
            std::array<double, N> c{};
            double factorial = 1.0;
            for (std::size_t n = 0; n < N; ++n)
            {
                if (n > 0)
                {
                    factorial *= static_cast<double>((2 * n) * (2 * n + 1));
                }
                c[n] = ((n % 2 == 0) ? 1.0 : -1.0) / factorial;
            }
            return c;
        }

        template <std::size_t N>
        constexpr std::array<double, N> cosCoefficients()
        {
            // 1/(2n)! with alternating sign
            std::array<double, N> c{};
            double factorial = 1.0;
            for (std::size_t n = 0; n < N; ++n)
            {
                if (n > 0)
                {
                    factorial *= static_cast<double>((2 * n - 1) * (2 * n));
                }
                c[n] = ((n % 2 == 0) ? 1.0 : -1.0) / factorial;
            }
            return c;
        }

        template <std::size_t N>
        constexpr std::array<double, N> atanCoefficients()
        {
            // (-1)^n / (2n+1)
            std::array<double, N> c{};
            for (std::size_t n = 0; n < N; ++n)
            {
                c[n] = ((n % 2 == 0) ? 1.0 : -1.0) / static_cast<double>(2 * n + 1);
            }
            return c;
        }

        // |r| <= pi/4: the last term kept is under 1e-19.
        constexpr auto SinTerms = sinCoefficients<10>();
        constexpr auto CosTerms = cosCoefficients<10>();
        // t <= tan(pi/12): 0.268^(2n+1)/(2n+1) is under 1e-18 by n = 16.
        constexpr auto AtanTerms = atanCoefficients<17>();

        double sinKernel(double r)
        {
            const double r2 = r * r;
            double s = 0.0;
            for (std::size_t i = SinTerms.size(); i-- > 0;)
            {
                s = s * r2 + SinTerms[i];
            }
            return r * s;
        }

        double cosKernel(double r)
        {
            const double r2 = r * r;
            double s = 0.0;
            for (std::size_t i = CosTerms.size(); i-- > 0;)
            {
                s = s * r2 + CosTerms[i];
            }
            return s;
        }

        /** x brought into [-pi/4, pi/4]; `quadrant` is how many quarter turns were removed, mod 4. */
        double reduce(double x, int& quadrant)
        {
            if (std::fabs(x) > MaxDirectReduction)
            {
                x = std::fmod(x, 2.0 * Pi);
            }
            const double k = std::floor(x * TwoOverPi + 0.5);
            const double r = ((x - k * Pio2First) - k * Pio2Second) - k * Pio2Third;
            quadrant = static_cast<int>(static_cast<std::int64_t>(k) & 3);
            return r;
        }

        double sinDouble(double x)
        {
            if (!std::isfinite(x))
            {
                return nan();
            }
            int quadrant = 0;
            const double r = reduce(x, quadrant);
            switch (quadrant)
            {
                case 0:
                    return sinKernel(r);
                case 1:
                    return cosKernel(r);
                case 2:
                    return -sinKernel(r);
                default:
                    return -cosKernel(r);
            }
        }

        double cosDouble(double x)
        {
            if (!std::isfinite(x))
            {
                return nan();
            }
            int quadrant = 0;
            const double r = reduce(x, quadrant);
            switch (quadrant)
            {
                case 0:
                    return cosKernel(r);
                case 1:
                    return -sinKernel(r);
                case 2:
                    return -cosKernel(r);
                default:
                    return sinKernel(r);
            }
        }

        /** atan of a non-negative finite number. */
        double atanNonNegative(double v)
        {
            const bool inverted = v > 1.0;
            double t = inverted ? 1.0 / v : v;

            double base = 0.0;
            if (t > Tan15)
            {
                t = (t * Sqrt3 - 1.0) / (Sqrt3 + t);
                base = SixthPi;
            }

            const double t2 = t * t;
            double s = 0.0;
            for (std::size_t i = AtanTerms.size(); i-- > 0;)
            {
                s = s * t2 + AtanTerms[i];
            }
            const double result = base + t * s;
            return inverted ? HalfPi - result : result;
        }

        double atan2Double(double y, double x)
        {
            if (std::isnan(x) || std::isnan(y))
            {
                return nan();
            }

            const bool yNegative = std::signbit(y);
            const bool xNegative = std::signbit(x);

            if (y == 0.0)
            {
                if (!xNegative)
                {
                    return y; // +-0
                }
                return yNegative ? -Pi : Pi;
            }

            if (x == 0.0)
            {
                return yNegative ? -HalfPi : HalfPi;
            }

            const bool xInfinite = std::isinf(x);
            const bool yInfinite = std::isinf(y);
            if (yInfinite)
            {
                double magnitude;
                if (!xInfinite)
                {
                    magnitude = HalfPi;
                }
                else
                {
                    magnitude = xNegative ? 3.0 * QuarterPi : QuarterPi;
                }
                return yNegative ? -magnitude : magnitude;
            }
            if (xInfinite)
            {
                const double magnitude = xNegative ? Pi : 0.0;
                return yNegative ? -magnitude : magnitude;
            }

            const double ratio = std::fabs(y) / std::fabs(x);
            const double angle = atanNonNegative(ratio);
            const double magnitude = xNegative ? Pi - angle : angle;
            return yNegative ? -magnitude : magnitude;
        }
    }

    float detSin(float x)
    {
        return static_cast<float>(sinDouble(static_cast<double>(x)));
    }

    float detCos(float x)
    {
        return static_cast<float>(cosDouble(static_cast<double>(x)));
    }

    float detAtan(float x)
    {
        const double v = static_cast<double>(x);
        if (std::isnan(v))
        {
            return x;
        }
        if (v == 0.0)
        {
            return x;
        }
        const double magnitude = std::isinf(v) ? HalfPi : atanNonNegative(std::fabs(v));
        return static_cast<float>(v < 0.0 ? -magnitude : magnitude);
    }

    float detAtan2(float y, float x)
    {
        return static_cast<float>(atan2Double(static_cast<double>(y), static_cast<double>(x)));
    }

    float detAcos(float x)
    {
        const double v = static_cast<double>(x);
        if (!(v >= -1.0 && v <= 1.0))
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        // (1 - v)(1 + v) is exact in double for a float v.
        const double sine = std::sqrt((1.0 - v) * (1.0 + v));
        return static_cast<float>(atan2Double(sine, v));
    }

    float detHypot(float a, float b)
    {
        const double da = static_cast<double>(a);
        const double db = static_cast<double>(b);
        if (std::isinf(da) || std::isinf(db))
        {
            return std::numeric_limits<float>::infinity();
        }
        if (std::isnan(da) || std::isnan(db))
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        // The squares are exact in double; the sum and the root are each
        // correctly rounded, so the answer is the same everywhere.
        return static_cast<float>(std::sqrt(da * da + db * db));
    }
}
