#include <catch2/catch_test_macros.hpp>
#include "core/CancelDetector.h"
#include "core/dsp/LoFi.h"
#include <vector>
#include <cmath>

using namespace spsx;

TEST_CASE ("CANCEL x4 within the window stops all; slow presses do not", "[cancel]")
{
    CancelDetector d;
    REQUIRE_FALSE (d.press (0.0));
    REQUIRE_FALSE (d.press (0.2));
    REQUIRE_FALSE (d.press (0.4));
    REQUIRE (d.press (0.6));

    CancelDetector slow;
    for (int i = 0; i < 8; ++i)
        REQUIRE_FALSE (slow.press (i * 0.5));
}

TEST_CASE ("LO-FI holds each value for holdFactor samples before smoothing", "[lofi]")
{
    dsp::LoFi lf;
    dsp::LoFi::Params p;
    p.lowpassHz = 20000.0f * 100.0f;   // effectively bypass the smoothing for this check
    lf.prepare (44100.0, p);

    std::vector<float> x (16);
    for (int i = 0; i < 16; ++i)
        x[(size_t) i] = (float) i / 16.0f;
    float* ch[1] = { x.data() };
    lf.process (ch, 1, 16);
    REQUIRE (x[1] == x[0]);
    REQUIRE (x[3] == x[0]);
    REQUIRE (x[4] != x[3]);
}

TEST_CASE ("LO-FI produces no NaN/Inf on extreme input", "[lofi]")
{
    dsp::LoFi lf;
    lf.prepare (44100.0, {});
    std::vector<float> x (1024);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = (i % 2 ? 1.0f : -1.0f) * 4.0f;
    float* ch[1] = { x.data() };
    lf.process (ch, 1, 1024);
    for (float v : x)
        REQUIRE (std::isfinite (v));
}
