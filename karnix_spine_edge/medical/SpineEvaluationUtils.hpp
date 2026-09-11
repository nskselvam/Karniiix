/**
 * SpineEvaluationUtils.hpp
 * Karnix Spine Edge – Clinical Evaluation, Record Management, ASCII Heatmap
 *
 * Implements:
 *   - SpineRecord   : per-patient scan record
 *   - SpineRecordDB : lightweight JSON-based longitudinal store
 *   - SpineReport   : risk scoring, classification, formatted CLI output
 *   - AsciiHeatmap  : terminal-friendly activation heatmap renderer
 *
 * No external libraries.  C++14 compatible.
 */

#ifndef SPINE_EVALUATION_UTILS_HPP
#define SPINE_EVALUATION_UTILS_HPP

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <stdexcept>

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  SpineRecord                                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */
struct SpineRecord {
    std::string patient_id;
    int         age         = 0;
    std::string sex;
    std::string date;          // YYYY-MM-DD
    double      risk_score  = 0.0;  // structural integrity score (0-100)
    double      abnormal_prob = 0.0;
    std::string image_path;
    std::string classification; // "Normal" | "Abnormal"
    std::string modality = "XRAY";  // XRAY | DEXA | CT | MRI | AUTO
    double      trabecular_bone_score = 0.0;
    double      cortical_thinning_index = 0.0;
    double      marrow_dark_ratio = 0.0;
    std::string marrow_signal;
};

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Minimal JSON helpers (no external deps)                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */
namespace MiniJson {

    /* escape a string for JSON output */
    inline std::string escape(const std::string& s) {
        std::string out;
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        return out;
    }

    /* extract the raw value string for a given key within a single JSON object {} */
    inline std::string extract_value(const std::string& obj, const std::string& key) {
        std::string search = "\"" + key + "\"";
        std::size_t kpos = obj.find(search);
        if (kpos == std::string::npos) return "";
        std::size_t colon = obj.find(':', kpos + search.size());
        if (colon == std::string::npos) return "";
        // skip whitespace
        std::size_t vstart = colon + 1;
        while (vstart < obj.size() && std::isspace((unsigned char)obj[vstart])) ++vstart;
        if (vstart >= obj.size()) return "";
        if (obj[vstart] == '"') {
            // string value
            std::size_t end = vstart + 1;
            while (end < obj.size()) {
                if (obj[end] == '\\') { end += 2; continue; }
                if (obj[end] == '"') break;
                ++end;
            }
            return obj.substr(vstart + 1, end - vstart - 1);
        } else {
            // number / bool / null
            std::size_t end = vstart;
            while (end < obj.size() && obj[end] != ',' && obj[end] != '}' && obj[end] != ']')
                ++end;
            std::string val = obj.substr(vstart, end - vstart);
            // trim trailing whitespace
            while (!val.empty() && std::isspace((unsigned char)val.back())) val.pop_back();
            return val;
        }
    }

    /* split a JSON array string "[ {...}, {...} ]" into individual object strings */
    inline std::vector<std::string> split_array(const std::string& json) {
        std::vector<std::string> objects;
        int depth = 0;
        std::size_t start = 0;
        bool in_string = false;

        for (std::size_t i = 0; i < json.size(); ++i) {
            char c = json[i];
            if (c == '\\' && in_string) { ++i; continue; }
            if (c == '"') { in_string = !in_string; continue; }
            if (in_string) continue;

            if (c == '{') {
                if (depth == 0) start = i;
                ++depth;
            } else if (c == '}') {
                --depth;
                if (depth == 0)
                    objects.push_back(json.substr(start, i - start + 1));
            }
        }
        return objects;
    }

    /* render a SpineRecord as a JSON object string */
    inline std::string record_to_json(const SpineRecord& r) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "  {\n"
           << "    \"patient_id\": \""    << escape(r.patient_id)     << "\",\n"
           << "    \"age\": "             << r.age                    << ",\n"
           << "    \"sex\": \""           << escape(r.sex)            << "\",\n"
           << "    \"date\": \""          << escape(r.date)           << "\",\n"
           << "    \"risk_score\": "      << r.risk_score             << ",\n"
           << "    \"abnormal_prob\": "   << r.abnormal_prob          << ",\n"
           << "    \"classification\": \"" << escape(r.classification) << "\",\n"
           << "    \"image_path\": \""    << escape(r.image_path)     << "\",\n"
           << "    \"modality\": \""      << escape(r.modality)       << "\",\n"
           << "    \"trabecular_bone_score\": " << r.trabecular_bone_score << ",\n"
           << "    \"cortical_thinning_index\": " << r.cortical_thinning_index << ",\n"
           << "    \"marrow_dark_ratio\": " << r.marrow_dark_ratio << ",\n"
           << "    \"marrow_signal\": \"" << escape(r.marrow_signal) << "\"\n"
           << "  }";
        return ss.str();
    }
} // namespace MiniJson

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  SpineRecordDB                                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */
class SpineRecordDB {
private:
    std::string file_path;
    std::vector<SpineRecord> records;

    SpineRecord parse_record(const std::string& obj) const {
        SpineRecord r;
        r.patient_id     = MiniJson::extract_value(obj, "patient_id");
        r.sex            = MiniJson::extract_value(obj, "sex");
        r.date           = MiniJson::extract_value(obj, "date");
        r.classification = MiniJson::extract_value(obj, "classification");
        r.image_path     = MiniJson::extract_value(obj, "image_path");
        r.modality       = MiniJson::extract_value(obj, "modality");
        r.marrow_signal  = MiniJson::extract_value(obj, "marrow_signal");

        std::string age_s  = MiniJson::extract_value(obj, "age");
        std::string rs_s   = MiniJson::extract_value(obj, "risk_score");
        std::string ap_s   = MiniJson::extract_value(obj, "abnormal_prob");
        std::string tbs_s  = MiniJson::extract_value(obj, "trabecular_bone_score");
        std::string cti_s  = MiniJson::extract_value(obj, "cortical_thinning_index");
        std::string mdr_s  = MiniJson::extract_value(obj, "marrow_dark_ratio");

        if (!age_s.empty()) r.age           = std::stoi(age_s);
        if (!rs_s.empty())  r.risk_score    = std::stod(rs_s);
        if (!ap_s.empty())  r.abnormal_prob = std::stod(ap_s);
        if (!tbs_s.empty()) r.trabecular_bone_score = std::stod(tbs_s);
        if (!cti_s.empty()) r.cortical_thinning_index = std::stod(cti_s);
        if (!mdr_s.empty()) r.marrow_dark_ratio = std::stod(mdr_s);
        if (r.modality.empty()) r.modality = "XRAY";
        return r;
    }

public:
    explicit SpineRecordDB(const std::string& path) : file_path(path) {}

    void load() {
        records.clear();
        std::ifstream f(file_path);
        if (!f.is_open()) return;  // first run – file doesn't exist yet
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        for (auto& obj : MiniJson::split_array(content))
            records.push_back(parse_record(obj));
    }

    void save() const {
        std::ofstream f(file_path);
        if (!f.is_open()) throw std::runtime_error("Cannot write: " + file_path);
        f << "[\n";
        for (std::size_t i = 0; i < records.size(); ++i) {
            f << MiniJson::record_to_json(records[i]);
            if (i + 1 < records.size()) f << ",";
            f << "\n";
        }
        f << "]\n";
    }

    /* Returns nullptr if patient has no prior record */
    const SpineRecord* find_latest(const std::string& patient_id) const {
        const SpineRecord* latest = nullptr;
        for (const auto& r : records)
            if (r.patient_id == patient_id)
                latest = &r;   // last occurrence is most recent
        return latest;
    }

    void append(const SpineRecord& r) { records.push_back(r); }

    const std::vector<SpineRecord>& all() const { return records; }
    std::size_t size() const { return records.size(); }
};

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  SpineReport – scoring helpers & CLI output                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */
namespace SpineReport {

    struct Result {
        double normal_prob       = 0.0;
        double abnormal_prob     = 0.0;
        double integrity_score   = 0.0;  // (1 - abnormal_prob) * 100
        std::string risk_level;          // High / Moderate / Low
        std::string classification;      // Normal / Abnormal
        std::string lesion_suspicion;    // Low / Medium / High
        double confidence        = 0.0;  // max(probs) * 100
        double inference_ms      = 0.0;
        int    total_params      = 0;
        double model_size_mb     = 0.0;
    };

    inline Result compute(double normal_prob, double abnormal_prob,
                          double inference_ms, int total_params, double model_size_mb) {
        Result r;
        r.normal_prob     = normal_prob;
        r.abnormal_prob   = abnormal_prob;
        r.integrity_score = (1.0 - abnormal_prob) * 100.0;
        r.inference_ms    = inference_ms;
        r.total_params    = total_params;
        r.model_size_mb   = model_size_mb;

        // Classification
        r.classification = (abnormal_prob > 0.5) ? "Abnormal" : "Normal";

        // Confidence
        r.confidence = std::max(normal_prob, abnormal_prob) * 100.0;

        // Risk level (based on integrity score)
        if      (r.integrity_score < 40.0) r.risk_level = "High";
        else if (r.integrity_score < 70.0) r.risk_level = "Moderate";
        else                               r.risk_level = "Low";

        // Lesion suspicion (mirror of risk but named differently)
        if      (abnormal_prob > 0.70) r.lesion_suspicion = "High";
        else if (abnormal_prob > 0.35) r.lesion_suspicion = "Medium";
        else                           r.lesion_suspicion = "Low";

        return r;
    }

    inline void print_report(const Result& res) {
        std::cout << "\n";
        std::cout << "----------------------------------\n";
        std::cout << "STRUCTURAL REPORT\n";
        std::cout << "----------------------------------\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Structural Integrity Score : " << res.integrity_score << " / 100\n";
        std::cout << "Risk Level                 : " << res.risk_level      << "\n";
        std::cout << "Lesion Suspicion Level     : " << res.lesion_suspicion << "\n";
        std::cout << "Classification             : " << res.classification   << "\n";
        std::cout << "Confidence                 : " << res.confidence << "%\n";
        std::cout << "Inference Time             : " << res.inference_ms << " ms\n";
        std::cout << "Model Parameters           : " << res.total_params     << "\n";
        std::cout << "Model Size                 : " << std::setprecision(2) << res.model_size_mb << " MB\n";
        std::cout << "----------------------------------\n";
    }

    /* print longitudinal delta */
    inline void print_delta(double current_score, double previous_score,
                            const std::string& previous_date) {
        double delta = current_score - previous_score;
        std::cout << "\n[Longitudinal Trend vs " << previous_date << "]\n";
        if (std::abs(delta) < 0.5) {
            std::cout << "  Structural condition unchanged (Δ = "
                      << std::fixed << std::setprecision(1) << delta << "%)\n";
        } else if (delta < 0.0) {
            // score decreased → integrity worsened
            std::cout << "  Structural risk increased by "
                      << std::fixed << std::setprecision(1) << std::abs(delta)
                      << "% since last scan\n";
        } else {
            std::cout << "  Structural condition improved by "
                      << std::fixed << std::setprecision(1) << delta
                      << "% since last scan\n";
        }
    }

} // namespace SpineReport

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  AsciiHeatmap                                                               */
/* ═══════════════════════════════════════════════════════════════════════════ */
namespace AsciiHeatmap {

    static const char GRADIENT[] = " .:-=+*#%@";
    static const int  GRAD_LEN   = 10; // length of GRADIENT (excl. '\0')

    inline char intensity_to_char(double v) {
        // v must be in [0, 1]
        v = std::max(0.0, std::min(1.0, v));
        int idx = static_cast<int>(v * (GRAD_LEN - 1));
        return GRADIENT[idx];
    }

    inline void print(const std::vector<std::vector<double>>& heatmap,
                      const std::string& title = "Activation Heatmap") {
        if (heatmap.empty()) return;
        int rows = static_cast<int>(heatmap.size());
        int cols = static_cast<int>(heatmap[0].size());

        // Print border + title
        std::string border(cols * 2 + 4, '-');
        std::cout << "\n" << border << "\n";
        std::cout << "  " << title << "\n";
        std::cout << border << "\n";

        for (int r = 0; r < rows; ++r) {
            std::cout << "| ";
            for (int c = 0; c < cols; ++c) {
                char ch = intensity_to_char(heatmap[r][c]);
                std::cout << ch << ' ';
            }
            std::cout << "|\n";
        }
        std::cout << border << "\n";

        // Legend
        std::cout << "  Legend: ";
        for (int i = 0; i < GRAD_LEN; ++i)
            std::cout << GRADIENT[i] << " ";
        std::cout << "  (low → high activation)\n";
    }

} // namespace AsciiHeatmap

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Date helper                                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */
namespace DateUtil {
    inline std::string today() {
        time_t t = time(nullptr);
        struct tm* tm_info = localtime(&t);
        char buf[11];
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
        return std::string(buf);
    }
}

#endif // SPINE_EVALUATION_UTILS_HPP
