// model.hpp -- loads model.bin (weights + per-feature min/max scaler) and runs
// the 3-layer MLP. Layout is produced by export_model.py; see Generated_model_schema.
//
//   int32  n_in, h1, h2, n_out
//   f32[n_in]      feature_min
//   f32[n_in]      feature_max
//   f32[h1*n_in]   W1   f32[h1]     b1
//   f32[h2*h1]     W2   f32[h2]     b2
//   f32[n_out*h2]  W3   f32[n_out]  b3
//
// Little-endian; every target (RPi 3/4, Beaglebone, x86 dev) is LE, so we read
// floats straight into memory with no byte swapping.
#pragma once

#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hids {

class Model {
public:
    std::int32_t n_in = 0, h1 = 0, h2 = 0, n_out = 0;
    std::vector<float> feature_min, feature_max;      // per-input scaler
    std::vector<float> W1, b1, W2, b2, W3, b3;

    static Model load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("model.bin: cannot open " + path);

        Model m;
        std::int32_t hdr[4];
        if (!f.read(reinterpret_cast<char*>(hdr), sizeof(hdr)))
            throw std::runtime_error("model.bin: cannot read header");
        m.n_in = hdr[0]; m.h1 = hdr[1]; m.h2 = hdr[2]; m.n_out = hdr[3];
        if (m.n_in <= 0 || m.h1 <= 0 || m.h2 <= 0 || m.n_out <= 0)
            throw std::runtime_error("model.bin: nonsensical dimensions");

        m.feature_min = read_floats(f, m.n_in, "feature_min");
        m.feature_max = read_floats(f, m.n_in, "feature_max");
        m.W1 = read_floats(f, static_cast<std::size_t>(m.h1) * m.n_in, "W1");
        m.b1 = read_floats(f, m.h1, "b1");
        m.W2 = read_floats(f, static_cast<std::size_t>(m.h2) * m.h1, "W2");
        m.b2 = read_floats(f, m.h2, "b2");
        m.W3 = read_floats(f, static_cast<std::size_t>(m.n_out) * m.h2, "W3");
        m.b3 = read_floats(f, m.n_out, "b3");

        f.peek();                        // trip eof if there is nothing left
        if (!f.eof())
            throw std::runtime_error("model.bin: trailing bytes; size mismatch vs header");
        return m;
    }

    // raw features (in FEATURE_ORDER) -> [0,1], clamped exactly as training did.
    void normalize(const float* raw, float* out) const {
        for (int j = 0; j < n_in; ++j) {
            const float d = feature_max[j] - feature_min[j];
            const float v = d > 0.0f ? (raw[j] - feature_min[j]) / d : 0.0f;
            out[j] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        }
    }

    // forward pass on already-normalized input. Returns class index; writes the
    // softmax probability of the winner to *confidence when non-null.
    int predict(const float* x, float* confidence = nullptr) const {
        std::vector<float> a(h1), b(h2), z(n_out);
        for (int i = 0; i < h1; ++i) {
            float s = b1[i];
            const float* w = &W1[static_cast<std::size_t>(i) * n_in];
            for (int j = 0; j < n_in; ++j) s += w[j] * x[j];
            a[i] = s > 0.0f ? s : 0.0f;                       // ReLU
        }
        for (int i = 0; i < h2; ++i) {
            float s = b2[i];
            const float* w = &W2[static_cast<std::size_t>(i) * h1];
            for (int j = 0; j < h1; ++j) s += w[j] * a[j];
            b[i] = s > 0.0f ? s : 0.0f;                       // ReLU
        }
        for (int i = 0; i < n_out; ++i) {
            float s = b3[i];
            const float* w = &W3[static_cast<std::size_t>(i) * h2];
            for (int j = 0; j < h2; ++j) s += w[j] * b[j];
            z[i] = s;                                         // logits
        }
        int best = 0;
        for (int i = 1; i < n_out; ++i) if (z[i] > z[best]) best = i;
        if (confidence) {
            float mx = z[best], sum = 0.0f;
            for (float v : z) sum += std::exp(v - mx);
            *confidence = 1.0f / sum;
        }
        return best;
    }

private:
    static std::vector<float> read_floats(std::ifstream& f, std::size_t n, const char* what) {
        std::vector<float> v(n);
        if (n && !f.read(reinterpret_cast<char*>(v.data()),
                         static_cast<std::streamsize>(n * sizeof(float))))
            throw std::runtime_error(std::string("model.bin: short read on ") + what);
        return v;
    }
};

}  // namespace hids
