#include "ModelLibrary.h"
#include <BinaryData.h>

namespace
{
const char* const kResourceNames[ModelLibrary::kNumModels][2] = {
    { "nocomp_L_nam",   "nocomp_R_nam" },
    { "fast_r2_L_nam",  "fast_r2_R_nam" },
    { "fast_r4_L_nam",  "fast_r4_R_nam" },
    { "fast_r10_L_nam", "fast_r10_R_nam" },
    { "slow_r2_L_nam",  "slow_r2_R_nam" },
    { "slow_r4_L_nam",  "slow_r4_R_nam" },
    { "slow_r10_L_nam", "slow_r10_R_nam" },
};

bool readIntArray (const juce::var& v, std::vector<int>& out)
{
    auto* arr = v.getArray();
    if (arr == nullptr) return false;
    out.clear();
    for (auto& x : *arr) out.push_back ((int) x);
    return true;
}

bool isActive (const juce::var& v) { return v.isObject() && (bool) v.getProperty ("active", false); }

// Parses one plain WaveNet model object (architecture + config + weights).
bool parseWaveNet (const juce::var& model, nam::ModelData& out, juce::String& why)
{
    if (model.getProperty ("architecture", {}).toString() != "WaveNet") { why = "not a WaveNet"; return false; }
    const auto cfg = model.getProperty ("config", {});
    auto* layers = cfg.getProperty ("layers", {}).getArray();
    if (layers == nullptr || layers->size() != 1) { why = "expected one layer array"; return false; }
    const auto L = layers->getReference (0);

    if ((int) L.getProperty ("input_size", 0) != 1 || (int) L.getProperty ("condition_size", 0) != 1)
    { why = "unsupported input/condition size"; return false; }

    out.channels   = (int) L.getProperty ("channels", 0);
    out.bottleneck = (int) L.getProperty ("bottleneck", out.channels);
    if (! readIntArray (L.getProperty ("kernel_sizes", {}), out.kernelSizes)
        || ! readIntArray (L.getProperty ("dilations", {}), out.dilations))
    { why = "missing kernel sizes / dilations"; return false; }

    const auto head = L.getProperty ("head", {});
    out.headKernel = head.isObject() ? (int) head.getProperty ("kernel_size", 1) : 1;
    if (head.isObject() && (int) head.getProperty ("out_channels", 1) != 1) { why = "head must be mono"; return false; }
    if (head.isObject() && ! (bool) head.getProperty ("bias", true)) { why = "head without bias unsupported"; return false; }

    if (! isActive (L.getProperty ("layer1x1", {}))) { why = "layer1x1 inactive unsupported"; return false; }
    if (isActive (L.getProperty ("head1x1", {})))    { why = "head1x1 unsupported"; return false; }
    for (auto key : { "conv_pre_film", "conv_post_film", "input_mixin_pre_film", "input_mixin_post_film",
                      "activation_pre_film", "activation_post_film", "layer1x1_post_film", "head1x1_post_film" })
        if (isActive (L.getProperty (key, {}))) { why = "FiLM unsupported"; return false; }
    if ((int) L.getProperty ("groups_input", 1) != 1 || (int) L.getProperty ("groups_input_mixin", 1) != 1)
    { why = "grouped convs unsupported"; return false; }

    if (auto* gm = L.getProperty ("gating_mode", {}).getArray())
        for (auto& g : *gm)
            if (g.toString() != "none") { why = "gated layers unsupported"; return false; }

    if (auto* acts = L.getProperty ("activation", {}).getArray())
    {
        for (auto& a : *acts)
            if (a.getProperty ("type", {}).toString() != "LeakyReLU") { why = "unsupported activation"; return false; }
        if (! acts->isEmpty())
            out.slope = (float) (double) acts->getReference (0).getProperty ("negative_slope", 0.01);
    }

    auto* w = model.getProperty ("weights", {}).getArray();
    if (w == nullptr) { why = "no weights"; return false; }
    out.weights.resize ((size_t) w->size());
    for (int i = 0; i < w->size(); ++i) out.weights[(size_t) i] = (float) (double) w->getReference (i);

    std::string reason;
    if (! out.isValid (&reason)) { why = reason; return false; }
    return true;
}

bool parseFile (const char* resName, nam::ModelData& full, nam::ModelData& slim, juce::String& why)
{
    int size = 0;
    const char* bytes = BinaryData::getNamedResource (resName, size);
    if (bytes == nullptr || size <= 0) { why = juce::String ("missing resource ") + resName; return false; }

    const auto root = juce::JSON::parse (juce::String::fromUTF8 (bytes, size));
    const auto arch = root.getProperty ("architecture", {}).toString();

    if (arch == "WaveNet")
    {
        if (! parseWaveNet (root, full, why)) return false;
        slim = full;
        return true;
    }
    if (arch != "SlimmableContainer") { why = "unsupported architecture " + arch; return false; }

    auto* subs = root.getProperty ("config", {}).getProperty ("submodels", {}).getArray();
    if (subs == nullptr || subs->isEmpty()) { why = "no submodels"; return false; }

    std::vector<nam::ModelData> parsed;
    for (auto& s : *subs)
    {
        nam::ModelData m;
        juce::String r;
        if (parseWaveNet (s.getProperty ("model", {}), m, r)) parsed.push_back (std::move (m));
    }
    if (parsed.empty()) { why = "no usable submodel"; return false; }
    std::sort (parsed.begin(), parsed.end(), [] (const auto& a, const auto& b) { return a.channels < b.channels; });
    slim = parsed.front();
    full = parsed.back();
    return true;
}
} // namespace

ModelLibrary::ModelLibrary()
{
    ok = true;
    for (int m = 0; m < kNumModels; ++m)
        for (int side = 0; side < 2; ++side)
        {
            juce::String why;
            if (! parseFile (kResourceNames[m][side], data[m][side][0], data[m][side][1], why))
            {
                ok = false;
                error << kResourceNames[m][side] << ": " << why << "\n";
            }
        }
    if (! ok) DBG ("ModelLibrary: " << error);
}
