#pragma once
// Click-free switching between NAM engines.
// A newly selected engine first runs in parallel for its receptive field
// (so its internal history is filled with real audio), then the output
// crossfades from the old engine to the new one.

#include "NamWaveNet.h"
#include <vector>
#include <cmath>

namespace dsp
{

class ModelSwitcher
{
public:
    void prepare (int maxBlock)
    {
        tmpA.assign ((size_t) maxBlock, 0.0f);
        tmpB.assign ((size_t) maxBlock, 0.0f);
        active = pending = nullptr;
        activeId = pendingId = -1;
    }

    void setEngine (int id, nam::Engine* e)
    {
        if (e == nullptr) return;
        if (active == nullptr)
        {
            active = e; activeId = id; active->reset();
            return;
        }
        if (id == activeId) { pending = nullptr; pendingId = -1; return; }   // cancel
        if (id == pendingId) return;
        pending = e; pendingId = id; pending->reset();
        warm = 0; fade = 0;
        warmLen = e->receptiveField() + 64;
    }

    int getActiveId() const { return activeId; }

    // n must be <= maxBlock passed to prepare()
    void process (const float* in, float* out, int n)
    {
        if (active == nullptr) { for (int i = 0; i < n; ++i) out[i] = in[i]; return; }
        if (pending == nullptr) { active->process (in, out, n); return; }

        active->process (in, tmpA.data(), n);
        pending->process (in, tmpB.data(), n);
        for (int i = 0; i < n; ++i)
        {
            if (warm < warmLen) { ++warm; out[i] = tmpA[(size_t) i]; continue; }
            const float g = (float) fade / (float) kFade;
            // equal-power is not needed: both engines are highly correlated
            out[i] = tmpA[(size_t) i] * (1.0f - g) + tmpB[(size_t) i] * g;
            if (fade < kFade) ++fade;
        }
        if (warm >= warmLen && fade >= kFade)
        {
            active = pending; activeId = pendingId;
            pending = nullptr; pendingId = -1;
        }
    }

private:
    static constexpr int kFade = 1024;
    nam::Engine* active = nullptr;
    nam::Engine* pending = nullptr;
    int activeId = -1, pendingId = -1;
    int warm = 0, warmLen = 0, fade = 0;
    std::vector<float> tmpA, tmpB;
};

} // namespace dsp
