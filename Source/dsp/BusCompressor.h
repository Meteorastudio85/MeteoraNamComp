#pragma once
// SSL-style stereo bus compressor gain computer (feed-forward VCA model).
// Linked stereo detector, soft knee, attack/release smoothing in the dB domain
// and a program-dependent "Auto" release (fast + slow dual envelope).

#include <cmath>
#include <algorithm>

namespace dsp
{

class BusCompressor
{
public:
    static constexpr float kAttackMs[6]  = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
    static constexpr float kReleaseS[4]  = { 0.1f, 0.3f, 0.6f, 1.2f };   // index 4 = Auto
    static constexpr float kRatios[3]    = { 2.0f, 4.0f, 10.0f };

    void prepare (double sampleRate) { sr = sampleRate; reset(); update(); }
    void reset() { envFast = envSlow = 0.0f; }

    void setParameters (float thresholdDb, int ratioIndex, int attackIndex, int releaseIndex)
    {
        thrDb = thresholdDb;
        ratio = kRatios[std::clamp (ratioIndex, 0, 2)];
        attackMs = kAttackMs[std::clamp (attackIndex, 0, 5)];
        autoRelease = releaseIndex >= 4;
        releaseS = autoRelease ? 0.1f : kReleaseS[std::clamp (releaseIndex, 0, 3)];
        update();
    }

    // detector = max(|L|,|R|) of the current sample. Returns gain change in dB (<= 0).
    inline float process (float detector)
    {
        const float x = 20.0f * std::log10 (std::max (detector, 1.0e-6f));
        const float target = staticCurve (x);

        if (target < envFast) envFast += (target - envFast) * aAtt;
        else                  envFast += (target - envFast) * aRel;

        if (! autoRelease) return envFast;

        // Slow envelope follows sustained compression and releases slowly; the
        // output takes whichever envelope asks for more reduction, so short
        // transients recover quickly and long passages recover gently.
        if (envFast < envSlow) envSlow += (envFast - envSlow) * aSlowAtt;
        else                   envSlow += (envFast - envSlow) * aSlowRel;
        return std::min (envFast, envSlow);
    }

    float staticCurve (float xDb) const
    {
        const float over = xDb - thrDb;
        const float slope = 1.0f / ratio - 1.0f;
        if (2.0f * over < -kKnee) return 0.0f;
        if (2.0f * std::abs (over) <= kKnee)
        {
            const float o = over + kKnee * 0.5f;
            return slope * o * o / (2.0f * kKnee);
        }
        return slope * over;
    }

private:
    static constexpr float kKnee = 6.0f;

    float coef (float seconds) const
    {
        return 1.0f - std::exp (-1.0f / (std::max (seconds, 1.0e-5f) * (float) sr));
    }

    void update()
    {
        aAtt = coef (attackMs * 0.001f);
        aRel = coef (releaseS);
        aSlowAtt = coef (0.45f);
        aSlowRel = coef (1.6f);
    }

    double sr = 48000.0;
    float thrDb = -12.0f, ratio = 4.0f, attackMs = 3.0f, releaseS = 0.1f;
    bool autoRelease = true;
    float aAtt = 0, aRel = 0, aSlowAtt = 0, aSlowRel = 0;
    float envFast = 0.0f, envSlow = 0.0f;
};

} // namespace dsp
