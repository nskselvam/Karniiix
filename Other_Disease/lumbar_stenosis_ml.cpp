#include "../karnix_spine_edge/medical/SpineCNN.hpp"

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
            std::cerr << "Usage: lumbar_stenosis_ml predict --model MODEL --image IMAGE\n";
            return 2;
        }
        const std::string modelPath = option_value(argc, argv, "--model");
        const std::string imagePath = option_value(argc, argv, "--image");
        if (modelPath.empty() || imagePath.empty()) throw std::runtime_error("--model and --image are required");
        if (!fs::exists(modelPath) || !fs::exists(imagePath)) throw std::runtime_error("Model or image does not exist");

        SpineCNN model;
        model.load_weights(modelPath);
        if (!model.are_weights_loaded()) throw std::runtime_error("Could not load lumbar stenosis weights");
        const Tensor prediction = model.forward(model.preprocess(imagePath));
        const auto& probabilities = prediction.get_data();
        if (probabilities.size() != 2) throw std::runtime_error("Unexpected lumbar stenosis model output");
        const int predicted = probabilities[1] > probabilities[0] ? 1 : 0;
        std::cout << std::fixed << std::setprecision(6)
                  << "{\"disease\":\"lumbar_stenosis\",\"model\":\"karnix_spine_mri_cnn\",\"class\":\""
                  << (predicted ? "structural_abnormality" : "no_structural_abnormality")
                  << "\",\"probability\":" << probabilities[predicted]
                  << ",\"research_only\":true,\"diagnosis\":false}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error=" << error.what() << "\n";
        return 1;
    }
}
