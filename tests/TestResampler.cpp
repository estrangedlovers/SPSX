#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "core/dsp/SincResampler.h"
#include <cmath>
#include <vector>

using spsx::dsp::SincResampler;

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    std::vector<float> run (double inRate, double outRate, const std::vector<float>& input)
    {
        SincResampler rs;
        rs.prepare (1, inRate, outRate, 256);
        std::vector<float> out;
        std::vector<float> buf (4096);
        for (size_t pos = 0; pos < input.size(); pos += 256)
        {
            const int n = (int) std::min<size_t> (256, input.size() - pos);
            const float* in[1] = { input.data() + pos };
            rs.push (in, n);
            float* o[1] = { buf.data() };
            const int got = rs.pull (o, rs.available());
            out.insert (out.end(), buf.begin(), buf.begin() + got);
        }
        return out;
    }

    std::vector<float> sine (double hz, double rate, int n, double amp = 0.5)
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = (float) (amp * std::sin (2.0 * kPi * hz * i / rate));
        return x;
    }

    double rms (const std::vector<float>& x, size_t from)
    {
        double s = 0.0;
        for (size_t i = from; i < x.size(); ++i)
            s += x[i] * (double) x[i];
        return std::sqrt (s / (double) (x.size() - from));
    }
}

TEST_CASE ("Resampler keeps DC exactly", "[resampler]")
{
    std::vector<float> x (20000, 0.5f);
    auto y = run (48000.0, 44100.0, x);
    REQUIRE (y.size() > 10000);
    for (size_t i = 1000; i < y.size(); ++i)
        REQUIRE (std::abs (y[i] - 0.5f) < 1e-5f);
}

TEST_CASE ("Resampler: passband tone is reproduced at the mapped time", "[resampler]")
{
    const auto rates = GENERATE (std::pair { 48000.0, 44100.0 }, std::pair { 44100.0, 48000.0 },
                                 std::pair { 44100.0, 96000.0 }, std::pair { 96000.0, 44100.0 });
    const double hz = 10000.0;
    auto x = sine (hz, rates.first, 40000);
    auto y = run (rates.first, rates.second, x);

    // Output k corresponds to input time k * step exactly (zero time offset).
    double maxErr = 0.0;
    for (size_t k = 2000; k < y.size(); ++k)
    {
        const double ideal = 0.5 * std::sin (2.0 * kPi * hz * (double) k / rates.second);
        maxErr = std::max (maxErr, std::abs (y[k] - ideal));
    }
    REQUIRE (maxErr < 5e-4);   // better than -60 dB re 0.5 at 10 kHz
}

TEST_CASE ("Resampler: content above the output Nyquist is rejected", "[resampler]")
{
    // 48k -> 44.1k: a 23 kHz tone would alias to 21.1 kHz.
    auto x = sine (23000.0, 48000.0, 48000);
    auto y = run (48000.0, 44100.0, x);
    const double level = rms (y, 2000) / (0.5 / std::sqrt (2.0));
    REQUIRE (20.0 * std::log10 (level + 1e-12) < -70.0);
}

TEST_CASE ("Resampler: output length tracks the rate ratio", "[resampler]")
{
    std::vector<float> x (48000, 0.0f);
    auto y = run (48000.0, 44100.0, x);
    SincResampler rs;
    rs.prepare (1, 48000.0, 44100.0, 256);
    const int lat = rs.latencyInInputSamples();
    const double expected = (48000.0 - lat) * 44100.0 / 48000.0;
    REQUIRE (std::abs ((double) y.size() - expected) < 3.0);
}
