#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

namespace fs = std::filesystem;

struct Image {
    std::vector<double> pixels;
    int width = 0;
    int height = 0;
};

struct Sample { std::string path; int label; };

static std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static Image load_pgm(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open image: " + path);
    std::string magic;
    input >> magic;
    if (magic != "P2" && magic != "P5") throw std::runtime_error("Expected PGM P2/P5: " + path);
    int width = 0, height = 0, maxValue = 0;
    input >> width >> height >> maxValue;
    if (width <= 0 || height <= 0 || maxValue <= 0) throw std::runtime_error("Invalid PGM: " + path);
    Image image{std::vector<double>(static_cast<size_t>(width * height)), width, height};
    if (magic == "P2") {
        for (double& pixel : image.pixels) { int value = 0; input >> value; pixel = value / static_cast<double>(maxValue); }
    } else {
        input.get();
        std::vector<unsigned char> bytes(image.pixels.size());
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) throw std::runtime_error("Truncated PGM: " + path);
        for (size_t i = 0; i < bytes.size(); ++i) image.pixels[i] = bytes[i] / static_cast<double>(maxValue);
    }
    return image;
}

#ifdef __APPLE__
static Image load_with_imageio(const std::string& path) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()), path.size(), false);
    if (!url) throw std::runtime_error("Cannot create image URL: " + path);
    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (!source) throw std::runtime_error("Cannot decode image: " + path);
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!image) throw std::runtime_error("Cannot decode image: " + path);
    const int width = static_cast<int>(CGImageGetWidth(image));
    const int height = static_cast<int>(CGImageGetHeight(image));
    Image result{std::vector<double>(static_cast<size_t>(width * height)), width, height};
    std::vector<unsigned char> pixels(result.pixels.size());
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceGray();
    CGContextRef context = CGBitmapContextCreate(pixels.data(), width, height, 8, width, colorSpace, kCGImageAlphaNone);
    if (!context) { CGColorSpaceRelease(colorSpace); CGImageRelease(image); throw std::runtime_error("Cannot create grayscale context"); }
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    CGContextRelease(context); CGColorSpaceRelease(colorSpace); CGImageRelease(image);
    for (size_t i = 0; i < pixels.size(); ++i) result.pixels[i] = pixels[i] / 255.0;
    return result;
}
#endif

static Image load_image(const std::string& path) {
    const std::string extension = lower(fs::path(path).extension().string());
    if (extension == ".pgm") return load_pgm(path);
#ifdef __APPLE__
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tif" || extension == ".tiff") return load_with_imageio(path);
#endif
    throw std::runtime_error("Unsupported image format (use PNG/JPG/BMP/TIFF/PGM): " + path);
}

static std::vector<double> features(const Image& source, int side) {
    std::vector<double> result(static_cast<size_t>(side * side));
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
        const double sourceY = (y + 0.5) * source.height / side - 0.5;
        const double sourceX = (x + 0.5) * source.width / side - 0.5;
        const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)), 0, source.height - 1);
        const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)), 0, source.width - 1);
        result[static_cast<size_t>(y * side + x)] = source.pixels[static_cast<size_t>(y0 * source.width + x0)];
    }
    double mean = std::accumulate(result.begin(), result.end(), 0.0) / result.size();
    for (double& value : result) value -= mean;
    return result;
}

class BinaryClassifier {
public:
    explicit BinaryClassifier(size_t featureCount) : weights_(featureCount + 1, 0.0) {}

    double predictProbability(const std::vector<double>& input) const {
        double score = weights_.back();
        for (size_t i = 0; i < input.size(); ++i) score += weights_[i] * input[i];
        score = std::clamp(score, -40.0, 40.0);
        return 1.0 / (1.0 + std::exp(-score));
    }

    void train(const std::vector<std::vector<double>>& inputs, const std::vector<int>& labels, int epochs, double learningRate, double l2) {
        if (inputs.empty() || inputs.size() != labels.size()) throw std::runtime_error("Training data is empty or inconsistent");
        for (int epoch = 1; epoch <= epochs; ++epoch) {
            double loss = 0.0;
            for (size_t row = 0; row < inputs.size(); ++row) {
                const double probability = predictProbability(inputs[row]);
                const double error = probability - labels[row];
                loss += -(labels[row] ? std::log(std::max(probability, 1e-12)) : std::log(std::max(1.0 - probability, 1e-12)));
                for (size_t i = 0; i < inputs[row].size(); ++i) weights_[i] -= learningRate * (error * inputs[row][i] + l2 * weights_[i]);
                weights_.back() -= learningRate * error;
            }
            if (epoch == 1 || epoch == epochs || epoch % 10 == 0) std::cout << "epoch=" << epoch << " loss=" << loss / inputs.size() << "\n";
        }
    }

    void save(const std::string& path, int side) const {
        std::ofstream output(path);
        if (!output) throw std::runtime_error("Cannot write model: " + path);
        output << "KARNIX_LUNG_XRAY_LOGISTIC_V1\n" << side << "\n" << weights_.size() << "\n";
        output << std::setprecision(17);
        for (double weight : weights_) output << weight << '\n';
    }

    static BinaryClassifier load(const std::string& path, int& side) {
        std::ifstream input(path);
        std::string magic; size_t count = 0;
        if (!(input >> magic) || magic != "KARNIX_LUNG_XRAY_LOGISTIC_V1" || !(input >> side) || !(input >> count) || count < 2) throw std::runtime_error("Invalid lung model: " + path);
        BinaryClassifier model(count - 1);
        for (double& weight : model.weights_) if (!(input >> weight)) throw std::runtime_error("Truncated lung model: " + path);
        return model;
    }

private:
    std::vector<double> weights_;
};

static std::vector<Sample> discover(const std::string& root) {
    std::vector<Sample> samples;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const std::string extension = lower(entry.path().extension().string());
        if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".bmp" && extension != ".tif" && extension != ".tiff" && extension != ".pgm") continue;
        const std::string parent = lower(entry.path().parent_path().filename().string());
        int label = -1;
        if (parent == "normal" || parent == "healthy") label = 0;
        if (parent == "sick" || parent == "pneumonia" || parent == "lung_cancer" || parent == "cancer" || parent == "abnormal") label = 1;
        if (label >= 0) samples.push_back({entry.path().string(), label});
    }
    if (samples.empty()) throw std::runtime_error("No labeled images found under " + root + ". Put images in normal/ and sick/ folders.");
    if (std::none_of(samples.begin(), samples.end(), [](const Sample& sample) { return sample.label == 0; }) || std::none_of(samples.begin(), samples.end(), [](const Sample& sample) { return sample.label == 1; })) throw std::runtime_error("Both normal/healthy and sick/pneumonia/cancer/abnormal classes are required.");
    return samples;
}

static std::string option_value(int argc, char** argv, const std::string& name, const std::string& fallback = "") {
    for (int i = 2; i + 1 < argc; ++i) {
        if (argv[i] == name) return argv[i + 1];
    }
    return fallback;
}

static void usage() {
    std::cout << "Karnix lung X-ray research classifier\n\n"
              << "Train:     lung_xray_ml train --data DATASET_DIR --model lung_model.weights\n"
              << "Predict:   lung_xray_ml predict --model lung_model.weights --image IMAGE\n"
              << "Evaluate:  lung_xray_ml evaluate --model lung_model.weights --data DATASET_DIR\n\n"
              << "Output is a research estimate only, not a diagnosis.\n";
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) { usage(); return 2; }
        const std::string command = argv[1];
        const std::string data = option_value(argc, argv, "--data");
        const std::string modelPath = option_value(argc, argv, "--model", "models/lung_xray.weights");
        constexpr int side = 32;
        if (command == "train") {
            if (data.empty()) throw std::runtime_error("train requires --data DATASET_DIR");
            const auto samples = discover(data);
            std::vector<std::vector<double>> inputs; std::vector<int> labels;
            for (const auto& sample : samples) { inputs.push_back(features(load_image(sample.path), side)); labels.push_back(sample.label); }
            BinaryClassifier classifier(static_cast<size_t>(side * side));
            classifier.train(inputs, labels, 100, 0.01, 0.0001); classifier.save(modelPath, side);
            std::cout << "saved_model=" << modelPath << " samples=" << samples.size() << "\n";
            return 0;
        }
        if (command == "predict") {
            const std::string imagePath = option_value(argc, argv, "--image");
            if (imagePath.empty()) throw std::runtime_error("predict requires --model MODEL --image IMAGE");
            int modelSide = 0; auto classifier = BinaryClassifier::load(modelPath, modelSide); const double probability = classifier.predictProbability(features(load_image(imagePath), modelSide));
            std::cout << "{\"disease\":\"lung_cancer\",\"model\":\"research_logistic_pixel_baseline\",\"class\":\"" << (probability >= 0.5 ? "positive_class" : "negative_class") << "\",\"probability\":" << probability << ",\"research_only\":true,\"diagnosis\":false}\n";
            return 0;
        }
        if (command == "evaluate") {
            if (data.empty()) throw std::runtime_error("evaluate requires --data DATASET_DIR");
            int modelSide = 0; auto classifier = BinaryClassifier::load(modelPath, modelSide); const auto samples = discover(data); int correct = 0;
            for (const auto& sample : samples) { const double probability = classifier.predictProbability(features(load_image(sample.path), modelSide)); if ((probability >= 0.5) == (sample.label == 1)) ++correct; }
            std::cout << "samples=" << samples.size() << " accuracy=" << static_cast<double>(correct) / samples.size() << "\n";
            return 0;
        }
        usage(); return 2;
    } catch (const std::exception& error) { std::cerr << "error=" << error.what() << "\n"; return 1; }
}
