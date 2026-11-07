#pragma once

namespace spsx::rt
{
    /** Marks code that must not allocate. Executables that want enforcement (unit
        tests, render_cli) replace global operator new and assert when
        `inRealtimeSection()` is true; see tests/AllocGuard.cpp. Plugins do not replace
        operator new because that would leak into the host process. */
    inline thread_local int realtimeDepth = 0;

    struct ScopedRealtime
    {
        ScopedRealtime() noexcept  { ++realtimeDepth; }
        ~ScopedRealtime() noexcept { --realtimeDepth; }
    };

    inline bool inRealtimeSection() noexcept { return realtimeDepth > 0; }

    /** Temporarily lift the guard (only for test scaffolding). */
    struct ScopedAllowAllocation
    {
        int saved;
        ScopedAllowAllocation() noexcept : saved (realtimeDepth) { realtimeDepth = 0; }
        ~ScopedAllowAllocation() noexcept { realtimeDepth = saved; }
    };
}
