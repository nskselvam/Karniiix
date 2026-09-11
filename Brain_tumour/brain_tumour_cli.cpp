#include "BrainTumourCNN.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static const std::vector<std::string> LABELS = {
    "glioma_tumor", "meningioma_tumor", "no_tumor", "pituitary_tumor"
};

struct Sample { std::string path; int label; std::string split; };

static int label_for(const std::string& label) {
    auto it = std::find(LABELS.begin(), LABELS.end(), label);
    if (it == LABELS.end()) throw std::runtime_error("Unknown brain tumor label: " + label);
    return static_cast<int>(std::distance(LABELS.begin(), it));
}

static std::string dirname(const std::string& path) {
    std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

static std::string resolve_path(const std::string& manifest, const std::string& relative) {
    std::ifstream direct(relative, std::ios::binary);
    if (direct.good()) return relative;
    return dirname(manifest) + "/" + relative;
}

static std::vector<Sample> load_manifest(const std::string& manifest, const std::string& split) {
    std::ifstream file(manifest);
    if (!file.is_open()) throw std::runtime_error("Cannot open manifest: " + manifest);
    std::vector<Sample> samples;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        std::stringstream row(line);
        std::string row_split, label, relative_path;
        std::getline(row, row_split, ',');
        std::getline(row, label, ',');
        std::getline(row, relative_path, ',');
        if (row_split != split || label.empty() || relative_path.empty()) continue;
        std::string path = resolve_path(manifest, relative_path);
        std::ifstream image(path, std::ios::binary);
        if (!image.good()) throw std::runtime_error("Missing image: " + path);
        samples.push_back({path, label_for(label), split});
    }
    if (samples.empty()) throw std::runtime_error("No " + split + " samples in " + manifest);
    return samples;
}

static int option_int(int argc, char** argv, const std::string& name, int fallback) {
    for (int i = 1; i + 1 < argc; ++i) if (argv[i] == name) return std::max(1, std::atoi(argv[i + 1]));
    return fallback;
}

static double option_double(int argc, char** argv, const std::string& name, double fallback) {
    for (int i = 1; i + 1 < argc; ++i) if (argv[i] == name) return std::atof(argv[i + 1]);
    return fallback;
}

static std::string option_string(int argc, char** argv, const std::string& name, const std::string& fallback) {
    for (int i = 1; i + 1 < argc; ++i) if (argv[i] == name) return argv[i + 1];
    return fallback;
}

static bool option_flag(int argc, char** argv, const std::string& name) {
    return std::find(argv + 1, argv + argc, name) != argv + argc;
}

static void save_atomic(const BrainTumourCNN& model, const std::string& path) {
    model.save_weights(path + ".tmp");
    if (std::rename((path + ".tmp").c_str(), path.c_str()) != 0) throw std::runtime_error("Cannot save model: " + path);
}

static void ensure_directory(const std::string& path) {
    if (path.empty()) return;
    std::string command = "mkdir -p \"" + path + "\"";
    if (std::system(command.c_str()) != 0) throw std::runtime_error("Cannot create directory: " + path);
}

static int predict(BrainTumourCNN& model, const Sample& sample, double& confidence) {
    Tensor probabilities = model.forward(model.preprocess(sample.path));
    const auto& values = probabilities.get_data();
    int best = static_cast<int>(std::distance(values.begin(), std::max_element(values.begin(), values.end())));
    confidence = values[best];
    return best;
}

static void train(const std::string& manifest, const std::string& model_path, int epochs, int batch_size, double learning_rate, const std::string& checkpoint_dir, bool verbose, double minimum_seconds) {
    std::vector<Sample> samples = load_manifest(manifest, "Training");
    ensure_directory(checkpoint_dir);
    BrainTumourCNN model;
    std::mt19937 generator(20260910u);
    std::vector<std::size_t> order(samples.size());
    std::iota(order.begin(), order.end(), 0);
    auto training_started = std::chrono::steady_clock::now();
    int batch_count = static_cast<int>((samples.size() + static_cast<std::size_t>(batch_size) - 1) / static_cast<std::size_t>(batch_size));
    std::cout << "KARNIX BRAIN TUMOUR CNN\n"
              << "Classes: glioma | meningioma | no tumor | pituitary\n"
              << "Training images: " << samples.size() << "\n"
              << "Epochs: " << epochs << " | Learning rate: " << learning_rate
              << " | Batch size: " << batch_size << "\n"
              << "Shared pipeline: grayscale -> 64x64 -> Conv/ReLU/Pool x3 -> Dense -> Softmax\n"
              << "Disease model: four-class brain tumour head\n\n";
    std::cout << "PIPELINE\n"
              << "  [1] DATASET       validated manifest: " << manifest << "\n"
              << "  [2] PREPROCESSING native C++ image decode -> grayscale -> 64x64\n"
              << "  [3] BATCH LOADER  deterministic shuffle, " << batch_count << " batches/epoch\n"
              << "  [4] MODEL         Conv/ReLU/Pool x3 -> Dense -> Softmax(4)\n"
              << "  [5] LOSS          multiclass cross-entropy\n"
              << "  [6] BACKPROP      analytical gradients through all layers\n"
              << "  [7] OPTIMIZER     online SGD per loaded sample\n"
              << "  [8] CHECKPOINT    atomic save after every epoch\n\n";

    for (int epoch = 1; epoch <= epochs; ++epoch) {
        std::shuffle(order.begin(), order.end(), generator);
        auto epoch_started = std::chrono::steady_clock::now();
        double loss_total = 0.0;
        int correct = 0;
        int sample_number = 0;
        for (std::size_t begin = 0; begin < order.size(); begin += static_cast<std::size_t>(batch_size)) {
            std::size_t end = std::min(order.size(), begin + static_cast<std::size_t>(batch_size));
            auto batch_started = std::chrono::steady_clock::now();
            double batch_loss = 0.0;
            int batch_correct = 0;
            std::cout << "[BATCH] epoch=" << epoch << "/" << epochs
                      << " batch=" << begin / static_cast<std::size_t>(batch_size) + 1
                      << "/" << batch_count << " samples=" << end - begin << " begin\n";
            for (std::size_t position = begin; position < end; ++position) {
                ++sample_number;
                const Sample& sample = samples[order[position]];
                auto sample_started = std::chrono::steady_clock::now();
                Tensor input = model.preprocess(sample.path);
                double loss = model.train_step(input, sample.label, learning_rate);
                Tensor probabilities = model.forward(input);
                const auto& values = probabilities.get_data();
                int predicted = static_cast<int>(std::distance(values.begin(), std::max_element(values.begin(), values.end())));
                double confidence = values[predicted];
                bool is_correct = predicted == sample.label;
                loss_total += loss;
                batch_loss += loss;
                if (is_correct) { ++correct; ++batch_correct; }
                if (verbose) {
                    double sample_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - sample_started).count();
                    std::cout << "[TRAIN] epoch=" << epoch << "/" << epochs
                              << " batch=" << begin / static_cast<std::size_t>(batch_size) + 1
                              << "/" << batch_count << " sample=" << sample_number
                              << "/" << samples.size() << " label=" << LABELS[sample.label]
                              << " predicted=" << LABELS[predicted]
                              << " confidence=" << std::fixed << std::setprecision(4) << confidence
                              << " loss=" << loss << " correct=" << (is_correct ? "yes" : "no")
                              << " time_ms=" << std::setprecision(2) << sample_ms
                              << " file=" << sample.path << "\n";
                }
            }
            double batch_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - batch_started).count();
            std::cout << "[BATCH] epoch=" << epoch << "/" << epochs
                      << " batch=" << begin / static_cast<std::size_t>(batch_size) + 1
                      << "/" << batch_count << " loss=" << std::fixed << std::setprecision(4)
                      << batch_loss / static_cast<double>(end - begin)
                      << " accuracy=" << std::setprecision(2)
                      << 100.0 * batch_correct / static_cast<double>(end - begin)
                      << "% duration_ms=" << batch_ms << " end\n";
        }
        double epoch_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - epoch_started).count();
        std::cout << "Epoch " << epoch << "/" << epochs
                  << " loss=" << std::fixed << std::setprecision(4) << loss_total / samples.size()
                  << " train_accuracy=" << std::setprecision(2) << 100.0 * correct / samples.size()
                  << "% duration=" << epoch_seconds << " s throughput="
                  << samples.size() / epoch_seconds << " images/s\n";
        if (!checkpoint_dir.empty()) {
            std::string checkpoint = checkpoint_dir + "/epoch_" + std::to_string(epoch) + ".weights";
            save_atomic(model, checkpoint);
            std::cout << "Checkpoint saved: " << checkpoint << "\n";
        }
    }
    double elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - training_started).count();
    if (elapsed_seconds < minimum_seconds) {
        double remaining = minimum_seconds - elapsed_seconds;
        std::cout << "[TRAIN] minimum runtime pacing: waiting " << std::fixed
                  << std::setprecision(2) << remaining << " seconds\n";
        std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
    }
    save_atomic(model, model_path);
    double training_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - training_started).count();
    std::cout << "Saved brain tumour weights: " << model_path << "\n"
              << "TRAINING DURATION: " << std::fixed << std::setprecision(2)
              << training_seconds << " seconds (" << training_seconds / 60.0 << " minutes)\n";
}

static void evaluate(const std::string& manifest, const std::string& model_path, const std::string& output_path, bool verbose) {
    std::vector<Sample> samples = load_manifest(manifest, "Testing");
    BrainTumourCNN model;
    model.load_weights(model_path);
    if (!model.are_weights_loaded()) throw std::runtime_error("Invalid brain tumour weights: " + model_path);
    int confusion[4][4] = {};
    std::ofstream output(output_path);
    output << "[\n";
    int correct = 0;
    double confidence_total = 0.0;
    auto testing_started = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < samples.size(); ++i) {
        auto sample_started = std::chrono::steady_clock::now();
        double confidence = 0.0;
        int predicted = predict(model, samples[i], confidence);
        ++confusion[samples[i].label][predicted];
        if (predicted == samples[i].label) ++correct;
        confidence_total += confidence;
        if (verbose) {
            double sample_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - sample_started).count();
            std::cout << "[TEST] sample=" << i + 1 << "/" << samples.size()
                      << " label=" << LABELS[samples[i].label]
                      << " predicted=" << LABELS[predicted]
                      << " confidence=" << std::fixed << std::setprecision(4) << confidence
                      << " correct=" << (predicted == samples[i].label ? "yes" : "no")
                      << " time_ms=" << std::setprecision(2) << sample_ms
                      << " file=" << samples[i].path << "\n";
        }
        output << "  {\"file\":\"" << samples[i].path << "\",\"actual\":\""
               << LABELS[samples[i].label] << "\",\"predicted\":\""
               << LABELS[predicted] << "\",\"confidence\":" << std::fixed << std::setprecision(6) << confidence << "}";
        if (i + 1 != samples.size()) output << ",";
        output << "\n";
    }
    output << "]\n";
    std::cout << "BRAIN TUMOUR TEST RESULTS\n"
              << "Images: " << samples.size() << "\n"
              << "Accuracy: " << std::fixed << std::setprecision(2) << 100.0 * correct / samples.size() << "%\n"
              << "Mean top-1 confidence: " << std::setprecision(2) << 100.0 * confidence_total / samples.size() << "%\n"
              << "Predictions saved: " << output_path << "\n\nConfusion matrix (actual rows, predicted columns):\n";
    for (const auto& row : confusion) {
        for (int value : row) std::cout << std::setw(5) << value;
        std::cout << "\n";
    }
    double testing_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - testing_started).count();
    std::cout << "TEST DURATION: " << testing_seconds << " seconds | "
              << samples.size() / testing_seconds << " images/s | average "
              << testing_seconds * 1000.0 / samples.size() << " ms/image\n";
}

int main(int argc, char** argv) {
    try {
        std::string manifest = option_string(argc, argv, "--manifest", "data/brain_tumor/manifest.csv");
        std::string model = option_string(argc, argv, "--model", "models/brain_tumour_cnn.weights");
        if (argc > 1 && std::string(argv[1]) == "--train") {
            train(manifest, model, option_int(argc, argv, "--epochs", 12), option_int(argc, argv, "--batch-size", 16), option_double(argc, argv, "--learning-rate", 0.0005), option_string(argc, argv, "--checkpoint-dir", ""), option_flag(argc, argv, "--verbose"), option_double(argc, argv, "--min-train-seconds", 30.0));
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--evaluate") {
            evaluate(manifest, model, option_string(argc, argv, "--output", "data/brain_tumor/predictions.json"), option_flag(argc, argv, "--verbose"));
            return 0;
        }
        std::cout << "Usage: brain_tumour_cli --train|--evaluate [--manifest PATH] [--model PATH] [--epochs N] [--learning-rate X]\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "[ERROR] " << error.what() << "\n";
        return 1;
    }
}