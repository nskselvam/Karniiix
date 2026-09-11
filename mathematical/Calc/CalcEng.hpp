#include <iostream>
#include <vector>
#include <cmath>
#include <functional>

class CalcEng {
private:
    std::vector<double> gradient_result;
    std::vector<std::vector<double>> jacobian_result;
    std::vector<std::vector<double>> hessian_result;
    double integration_result;
    double differentiation_result;
    
    // Small epsilon for numerical differentiation
    const double epsilon = 1e-8;

public:
    // Step 1: Differentiation - Compute slope or rate of change
    double differentiation(std::function<double(double)> f, double x) {
        // Numerical differentiation using central difference
        differentiation_result = (f(x + epsilon) - f(x - epsilon)) / (2.0 * epsilon);
        return differentiation_result;
    }
    
    void displayDifferentiation(double x) {
        std::cout << "Differentiation at x=" << x << ": " << differentiation_result << std::endl;
    }

    double integrationWithoutData(std::function<double(double)> , int n = 1000){
        std::cout<< "Integeration without Data" << std::endl;
    }

    // Step 2: Integration - Compute cumulative quantities  
    double integration(std::function<double(double)> f, double a, double b, int n = 1000) {
        // Numerical integration using trapezoidal rule
        double h = (b - a) / n;
        integration_result = 0.5 * (f(a) + f(b));
        
        for (int i = 1; i < n; i++) {
            double x = a + i * h;
            integration_result += f(x);
        }
        
        integration_result *= h;
        return integration_result;
    }
    
    void displayIntegration(double a, double b) {
        std::cout << "Integration from " << a << " to " << b << ": " << integration_result << std::endl;
    }

    


    // Step 3: Gradient - Multi-variable slope vector
    std::vector<double> gradient(std::function<double(std::vector<double>)> f, const std::vector<double>& x) {
        gradient_result.clear();
        gradient_result.resize(x.size());
        
        for (int i = 0; i < x.size(); i++) {
            std::vector<double> x_plus = x;
            std::vector<double> x_minus = x;
            
            x_plus[i] += epsilon;
            x_minus[i] -= epsilon;
            
            gradient_result[i] = (f(x_plus) - f(x_minus)) / (2.0 * epsilon);
        }
        
        return gradient_result;
    }
    
    void displayGradient(const std::vector<double>& x) {
        std::cout << "Gradient at (";
        for (int i = 0; i < x.size(); i++) {
            std::cout << x[i];
            if (i < x.size() - 1) std::cout << ", ";
        }
        std::cout << "): [";
        for (int i = 0; i < gradient_result.size(); i++) {
            std::cout << gradient_result[i];
            if (i < gradient_result.size() - 1) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }

    // Step 4: Jacobian - Matrix of partial derivatives
    std::vector<std::vector<double>> jacobian(
        const std::vector<std::function<double(std::vector<double>)>>& functions,
        const std::vector<double>& x) {
        
        jacobian_result.clear();
        jacobian_result.resize(functions.size(), std::vector<double>(x.size()));
        
        for (int i = 0; i < functions.size(); i++) {
            for (int j = 0; j < x.size(); j++) {
                std::vector<double> x_plus = x;
                std::vector<double> x_minus = x;
                
                x_plus[j] += epsilon;
                x_minus[j] -= epsilon;
                
                jacobian_result[i][j] = (functions[i](x_plus) - functions[i](x_minus)) / (2.0 * epsilon);
            }
        }
        
        return jacobian_result;
    }

    
    void displayJacobian(const std::vector<double>& x) {
        std::cout << "Jacobian matrix at (";
        for (int i = 0; i < x.size(); i++) {
            std::cout << x[i];
            if (i < x.size() - 1) std::cout << ", ";
        }
        std::cout << "):" << std::endl;
        
        for (const auto& row : jacobian_result) {
            std::cout << "[";
            for (int j = 0; j < row.size(); j++) {
                std::cout << row[j];
                if (j < row.size() - 1) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
    }

    // Step 5: Hessian - Second-order curvature information
    std::vector<std::vector<double>> hessian(std::function<double(std::vector<double>)> f, const std::vector<double>& x) {
        hessian_result.clear();
        hessian_result.resize(x.size(), std::vector<double>(x.size()));
        
        for (int i = 0; i < x.size(); i++) {
            for (int j = 0; j < x.size(); j++) {
                std::vector<double> x_pp = x, x_pm = x, x_mp = x, x_mm = x;
                
                // For mixed partial derivatives
                x_pp[i] += epsilon; x_pp[j] += epsilon;
                x_pm[i] += epsilon; x_pm[j] -= epsilon;
                x_mp[i] -= epsilon; x_mp[j] += epsilon;
                x_mm[i] -= epsilon; x_mm[j] -= epsilon;
                
                hessian_result[i][j] = (f(x_pp) - f(x_pm) - f(x_mp) + f(x_mm)) / (4.0 * epsilon * epsilon);
            }
        }
        
        return hessian_result;
    }
    
    void displayHessian(const std::vector<double>& x) {
        std::cout << "Hessian matrix at (";
        for (int i = 0; i < x.size(); i++) {
            std::cout << x[i];
            if (i < x.size() - 1) std::cout << ", ";
        }
        std::cout << "):" << std::endl;
        
        for (const auto& row : hessian_result) {
            std::cout << "[";
            for (int j = 0; j < row.size(); j++) {
                std::cout << row[j];
                if (j < row.size() - 1) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
    }

    // AI-specific utility functions
    double neural_activation_sigmoid(double x) {
        return 1.0 / (1.0 + std::exp(-x));
    }
    
    double neural_activation_relu(double x) {
        return std::max(0.0, x);
    }
    
    double loss_function_mse(const std::vector<double>& predicted, const std::vector<double>& actual) {
        double sum = 0.0;
        for (int i = 0; i < predicted.size(); i++) {
            double diff = predicted[i] - actual[i];
            sum += diff * diff;
        }
        return sum / predicted.size();
    }
    
    // Example AI function: f(x,y) = x²y + xy² (common in neural networks)
    double ai_example_function(const std::vector<double>& vars) {
        if (vars.size() < 2) return 0.0;
        double x = vars[0], y = vars[1];
        return x * x * y + x * y * y;
    }
    
    void demonstrateAICalculus() {
        std::cout << "\n=== AI/ML Calculus Demonstrations ===" << std::endl;
        
        // Test point
        std::vector<double> point = {2.0, 3.0};
        
        // 1. Differentiation of sigmoid activation
        auto sigmoid = [this](double x) { return neural_activation_sigmoid(x); };
        differentiation(sigmoid, 1.0);
        std::cout << "Sigmoid derivative at x=1.0: " << differentiation_result << std::endl;
        
        // 2. Integration of a simple function
        auto simple_func = [](double x) { return x * x; };
        integration(simple_func, 0.0, 2.0);
        std::cout << "∫x² dx from 0 to 2: " << integration_result << std::endl;
        
        // 3. Gradient of AI function
        auto ai_func = [this](const std::vector<double>& v) { return ai_example_function(v); };
        gradient(ai_func, point);
        std::cout << "Gradient of f(x,y) = x²y + xy² at (2,3): [" 
                  << gradient_result[0] << ", " << gradient_result[1] << "]" << std::endl;
        
        // 4. Hessian for optimization
        hessian(ai_func, point);
        std::cout << "Hessian matrix (for optimization algorithms):" << std::endl;
        displayHessian(point);
    }
};