#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

namespace spsx
{
    /** Lock-free single-producer/single-consumer audio FIFO between the audio thread
        (writes recorded frames) and the disk-writer thread (drains them to a file).
        Sized for several seconds so disk hiccups never drop audio. */
    class RecordTap
    {
    public:
        RecordTap (int numChannels, int capacityFrames)
            : channels (juce::jlimit (1, 2, numChannels)), fifo (capacityFrames)
        {
            for (auto& b : buf)
                b.assign ((size_t) capacityFrames, 0.0f);
        }

        int numChannels() const noexcept { return channels; }

        /** Audio thread. Returns false (and flags overflow) if the writer fell behind. */
        bool write (const float* l, const float* r, int n) noexcept
        {
            int s1, n1, s2, n2;
            fifo.prepareToWrite (n, s1, n1, s2, n2);
            if (n1 + n2 < n)
            {
                overflowed.store (true);
                n = n1 + n2;
            }
            std::copy (l, l + n1, buf[0].begin() + s1);
            std::copy (l + n1, l + n1 + n2, buf[0].begin() + s2);
            if (channels > 1)
            {
                std::copy (r, r + n1, buf[1].begin() + s1);
                std::copy (r + n1, r + n1 + n2, buf[1].begin() + s2);
            }
            fifo.finishedWrite (n1 + n2);
            framesWritten.fetch_add (n1 + n2);
            return ! overflowed.load();
        }

        /** Writer thread. */
        int read (float* l, float* r, int maxFrames) noexcept
        {
            int s1, n1, s2, n2;
            fifo.prepareToRead (maxFrames, s1, n1, s2, n2);
            std::copy (buf[0].begin() + s1, buf[0].begin() + s1 + n1, l);
            std::copy (buf[0].begin() + s2, buf[0].begin() + s2 + n2, l + n1);
            if (channels > 1)
            {
                std::copy (buf[1].begin() + s1, buf[1].begin() + s1 + n1, r);
                std::copy (buf[1].begin() + s2, buf[1].begin() + s2 + n2, r + n1);
            }
            fifo.finishedRead (n1 + n2);
            return n1 + n2;
        }

        int available() const noexcept { return fifo.getNumReady(); }

        std::atomic<bool> overflowed { false };
        std::atomic<juce::int64> framesWritten { 0 };

    private:
        int channels;
        juce::AbstractFifo fifo;
        std::array<std::vector<float>, 2> buf;
    };
}
