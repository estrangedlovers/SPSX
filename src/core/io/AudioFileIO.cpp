#include "AudioFileIO.h"
#include "../dsp/SincResampler.h"
#include <cmath>

namespace spsx::io
{
    void requantize16 (juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* x = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float q = std::nearbyint (x[i] * 32768.0f);
                x[i] = juce::jlimit (-32768.0f, 32767.0f, q) / 32768.0f;
            }
        }
    }

    juce::AudioBuffer<float> toEngineRate (const juce::AudioBuffer<float>& in, double inRate)
    {
        const int channels = juce::jmin (2, in.getNumChannels());

        if (std::abs (inRate - kEngineRate) < 1e-6)
        {
            juce::AudioBuffer<float> out (channels, in.getNumSamples());
            for (int ch = 0; ch < channels; ++ch)
                out.copyFrom (ch, 0, in, ch, 0, in.getNumSamples());
            return out;
        }

        constexpr int chunk = 4096;
        dsp::SincResampler rs;
        rs.prepare (channels, inRate, kEngineRate, chunk);

        const int expected = (int) std::llround (in.getNumSamples() * kEngineRate / inRate);
        juce::AudioBuffer<float> out (channels, expected + 8);
        out.clear();

        const int latency = rs.latencyInInputSamples();
        juce::AudioBuffer<float> zeros (channels, chunk);
        zeros.clear();

        int written = 0, readPos = 0;
        const int totalIn = in.getNumSamples() + latency;   // flush with zeros to emit the tail

        while (readPos < totalIn && written < expected)
        {
            const int n = juce::jmin (chunk, totalIn - readPos);
            const float* src[2];
            juce::AudioBuffer<float> tmp (channels, n);
            for (int ch = 0; ch < channels; ++ch)
            {
                for (int i = 0; i < n; ++i)
                {
                    const int k = readPos + i;
                    tmp.setSample (ch, i, k < in.getNumSamples() ? in.getSample (ch, k) : 0.0f);
                }
                src[ch] = tmp.getReadPointer (ch);
            }
            rs.push (src, n);
            readPos += n;

            float* dst[2];
            for (int ch = 0; ch < channels; ++ch)
                dst[ch] = out.getWritePointer (ch) + written;
            written += rs.pull (dst, juce::jmin (rs.available(), expected - written));
        }

        out.setSize (channels, expected, true);
        return out;
    }

    /** Imported audio is untrusted: NaN / Inf become silence and float files are capped
        at +18 dBFS so a corrupt file cannot drive the effects into overflow. */
    static bool needsSanitising (const juce::AudioBuffer<float>& b) noexcept
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* x = b.getReadPointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! (std::abs (x[i]) <= kMaxImportLevel))
                    return true;
        }
        return false;
    }

    void sanitise (juce::AudioBuffer<float>& b) noexcept
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            float* x = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                x[i] = std::isfinite (x[i]) ? std::clamp (x[i], -kMaxImportLevel, kMaxImportLevel) : 0.0f;
        }
    }

    SampleData::Ptr makeSample (const juce::AudioBuffer<float>& audio, double rate, bool quantize16)
    {
        if (needsSanitising (audio))
        {
            juce::AudioBuffer<float> clean (audio);
            sanitise (clean);
            return makeSample (clean, rate, quantize16);
        }
        auto converted = toEngineRate (audio, rate);
        if (quantize16)
            requantize16 (converted);
        return new SampleData (std::move (converted), quantize16);
    }

    LoadResult loadFile (const juce::File& file, bool quantize16)
    {
        LoadResult r;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            r.code = "UnS";
            r.error = "Unsupported or unreadable audio file: " + file.getFullPathName();
            return r;
        }

        const auto channels = (int) juce::jmin<unsigned int> (2, reader->numChannels);
        if ((juce::int64) reader->lengthInSamples * channels * 2 > kMaxSampleBytes)
        {
            r.code = "FUL";
            r.error = "File exceeds the 2 GB per-sample limit";
            return r;
        }
        if (reader->lengthInSamples <= 0 || channels <= 0)
        {
            r.code = "UnS";
            r.error = "Empty audio file";
            return r;
        }

        juce::AudioBuffer<float> audio (channels, (int) reader->lengthInSamples);
        reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, channels > 1);
        r.sample = makeSample (audio, reader->sampleRate, quantize16);
        return r;
    }

    juce::Result writeFile (const juce::File& file, const SampleData& sample, FileType type, int bitDepth)
    {
        std::unique_ptr<juce::AudioFormat> format;
        if (type == FileType::wav) format = std::make_unique<juce::WavAudioFormat>();
        else                       format = std::make_unique<juce::AiffAudioFormat>();

        file.deleteFile();
        auto stream = file.createOutputStream();
        if (stream == nullptr)
            return juce::Result::fail ("Cannot write " + file.getFullPathName());

        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (kEngineRate)
                                 .withNumChannels (sample.numChannels())
                                 .withBitsPerSample (bitDepth);

        std::unique_ptr<juce::OutputStream> os (stream.release());
        auto writer = format->createWriterFor (os, options);
        if (writer == nullptr)
            return juce::Result::fail ("Cannot create writer for " + file.getFullPathName());

        // JUCE's float path scales by 2^31 - 1 and truncates, which drops one LSB on
        // positive samples. Write left-justified integers ourselves so 16-bit data on
        // the hardware grid round-trips bit for bit.
        const int channels = sample.numChannels();
        const double scale = bitDepth >= 24 ? 8388608.0 : 32768.0;
        const int shift = bitDepth >= 24 ? 8 : 16;
        const int maxCode = (int) scale - 1;
        constexpr int chunk = 8192;

        std::vector<std::vector<int>> ints ((size_t) channels, std::vector<int> ((size_t) chunk));
        std::vector<const int*> ptrs ((size_t) channels + 1, nullptr);

        for (SampleIndex pos = 0; pos < sample.length(); pos += chunk)
        {
            const int n = (int) juce::jmin<SampleIndex> (chunk, sample.length() - pos);
            for (int ch = 0; ch < channels; ++ch)
            {
                const float* x = sample.channel (ch) + pos;
                for (int i = 0; i < n; ++i)
                {
                    const auto code = juce::jlimit (-(int) scale, maxCode, (int) std::lrint (x[i] * scale));
                    ints[(size_t) ch][(size_t) i] = (int) ((unsigned int) code << shift);
                }
                ptrs[(size_t) ch] = ints[(size_t) ch].data();
            }
            if (! writer->write (ptrs.data(), n))
                return juce::Result::fail ("Write failed: " + file.getFullPathName());
        }
        return juce::Result::ok();
    }
}
