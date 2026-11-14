#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ModelLibrary.h"
#include "dsp/NamStage.h"
#include "dsp/BusCompressor.h"
#include <atomic>

namespace ParamIDs
{
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* makeup    = "makeup";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* power     = "power";
    inline constexpr const char* vuMode    = "vumode";
    inline constexpr const char* capture   = "capture";
    inline constexpr const char* eco       = "eco";
}

class MeteoraNamCompressorProcessor : public juce::AudioProcessor
{
public:
    MeteoraNamCompressorProcessor();
    ~MeteoraNamCompressorProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- meters (written by the audio thread, consumed by the editor)
    struct Meters
    {
        std::atomic<float> grPeakDb { 0.0f };   // positive dB of reduction, max since last read
        std::atomic<float> outSumSq { 0.0f };
        std::atomic<int>   outCount { 0 };
        std::atomic<bool>  clipped  { false };
    } meters;

    bool modelsLoaded() const { return library->isOk(); }

    // editor size memory (not saved in the session)
    float editorScale = 0.7f;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publishMeters (float grDb, float sumSq, int count, bool clip);

    juce::SharedResourcePointer<ModelLibrary> library;
    // [side][eco][model]
    std::unique_ptr<nam::Engine> engines[2][2][ModelLibrary::kNumModels];
    dsp::NamStage stage[2];
    dsp::BusCompressor comp;

    struct DelayLine
    {
        std::vector<float> buf; int pos = 0, delay = 0;
        void prepare (int d) { delay = d; buf.assign ((size_t) d + 1, 0.0f); pos = 0; }
        inline float process (float x)
        {
            if (delay == 0) return x;
            buf[(size_t) pos] = x;
            pos = (pos + 1) % (int) buf.size();
            return buf[(size_t) pos];
        }
    } dryDelay[2];

    std::vector<float> pre[2], wet[2], dry[2];
    juce::SmoothedValue<float> preGain, postGain, mixS, powerS;
    float envIn = 0.0f, envOut = 0.0f, envCoef = 0.0f;
    int maxBlock = 512;
    double currentRate = 48000.0;

    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pRatio = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pMakeup = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pPower = nullptr;
    std::atomic<float>* pCapture = nullptr;
    std::atomic<float>* pEco = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeteoraNamCompressorProcessor)
};
