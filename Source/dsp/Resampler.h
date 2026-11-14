#pragma once
// Streaming windowed-sinc sample-rate converter (push / pull).
// Output sample j corresponds exactly to input time j * (inRate / outRate);
// the only delay is the wait for future input (half the kernel width), so a
// pipeline  host -> up -> process -> down  is time aligned and its latency is
// whatever FIFO pre-fill the caller adds.

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace dsp
{

class Resampler
{
public:
    // timeOffset (in input samples) advances the output timing, e.g. to cancel a
    // known delay of the processing that happened before this converter.
    void prepare (double inRate, double outRate, int maxInputPerPush, double timeOffset = 0.0)
    {
        offset = timeOffset;
        step = inRate / outRate;
        cutoff = std::min (1.0, outRate / inRate) * 0.94;  // fraction of input Nyquist
        halfWidth = (int) std::ceil (kZeros / cutoff) + 1;
        buildTable();
        capacity = maxInputPerPush + 4 * halfWidth + 64 + (int) std::ceil (timeOffset);
        buf.assign ((size_t) capacity, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        count = halfWidth;          // history of zeros
        t = (double) halfWidth + offset;   // time of next output (buffer index)
    }

    int getHalfWidth() const { return halfWidth; }

    // Returns false if the input did not fit (caller must chunk smaller).
    bool push (const float* in, int n)
    {
        compact();
        if (count + n > capacity) return false;
        std::memcpy (buf.data() + count, in, sizeof (float) * (size_t) n);
        count += n;
        return true;
    }

    int pull (float* out, int maxOut)
    {
        int produced = 0;
        while (produced < maxOut)
        {
            const int ti = (int) std::floor (t);
            if (ti + halfWidth >= count) break;
            const double frac = t - ti;
            double acc = 0.0;
            const int i0 = ti - halfWidth + 1;
            const int i1 = ti + halfWidth;
            for (int i = i0; i <= i1; ++i)
            {
                const double u = std::abs ((double) (ti - i) + frac) * cutoff;
                acc += (double) buf[(size_t) i] * kernel (u);
            }
            out[produced++] = (float) (acc * cutoff);
            t += step;
        }
        return produced;
    }

private:
    static constexpr int kZeros = 16;     // zero crossings each side (at cutoff)
    static constexpr int kRes = 1024;     // table points per zero crossing
    static constexpr double kPi = 3.14159265358979323846;

    void buildTable()
    {
        table.assign ((size_t) (kZeros * kRes + 2), 0.0f);
        const double beta = 8.6;
        const double i0b = besselI0 (beta);
        for (int j = 0; j <= kZeros * kRes; ++j)
        {
            const double u = (double) j / kRes;
            const double s = u == 0.0 ? 1.0 : std::sin (kPi * u) / (kPi * u);
            const double r = u / kZeros;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            table[(size_t) j] = (float) (s * w);
        }
    }

    inline double kernel (double u) const
    {
        const double p = u * kRes;
        const int j = (int) p;
        if (j >= kZeros * kRes) return 0.0;
        const double f = p - j;
        return table[(size_t) j] + (table[(size_t) j + 1] - table[(size_t) j]) * f;
    }

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0, k = 1.0;
        const double q = x * x / 4.0;
        while (term > 1e-12 * sum) { term *= q / (k * k); sum += term; k += 1.0; }
        return sum;
    }

    void compact()
    {
        const int keepFrom = (int) std::floor (t) - halfWidth - 2;
        if (keepFrom <= 0) return;
        const int remain = count - keepFrom;
        std::memmove (buf.data(), buf.data() + keepFrom, sizeof (float) * (size_t) remain);
        count = remain;
        t -= keepFrom;
    }

    std::vector<float> buf, table;
    int capacity = 0, count = 0, halfWidth = 0;
    double t = 0.0, step = 1.0, cutoff = 1.0, offset = 0.0;
};

// Simple single-channel FIFO used to hand out exactly N samples per block.
class Fifo
{
public:
    void prepare (int cap) { data.assign ((size_t) cap, 0.0f); rd = wr = size = 0; }
    void clear() { rd = wr = size = 0; }
    int available() const { return size; }
    void pushZeros (int n) { for (int i = 0; i < n; ++i) pushOne (0.0f); }
    void push (const float* x, int n) { for (int i = 0; i < n; ++i) pushOne (x[i]); }
    int pop (float* out, int n)
    {
        int got = 0;
        for (; got < n && size > 0; ++got)
        {
            out[got] = data[(size_t) rd];
            rd = (rd + 1) % (int) data.size();
            --size;
        }
        for (int i = got; i < n; ++i) out[i] = 0.0f;   // underflow -> silence
        return got;
    }

private:
    void pushOne (float v)
    {
        if (size == (int) data.size()) return;          // overflow: drop (never expected)
        data[(size_t) wr] = v;
        wr = (wr + 1) % (int) data.size();
        ++size;
    }
    std::vector<float> data;
    int rd = 0, wr = 0, size = 0;
};

} // namespace dsp
