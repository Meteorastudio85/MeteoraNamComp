#pragma once
// Minimal, allocation-free (after construction) streaming inference for the
// NAM 0.7 WaveNet models used by this plugin (single layer array, no gating,
// no FiLM, LeakyReLU, layer 1x1 active, head1x1 inactive, convolutional head).
//
// Weight order (identical to NeuralAmpModelerCore):
//   rechannel[C x in]                       (no bias)
//   for each layer:
//       conv  [B][C][K] + bias[B]
//       input mixin [B][cond]               (no bias)
//       layer 1x1  [C][B] + bias[C]
//   head conv [out][B][Kh] + bias[out]
//   head_scale
//
// Kernel tap k of a dilated conv reads the input at time  t - (K-1-k)*d.

#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <cstring>
#include <algorithm>

#if defined(_MSC_VER)
 #define NAM_RESTRICT __restrict
#else
 #define NAM_RESTRICT __restrict__
#endif

namespace nam
{

struct ModelData
{
    int channels = 0;
    int bottleneck = 0;
    int headKernel = 1;
    std::vector<int> kernelSizes;
    std::vector<int> dilations;
    std::vector<float> weights;
    float slope = 0.01f;          // LeakyReLU negative slope
    double sampleRate = 48000.0;

    size_t expectedWeightCount() const
    {
        const size_t C = (size_t) channels, B = (size_t) bottleneck;
        size_t n = C; // rechannel
        for (size_t i = 0; i < kernelSizes.size(); ++i)
            n += B * C * (size_t) kernelSizes[i] + B + B + C * B + C;
        n += B * (size_t) headKernel + 1; // head
        n += 1;                           // head scale
        return n;
    }

    bool isValid (std::string* why = nullptr) const
    {
        auto fail = [why] (const char* s) { if (why) *why = s; return false; };
        if (channels != 3 && channels != 8) return fail ("unsupported channel count");
        if (bottleneck != channels)         return fail ("bottleneck != channels");
        if (kernelSizes.empty() || kernelSizes.size() != dilations.size()) return fail ("bad layer list");
        if (headKernel < 1 || headKernel > 64) return fail ("bad head kernel");
        if (weights.size() != expectedWeightCount()) return fail ("weight count mismatch");
        return true;
    }
};

class Engine
{
public:
    virtual ~Engine() = default;
    virtual void reset() = 0;
    virtual void process (const float* in, float* out, int n) = 0;
    virtual int receptiveField() const = 0;
};

// Block-planar implementation: every layer processes a block of samples at a
// time with channel-planar buffers, so the inner loops run along time and
// vectorise well on any SIMD width (SSE2 / AVX / NEON).
template <int C>
class WaveNetEngine final : public Engine
{
public:
    static constexpr int kBlock = 64;

    explicit WaveNetEngine (const ModelData& m)
    {
        const float* w = m.weights.data();
        slope = m.slope;
        for (int c = 0; c < C; ++c) rech[c] = *w++;

        layers.resize (m.kernelSizes.size());
        rf = 1;
        for (size_t li = 0; li < layers.size(); ++li)
        {
            auto& L = layers[li];
            L.K = m.kernelSizes[li];
            L.d = m.dilations[li];
            L.W.assign ((size_t) L.K * C * C, 0.0f);         // [k][cin][bout]
            for (int b = 0; b < C; ++b)
                for (int c = 0; c < C; ++c)
                    for (int k = 0; k < L.K; ++k)
                        L.W[((size_t) k * C + c) * C + b] = *w++;
            for (int b = 0; b < C; ++b) L.cb[b] = *w++;
            for (int b = 0; b < C; ++b) L.mix[b] = *w++;
            for (int co = 0; co < C; ++co)
                for (int b = 0; b < C; ++b)
                    L.ow[co * C + b] = *w++;                  // [cout][b]
            for (int co = 0; co < C; ++co) L.ob[co] = *w++;

            const int span = (L.K - 1) * L.d + 1;
            L.ring.init (span);
            rf += (L.K - 1) * L.d;
        }

        hk = m.headKernel;
        hw.assign ((size_t) hk * C, 0.0f);                    // [k][b]
        for (int b = 0; b < C; ++b)
            for (int k = 0; k < hk; ++k)
                hw[(size_t) k * C + b] = *w++;
        hb = *w++;
        scale = *w++;
        hring.init (hk);
        rf += hk - 1;
    }

    void reset() override
    {
        for (auto& L : layers) L.ring.clear();
        hring.clear();
    }

    int receptiveField() const override { return rf; }

    void process (const float* in, float* out, int n) override
    {
        while (n > 0)
        {
            const int N = n < kBlock ? n : kBlock;
            block (in, out, N);
            in += N; out += N; n -= N;
        }
    }

private:
    // Mirrored ring (every frame stored twice) so any window of up to kBlock
    // frames is contiguous in memory.
    struct Ring
    {
        int size = 0, mask = 0, pos = 0;
        std::vector<float> data;                              // [c][2*size]
        void init (int span)
        {
            size = 1;
            while (size < span + kBlock) size <<= 1;
            mask = size - 1;
            data.assign ((size_t) C * 2 * (size_t) size, 0.0f);
            pos = 0;
        }
        void clear() { std::fill (data.begin(), data.end(), 0.0f); pos = 0; }
        float* chan (int c) { return data.data() + (size_t) c * 2 * (size_t) size; }
        void write (const float (*src)[kBlock], int N)
        {
            for (int c = 0; c < C; ++c)
            {
                float* d = chan (c);
                for (int i = 0; i < N; ++i)
                {
                    const int p = (pos + i) & mask;
                    d[p] = src[c][i];
                    d[p + size] = src[c][i];
                }
            }
        }
        // pointer to N contiguous frames, the last one being  (newest - delay)
        // kBlock contiguous frames; frame i is (first frame of this block + i - delay)
        const float* window (int c, int delay, int /*N*/)
        {
            return chan (c) + ((pos - delay) & mask);
        }
        void advance (int N) { pos = (pos + N) & mask; }
    };

    struct Layer
    {
        int K = 1, d = 1;
        std::vector<float> W;
        float cb[C], mix[C], ow[C * C], ob[C];
        Ring ring;
    };

    // All inner loops run over the full kBlock width (compile-time trip count,
    // vectorises on every compiler); lanes >= N are computed but discarded.
    void block (const float* xIn, float* out, int N)
    {
        alignas (32) float x[kBlock];
        alignas (32) float y[kBlock];
        for (int i = 0; i < kBlock; ++i) x[i] = i < N ? xIn[i] : 0.0f;
        alignas (32) float h[C][kBlock];
        alignas (32) float z[C][kBlock];
        alignas (32) float head[C][kBlock];

        for (int c = 0; c < C; ++c)
            for (int i = 0; i < kBlock; ++i) { h[c][i] = rech[c] * x[i]; head[c][i] = 0.0f; }

        for (auto& L : layers)
        {
            L.ring.write (h, N);

            for (int b = 0; b < C; ++b)
            {
                const float cb = L.cb[b], mx = L.mix[b];
                for (int i = 0; i < kBlock; ++i) z[b][i] = cb + mx * x[i];
            }
            for (int k = 0; k < L.K; ++k)
            {
                const int delay = (L.K - 1 - k) * L.d;
                const float* Wk = &L.W[(size_t) k * C * C];
                for (int c = 0; c < C; ++c)
                {
                    const float* NAM_RESTRICT src = L.ring.window (c, delay, N);
                    const float* wr = Wk + c * C;
                    for (int b = 0; b < C; ++b)
                    {
                        const float wv = wr[b];
                        float* NAM_RESTRICT zb = z[b];
                        for (int i = 0; i < kBlock; ++i) zb[i] += wv * src[i];
                    }
                }
            }
            for (int b = 0; b < C; ++b)
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = z[b][i];
                    const float a = v > 0.0f ? v : slope * v;
                    z[b][i] = a;
                    head[b][i] += a;
                }
            for (int c = 0; c < C; ++c)
            {
                const float* wr = &L.ow[c * C];
                const float bias = L.ob[c];
                float* NAM_RESTRICT hc = h[c];
                for (int i = 0; i < kBlock; ++i) hc[i] += bias;
                for (int b = 0; b < C; ++b)
                {
                    const float wv = wr[b];
                    const float* NAM_RESTRICT zb = z[b];
                    for (int i = 0; i < kBlock; ++i) hc[i] += wv * zb[i];
                }
            }
            L.ring.advance (N);
        }

        hring.write (head, N);
        for (int i = 0; i < kBlock; ++i) y[i] = hb;
        for (int k = 0; k < hk; ++k)
        {
            const int delay = hk - 1 - k;
            for (int b = 0; b < C; ++b)
            {
                const float* src = hring.window (b, delay, N);
                const float wv = hw[(size_t) k * C + b];
                for (int i = 0; i < kBlock; ++i) y[i] += wv * src[i];
            }
        }
        hring.advance (N);
        for (int i = 0; i < N; ++i) out[i] = y[i] * scale;
    }

    float rech[C];
    std::vector<Layer> layers;
    std::vector<float> hw;
    Ring hring;
    float hb = 0.0f, scale = 1.0f, slope = 0.01f;
    int hk = 1, rf = 1;
};

inline std::unique_ptr<Engine> makeEngine (const ModelData& m)
{
    if (! m.isValid()) return nullptr;
    if (m.channels == 8) return std::make_unique<WaveNetEngine<8>> (m);
    if (m.channels == 3) return std::make_unique<WaveNetEngine<3>> (m);
    return nullptr;
}

} // namespace nam
