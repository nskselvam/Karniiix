#include "../Brain_tumour/BrainTumourCNN.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static std::string option_value(int argc, char** argv, const std::string& name) {
    for (int index = 2; index + 1 < argc; ++index) if (argv[index] == name) return argv[index + 1];
    return {};
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) != "predict") {
            std::cerr << "Usage: brain_tumor_ml predict --model MODEL --image IMAGE\n";
            return 2;
        }
        const std::string modelPath = option_value(argc, argv, "--model");
        const std::string imagePath = option_value(argc, argv, "--image");
        if (modelPath.empty() || imagePath.empty()) throw std::runtime_error("--model and --image are required");
        if (!fs::exists(modelPath) || !fs::exists(imagePath)) throw std::runtime_error("Model or image does not exist");

        BrainTumourCNN model;
        model.load_weights(modelPath);
        if (!model.are_weights_loaded()) throw std::runtime_error("Could not load brain tumor weights");
        const Tensor prediction = model.forward(model.preprocess(imagePath));
        const auto& probabilities = prediction.get_data();
        const std::vector<std::string> labels = {"glioma_tumor", "meningioma_tumor", "no_tumor", "pituitary_tumor"};
        if (probabilities.size() != labels.size()) throw std::runtime_error("Unexpected brain tumor model output");
        const auto best = std::max_element(probabilities.begin(), probabilities.end());
        const size_t index = static_cast<size_t>(std::distance(probabilities.begin(), best));
        std::cout << std::fixed << std::setprecision(6)
                  << "{\"disease\":\"brain_tumor\",\"model\":\"brain_tumour_cnn\",\"class\":\""
                  << labels[index] << "\",\"probability\":" << *best
                  << ",\"research_only\":true,\"diagnosis\":false}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error=" << error.what() << "\n";
        return 1;
    }
}
