#pragma once

#include "../state/SampleData.h"

namespace spsx
{
    /** Extended-mode Time Modify: renders the playback region at a new length with
        signalsmith-stretch (MIT). Offline and allocating: run it on a worker thread.
        speed = source read speed (target BPM / original BPM); output length = region / speed. */
    SampleData::Ptr renderStretched (const SampleData& sample, SampleIndex start, SampleIndex end, double speed);
}
