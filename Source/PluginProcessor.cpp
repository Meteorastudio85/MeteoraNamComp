#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr float kThresholdRef = -12.0f;   // knob 0 dB  ->  -12 dBFS threshold

inline float dbToGain (float db) { return std::exp (db * 0.11512925464970229f); }
}

juce::AudioProcessorValueTreeState::ParameterLayout MeteoraNamCompressorProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto dbText = [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; };

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::threshold, 1 }, "Threshold",
                                                  NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::ratio, 1 }, "Ratio",
                                                   StringArray { "2:1", "4:1", "10:1" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::attack, 1 }, "Attack",
                                                   StringArray { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, 3));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::release, 1 }, "Release",
                                                   StringArray { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" }, 4));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::makeup, 1 }, "Make-Up Gain",
                                                  NormalisableRange<float> (0.0f, 15.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::mix, 1 }, "Mix",
                                                  NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                                  AudioParameterFloatAttributes().withLabel ("%")
                                                      .withStringFromValueFunction ([] (float v, int) { return String ((int) std::round (v)) + " %"; })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::power, 1 }, "Power", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::vuMode, 1 }, "Meter: Output VU", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::capture, 1 }, "Capture Mode", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::eco, 1 }, "Eco (light CPU)", false));
    return l;
}

MeteoraNamCompressorProcessor::MeteoraNamCompressorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pThreshold = apvts.getRawParameterValue (ParamIDs::threshold);
    pRatio     = apvts.getRawParameterValue (ParamIDs::ratio);
    pAttack    = apvts.getRawParameterValue (ParamIDs::attack);
    pRelease   = apvts.getRawParameterValue (ParamIDs::release);
    pMakeup    = apvts.getRawParameterValue (ParamIDs::makeup);
    pMix       = apvts.getRawParameterValue (ParamIDs::mix);
    pPower     = apvts.getRawParameterValue (ParamIDs::power);
    pCapture   = apvts.getRawParameterValue (ParamIDs::capture);
    pEco       = apvts.getRawParameterValue (ParamIDs::eco);

    // Build every engine once (weights + state buffers). Switching models later
    // never allocates on the audio thread.
    for (int side = 0; side < 2; ++side)
        for (int eco = 0; eco < 2; ++eco)
            for (int m = 0; m < ModelLibrary::kNumModels; ++m)
                engines[side][eco][m] = nam::makeEngine (library->get (m, side, eco == 1));
}

bool MeteoraNamCompressorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void MeteoraNamCompressorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);

    for (int ch = 0; ch < 2; ++ch)
    {
        stage[ch].prepare (sampleRate, maxBlock);
        pre[ch].assign ((size_t) maxBlock, 0.0f);
        wet[ch].assign ((size_t) maxBlock, 0.0f);
        dry[ch].assign ((size_t) maxBlock, 0.0f);
        for (int eco = 0; eco < 2; ++eco)
            for (auto& e : engines[ch][eco])
                if (e) e->reset();
    }
    const int latency = stage[0].getLatency();
    for (auto& d : dryDelay) d.prepare (latency);
    setLatencySamples (latency);

    comp.prepare (sampleRate);
    for (auto* s : { &preGain, &postGain, &mixS, &powerS })
        s->reset (sampleRate, 0.03);
    preGain.setCurrentAndTargetValue (1.0f);
    postGain.setCurrentAndTargetValue (1.0f);
    mixS.setCurrentAndTargetValue (pMix->load() * 0.01f);
    powerS.setCurrentAndTargetValue (pPower->load() > 0.5f ? 1.0f : 0.0f);
    envIn = envOut = 0.0f;
    envCoef = 1.0f - std::exp (-1.0f / (0.03f * (float) sampleRate));
}

void MeteoraNamCompressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int nch = juce::jmin (2, numIn, buffer.getNumChannels());
    const int total = buffer.getNumSamples();
    if (nch == 0 || total == 0) return;

    // ---- parameters
    const float thr = pThreshold->load();
    const int ratioIdx = juce::jlimit (0, 2, (int) pRatio->load());
    const int attackIdx = juce::jlimit (0, 5, (int) pAttack->load());
    const int releaseIdx = juce::jlimit (0, 4, (int) pRelease->load());
    const float makeup = pMakeup->load();
    const bool capture = pCapture->load() > 0.5f;
    const bool eco = pEco->load() > 0.5f;

    comp.setParameters (kThresholdRef + thr, ratioIdx, attackIdx, releaseIdx);

    // Hybrid : compressor (DSP) -> make-up -> NAM colour model
    // Capture: drive (= -threshold) -> NAM compressor capture -> (+threshold) -> make-up
    preGain.setTargetValue (capture ? dbToGain (-thr) : dbToGain (makeup));
    postGain.setTargetValue (capture ? dbToGain (thr + makeup) : 1.0f);
    mixS.setTargetValue (pMix->load() * 0.01f);
    powerS.setTargetValue (pPower->load() > 0.5f ? 1.0f : 0.0f);

    const int model = ModelLibrary::modelIndex (capture, releaseIdx == 4, ratioIdx);
    for (int ch = 0; ch < nch; ++ch)
        if (auto* e = engines[ch][eco ? 1 : 0][model].get())
            stage[ch].getSwitcher().setEngine (model * 2 + (eco ? 1 : 0), e);

    float grMax = 0.0f, sumSq = 0.0f;
    bool clip = false;

    for (int offset = 0; offset < total; offset += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - offset);
        float* io[2] = { buffer.getWritePointer (0, offset),
                         buffer.getWritePointer (nch > 1 ? 1 : 0, offset) };

        // 1) detector, gain computer, pre-gain, latency-aligned dry copy
        for (int i = 0; i < n; ++i)
        {
            const float l = io[0][i];
            const float r = nch > 1 ? io[1][i] : l;
            float g = preGain.getNextValue();
            if (! capture)
            {
                const float grDb = comp.process (juce::jmax (std::abs (l), std::abs (r)));
                grMax = juce::jmax (grMax, -grDb);
                g *= dbToGain (grDb);
            }
            else
            {
                comp.process (0.0f);    // keep envelopes relaxed for a smooth return to hybrid
            }
            pre[0][(size_t) i] = l * g;
            pre[1][(size_t) i] = r * g;
            dry[0][(size_t) i] = dryDelay[0].process (l);
            dry[1][(size_t) i] = nch > 1 ? dryDelay[1].process (r) : dry[0][(size_t) i];
        }

        // 2) NAM stage (48 kHz internally)
        for (int ch = 0; ch < nch; ++ch)
            stage[ch].process (pre[ch].data(), wet[ch].data(), n);

        // 3) post gain, mix, power, meters
        for (int i = 0; i < n; ++i)
        {
            const float post = postGain.getNextValue();
            const float m = mixS.getNextValue();
            const float p = powerS.getNextValue();

            if (capture)
            {
                const float a = pre[0][(size_t) i], b = wet[0][(size_t) i];
                envIn  += envCoef * (a * a - envIn);
                envOut += envCoef * (b * b - envOut);
                if (envIn > 1.0e-8f)
                    grMax = juce::jmax (grMax, -10.0f * std::log10 (juce::jmax (envOut, 1.0e-12f) / envIn));
            }

            for (int ch = 0; ch < nch; ++ch)
            {
                const float d = dry[ch][(size_t) i];
                float y = wet[ch][(size_t) i] * post;
                if (! std::isfinite (y)) y = 0.0f;
                y = d + (y - d) * m;           // mix
                y = d + (y - d) * p;           // power (bypass keeps latency)
                io[ch][i] = y;
                sumSq += y * y;
                clip = clip || std::abs (y) >= 0.999f;
            }
        }
    }

    if (! capture) { envIn = envOut = 0.0f; }
    if (powerS.getTargetValue() < 0.5f) grMax = 0.0f;
    publishMeters (grMax, sumSq / (float) nch, total, clip);
}

void MeteoraNamCompressorProcessor::publishMeters (float grDb, float sumSq, int count, bool clip)
{
    float prev = meters.grPeakDb.load();
    while (grDb > prev && ! meters.grPeakDb.compare_exchange_weak (prev, grDb)) {}
    float s = meters.outSumSq.load();
    while (! meters.outSumSq.compare_exchange_weak (s, s + sumSq)) {}
    meters.outCount.fetch_add (count);
    if (clip) meters.clipped.store (true);
}

juce::AudioProcessorEditor* MeteoraNamCompressorProcessor::createEditor()
{
    return new MeteoraNamCompressorEditor (*this);
}

void MeteoraNamCompressorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MeteoraNamCompressorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MeteoraNamCompressorProcessor();
}
