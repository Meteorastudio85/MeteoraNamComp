#pragma once
#include <juce_core/juce_core.h>
#include "dsp/NamWaveNet.h"

// Parses the 14 embedded .nam captures once per process (shared between all
// plugin instances through juce::SharedResourcePointer).
//
// Model index: 0 = NoCompression (colour)
//              1..3 = A-10 / R-Auto,  ratio 2 / 4 / 10   ("fast")
//              4..6 = A-30 / R-100,   ratio 2 / 4 / 10   ("slow")
class ModelLibrary
{
public:
    static constexpr int kNumModels = 7;

    ModelLibrary();

    // side: 0 = L, 1 = R.  eco: true = light 3-channel network
    const nam::ModelData& get (int model, int side, bool eco) const { return data[model][side][eco ? 1 : 0]; }
    bool isOk() const { return ok; }
    const juce::String& getError() const { return error; }

    static int modelIndex (bool capture, bool autoRelease, int ratioIndex)
    {
        if (! capture) return 0;
        return (autoRelease ? 1 : 4) + juce::jlimit (0, 2, ratioIndex);
    }

private:
    nam::ModelData data[kNumModels][2][2];
    bool ok = false;
    juce::String error;
};
