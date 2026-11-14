#pragma once
// One audio channel through a NAM model that was trained at 48 kHz:
//   host rate -> 48 kHz -> (model switcher) -> host rate
// The captures themselves delay the signal by kModelDelay samples (at 48 kHz).
// getLatency() is the total delay of the wet path; the processor delays the
// dry path by the same amount so the Mix control never comb-filters.
//  - host at 48 kHz : no resampling, latency = kModelDelay
//  - other rates    : the output converter's timing is advanced by kModelDelay
//                     (exact, also fractional) and a FIFO adds a fixed latency.

#include "Resampler.h"
#include "ModelSwitcher.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace dsp
{

class NamStage
{
public:
    static constexpr double kModelRate = 48000.0;
    static constexpr int kModelDelay = 16;      // measured on all 14 captures

    void prepare (double hostRate, int maxBlock)
    {
        maxHostBlock = std::max (1, maxBlock);
        resampling = std::abs (hostRate - kModelRate) > 0.5;
        if (resampling)
        {
            up.prepare (hostRate, kModelRate, maxHostBlock);
            down.prepare (kModelRate, hostRate, (int) std::ceil (maxHostBlock * kModelRate / hostRate) + 64, (double) kModelDelay);
            max48 = (int) std::ceil (maxHostBlock * kModelRate / hostRate) + 64;
            latency = up.getHalfWidth() + (int) std::ceil ((down.getHalfWidth() + kModelDelay) * hostRate / kModelRate) + 4;
            fifo.prepare (latency + 4 * maxHostBlock + 256);
            buf48.assign ((size_t) max48, 0.0f);
            back.assign ((size_t) maxHostBlock + 256, 0.0f);
        }
        else
        {
            max48 = maxHostBlock;
            latency = kModelDelay;
            buf48.assign ((size_t) max48, 0.0f);
        }
        switcher.prepare (max48);
        reset();
    }

    void reset()
    {
        if (resampling)
        {
            up.reset(); down.reset(); fifo.clear(); fifo.pushZeros (latency);
        }
    }

    int getLatency() const { return latency; }
    ModelSwitcher& getSwitcher() { return switcher; }

    // n <= maxBlock given to prepare()
    void process (const float* in, float* out, int n)
    {
        if (! resampling)
        {
            switcher.process (in, out, n);
            return;
        }
        up.push (in, n);
        const int m = up.pull (buf48.data(), max48);
        switcher.process (buf48.data(), buf48.data(), m);
        down.push (buf48.data(), m);
        int k;
        while ((k = down.pull (back.data(), (int) back.size())) > 0)
            fifo.push (back.data(), k);
        fifo.pop (out, n);
    }

private:
    bool resampling = false;
    int maxHostBlock = 512, max48 = 512, latency = 0;
    Resampler up, down;
    Fifo fifo;
    std::vector<float> buf48, back;
    ModelSwitcher switcher;
};

} // namespace dsp
