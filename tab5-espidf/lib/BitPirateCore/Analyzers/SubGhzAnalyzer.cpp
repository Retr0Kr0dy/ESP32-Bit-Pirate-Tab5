#include "SubGhzAnalyzer.h"
#include <cmath>
#include <sstream>
#include <unordered_set>

std::string SubGhzAnalyzer::analyzeFrame(const std::vector<rmt_symbol_word_t>& items, float tickPerUs) {
    SubGhzDetectResult r;
    if (items.empty()) { r.notes = "Empty frame"; return formatFrame(r); }

    std::vector<uint32_t> highs, lows;
    collectDurations(items, tickPerUs, highs, lows);
    r.pulseCount = items.size() * 2;

    float T = estimateBaseT(highs, lows);
    r.baseT_us = T;

    float ratio = 0.f;
    bool pulse  = looksPulseLength(T, highs, lows, ratio);
    bool manch  = looksManchester(T, highs, lows);
    bool pwm    = looksPWM(T, highs, lows);

    if (manch) r.encoding = RfEncoding::Manchester;
    else if (pulse) r.encoding = RfEncoding::PulseLength;
    else if (pwm) r.encoding = RfEncoding::PWM;
    else r.encoding = RfEncoding::Unknown;

    // Bitrate estimate
    if (r.encoding == RfEncoding::PulseLength) {
        std::vector<float> ds;
        ds.reserve(items.size() * 2);
        for (const auto& it : items) {
            float h = it.duration0 / tickPerUs;
            float l = it.duration1 / tickPerUs;
            if (h >= 2.f) ds.push_back(h);
            if (l >= 2.f) ds.push_back(l);
        }

        float S = (T > 0.f) ? T : 0.f;
        float L = 0.f;

        if (ds.size() >= 16) {
            float vmin = ds[0], vmax = ds[0];
            for (float d : ds) { if (d < vmin) vmin = d; if (d > vmax) vmax = d; }

            float c1 = vmin, c2 = vmax;
            for (int it = 0; it < 8; ++it) {
                float s1 = 0.f, s2 = 0.f; int n1 = 0, n2 = 0;
                for (float d : ds) {
                    if (std::fabs(d - c1) <= std::fabs(d - c2)) { s1 += d; ++n1; }
                    else                                        { s2 += d; ++n2; }
                }
                if (n1 > 0) c1 = s1 / n1;
                if (n2 > 0) c2 = s2 / n2;
            }

            S = (c1 < c2) ? c1 : c2;
            L = (c1 < c2) ? c2 : c1;
        }

        if (S > 0.f) {
            float sym = (L > 0.f) ? (S + L) : (3.f * S);
            if (sym > 0.f) r.bitrate_kbps = 1000.f / sym;
            r.baseT_us = S;
        }
    } else {
        if (r.encoding == RfEncoding::Manchester && T > 0.f) r.bitrate_kbps = 1000.f / (2.f * T);
        else if (T > 0.f) r.bitrate_kbps = 1000.f / T;
    }

    // Attempt decode
    if (r.encoding == RfEncoding::PulseLength) {
        std::string payload;
        int symbols = 0;
        if (decodePT2262Like(T, items, tickPerUs, payload, symbols)) {
            r.payloadHex = payload;
            r.bitCount   = symbols;

            // - If payload contains F => tri-state family (PT2262/SC2262/etc).
            // - If pure hex (only 0-9A-F) => binary code; often EV1527-ish or fixed-code remotes.
            bool hasF = (payload.find('F') != std::string::npos);

            if (hasF) {
                // Common tri-state lengths: 12, 24, 28 (varies by encoder + framing)
                if (symbols == 12 || symbols == 24 || symbols == 28) r.protocolGuess = "PT2262/SC2262-like";
                else r.protocolGuess = "Tri-state OOK";
            } else {
                r.protocolGuess = "PT2262/EV1527-like";
            }

            // Confidence: base on ratio + presence of a sane symbol count
            float c = 0.55f + 0.20f * (ratio > 1.9f);
            if (symbols >= 12 && symbols <= 40) c += 0.10f;
            r.confidence = clamp01(c);
        } else {
            r.protocolGuess = "Pulse-length";
            r.confidence = 0.30f;
        }
    } else if (r.encoding == RfEncoding::Manchester) {
        r.protocolGuess = "Manchester";
        r.confidence = 0.5f;
    } else if (r.encoding == RfEncoding::PWM) {
        r.protocolGuess = "PWM (generic)";
        r.confidence = 0.4f;
    } else {
        r.protocolGuess = "Unknown";
        r.confidence = 0.2f;
    }

    return formatFrame(r);
}

std::string SubGhzAnalyzer::analyzeFrequencyActivity(
    int dwellMs,
    int windowMs,
    int thresholdDbm,
    const std::function<int(int)>& measure,
    const std::function<bool()>& shouldAbort,
    float neighborLeftConf,
    float neighborRightConf
) {
    if (windowMs < 1) windowMs = 1;
    const int windows = std::max(1, dwellMs / windowMs);

    int hits = 0;
    int peakDbm = -127, minDbm = 127, maxDbm = -127;
    long long sum = 0;

    std::vector<int> samples;
    samples.reserve(windows);

    for (int i = 0; i < windows; ++i) {
        if (shouldAbort && shouldAbort()) break;
        const int v = measure(windowMs);
        samples.push_back(v);
        sum += v;
        if (v > peakDbm) peakDbm = v;
        if (v < minDbm)  minDbm  = v;
        if (v > maxDbm)  maxDbm  = v;
        if (v >= thresholdDbm)   hits++;
    }

    const int used = static_cast<int>(samples.size());
    const float avgDbm = (used > 0) ? static_cast<float>(sum) / used : -127.f;

    // Variance
    double var = 0.0;
    for (int v : samples) {
        const double d = static_cast<double>(v) - static_cast<double>(avgDbm);
        var += d * d;
    }
    var /= std::max(1, used);
    const float stddevDbm = static_cast<float>(std::sqrt(var));

    const float hitRatio = (used > 0) ? static_cast<float>(hits) / used : 0.f;

    // Confidence
    const float margin = static_cast<float>(peakDbm - thresholdDbm);
    float mScore = margin / 25.f;
    if (mScore < 0.f) mScore = 0.f;
    else if (mScore > 1.f) mScore = 1.f;

    float aScore = hitRatio;
    if (aScore < 0.f) aScore = 0.f;
    else if (aScore > 1.f) aScore = 1.f;

    float conf = 0.25f + 0.55f * mScore + 0.30f * aScore;

    if (hits == 0 || peakDbm < thresholdDbm) {
        conf = 0.f;                              // nothing
    } else if (hitRatio < 0.10f && (margin) < 5) {
        conf *= 0.3f;                            // rare
    }

    // Decode modulation
    auto mod = decodeModulationRSSI(samples);
    std::string modStr =
        (mod.first == ModGuess::ASK_OOK)   ? "ASK/OOK" :
        (mod.first == ModGuess::FSK_GFSK) ? "FSK/GFSK" :
        (mod.first == ModGuess::NFM_FM)   ? "NFM/FM"   : "Unknown";

    // Bonus stable
    if (margin >= 15.f && hitRatio >= 0.60f && stddevDbm <= 3.0f) conf += 0.05f;

    // Boost neighboring confidence
    if (neighborLeftConf  > 0.60f) conf += 0.03f;
    if (neighborRightConf > 0.60f) conf += 0.03f;
    if (neighborLeftConf  > 0.80f && neighborRightConf > 0.80f) conf += 0.02f;

    if (conf < 0.f) conf = 0.f;
    else if (conf > 1.f) conf = 1.f;

    auto formatted = formatFrequency(peakDbm, hits, used, avgDbm, stddevDbm, hitRatio, conf, modStr);
    return formatted;
}

std::string SubGhzAnalyzer::formatFrequency(
    int peakDbm,
    int hits,
    int windows,
    float avgDbm,
    float stddevDbm,
    float hitRatio,
    float confidence,
    std::string mod
) {
    std::ostringstream oss;
    oss << "peak=" << peakDbm << " dBm"
        << "  activity=" << static_cast<int>(hitRatio * 100.f) << '%'
        << "  conf=" << static_cast<int>(confidence * 100.f) << '%'
        << "  avg=" << static_cast<int>(std::lround(avgDbm)) << " dBm"
        << " (std=" << static_cast<int>(std::lround(stddevDbm)) << ")"
        << "  hits=" << hits << '/' << windows
        << "  mod=" << mod;
    return oss.str();
}


std::string SubGhzAnalyzer::formatFrame(const SubGhzDetectResult& r) const {
    auto encToStr = [](RfEncoding e){
        switch (e) {
            case RfEncoding::PulseLength: return "PulseLength";
            case RfEncoding::Manchester:  return "Manchester";
            case RfEncoding::PWM:         return "PWM";
            default:                      return "Unknown";
        }
    };

    std::ostringstream oss;
    oss << " [Frame received]\r\n"
        << " Pulse count  : " << std::to_string(r.pulseCount) << "\r\n"
        << " Encoding     : " << encToStr(r.encoding) << "\r\n"
        << " Base T (us)  : " << std::lround(r.baseT_us) << "\r\n"
        << " Bitrate (kb) : " << r.bitrate_kbps << "\r\n"
        << " Bit count    : " << r.bitCount << "\r\n"
        << " Payload      : " << (r.payloadHex.empty() ? "-" : r.payloadHex) << "\r\n"
        << " Protocol     : " << r.protocolGuess << "\r\n"
        << " Confidence   : " << r.confidence << "\r\n";
    if (!r.notes.empty()) oss << "Notes        : " << r.notes << "\r\n";
    return oss.str();
}

void SubGhzAnalyzer::collectDurations(const std::vector<rmt_symbol_word_t>& items, float tickPerUs,
                                            std::vector<uint32_t>& highs, std::vector<uint32_t>& lows) {
    highs.clear(); lows.clear();
    highs.reserve(items.size());
    lows.reserve(items.size());

    for (const auto& it : items) {
        // Convert RMT ticks
        uint32_t d0 = (uint32_t)std::max(1.f, std::round(it.duration0 / tickPerUs));
        uint32_t d1 = (uint32_t)std::max(1.f, std::round(it.duration1 / tickPerUs));
        highs.push_back(d0);
        lows.push_back(d1);
    }
}

float SubGhzAnalyzer::median(std::vector<uint32_t> v) {
    if (v.empty()) return 0.f;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n & 1) ? (float)v[n/2] : 0.5f * (v[n/2 - 1] + v[n/2]);
}

float SubGhzAnalyzer::estimateBaseT(const std::vector<uint32_t>& highs, const std::vector<uint32_t>& lows) {
    // Take short durations as approximation
    std::vector<uint32_t> pool;
    pool.reserve(highs.size() + lows.size());
    for (auto d : highs) if (d >= 2) pool.push_back(d);
    for (auto d : lows)  if (d >= 2) pool.push_back(d);
    if (pool.size() < 8) return 0.f;

    std::sort(pool.begin(), pool.end());
    size_t m = std::max<size_t>(4, pool.size()/4);
    std::vector<uint32_t> q(pool.begin(), pool.begin() + m);
    return median(q);
}

bool SubGhzAnalyzer::looksManchester(float T, const std::vector<uint32_t>& highs, const std::vector<uint32_t>& lows) {
    if (T <= 0.f || highs.size() < 8) return false;
    int ok = 0, total = 0;
    float tol = T * 0.40f; // tolerance
    for (size_t i = 0; i < highs.size(); ++i) {
        total++;
        if (nearf(highs[i], T, tol) && nearf(lows[i], T, tol)) ok++;
    }
    return total >= 8 && (float)ok / total > 0.6f;
}

bool SubGhzAnalyzer::looksPulseLength(float T,
                                            const std::vector<uint32_t>& highs,
                                            const std::vector<uint32_t>& lows,
                                            float& ratioOut)
{
    ratioOut = 0.f;
    if (highs.size() < 8 || lows.size() < 8) return false;

    // Build a duration pool
    std::vector<float> ds;
    ds.reserve(highs.size() + lows.size());
    for (auto d : highs) if (d >= 2) ds.push_back((float)d);
    for (auto d : lows)  if (d >= 2) ds.push_back((float)d);
    if (ds.size() < 16) return false;

    // 1D k-means (k=2) to find short/long clusters
    float vmin = ds[0], vmax = ds[0];
    for (float d : ds) { if (d < vmin) vmin = d; if (d > vmax) vmax = d; }

    float c1 = vmin, c2 = vmax;
    for (int it = 0; it < 8; ++it) {
        float s1 = 0.f, s2 = 0.f; int n1 = 0, n2 = 0;
        for (float d : ds) {
            if (std::fabs(d - c1) <= std::fabs(d - c2)) { s1 += d; ++n1; }
            else                                        { s2 += d; ++n2; }
        }
        if (n1 > 0) c1 = s1 / n1;
        if (n2 > 0) c2 = s2 / n2;
    }

    float S = (c1 < c2) ? c1 : c2;
    float L = (c1 < c2) ? c2 : c1;
    if (S <= 0.f || L <= 0.f) return false;

    float ratio = L / S;
    ratioOut = ratio;

    // Typical OOK pulse length: long 2*short (sometimes 3*short)
    if (ratio < 1.45f || ratio > 3.60f) return false;

    // Check classification coverage
    const float tolS = 0.30f * S;  // 30%
    const float tolL = 0.30f * L;  // 30%
    int ok = 0;
    for (float d : ds) {
        bool nearS = (std::fabs(d - S) <= tolS);
        bool nearL = (std::fabs(d - L) <= tolL);
        if (nearS || nearL) ok++;
    }

    float coverage = (ds.empty() ? 0.f : (float)ok / (float)ds.size());
    return coverage >= 0.70f;
}

bool SubGhzAnalyzer::looksPWM(float T, const std::vector<uint32_t>& highs, const std::vector<uint32_t>& lows) {
    if (T <= 0.f || highs.size() < 8) return false;

    auto medH = median(highs);
    auto medL = median(lows);
    float varH = 0.f, varL = 0.f;
    for (auto d : highs) varH += std::fabs((float)d - medH);
    for (auto d : lows)  varL += std::fabs((float)d - medL);
    varH /= std::max<size_t>(1, highs.size());
    varL /= std::max<size_t>(1, lows.size());

    return (varH < 0.25f * varL) || (varL < 0.25f * varH);
}

bool SubGhzAnalyzer::decodePT2262Like(float /*T*/,
                                            const std::vector<rmt_symbol_word_t>& items,
                                            float tickPerUs,
                                            std::string& hexOut,
                                            int& bitCountOut)
{
    hexOut.clear();
    bitCountOut = 0;
    if (items.size() < 8) return false;

    // Estimate Short/Long via 1D k-means (k=2)
    std::vector<float> ds;
    ds.reserve(items.size() * 2);

    float maxLow = 0.f;
    for (const auto& it : items) {
        float h = it.duration0 / tickPerUs;
        float l = it.duration1 / tickPerUs;
        if (h >= 2.f) ds.push_back(h);
        if (l >= 2.f) ds.push_back(l);
        if (l > maxLow) maxLow = l;
    }
    if (ds.size() < 16) return false;

    float vmin = ds[0], vmax = ds[0];
    for (float d : ds) { if (d < vmin) vmin = d; if (d > vmax) vmax = d; }

    float c1 = vmin, c2 = vmax;
    for (int it = 0; it < 8; ++it) {
        float s1 = 0.f, s2 = 0.f; int n1 = 0, n2 = 0;
        for (float d : ds) {
            if (std::fabs(d - c1) <= std::fabs(d - c2)) { s1 += d; ++n1; }
            else                                        { s2 += d; ++n2; }
        }
        if (n1 > 0) c1 = s1 / n1;
        if (n2 > 0) c2 = s2 / n2;
    }

    float S = (c1 < c2) ? c1 : c2;
    float L = (c1 < c2) ? c2 : c1;
    if (S <= 0.f || L <= 0.f) return false;

    // Classifiers
    const float tolS = 0.30f * S;
    const float tolL = 0.30f * L;

    auto isS = [&](float d){ return std::fabs(d - S) <= tolS; };
    auto isL = [&](float d){ return std::fabs(d - L) <= tolL; };
    // Baseline: 8*S
    float syncMin = 8.f * S;

    // If there's a clear huge low gap (outlier), use it to set threshold
    // Condition: maxLow much larger than typical long low.
    if (maxLow > 4.f * L) {
        float candidate = 0.40f * maxLow;
        if (candidate > syncMin) syncMin = candidate;
    }

    // Decode to raw trits/bits, stop at first sync after data
    std::string raw;
    raw.reserve(items.size());

    int total = 0;
    int good  = 0;

    for (const auto& it : items) {
        float h = it.duration0 / tickPerUs;
        float l = it.duration1 / tickPerUs;
        if (h < 2.f || l < 2.f) continue;

        // Stop at sync AFTER we started collecting (avoid concatenation of repeats)
        if (l >= syncMin) {
            if (!raw.empty()) break;   // end of frame
            continue;                  // ignore leading gap / preamble
        }

        bool hS = isS(h), hL = isL(h);
        bool lS = isS(l), lL = isL(l);

        total++;

        // Classified cleanly?
        if ((hS || hL) && (lS || lL)) good++;

        // Tri-state mapping:
        // 0 = S+L, 1 = L+S, F = S+S
        if (hS && lL)      raw.push_back('0');
        else if (hL && lS) raw.push_back('1');
        else if (hS && lS) raw.push_back('F');
        else if (hL && lL) raw.push_back('F');
        else {
            // fallback nearest (counts as low-quality)
            float dhS = std::fabs(h - S), dhL = std::fabs(h - L);
            float dlS = std::fabs(l - S), dlL = std::fabs(l - L);
            bool hhS = (dhS <= dhL);
            bool llS = (dlS <= dlL);

            if (hhS && !llS)      raw.push_back('0');
            else if (!hhS && llS) raw.push_back('1');
            else                  raw.push_back('F');
        }
    }

    if ((int)raw.size() < 12) return false;

    // Reject if too many fallbacks / poor clustering fit
    float quality = (total > 0) ? ((float)good / (float)total) : 0.f;
    if (quality < 0.55f) return false;

    // If purely binary => HEX, else keep raw "01F..."
    bool isBinary = true;
    for (char c : raw) {
        if (c != '0' && c != '1') { isBinary = false; break; }
    }

    if (isBinary) {
        int n = (int)(raw.size() & ~0x3);
        if (n < 4) {
            bitCountOut = (int)raw.size();
            hexOut = raw;
            return true;
        }
        raw.resize((size_t)n);
        bitCountOut = n;
        hexOut = bitsToHex(raw);
        return true;
    } else {
        bitCountOut = (int)raw.size();
        hexOut = raw;
        return true;
    }
}

bool SubGhzAnalyzer::nearf(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

float SubGhzAnalyzer::clamp01(float v) {
    if (v < 0.f) return 0.f;
    if (v > 1.f) return 1.f;
    return v;
}

std::string SubGhzAnalyzer::bitsToHex(const std::string& bits) {
    static const char* HEX = "0123456789ABCDEF";
    std::string out;
    out.reserve(bits.size()/4);
    for (size_t i = 0; i + 3 < bits.size(); i += 4) {
        int v = ((bits[i]-'0')<<3) | ((bits[i+1]-'0')<<2) | ((bits[i+2]-'0')<<1) | (bits[i+3]-'0');
        out.push_back(HEX[v & 0xF]);
    }
    return out;
}

std::pair<SubGhzAnalyzer::ModGuess, float>
SubGhzAnalyzer::decodeModulationRSSI(const std::vector<int>& samples) {
    if (samples.size() < 8) return {ModGuess::Unknown, 0.f};

    // k means
    int vmin = 127, vmax = -127;
    for (size_t i = 0; i < samples.size(); ++i) {
        int v = samples[i];
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }

    double c1 = vmin, c2 = vmax;
    for (int it = 0; it < 6; ++it) {
        double s1 = 0.0, s2 = 0.0; int n1 = 0, n2 = 0;
        for (size_t i = 0; i < samples.size(); ++i) {
            int v = samples[i];
            if (std::fabs(v - c1) <= std::fabs(v - c2)) { s1 += v; ++n1; }
            else                                         { s2 += v; ++n2; }
        }
        if (n1 > 0) c1 = s1 / n1;
        if (n2 > 0) c2 = s2 / n2;
    }

    // Variances in clusters
    double s1 = 0.0, s2 = 0.0; int n1 = 0, n2 = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
        int v = samples[i];
        if (std::fabs(v - c1) <= std::fabs(v - c2)) { s1 += (v - c1) * (v - c1); ++n1; }
        else                                         { s2 += (v - c2) * (v - c2); ++n2; }
    }
    double v1 = (n1 > 1) ? (s1 / (n1 - 1)) : 1.0;
    double v2 = (n2 > 1) ? (s2 / (n2 - 1)) : 1.0;

    // Normalize
    double sep = std::fabs(c1 - c2);
    double sp  = std::sqrt(v1 + v2 + 1e-6);
    double d   = (sp > 0.0) ? (sep / sp) : 0.0;

    double p1 = (samples.empty() ? 0.0 : double(n1) / double(samples.size()));
    double p2 = (samples.empty() ? 0.0 : double(n2) / double(samples.size()));
    double pmin = (p1 < p2) ? p1 : p2;

    // Global stats
    double mean = 0.0;
    for (size_t i = 0; i < samples.size(); ++i) mean += samples[i];
    mean /= samples.size();

    double var = 0.0;
    for (size_t i = 0; i < samples.size(); ++i) {
        double e = samples[i] - mean;
        var += e * e;
    }
    var /= samples.size();
    double sd = std::sqrt(var);

    // ASK/OOK : bimodal
    if (d >= 1.6 && pmin >= 0.15) {
        double t = d / 3.0;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        double score = t * (pmin / 0.5);
        if (score < 0.0) score = 0.0;
        if (score > 1.0) score = 1.0;
        return {ModGuess::ASK_OOK, float(score)};
    }

    // FSK/GFSK : weak amplitude variation
    if (sd <= 2.5 && d < 0.9) {
        double t = (2.5 - sd) / 2.5;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        return {ModGuess::FSK_GFSK, float(t)};
    }

    // NFM Moderate amplitude variation
    if (sd >= 3.0 && sd <= 10.0 && d < 1.2) {
        float sSd = float((sd - 3.0) / 7.0);   // 0..1
        if (sSd < 0.0f) sSd = 0.0f; if (sSd > 1.0f) sSd = 1.0f;
        float sD  = float((1.2 - d) / 1.2);    // 0..1
        if (sD  < 0.0f) sD  = 0.0f; if (sD  > 1.0f) sD  = 1.0f;

        float score = 0.85f * sSd + 0.15f * sD;
        if (score < 0.0f) score = 0.0f;
        if (score > 1.0f) score = 1.0f;
        return {ModGuess::NFM_FM, score};
    }

    return {ModGuess::Unknown, 0.f};
}
