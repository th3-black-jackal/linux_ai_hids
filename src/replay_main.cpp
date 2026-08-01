// replay_main.cpp -- Tier-0 offline validation over one or many dataset CSVs.
//
//   hids_replay <model.bin> <csv-or-dir> [<csv-or-dir> ...] [--raw] [--dump]
//
// Any directory argument is walked recursively for *.csv (the released dataset
// is organized as device/condition/*.csv). All input files are sorted so the
// row order is deterministic and identical to the Python oracle (tools/
// bin_oracle.py), which is what makes prediction-agreement diffs line up.
//
//   --raw   apply Model::normalize (min/max scaler) before inference; use for
//           RAW captures. Omit for the released dataset (already [0,1]).
//   --dump  print "<index> <true> <pred>" per row (true = -1 if unlabeled);
//           used to diff C++ predictions against the oracle.
//
// Columns are resolved to the model's inputs BY HEADER NAME against
// FEATURE_ORDER, per file, so a reordered/mismatched CSV fails loudly. A
// 'label' column is used when present; when absent the row still gets a
// prediction (so unlabeled captures can be validated for agreement), but is
// excluded from accuracy.

#include "hids/model.hpp"
#include "hids/model_meta.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split(const std::string& line, char sep = ',') {
    std::vector<std::string> out;
    std::string cell;
    std::stringstream ss(line);
    while (std::getline(ss, cell, sep)) out.push_back(trim(cell));
    return out;
}

std::vector<std::string> gather_csvs(const std::vector<std::string>& inputs) {
    std::vector<std::string> files;
    for (const auto& in : inputs) {
        std::error_code ec;
        if (fs::is_directory(in, ec)) {
            for (auto& e : fs::recursive_directory_iterator(in, ec))
                if (e.is_regular_file() && e.path().extension() == ".csv")
                    files.push_back(e.path().string());
        } else {
            files.push_back(in);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

struct Tally {
    int K;
    std::vector<std::vector<long>> cm;         // [true][pred]
    long total = 0, correct = 0, bad = 0, unlabeled = 0, gidx = 0;
    explicit Tally(int k) : K(k), cm(k, std::vector<long>(k, 0)) {}
};

void process_file(const hids::Model& model, const std::string& path,
                  bool do_normalize, bool dump, Tally& t) {
    std::ifstream f(path);
    if (!f) { std::cerr << "warning: cannot open " << path << " (skipped)\n"; return; }

    std::string header;
    if (!std::getline(f, header)) return;
    const std::vector<std::string> cols = split(header);
    auto col_index = [&](const std::string& n) -> int {
        for (std::size_t i = 0; i < cols.size(); ++i) if (cols[i] == n) return (int)i;
        return -1;
    };

    std::vector<int> feat_col(model.n_in);
    for (int j = 0; j < model.n_in; ++j) {
        feat_col[j] = col_index(FEATURE_ORDER[j]);
        if (feat_col[j] < 0) {
            std::cerr << "error: " << path << " missing feature column '"
                      << FEATURE_ORDER[j] << "'\n";
            std::exit(1);
        }
    }
    const int label_col = col_index("label");

    std::vector<float> raw(model.n_in), x(model.n_in);
    std::string line;
    while (std::getline(f, line)) {
        if (trim(line).empty()) continue;
        const std::vector<std::string> v = split(line);
        const int need = std::max(label_col, *std::max_element(feat_col.begin(), feat_col.end()));
        if ((int)v.size() <= need) { ++t.bad; continue; }
        try {
            for (int j = 0; j < model.n_in; ++j) raw[j] = std::stof(v[feat_col[j]]);
            const float* in = raw.data();
            if (do_normalize) { model.normalize(raw.data(), x.data()); in = x.data(); }
            const int pred = model.predict(in);

            int y = -1;
            if (label_col >= 0) { y = std::stoi(v[label_col]); if (y < 0 || y >= t.K) y = -2; }
            ++t.gidx;
            if (dump) { std::printf("%ld %d %d\n", t.gidx, y, pred); }

            if (y == -1) { ++t.unlabeled; }
            else if (y == -2) { ++t.bad; }
            else { t.cm[y][pred] += 1; ++t.total; if (pred == y) ++t.correct; }
        } catch (const std::exception&) { ++t.bad; }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> pos;
    bool do_normalize = false, dump = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--raw") do_normalize = true;
        else if (a == "--dump") dump = true;
        else pos.push_back(a);
    }
    if (pos.size() < 2) {
        std::cerr << "usage: hids_replay <model.bin> <csv-or-dir>... [--raw] [--dump]\n";
        return 2;
    }

    hids::Model model;
    try { model = hids::Model::load(pos[0]); }
    catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }

    if (model.n_in != MODEL_N_FEATURES)
        std::cerr << "warning: model n_in=" << model.n_in
                  << " != MODEL_N_FEATURES=" << MODEL_N_FEATURES << "\n";
    if (model.n_out != MODEL_N_CLASSES)
        std::cerr << "warning: model n_out=" << model.n_out
                  << " != MODEL_N_CLASSES=" << MODEL_N_CLASSES << "\n";

    const std::vector<std::string> files =
        gather_csvs({pos.begin() + 1, pos.end()});
    if (files.empty()) { std::cerr << "error: no CSV inputs found\n"; return 1; }
    if (!dump) std::fprintf(stderr, "reading %zu file(s)...\n", files.size());

    Tally t(model.n_out);
    for (const auto& path : files) process_file(model, path, do_normalize, dump, t);

    if (dump) return 0;
    if (t.total == 0 && t.unlabeled == 0) { std::cerr << "error: no usable rows\n"; return 1; }

    std::printf("\nfiles: %zu   labeled rows: %ld   unlabeled: %ld   unparseable: %ld\n",
                files.size(), t.total, t.unlabeled, t.bad);
    std::printf("scaler: %s\n", do_normalize ? "applied (min/max)" : "skipped (pre-normalized)");
    if (t.total == 0) { std::printf("\n(no labels -> agreement-only run; use --dump vs oracle)\n"); return 0; }

    std::printf("\noverall accuracy: %.4f\n\n", (double)t.correct / t.total);
    std::printf("%-14s %8s %8s %8s %8s\n", "class", "support", "prec", "recall", "f1");
    double macro_f1 = 0.0;
    for (int c = 0; c < t.K; ++c) {
        long tp = t.cm[c][c], fp = 0, fn = 0, support = 0;
        for (int r = 0; r < t.K; ++r) { fp += t.cm[r][c]; fn += t.cm[c][r]; support += t.cm[c][r]; }
        fp -= tp; fn -= tp;
        const double prec = (tp + fp) ? (double)tp / (tp + fp) : 0.0;
        const double rec  = (tp + fn) ? (double)tp / (tp + fn) : 0.0;
        const double f1   = (prec + rec) ? 2 * prec * rec / (prec + rec) : 0.0;
        macro_f1 += f1;
        const char* name = (c < MODEL_N_CLASSES) ? CLASS_NAMES[c] : "?";
        std::printf("%-14s %8ld %8.3f %8.3f %8.3f\n", name, support, prec, rec, f1);
    }
    std::printf("\nmacro F1: %.4f\n", macro_f1 / t.K);

    std::printf("\nconfusion matrix (row=true, col=pred):\n%14s", "");
    for (int c = 0; c < t.K; ++c) std::printf(" %6d", c);
    std::printf("\n");
    for (int r = 0; r < t.K; ++r) {
        const char* name = (r < MODEL_N_CLASSES) ? CLASS_NAMES[r] : "?";
        std::printf("%12s %d", name, r);
        for (int c = 0; c < t.K; ++c) std::printf(" %6ld", t.cm[r][c]);
        std::printf("\n");
    }
    return 0;
}
