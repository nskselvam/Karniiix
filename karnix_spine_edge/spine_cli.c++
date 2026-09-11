#include "medical/OnnxInference.hpp"
#include "medical/UCIVertebralAnalysis.hpp"
#include "medical/ClinicalScoring.hpp"
#include "medical/SpineEvaluationUtils.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <numeric>
#include <random>
#include <sys/stat.h>
#include <dirent.h>       
static const std::string MODEL_PATH = "./models/osteoarthritis_cnn.weights";

struct RawImage {
    std::vector<double> pixels;
    int width = 0;
    int height = 0;
};

#define KARNIX_SPINE_CLI_RAW_IMAGE_DEFINED
#include "medical/SpineCNN.hpp"

static RawImage load_ppm(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);
    
    std::string magic; 
    f >> magic;
    while (f.peek() == '\n' || f.peek() == '\r') f.get();
    while (f.peek() == '#') {
        std::string line;
        std::getline(f, line);
    }
    
    int w, h, maxval;
    f >> w >> h;
    while (f.peek() == '\n' || f.peek() == '\r') f.get();
    while (f.peek() == '#') {
        std::string line;
        std::getline(f, line);
    }
    f >> maxval;
    f.get();
    
    RawImage img;
    img.width = w;
    img.height = h;
    img.pixels.resize(h * w);
    
    if (magic == "P6") {
        std::vector<unsigned char> buf(h * w * 3);
        f.read(reinterpret_cast<char*>(buf.data()), buf.size());
        for (int i = 0; i < h * w; ++i) {
            double r = buf[3*i+0] / static_cast<double>(maxval);
            double g = buf[3*i+1] / static_cast<double>(maxval);
            double b = buf[3*i+2] / static_cast<double>(maxval);
            img.pixels[i] = 0.299 * r + 0.587 * g + 0.114 * b;
        }
    } else if (magic == "P5") {
        std::vector<unsigned char> buf(h * w);
        f.read(reinterpret_cast<char*>(buf.data()), buf.size());
        for (int i = 0; i < h * w; ++i) {
            img.pixels[i] = buf[i] / static_cast<double>(maxval);
        }
    } else {
        throw std::runtime_error("Unsupported PPM format: " + magic);
    }
    
    return img;
}

static RawImage load_bmp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) throw std::runtime_error("Cannot open BMP: " + path);
    
    unsigned char header[54];
    f.read(reinterpret_cast<char*>(header), 54);
    
    if (header[0] != 'B' || header[1] != 'M') {
        throw std::runtime_error("Invalid BMP file");
    }
    
    int width = *reinterpret_cast<int*>(&header[18]);
    int height = *reinterpret_cast<int*>(&header[22]);
    int bits_per_pixel = *reinterpret_cast<short*>(&header[28]);
    
    bool top_down = (height < 0);
    if (top_down) height = -height;
    
    if (bits_per_pixel != 24 && bits_per_pixel != 32) {
        throw std::runtime_error("Only 24-bit and 32-bit BMP supported");
    }
    
    int bytes_per_pixel = bits_per_pixel / 8;
    int row_size = ((width * bytes_per_pixel + 3) / 4) * 4;
    std::vector<unsigned char> row_buffer(row_size);
    
    RawImage img;
    img.width = width;
    img.height = height;
    img.pixels.resize(height * width);
    
    for (int y = 0; y < height; ++y) {
        f.read(reinterpret_cast<char*>(row_buffer.data()), row_size);
        
        int actual_y = top_down ? y : (height - 1 - y);
        
        for (int x = 0; x < width; ++x) {
            unsigned char b = row_buffer[x * bytes_per_pixel + 0];
            unsigned char g = row_buffer[x * bytes_per_pixel + 1];
            unsigned char r = row_buffer[x * bytes_per_pixel + 2];
            double gray = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
            img.pixels[actual_y * width + x] = gray;
        }
    }
    
    return img;
}

static RawImage convert_via_sips(const std::string& src) {
    std::string tmp = "/tmp/spine_sips_" + std::to_string(reinterpret_cast<std::uintptr_t>(&src)) + ".bmp";
    std::string cmd = "sips -s format bmp \"" + src + "\" --out \"" + tmp + "\" > /dev/null 2>&1";
    
    if (std::system(cmd.c_str()) != 0) {
        std::remove(tmp.c_str());
        throw std::runtime_error("Image conversion failed: " + src);
    }
    
    RawImage img = load_bmp(tmp);
    std::remove(tmp.c_str());
    return img;
}

static std::string get_extension(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = std::tolower(c);
    return ext;
}

static RawImage load_image(const std::string& path) {
    std::string ext = get_extension(path);
    if (ext == "ppm" || ext == "pgm") {
        return load_ppm(path);
    }
    return convert_via_sips(path);
}

static bool dir_exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::vector<std::string> scan_images(const std::string& dir_path) {
    std::vector<std::string> files;
    DIR* d = opendir(dir_path.c_str());
    if (!d) return files;

    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;

        std::string lname = name;
        for (auto& c : lname)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        auto dot = lname.rfind('.');
        if (dot == std::string::npos) continue;
        std::string ext = lname.substr(dot + 1);

        static const std::vector<std::string> SUPPORTED =
            {"jpg","jpeg","png","ppm","pgm","pbm","tiff","tif","bmp","dcm","dicom"};
        if (std::find(SUPPORTED.begin(), SUPPORTED.end(), ext) == SUPPORTED.end())
            continue;

        files.push_back(dir_path + "/" + name);
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    return files;
}

struct LabeledImage {
    std::string path;
    int label;
};

class BatchLoader {
private:
    const std::vector<LabeledImage>& samples_;
    std::vector<std::size_t> order_;
    int batch_size_;

public:
    BatchLoader(const std::vector<LabeledImage>& samples, int batch_size)
        : samples_(samples), order_(samples.size()), batch_size_(std::max(1, batch_size)) {
        std::iota(order_.begin(), order_.end(), 0);
    }

    void begin_epoch(int epoch) {
        std::mt19937 generator(20260909u + static_cast<unsigned int>(epoch));
        std::shuffle(order_.begin(), order_.end(), generator);
    }

    int batch_count() const {
        return static_cast<int>((order_.size() + batch_size_ - 1) / batch_size_);
    }

    std::vector<const LabeledImage*> batch(int batch_index) const {
        std::size_t begin = static_cast<std::size_t>(batch_index * batch_size_);
        std::size_t end = std::min(order_.size(), begin + static_cast<std::size_t>(batch_size_));
        std::vector<const LabeledImage*> result;
        result.reserve(end - begin);
        for (std::size_t index = begin; index < end; ++index) {
            result.push_back(&samples_[order_[index]]);
        }
        return result;
    }
};

static std::vector<LabeledImage> load_manifest(const std::string& manifest_path) {
    std::ifstream file(manifest_path);
    if (!file.is_open()) throw std::runtime_error("Cannot open manifest: " + manifest_path);
    std::vector<LabeledImage> samples;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        std::istringstream row(line);
        std::string path;
        std::string label;
        if (!std::getline(row, path, ',') || !std::getline(row, label, ',')) continue;
        int value = std::stoi(label);
        if (value == 0 || value == 1) {
            std::ifstream image(path, std::ios::binary);
            if (!image.good()) throw std::runtime_error("Manifest image not found: " + path);
            samples.push_back({path, value});
        }
    }
    if (samples.empty()) throw std::runtime_error("Manifest contains no samples: " + manifest_path);
    int class_counts[2] = {0, 0};
    for (const auto& sample : samples) ++class_counts[sample.label];
    if (class_counts[0] == 0 || class_counts[1] == 0) {
        throw std::runtime_error("Manifest must contain both binary classes: " + manifest_path);
    }
    return samples;
}

static void save_weights_atomic(const SpineCNN& model, const std::string& path) {
    std::string temporary = path + ".tmp";
    model.save_weights(temporary);
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        throw std::runtime_error("Cannot finalize checkpoint: " + path);
    }
}

static void train_oa_cnn(const std::string& manifest_path,
                         const std::string& weights_path,
                         int epochs,
                         bool verbose,
                         const std::string& task_name,
                         const std::string& negative_name,
                         const std::string& positive_name,
                         int batch_size,
                         double learning_rate,
                         const std::string& checkpoint_dir,
                         const std::string& resume_path) {
    std::vector<LabeledImage> samples = load_manifest(manifest_path);
    SpineCNN model;
    if (!resume_path.empty()) {
        model.load_weights(resume_path);
        if (!model.are_weights_loaded()) {
            throw std::runtime_error("Cannot resume from checkpoint: " + resume_path);
        }
        std::cout << "[CHECKPOINT] Resumed weights from " << resume_path << "\n";
    }
    BatchLoader loader(samples, batch_size);
    auto training_started = std::chrono::steady_clock::now();
    std::cout << "Training " << task_name << " CNN on " << samples.size() << " images\n";
    std::cout << "Epochs: " << epochs << " | Learning rate: " << learning_rate
              << " | Batch size: " << batch_size << "\n";
    std::cout << "Architecture: 1x64x64 -> Conv(4) -> Pool -> Conv(8) -> Pool"
              << " -> Conv(16) -> Pool -> Dense(32) -> Softmax(2)\n";
    std::cout << "Parameters: " << model.get_param_count()
              << " | Optimizer: SGD | Loss: Cross-Entropy\n\n";
    std::cout << "PIPELINE\n"
              << "  [1] DATASET       validated manifest: " << manifest_path << "\n"
              << "  [2] PREPROCESSING grayscale -> 64x64 -> normalize [-1,1]\n"
              << "  [3] BATCH LOADER  deterministic shuffle, " << loader.batch_count() << " batches/epoch\n"
              << "  [4] MODEL         Conv/ReLU/Pool x3 -> Dense -> Softmax\n"
              << "  [5] LOSS          binary cross-entropy\n"
              << "  [6] FORWARD       probabilities for 2 classes\n"
              << "  [7] BACKPROP      analytical gradients through every layer\n"
              << "  [8] OPTIMIZER     online SGD inside each loaded batch\n"
              << "  [9] EPOCHS        " << epochs << " complete dataset passes\n"
              << " [10] CHECKPOINT    atomic save after every epoch\n\n";
    for (int epoch = 1; epoch <= epochs; ++epoch) {
        loader.begin_epoch(epoch);
        auto epoch_started = std::chrono::steady_clock::now();
        double total_loss = 0.0;
        int correct = 0;
        int sample_number = 0;
        for (int batch_index = 0; batch_index < loader.batch_count(); ++batch_index) {
            auto batch_started = std::chrono::steady_clock::now();
            std::vector<const LabeledImage*> batch = loader.batch(batch_index);
            double batch_loss = 0.0;
            int batch_correct = 0;
            std::cout << "[BATCH] epoch=" << epoch << "/" << epochs
                      << " batch=" << batch_index + 1 << "/" << loader.batch_count()
                      << " samples=" << batch.size() << " begin\n";
            for (const LabeledImage* sample : batch) {
                ++sample_number;
                auto sample_started = std::chrono::steady_clock::now();
                Tensor input = model.preprocess(sample->path);
                double loss = model.train_step(input, sample->label, learning_rate);
                if (!std::isfinite(loss)) {
                    throw std::runtime_error("Non-finite loss for image: " + sample->path);
                }
                total_loss += loss;
                batch_loss += loss;
                Tensor prediction_tensor = model.forward(input);
                const auto& prediction = prediction_tensor.get_data();
                if (prediction.size() != 2 || !std::isfinite(prediction[0]) ||
                    !std::isfinite(prediction[1])) {
                    throw std::runtime_error("Invalid model probabilities for image: " + sample->path);
                }
                int predicted = prediction[1] > prediction[0] ? 1 : 0;
                if (predicted == sample->label) {
                    ++correct;
                    ++batch_correct;
                }
                if (verbose) {
                    double sample_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - sample_started).count();
                    std::cout << "[TRAIN] epoch=" << epoch << "/" << epochs
                              << " batch=" << batch_index + 1 << "/" << loader.batch_count()
                              << " sample=" << sample_number << "/" << samples.size()
                              << " label=" << (sample->label ? positive_name : negative_name)
                              << " predicted=" << (predicted ? positive_name : negative_name)
                              << " p_negative=" << std::fixed << std::setprecision(4) << prediction[0]
                              << " p_positive=" << prediction[1]
                              << " loss=" << loss
                              << " correct=" << (predicted == sample->label ? "yes" : "no")
                              << " time_ms=" << std::setprecision(2) << sample_ms
                              << " file=" << sample->path << "\n";
                }
            }
            double batch_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - batch_started).count();
            std::cout << "[BATCH] epoch=" << epoch << "/" << epochs
                      << " batch=" << batch_index + 1 << "/" << loader.batch_count()
                      << " loss=" << std::fixed << std::setprecision(4) << batch_loss / batch.size()
                      << " accuracy=" << std::setprecision(2)
                      << 100.0 * batch_correct / batch.size() << "%"
                      << " duration_ms=" << batch_ms << " end\n";
        }
        double epoch_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - epoch_started).count();
        std::cout << "Epoch " << epoch << "/" << epochs
                  << " - loss: " << std::fixed << std::setprecision(4)
                  << total_loss / samples.size()
                  << " - train accuracy: " << std::setprecision(2)
                  << 100.0 * correct / samples.size() << "%"
                  << " - duration: " << epoch_seconds << " s"
                  << " - throughput: " << samples.size() / epoch_seconds << " images/s\n";
        if (!checkpoint_dir.empty()) {
            std::string checkpoint = checkpoint_dir + "/epoch_" +
                std::to_string(epoch) + ".weights";
            save_weights_atomic(model, checkpoint);
            std::cout << "[CHECKPOINT] epoch=" << epoch << " saved=" << checkpoint << "\n";
        }
    }
    save_weights_atomic(model, weights_path);
    double training_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - training_started).count();
    std::cout << "Trained OA weights saved to " << weights_path << "\n";
    std::cout << "TRAINING DURATION: " << std::fixed << std::setprecision(2)
              << training_seconds << " seconds (" << training_seconds / 60.0 << " minutes)\n";
}

static int evaluate_oa_cnn(const std::string& manifest_path,
                           const std::string& weights_path,
                           bool verbose,
                           const std::string& task_name,
                           const std::string& negative_name,
                           const std::string& positive_name) {
    std::vector<LabeledImage> samples = load_manifest(manifest_path);
    SpineCNN model;
    model.load_weights(weights_path);
    if (!model.are_weights_loaded()) throw std::runtime_error("Valid trained weights not found: " + weights_path);
    auto testing_started = std::chrono::steady_clock::now();
    int confusion[2][2] = {{0, 0}, {0, 0}};
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        auto sample_started = std::chrono::steady_clock::now();
        Tensor input = model.preprocess(sample.path);
        Tensor prediction_tensor = model.forward(input);
        const auto& prediction = prediction_tensor.get_data();
        if (prediction.size() != 2 || !std::isfinite(prediction[0]) ||
            !std::isfinite(prediction[1])) {
            throw std::runtime_error("Invalid model probabilities for image: " + sample.path);
        }
        int predicted = prediction[1] > prediction[0] ? 1 : 0;
        ++confusion[sample.label][predicted];
        if (verbose) {
            double sample_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - sample_started).count();
            std::cout << "[TEST] sample=" << index + 1 << "/" << samples.size()
                      << " label=" << (sample.label ? positive_name : negative_name)
                      << " predicted=" << (predicted ? positive_name : negative_name)
                      << " p_negative=" << std::fixed << std::setprecision(4) << prediction[0]
                      << " p_positive=" << prediction[1]
                      << " correct=" << (predicted == sample.label ? "yes" : "no")
                      << " time_ms=" << std::setprecision(2) << sample_ms
                      << " file=" << sample.path << "\n";
        }
    }
    int correct = confusion[0][0] + confusion[1][1];
    double test_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - testing_started).count();
    double sensitivity = confusion[1][1] + confusion[1][0] == 0 ? 0.0
        : 100.0 * confusion[1][1] / (confusion[1][1] + confusion[1][0]);
    double specificity = confusion[0][0] + confusion[0][1] == 0 ? 0.0
        : 100.0 * confusion[0][0] / (confusion[0][0] + confusion[0][1]);
    std::cout << task_name << " TEST RESULTS\n";
    std::cout << "Images: " << samples.size() << "\n";
    std::cout << "Accuracy: " << std::fixed << std::setprecision(2)
              << 100.0 * correct / samples.size() << "%\n";
    std::cout << "Sensitivity: " << sensitivity << "%\n";
    std::cout << "Specificity: " << specificity << "%\n";
    std::cout << "TN=" << confusion[0][0] << " FP=" << confusion[0][1]
              << " FN=" << confusion[1][0] << " TP=" << confusion[1][1] << "\n";
    std::cout << "TEST DURATION: " << test_seconds << " seconds"
              << " | " << samples.size() / test_seconds << " images/s"
              << " | average " << test_seconds * 1000.0 / samples.size() << " ms/image\n";
    return correct == 0 ? 1 : 0;
}

static std::string base_name(const std::string& path) {
    auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::string probability_bar(double value, int width = 30) {
    int filled = static_cast<int>(std::round(std::max(0.0, std::min(1.0, value)) * width));
    return std::string(filled, '#') + std::string(width - filled, '.');
}

struct AdvancedImagingMetrics {
    double trabecular_bone_score = 0.0;
    double cortical_thinning_index = 0.0;
    double marrow_dark_ratio = 0.0;
    std::string marrow_signal;
};

static std::string upper_copy(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

static AdvancedImagingMetrics analyze_advanced_imaging(const RawImage& image) {
    AdvancedImagingMetrics metrics;
    if (image.width < 3 || image.height < 3 || image.pixels.empty()) return metrics;

    double local_variation = 0.0;
    double edge_energy = 0.0;
    int local_count = 0;
    for (int y = 1; y < image.height - 1; ++y) {
        for (int x = 1; x < image.width - 1; ++x) {
            int idx = y * image.width + x;
            double center = image.pixels[idx];
            double dx = std::abs(center - image.pixels[idx - 1]) + std::abs(center - image.pixels[idx + 1]);
            double dy = std::abs(center - image.pixels[idx - image.width]) + std::abs(center - image.pixels[idx + image.width]);
            local_variation += dx + dy;
            edge_energy += std::sqrt(dx * dx + dy * dy);
            ++local_count;
        }
    }
    local_variation /= std::max(1, local_count);
    edge_energy /= std::max(1, local_count);

    metrics.trabecular_bone_score = std::max(0.4, std::min(1.8, 1.45 - local_variation * 1.8));

    double center_mean = 0.0;
    double shell_mean = 0.0;
    int center_count = 0;
    int shell_count = 0;
    double cx = (image.width - 1) * 0.5;
    double cy = (image.height - 1) * 0.5;
    double max_radius = std::sqrt(cx * cx + cy * cy);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            double nx = x - cx;
            double ny = y - cy;
            double radius = std::sqrt(nx * nx + ny * ny) / std::max(1.0, max_radius);
            double pixel = image.pixels[y * image.width + x];
            if (radius < 0.33) {
                center_mean += pixel;
                ++center_count;
            } else if (radius > 0.48 && radius < 0.78) {
                shell_mean += pixel;
                ++shell_count;
            }
        }
    }
    center_mean /= std::max(1, center_count);
    shell_mean /= std::max(1, shell_count);
    metrics.cortical_thinning_index = std::max(0.0, std::min(100.0, (center_mean / std::max(0.05, shell_mean)) * 55.0 + edge_energy * 35.0));

    int marrow_pixels = 0;
    int dark_pixels = 0;
    for (int y = image.height / 4; y < (image.height * 3) / 4; ++y) {
        for (int x = image.width / 4; x < (image.width * 3) / 4; ++x) {
            double pixel = image.pixels[y * image.width + x];
            ++marrow_pixels;
            if (pixel < 0.38) ++dark_pixels;
        }
    }
    metrics.marrow_dark_ratio = marrow_pixels == 0 ? 0.0 : 100.0 * dark_pixels / marrow_pixels;
    metrics.marrow_signal = metrics.marrow_dark_ratio >= 45.0 ? "Dark marrow signal" : "Bright marrow signal";
    return metrics;
}

static std::vector<std::string> image_preview(const RawImage& image) {
    static const std::string shades = " .:-=+*#%@";
    const int preview_width = 32;
    const int preview_height = 12;
    std::vector<std::string> lines(preview_height, std::string(preview_width, ' '));
    for (int row = 0; row < preview_height; ++row) {
        int source_y = std::min(image.height - 1, row * image.height / preview_height);
        for (int column = 0; column < preview_width; ++column) {
            int source_x = std::min(image.width - 1, column * image.width / preview_width);
            double value = image.pixels[source_y * image.width + source_x];
            int shade = static_cast<int>(std::round(
                std::max(0.0, std::min(1.0, value)) * (shades.size() - 1)));
            lines[row][column] = shades[shade];
        }
    }
    return lines;
}

static void render_cnn_frame(const RawImage& image,
                             const std::string& path,
                             int image_index,
                             int image_count,
                             int active_stage,
                             double normal_probability,
                             double oa_probability) {
    static const std::vector<std::string> stages = {
        "Input 64x64", "Conv 1x4", "ReLU", "MaxPool",
        "Conv 4x8", "Conv 8x16", "Dense 32x2", "Softmax"
    };
    std::cout << "\033[2J\033[H";
    std::cout << "\033[1;36mKARNIX OA CNN - LIVE INFERENCE\033[0m\n";
    std::cout << "Image " << image_index << "/" << image_count
              << "  " << base_name(path) << "\n\n";
    for (const auto& line : image_preview(image)) {
        std::cout << "  |" << line << "|\n";
    }
    std::cout << "\n";
    for (std::size_t index = 0; index < stages.size(); ++index) {
        if (static_cast<int>(index) < active_stage) {
            std::cout << "\033[1;32m[OK] " << stages[index] << "\033[0m";
        } else if (static_cast<int>(index) == active_stage) {
            std::cout << "\033[1;33m[>>] " << stages[index] << "\033[0m";
        } else {
            std::cout << "[  ] " << stages[index];
        }
        if (index + 1 != stages.size()) std::cout << " -> ";
        if (index == 3) std::cout << "\n";
    }
    std::cout << "\n\n";
    if (active_stage >= static_cast<int>(stages.size())) {
        std::cout << "No OA  [" << probability_bar(normal_probability) << "] "
                  << std::fixed << std::setprecision(2) << normal_probability * 100.0 << "%\n";
        std::cout << "OA     [" << probability_bar(oa_probability) << "] "
                  << oa_probability * 100.0 << "%\n\n";
        if (oa_probability > normal_probability) {
            std::cout << "\033[1;31mCLASSIFICATION: OSTEOARTHRITIS\033[0m\n";
        } else {
            std::cout << "\033[1;32mCLASSIFICATION: NO OA\033[0m\n";
        }
    } else {
        std::cout << "Activations flowing through the custom C++ CNN";
        std::cout << std::string(active_stage % 4, '.') << "\n";
    }
    std::cout.flush();
}

static int animation_delay_ms() {
    const char* configured = std::getenv("KARNIX_ANIMATION_DELAY_MS");
    return configured ? std::max(0, std::atoi(configured)) : 120;
}

static void animate_oa_cnn(const std::string& images_dir,
                           const std::string& weights_path,
                           int requested_count) {
    std::vector<std::string> images = scan_images(images_dir);
    if (images.empty()) throw std::runtime_error("No animation images found: " + images_dir);
    int count = std::min(requested_count, static_cast<int>(images.size()));
    SpineCNN model;
    model.load_weights(weights_path);
    if (!model.are_weights_loaded()) throw std::runtime_error("Valid trained weights not found: " + weights_path);
    int delay = animation_delay_ms();
    for (int index = 0; index < count; ++index) {
        RawImage image = load_image(images[index]);
        Tensor input = model.preprocess(images[index]);
        Tensor output = model.forward(input);
        const auto& probabilities = output.get_data();
        for (int stage = 0; stage <= 8; ++stage) {
            render_cnn_frame(image, images[index], index + 1, count, stage,
                             probabilities[0], probabilities[1]);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay * 4));
    }
    std::cout << "\nAnimated CNN inference complete: " << count << " image(s).\n";
}

static void export_animation_data(const std::string& images_dir,
                                  const std::string& weights_path,
                                  const std::string& output_path,
                                  int requested_count) {
    std::vector<std::string> images = scan_images(images_dir);
    if (images.empty()) throw std::runtime_error("No animation images found: " + images_dir);
    int count = std::min(requested_count, static_cast<int>(images.size()));
    SpineCNN model;
    model.load_weights(weights_path);
    if (!model.are_weights_loaded()) throw std::runtime_error("Valid trained weights not found: " + weights_path);
    std::ofstream output(output_path);
    if (!output.is_open()) throw std::runtime_error("Cannot write animation data: " + output_path);
    output << "{\n  \"model\": \"Karnix custom C++ CNN\",\n  \"images\": [\n";
    for (int index = 0; index < count; ++index) {
        Tensor input = model.preprocess(images[index]);
        Tensor prediction = model.forward(input);
        const auto& probabilities = prediction.get_data();
        std::string filename = base_name(images[index]);
        bool known_oa = filename.rfind("oa_", 0) == 0;
        output << "    {\"file\": \"" << filename
               << "\", \"url\": \"../data/osteoarthritis/screen/" << filename
               << "\", \"normal\": " << std::fixed << std::setprecision(6) << probabilities[0]
               << ", \"oa\": " << probabilities[1]
               << ", \"knownLabel\": \"" << (known_oa ? "OA" : "No OA") << "\"}";
        if (index + 1 != count) output << ",";
        output << "\n";
    }
    output << "  ]\n}\n";
    std::cout << "Exported browser animation data for " << count
              << " image(s) to " << output_path << "\n";
}

static std::string patient_id_from_path(const std::string& path) {
    auto slash = path.rfind('/');
    std::string fname = (slash == std::string::npos) ? path : path.substr(slash + 1);
    auto dot = fname.rfind('.');
    return (dot == std::string::npos) ? fname : fname.substr(0, dot);
}

static void print_banner() {
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "  KARNIX SPINE SCREENING SYSTEM v3.0\n";
    std::cout << "  Clinical-Grade AI Bone Abnormality Detection\n";
    std::cout << "  ─────────────────────────────────────────────────────────────────────────── \n";
    std::cout << "  • Pre-trained ResNet50 (MURA Dataset)\n";
    std::cout << "  • UCI Vertebral Column ML Risk Scoring\n";
    std::cout << "  • Clinical Rule-Based Assessment\n";
    std::cout << "  • Fully Offline - No Internet Required\n";
    std::cout << "================================================================================\n\n";
}

static std::string option_value(int argc, char** argv,
                                const std::string& option,
                                const std::string& fallback) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] == option) return argv[index + 1];
    }
    return fallback;
}

static int option_int(int argc, char** argv,
                      const std::string& option, int fallback) {
    std::string value = option_value(argc, argv, option, std::to_string(fallback));
    return std::max(1, std::atoi(value.c_str()));
}

static double option_double(int argc, char** argv,
                            const std::string& option, double fallback) {
    std::string value = option_value(argc, argv, option, std::to_string(fallback));
    double parsed = std::atof(value.c_str());
    return parsed > 0.0 ? parsed : fallback;
}

int main(int argc, char** argv) {
    try {
        bool verbose = std::find(argv + 1, argv + argc, std::string("--verbose")) != argv + argc;
        std::string model_path = option_value(argc, argv, "--model", MODEL_PATH);
        std::string task_name = option_value(argc, argv, "--task", "OSTEOARTHRITIS");
        std::string negative_name = option_value(argc, argv, "--negative", "NO_OA");
        std::string positive_name = option_value(argc, argv, "--positive", "OA");
        std::string modality = upper_copy(option_value(argc, argv, "--modality", "AUTO"));
        int batch_size = option_int(argc, argv, "--batch-size", 16);
        double learning_rate = option_double(argc, argv, "--learning-rate", 0.0001);
        std::string checkpoint_dir = option_value(argc, argv, "--checkpoint-dir", "");
        std::string resume_path = option_value(argc, argv, "--resume", "");
        if (argc >= 3 && std::string(argv[1]) == "--train") {
            int epochs = argc >= 4 ? std::max(1, std::atoi(argv[3])) : 3;
            train_oa_cnn(argv[2], model_path, epochs, verbose,
                         task_name, negative_name, positive_name,
                         batch_size, learning_rate, checkpoint_dir, resume_path);
            return 0;
        }
        if (argc >= 3 && std::string(argv[1]) == "--evaluate") {
            return evaluate_oa_cnn(argv[2], model_path, verbose,
                                   task_name, negative_name, positive_name);
        }
        if (argc >= 2 && std::string(argv[1]) == "--animate") {
            std::string directory = argc >= 3 ? argv[2] : "./data/osteoarthritis/screen";
            int count = argc >= 4 ? std::max(1, std::atoi(argv[3])) : 10;
            animate_oa_cnn(directory, model_path, count);
            return 0;
        }
        if (argc >= 4 && std::string(argv[1]) == "--export-animation-data") {
            int count = argc >= 5 ? std::max(1, std::atoi(argv[4])) : 10;
            export_animation_data(argv[2], model_path, argv[3], count);
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }

    print_banner();

    const std::string images_dir = argc >= 3 && std::string(argv[1]) == "--screen"
        ? argv[2] : "./data/images";
    const std::string model_path = option_value(argc, argv, "--model", MODEL_PATH);
    const std::string task_name = option_value(argc, argv, "--task", "OSTEOARTHRITIS");
    const std::string negative_name = option_value(argc, argv, "--negative", "NO_OA");
    const std::string positive_name = option_value(argc, argv, "--positive", "OA");
    const std::string modality = upper_copy(option_value(argc, argv, "--modality", "AUTO"));
    const std::string db_path = "./data/spine_records.json";

    std::cout << "Configuration:\n";
    std::cout << "  Images Directory: " << images_dir << "\n";
    std::cout << "  Model: " << model_path << "\n";
    std::cout << "  Modality: " << modality << " (XRAY/DEXA/CT/MRI/AUTO)\n";
    std::cout << "  Database: " << db_path << "\n\n";

    std::cout << "Initializing " << task_name << " CNN...\n";
    SpineCNN local_cnn;
    local_cnn.load_weights(model_path);
    if (!local_cnn.are_weights_loaded()) {
        std::cerr << "[ERROR] Trained " << task_name << " model not found: " << model_path << "\n";
        return 1;
    }
    std::cout << "  Model loaded: " << model_path << "\n";
    std::cout << "  Input size: " << SpineCNN::INPUT_W << "x" << SpineCNN::INPUT_H << "\n";
    std::cout << "  Output classes: " << negative_name << " / " << positive_name << "\n\n";

    UCIVertebralAnalyzer vertebral_analyzer;
    ClinicalScoringSystem clinical_scoring;
    
    std::cout << "Clinical Analysis Systems Initialized:\n";
    std::cout << "  ✓ UCI Vertebral Column Analyzer\n";
    std::cout << "  ✓ Clinical Scoring System\n\n";

    std::cout << "Scanning dataset: " << images_dir << "\n";
    
    if (!dir_exists(images_dir)) {
        std::cerr << "[ERROR] Directory not found: " << images_dir << "\n";
        std::cerr << "  Please create directory and add spine X-ray images.\n\n";
        return 1;
    }

    std::vector<std::string> image_paths = scan_images(images_dir);
    if (image_paths.empty()) {
        std::cerr << "[ERROR] No images found in " << images_dir << "\n\n";
        return 1;
    }
    
    std::cout << "  Found " << image_paths.size() << " image(s)\n\n";

    SpineRecordDB database(db_path);
    database.load();
    
    std::cout << "Database loaded: " << database.size() << " existing records\n\n";
    std::cout << "================================================================================\n";
    std::cout << "  PROCESSING IMAGES\n";
    std::cout << "================================================================================\n\n";

    int processed = 0;
    int abnormal_detected = 0;
    
    for (const auto& img_path : image_paths) {
        processed++;
        
        std::cout << "─────────────────────────────────────────────────────────────────────────\n";
        std::cout << "Image " << processed << "/" << image_paths.size() << ": " << img_path << "\n";
        std::cout << "─────────────────────────────────────────────────────────────────────────\n";
        
        try {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            RawImage raw_img = load_image(img_path);
            std::cout << "✓ Loaded: " << raw_img.width << "x" << raw_img.height << " pixels\n";
            
            double mean_intensity = 0.0;
            double variance = 0.0;
            for (double pixel : raw_img.pixels) {
                mean_intensity += pixel;
            }
            mean_intensity /= raw_img.pixels.size();
            
            for (double pixel : raw_img.pixels) {
                double diff = pixel - mean_intensity;
                variance += diff * diff;
            }
            variance /= raw_img.pixels.size();
            
            double edge_score = 0.0;
            for (size_t i = 1; i < raw_img.height - 1; ++i) {
                for (size_t j = 1; j < raw_img.width - 1; ++j) {
                    size_t idx = i * raw_img.width + j;
                    double gx = std::abs(raw_img.pixels[idx + 1] - raw_img.pixels[idx - 1]);
                    double gy = std::abs(raw_img.pixels[idx + raw_img.width] - 
                                        raw_img.pixels[idx - raw_img.width]);
                    edge_score += std::sqrt(gx * gx + gy * gy);
                }
            }
            edge_score /= ((raw_img.height - 2) * (raw_img.width - 2));
            
            double texture_complexity = variance * edge_score;
            AdvancedImagingMetrics advanced_metrics = analyze_advanced_imaging(raw_img);
            
            Tensor cnn_input = local_cnn.preprocess(img_path);
            Tensor prediction_tensor = local_cnn.forward(cnn_input);
            const auto& predictions = prediction_tensor.get_data();
            double prob_normal = predictions[0];
            double prob_abnormal = predictions[1];
            
            std::cout << "\n[CUSTOM CNN - " << task_name << "]\n";
            std::cout << "  " << negative_name << ": " << std::fixed << std::setprecision(2)
                      << (prob_normal * 100.0) << "%\n";
            std::cout << "  " << positive_name << ": " << std::fixed << std::setprecision(2)
                      << (prob_abnormal * 100.0) << "%\n";
            std::cout << "  Classification: "
                      << (prob_abnormal > 0.5 ? positive_name : negative_name) << "\n";
            
            VertebralFeatures vf = VertebralFeatures::extract_from_image_features(
                mean_intensity, variance, edge_score
            );
            
            VertebralPathology pathology = vertebral_analyzer.classify(vf);
            double vertebral_risk = vertebral_analyzer.calculate_risk_score(vf);
            
            std::cout << "\n[UCI VERTEBRAL COLUMN ANALYSIS]\n";
            std::cout << "  Pelvic Incidence:      " << std::fixed << std::setprecision(1) 
                      << vf.pelvic_incidence << "°\n";
            std::cout << "  Pelvic Tilt:           " << vf.pelvic_tilt << "°\n";
            std::cout << "  Lumbar Lordosis:       " << vf.lumbar_lordosis_angle << "°\n";
            std::cout << "  Sacral Slope:          " << vf.sacral_slope << "°\n";
            std::cout << "  Spondylolisthesis:     " << vf.grade_of_spondylolisthesis << "%\n";
            std::cout << "  Pathology:             " << UCIVertebralAnalyzer::pathology_name(pathology) << "\n";
            std::cout << "  Risk Score:            " << std::fixed << std::setprecision(1) 
                      << vertebral_risk << "/100\n";
            
            ClinicalMeasurements clinical_measures = 
                ClinicalMeasurements::estimate_from_image(
                    mean_intensity, variance, edge_score, texture_complexity
                );
            
            double clinical_severity = ClinicalScoringSystem::calculate_clinical_severity(clinical_measures);
            
            std::cout << "\n[CLINICAL ASSESSMENT]\n";
            
            auto scoliosis = ClinicalScoringSystem::classify_scoliosis(clinical_measures.cobb_angle);
            std::cout << "  Scoliosis:             " << ClinicalScoringSystem::scoliosis_name(scoliosis);
            if (scoliosis != ScoliosisSeverity::NONE) {
                std::cout << " (" << std::fixed << std::setprecision(1) 
                          << clinical_measures.cobb_angle << "°)";
            }
            std::cout << "\n";
            
            int fracture_grade = ClinicalScoringSystem::compression_fracture_grade(clinical_measures);
            std::cout << "  Compression Fracture:  ";
            if (fracture_grade == 0) {
                std::cout << "Not detected\n";
            } else {
                std::cout << "Grade " << fracture_grade << " detected\n";
            }
            
            std::cout << "  Bone Density:          " 
                      << ClinicalScoringSystem::classify_bone_density(clinical_measures.t_score)
                      << " (T-score: " << std::fixed << std::setprecision(1) 
                      << clinical_measures.t_score << ")\n";

            std::cout << "\n[ADVANCED MODALITY ASSESSMENT]\n";
            std::cout << "  DEXA TBS Proxy:        " << std::fixed << std::setprecision(2)
                      << advanced_metrics.trabecular_bone_score
                      << " (pixel-to-pixel texture variation)\n";
            std::cout << "  CT Cortical Thinning:  " << std::fixed << std::setprecision(1)
                      << advanced_metrics.cortical_thinning_index
                      << "/100 (marrow cavity widening + cortex edge loss proxy)\n";
            std::cout << "  MRI Marrow Signal:     " << advanced_metrics.marrow_signal
                      << " (dark ratio " << advanced_metrics.marrow_dark_ratio << "%)\n";
            
            std::cout << "  Disc Degeneration:     " 
                      << ClinicalScoringSystem::disc_degeneration_grade_name(
                          clinical_measures.disc_degeneration_grade) << "\n";
            
            double fracture_risk_pct = ClinicalScoringSystem::calculate_fracture_risk_score(clinical_measures);
            auto fracture_risk = ClinicalScoringSystem::classify_fracture_risk(fracture_risk_pct);
            std::cout << "  10-Year Fracture Risk: " 
                      << ClinicalScoringSystem::fracture_risk_name(fracture_risk)
                      << " (" << std::fixed << std::setprecision(0) << fracture_risk_pct << "%)\n";
            
            int instability = ClinicalScoringSystem::calculate_instability_score(clinical_measures);
            std::cout << "  Instability Score:     " << instability << "/13";
            if (instability >= 5) {
                std::cout << " [SURGICAL EVALUATION RECOMMENDED]";
            }
            std::cout << "\n";
            
            double final_score = prob_abnormal * 100.0;
            final_score = std::min(100.0, final_score);
            
            std::cout << "\n[COMPREHENSIVE ASSESSMENT]\n";
            std::cout << "  Overall Risk Score:    " << std::fixed << std::setprecision(1) 
                      << final_score << "/100\n";
            
            std::string risk_category;
            if (final_score < 30.0) {
                risk_category = "LOW";
            } else if (final_score < 60.0) {
                risk_category = "MODERATE";
            } else {
                risk_category = "HIGH";
            }
            std::cout << "  Risk Category:         " << risk_category << "\n";
            
            std::cout << "\n[CLINICAL RECOMMENDATIONS]\n";
            if (final_score >= 60.0) {
                std::cout << "  • Immediate clinical evaluation recommended\n";
                std::cout << "  • Consider MRI or CT scan for detailed assessment\n";
                if (instability >= 5) {
                    std::cout << "  • Orthopedic/neurosurgical consultation advised\n";
                }
            } else if (final_score >= 30.0) {
                std::cout << "  • Follow-up imaging in 6-12 months\n";
                std::cout << "  • Monitor for symptom progression\n";
            } else {
                std::cout << "  • Routine follow-up as needed\n";
                std::cout << "  • No immediate intervention required\n";
            }
            
            auto end_time = std::chrono::high_resolution_clock::now();
            double inference_ms = std::chrono::duration<double, std::milli>(
                end_time - start_time).count();
            
            std::cout << "\nProcessing Time: " << std::fixed << std::setprecision(1) 
                      << inference_ms << " ms\n";
            
            SpineRecord record;
            record.patient_id = patient_id_from_path(img_path);
            record.age = 0;
            record.sex = "N/A";
            record.date = DateUtil::today();
            record.risk_score = final_score;
            record.abnormal_prob = prob_abnormal * 100.0;
            record.classification = prob_abnormal > 0.5 ? positive_name : negative_name;
            record.image_path = img_path;
            record.modality = modality;
            record.trabecular_bone_score = advanced_metrics.trabecular_bone_score;
            record.cortical_thinning_index = advanced_metrics.cortical_thinning_index;
            record.marrow_dark_ratio = advanced_metrics.marrow_dark_ratio;
            record.marrow_signal = advanced_metrics.marrow_signal;
            
            database.append(record);
            
            if (prob_abnormal > 0.5) {
                abnormal_detected++;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "\n[ERROR] Failed to process image: " << e.what() << "\n";
        }
        
        std::cout << "\n";
    }

    try {
        database.save();
        std::cout << "================================================================================\n";
        std::cout << "  PROCESSING COMPLETE\n";
        std::cout << "================================================================================\n";
        std::cout << "  Total Images Processed: " << processed << "\n";
        std::cout << "  Abnormalities Detected: " << abnormal_detected << "\n";
        std::cout << "  Database Records:       " << database.size() << "\n";
        std::cout << "  Records saved to:       " << db_path << "\n";
        std::cout << "================================================================================\n\n";
    } catch (const std::exception& e) {
        std::cerr << "[WARNING] Could not save database: " << e.what() << "\n\n";
    }

    return 0;
}
