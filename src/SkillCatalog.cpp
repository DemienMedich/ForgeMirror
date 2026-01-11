#include "SkillCatalog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <unordered_set>

namespace {

struct SkillEntry {
    const char* name;
    double weight;
    const char* description;
};

void append_utf8(std::string& out, uint32_t cp);

const std::vector<SkillEntry> kDefaultSkills = {};

double clamp_weight(double value) {
    const double minW = 0.5;
    const double maxW = 1.6;
    if (value < minW) return minW;
    if (value > maxW) return maxW;
    return value;
}

bool parse_double_ascii(const std::string& value, double& outValue) {
    if (value.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (value[i] == '-') {
        neg = true;
        ++i;
    }
    double intPart = 0.0;
    bool hasDigit = false;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
        intPart = intPart * 10.0 + static_cast<double>(value[i] - '0');
        ++i;
        hasDigit = true;
    }
    double fracPart = 0.0;
    double fracScale = 1.0;
    if (i < value.size() && value[i] == '.') {
        ++i;
        while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
            fracPart = fracPart * 10.0 + static_cast<double>(value[i] - '0');
            fracScale *= 10.0;
            ++i;
            hasDigit = true;
        }
    }
    if (!hasDigit) return false;
    double result = intPart + (fracPart / fracScale);
    outValue = neg ? -result : result;
    return true;
}

double parse_weight(const std::string& value, double fallback) {
    std::string out;
    out.reserve(value.size());
    bool hasDigit = false;
    for (unsigned char ch : value) {
        if (ch >= '0' && ch <= '9') {
            out.push_back(static_cast<char>(ch));
            hasDigit = true;
        } else if (ch == '.' || ch == ',') {
            out.push_back('.');
        } else if (ch == '-' && out.empty()) {
            out.push_back('-');
        }
    }
    if (!hasDigit) return fallback;
    double parsed = fallback;
    if (!parse_double_ascii(out, parsed)) return fallback;
    return parsed;
}

bool try_parse_weight(const std::string& value, double& outValue) {
    std::string out;
    out.reserve(value.size());
    bool hasDigit = false;
    for (unsigned char ch : value) {
        if (ch >= '0' && ch <= '9') {
            out.push_back(static_cast<char>(ch));
            hasDigit = true;
        } else if (ch == '.' || ch == ',') {
            out.push_back('.');
        } else if (ch == '-' && out.empty()) {
            out.push_back('-');
        }
    }
    if (!hasDigit) return false;
    return parse_double_ascii(out, outValue);
}

std::string DecodeUtf16Bytes(const std::string& bytes, bool bigEndian) {
    std::string out;
    out.reserve(bytes.size());
    size_t i = 0;
    const size_t size = bytes.size() - (bytes.size() % 2);
    while (i + 1 < size) {
        uint16_t unit = 0;
        unsigned char b0 = static_cast<unsigned char>(bytes[i]);
        unsigned char b1 = static_cast<unsigned char>(bytes[i + 1]);
        unit = bigEndian ? static_cast<uint16_t>((b0 << 8) | b1)
                         : static_cast<uint16_t>((b1 << 8) | b0);
        i += 2;
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < size) {
            unsigned char c0 = static_cast<unsigned char>(bytes[i]);
            unsigned char c1 = static_cast<unsigned char>(bytes[i + 1]);
            uint16_t lo = bigEndian ? static_cast<uint16_t>((c0 << 8) | c1)
                                    : static_cast<uint16_t>((c1 << 8) | c0);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                uint32_t cp = 0x10000u + (((unit - 0xD800u) << 10) | (lo - 0xDC00u));
                append_utf8(out, cp);
                i += 2;
                continue;
            }
        }
        append_utf8(out, unit);
    }
    return out;
}

std::string DecodeTextFileContent(const std::string& bytes) {
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        return bytes.substr(3);
    }
    if (bytes.size() >= 2) {
        unsigned char b0 = static_cast<unsigned char>(bytes[0]);
        unsigned char b1 = static_cast<unsigned char>(bytes[1]);
        if (b0 == 0xFF && b1 == 0xFE) {
            return DecodeUtf16Bytes(bytes.substr(2), false);
        }
        if (b0 == 0xFE && b1 == 0xFF) {
            return DecodeUtf16Bytes(bytes.substr(2), true);
        }
    }
    return bytes;
}

bool decode_utf8(const std::string& s, size_t& i, uint32_t& out) {
    unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        out = c0;
        ++i;
        return true;
    }
    if ((c0 >> 5) == 0x6 && i + 1 < s.size()) {
        unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        if ((c1 & 0xC0) != 0x80) return false;
        out = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        i += 2;
        return true;
    }
    if ((c0 >> 4) == 0xE && i + 2 < s.size()) {
        unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
        out = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        i += 3;
        return true;
    }
    if ((c0 >> 3) == 0x1E && i + 3 < s.size()) {
        unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
        out = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        i += 4;
        return true;
    }
    return false;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool encode_cp1251(uint32_t cp, unsigned char& out) {
    if (cp <= 0x7F) {
        out = static_cast<unsigned char>(cp);
        return true;
    }
    if (cp >= 0x0410 && cp <= 0x044F) {
        out = static_cast<unsigned char>(0xC0 + (cp - 0x0410));
        return true;
    }
    switch (cp) {
        case 0x0401: out = 0xA8; return true;
        case 0x0451: out = 0xB8; return true;
        case 0x0402: out = 0x80; return true;
        case 0x0403: out = 0x81; return true;
        case 0x201A: out = 0x82; return true;
        case 0x0453: out = 0x83; return true;
        case 0x201E: out = 0x84; return true;
        case 0x2026: out = 0x85; return true;
        case 0x2020: out = 0x86; return true;
        case 0x2021: out = 0x87; return true;
        case 0x20AC: out = 0x88; return true;
        case 0x2030: out = 0x89; return true;
        case 0x0409: out = 0x8A; return true;
        case 0x2039: out = 0x8B; return true;
        case 0x040A: out = 0x8C; return true;
        case 0x040C: out = 0x8D; return true;
        case 0x040B: out = 0x8E; return true;
        case 0x040F: out = 0x8F; return true;
        case 0x0452: out = 0x90; return true;
        case 0x2018: out = 0x91; return true;
        case 0x2019: out = 0x92; return true;
        case 0x201C: out = 0x93; return true;
        case 0x201D: out = 0x94; return true;
        case 0x2022: out = 0x95; return true;
        case 0x2013: out = 0x96; return true;
        case 0x2014: out = 0x97; return true;
        case 0x2122: out = 0x99; return true;
        case 0x0459: out = 0x9A; return true;
        case 0x203A: out = 0x9B; return true;
        case 0x045A: out = 0x9C; return true;
        case 0x045C: out = 0x9D; return true;
        case 0x045B: out = 0x9E; return true;
        case 0x045F: out = 0x9F; return true;
        case 0x00A0: out = 0xA0; return true;
        case 0x040E: out = 0xA1; return true;
        case 0x045E: out = 0xA2; return true;
        case 0x0408: out = 0xA3; return true;
        case 0x00A4: out = 0xA4; return true;
        case 0x0490: out = 0xA5; return true;
        case 0x00A6: out = 0xA6; return true;
        case 0x00A7: out = 0xA7; return true;
        case 0x00A9: out = 0xA9; return true;
        case 0x0404: out = 0xAA; return true;
        case 0x00AB: out = 0xAB; return true;
        case 0x00AC: out = 0xAC; return true;
        case 0x00AD: out = 0xAD; return true;
        case 0x00AE: out = 0xAE; return true;
        case 0x0407: out = 0xAF; return true;
        case 0x00B0: out = 0xB0; return true;
        case 0x00B1: out = 0xB1; return true;
        case 0x0406: out = 0xB2; return true;
        case 0x0456: out = 0xB3; return true;
        case 0x0491: out = 0xB4; return true;
        case 0x00B5: out = 0xB5; return true;
        case 0x00B6: out = 0xB6; return true;
        case 0x00B7: out = 0xB7; return true;
        case 0x2116: out = 0xB9; return true;
        case 0x0454: out = 0xBA; return true;
        case 0x00BB: out = 0xBB; return true;
        case 0x0458: out = 0xBC; return true;
        case 0x0405: out = 0xBD; return true;
        case 0x0455: out = 0xBE; return true;
        case 0x0457: out = 0xBF; return true;
        default: break;
    }
    return false;
}

uint32_t decode_cp1251_byte(unsigned char b) {
    if (b <= 0x7F) return b;
    switch (b) {
        case 0x80: return 0x0402;
        case 0x81: return 0x0403;
        case 0x82: return 0x201A;
        case 0x83: return 0x0453;
        case 0x84: return 0x201E;
        case 0x85: return 0x2026;
        case 0x86: return 0x2020;
        case 0x87: return 0x2021;
        case 0x88: return 0x20AC;
        case 0x89: return 0x2030;
        case 0x8A: return 0x0409;
        case 0x8B: return 0x2039;
        case 0x8C: return 0x040A;
        case 0x8D: return 0x040C;
        case 0x8E: return 0x040B;
        case 0x8F: return 0x040F;
        case 0x90: return 0x0452;
        case 0x91: return 0x2018;
        case 0x92: return 0x2019;
        case 0x93: return 0x201C;
        case 0x94: return 0x201D;
        case 0x95: return 0x2022;
        case 0x96: return 0x2013;
        case 0x97: return 0x2014;
        case 0x99: return 0x2122;
        case 0x9A: return 0x0459;
        case 0x9B: return 0x203A;
        case 0x9C: return 0x045A;
        case 0x9D: return 0x045C;
        case 0x9E: return 0x045B;
        case 0x9F: return 0x045F;
        case 0xA0: return 0x00A0;
        case 0xA1: return 0x040E;
        case 0xA2: return 0x045E;
        case 0xA3: return 0x0408;
        case 0xA4: return 0x00A4;
        case 0xA5: return 0x0490;
        case 0xA6: return 0x00A6;
        case 0xA7: return 0x00A7;
        case 0xA8: return 0x0401;
        case 0xA9: return 0x00A9;
        case 0xAA: return 0x0404;
        case 0xAB: return 0x00AB;
        case 0xAC: return 0x00AC;
        case 0xAD: return 0x00AD;
        case 0xAE: return 0x00AE;
        case 0xAF: return 0x0407;
        case 0xB0: return 0x00B0;
        case 0xB1: return 0x00B1;
        case 0xB2: return 0x0406;
        case 0xB3: return 0x0456;
        case 0xB4: return 0x0491;
        case 0xB5: return 0x00B5;
        case 0xB6: return 0x00B6;
        case 0xB7: return 0x00B7;
        case 0xB8: return 0x0451;
        case 0xB9: return 0x2116;
        case 0xBA: return 0x0454;
        case 0xBB: return 0x00BB;
        case 0xBC: return 0x0458;
        case 0xBD: return 0x0405;
        case 0xBE: return 0x0455;
        case 0xBF: return 0x0457;
        default:
            if (b >= 0xC0 && b <= 0xFF) {
                return 0x0410 + (b - 0xC0);
            }
            return 0x003F; // '?'
    }
}

uint32_t lower_codepoint(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
    if (cp == 0x0401) return 0x0451;
    return cp;
}

bool is_cyrillic(uint32_t cp) {
    return (cp >= 0x0410 && cp <= 0x044F) || cp == 0x0401 || cp == 0x0451;
}

bool is_mojibake_marker(uint32_t cp) {
    switch (cp) {
        case 0x00A0:
        case 0x00B1:
        case 0x00B5:
        case 0x201A:
        case 0x201E:
        case 0x2026:
        case 0x2022:
        case 0x0402:
        case 0x0403:
        case 0x0408:
        case 0x0409:
        case 0x040A:
        case 0x040B:
        case 0x040C:
        case 0x040E:
        case 0x040F:
        case 0x0452:
        case 0x0453:
        case 0x0455:
        case 0x0458:
        case 0x0459:
        case 0x045A:
        case 0x045B:
        case 0x045C:
        case 0x045E:
        case 0x045F:
        case 0x0490:
        case 0x0491:
            return true;
        default:
            return false;
    }
}

struct TextQuality {
    int cyrillic = 0;
    int markers = 0;
    int invalid = 0;
};

TextQuality MeasureTextQuality(const std::string& text) {
    TextQuality q;
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = 0;
        size_t next = i;
        if (!decode_utf8(text, next, cp)) {
            q.invalid += 1;
            i += 1;
            continue;
        }
        i = next;
        if (is_cyrillic(cp)) q.cyrillic += 1;
        if (is_mojibake_marker(cp)) q.markers += 1;
    }
    return q;
}

int MojibakeScore(const TextQuality& q) {
    return q.markers * 2 + q.invalid * 3 - q.cyrillic;
}

bool LooksLikeMojibake(const TextQuality& q) {
    return q.markers > 0 && q.cyrillic > 0;
}

std::string FixMojibakeCp1251Utf8(const std::string& text) {
    std::string bytes;
    bytes.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = 0;
        size_t next = i;
        if (!decode_utf8(text, next, cp)) {
            return text;
        }
        i = next;
        unsigned char b = 0;
        if (!encode_cp1251(cp, b)) {
            return text;
        }
        bytes.push_back(static_cast<char>(b));
    }
    std::string out;
    out.reserve(bytes.size());
    size_t j = 0;
    while (j < bytes.size()) {
        uint32_t cp = 0;
        if (!decode_utf8(bytes, j, cp)) {
            return text;
        }
        append_utf8(out, cp);
    }
    return out;
}

std::string DecodeCp1251ToUtf8(const std::string& bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (unsigned char b : bytes) {
        append_utf8(out, decode_cp1251_byte(b));
    }
    return out;
}

std::string MaybeFixMojibake(const std::string& text) {
    if (text.empty()) return text;
    const TextQuality before = MeasureTextQuality(text);
    int bestScore = MojibakeScore(before);
    std::string best = text;

    if (LooksLikeMojibake(before)) {
        const std::string fixed = FixMojibakeCp1251Utf8(text);
        if (fixed != text) {
            const int score = MojibakeScore(MeasureTextQuality(fixed));
            if (score < bestScore) {
                bestScore = score;
                best = fixed;
            }
        }
    }

    // Try full CP1251 decode if text was likely saved as CP1251 bytes.
    const std::string cpCandidate = DecodeCp1251ToUtf8(text);
    if (cpCandidate != text) {
        const int score = MojibakeScore(MeasureTextQuality(cpCandidate));
        if (score < bestScore) {
            bestScore = score;
            best = cpCandidate;
        }
    }

    return best;
}

std::string lowercase_utf8(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    size_t i = 0;
    while (i < value.size()) {
        uint32_t cp = 0;
        size_t next = i;
        if (!decode_utf8(value, next, cp)) {
            out.push_back(value[i]);
            ++i;
            continue;
        }
        i = next;
        append_utf8(out, lower_codepoint(cp));
    }
    return out;
}

} // namespace

SkillCatalog::SkillCatalog(std::filesystem::path baseDir)
    : baseDir_(std::move(baseDir)) {
    load();
}

bool SkillCatalog::contains_id(const std::string& id) const {
    return namesById_.count(id) > 0;
}

bool SkillCatalog::contains_name(const std::string& name) const {
    return idByName_.count(normalize(name)) > 0;
}

std::optional<std::string> SkillCatalog::id_for_name(const std::string& name) const {
    auto norm = normalize(name);
    auto it = idByName_.find(norm);
    if (it == idByName_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> SkillCatalog::resolve_id(const std::string& idOrName) const {
    if (contains_id(idOrName)) return idOrName;
    return id_for_name(idOrName);
}

double SkillCatalog::weight(const std::string& skill) const {
    auto id = resolve_id(skill);
    if (id) {
        auto it = weightsById_.find(*id);
        if (it != weightsById_.end()) return it->second;
    }
    return 1.0;
}

std::string SkillCatalog::display_name(const std::string& id) const {
    if (auto resolved = resolve_id(id)) {
        auto it = namesById_.find(*resolved);
        if (it != namesById_.end()) return it->second;
    }
    return id;
}

std::string SkillCatalog::description(const std::string& id) const {
    if (auto resolved = resolve_id(id)) {
        auto it = descriptionsById_.find(*resolved);
        if (it != descriptionsById_.end()) return it->second;
    }
    return {};
}

bool WriteTextFileAtomic(const std::filesystem::path& path, const std::string& data) {
    std::error_code ec;
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << data;
        if (!out.good()) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

void SkillCatalog::reload() {
    load();
}

bool SkillCatalog::add_skill(const std::string& skill, double weight, const std::string& description) {
    std::string trimmed = trim(skill);
    if (trimmed.empty()) return false;
    weight = clamp_weight(weight);
    std::string desc = trim(description);

    auto norm = normalize(trimmed);
    auto it = idByName_.find(norm);
    if (it != idByName_.end()) {
        const std::string& id = it->second;
        double& storedWeight = weightsById_[id];
        std::string& storedDesc = descriptionsById_[id];
        bool changed = false;
        if (std::abs(storedWeight - weight) >= 1e-3) {
            storedWeight = weight;
            changed = true;
        }
        if (storedDesc != desc) {
            storedDesc = std::move(desc);
            changed = true;
        }
        if (changed) save();
        return changed;
    }

    const std::string id = make_id(trimmed);
    add_internal(id, trimmed, weight, desc, true);
    return true;
}

bool SkillCatalog::update_skill(const std::string& id, const std::string& displayName, double weight, const std::string& description) {
    auto resolved = resolve_id(id);
    if (!resolved) return false;
    std::string trimmed = trim(displayName);
    if (trimmed.empty()) return false;
    weight = clamp_weight(weight);
    std::string desc = trim(description);

    const std::string currentId = *resolved;
    const std::string newNorm = normalize(trimmed);
    auto existing = idByName_.find(newNorm);
    if (existing != idByName_.end() && existing->second != currentId) {
        return false;
    }

    bool changed = false;
    auto nameIt = namesById_.find(currentId);
    if (nameIt != namesById_.end() && nameIt->second != trimmed) {
        if (nameIt->second.size()) {
            idByName_.erase(normalize(nameIt->second));
        }
        nameIt->second = trimmed;
        idByName_[newNorm] = currentId;
        changed = true;
    }

    double& storedWeight = weightsById_[currentId];
    if (std::abs(storedWeight - weight) >= 1e-3) {
        storedWeight = weight;
        changed = true;
    }
    std::string& storedDesc = descriptionsById_[currentId];
    if (storedDesc != desc) {
        storedDesc = std::move(desc);
        changed = true;
    }
    if (changed) save();
    return changed;
}

bool SkillCatalog::remove_skill(const std::string& idOrName) {
    auto resolved = resolve_id(idOrName);
    if (!resolved) return false;
    const std::string id = *resolved;
    auto nameIt = namesById_.find(id);
    if (nameIt == namesById_.end()) return false;

    idByName_.erase(normalize(nameIt->second));
    namesById_.erase(id);
    weightsById_.erase(id);
    descriptionsById_.erase(id);
    orderedIds_.erase(std::remove(orderedIds_.begin(), orderedIds_.end(), id), orderedIds_.end());
    save();
    return true;
}

void SkillCatalog::load() {
    orderedIds_.clear();
    idByName_.clear();
    namesById_.clear();
    weightsById_.clear();
    descriptionsById_.clear();
    bool repaired = false;

    std::ifstream in(file_path(), std::ios::binary);
    if (!in) {
        for (const auto& entry : kDefaultSkills) {
            add_internal(make_id(entry.name), entry.name, entry.weight, entry.description, false);
        }
        save();
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string content = DecodeTextFileContent(buffer.str());
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        std::vector<std::string> parts;
        std::string part;
        std::istringstream ss(trimmed);
        while (std::getline(ss, part, '|')) {
            parts.push_back(trim(part));
        }
        std::string id;
        std::string name;
        std::string desc;
        double weight = 1.0;
        auto append_desc = [&](size_t start) {
            if (start >= parts.size()) return;
            desc = parts[start];
            for (size_t i = start + 1; i < parts.size(); ++i) {
                if (!desc.empty()) desc += "|";
                desc += parts[i];
            }
        };

        if (parts.size() >= 4) {
            id = parts[0];
            name = parts[1];
            double parsed = 1.0;
            if (try_parse_weight(parts[2], parsed)) {
                weight = parsed;
                append_desc(3);
            } else if (try_parse_weight(parts[3], parsed)) {
                weight = parsed;
                desc = parts[2];
                if (parts.size() > 4) {
                    for (size_t i = 4; i < parts.size(); ++i) {
                        if (!desc.empty()) desc += "|";
                        desc += parts[i];
                    }
                }
            } else {
                weight = parse_weight(parts[2], 1.0);
                append_desc(3);
            }
        } else if (parts.size() >= 3) {
            name = parts[0];
            double parsed = 1.0;
            if (try_parse_weight(parts[1], parsed)) {
                weight = parsed;
                desc = parts[2];
            } else if (try_parse_weight(parts[2], parsed)) {
                weight = parsed;
                desc = parts[1];
            } else {
                weight = parse_weight(parts[1], 1.0);
                desc = parts[2];
            }
        } else if (parts.size() >= 2) {
            name = parts[0];
            weight = parse_weight(parts[1], 1.0);
        } else if (!parts.empty()) {
            name = parts[0];
        }
        if (!name.empty()) {
            std::string fixedName = MaybeFixMojibake(name);
            if (fixedName != name) {
                name = std::move(fixedName);
                repaired = true;
            }
        }
        if (!desc.empty()) {
            std::string fixedDesc = MaybeFixMojibake(desc);
            if (fixedDesc != desc) {
                desc = std::move(fixedDesc);
                repaired = true;
            }
        }
        name = trim(name);
        desc = trim(desc);
        if (name.empty()) continue;
        if (id.empty()) id = make_id(name);
        add_internal(id, name, clamp_weight(weight), desc, false);
    }

    if (orderedIds_.empty()) {
        for (const auto& entry : kDefaultSkills) {
            add_internal(make_id(entry.name), entry.name, entry.weight, entry.description, false);
        }
        save();
    } else {
        bool changed = repaired;
        std::unordered_set<std::string> existing(orderedIds_.begin(), orderedIds_.end());
        for (const auto& entry : kDefaultSkills) {
            const std::string name(entry.name);
            if (!idByName_.count(normalize(name))) {
                const std::string id = make_id(name);
                orderedIds_.push_back(id);
                namesById_[id] = name;
                idByName_[normalize(name)] = id;
                weightsById_[id] = entry.weight;
                descriptionsById_[id] = entry.description;
                changed = true;
            } else {
                const std::string id = *id_for_name(name);
                double& w = weightsById_[id];
                if (std::abs(w - entry.weight) > 1e-3) {
                    w = entry.weight;
                    changed = true;
                }
                auto& desc = descriptionsById_[id];
                if (desc.empty() && entry.description) {
                    desc = entry.description;
                    changed = true;
                }
            }
        }
        if (changed) save();
    }
}

void SkillCatalog::save() const {
    auto path = file_path();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    for (const auto& id : orderedIds_) {
        auto nameIt = namesById_.find(id);
        if (nameIt == namesById_.end()) continue;
        const double w = weight(id);
        out << id << "|" << nameIt->second << "|" << w;
        auto it = descriptionsById_.find(id);
        if (it != descriptionsById_.end() && !it->second.empty()) {
            out << "|" << it->second;
        }
        out << "\n";
    }
    std::string data = out.str();
    data.insert(data.begin(), static_cast<char>(0xEF));
    data.insert(data.begin() + 1, static_cast<char>(0xBB));
    data.insert(data.begin() + 2, static_cast<char>(0xBF));
    WriteTextFileAtomic(path, data);
}

std::filesystem::path SkillCatalog::file_path() const {
    return baseDir_ / "skills.txt";
}

std::string SkillCatalog::trim(std::string s) {
    auto is_space = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){ return !is_space(c); }).base(), s.end());
    return s;
}

std::string SkillCatalog::normalize(const std::string& s) {
    std::string trimmed = trim(s);
    std::string lower = lowercase_utf8(trimmed);
    std::string out;
    out.reserve(lower.size());
    for (char c : lower) {
        if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')) {
            out.push_back(c);
        }
    }
    return out;
}

std::string SkillCatalog::make_id(const std::string& displayName) const {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : displayName) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << "sk_" << std::hex << std::setw(16) << std::setfill('0') << hash;
    std::string base = ss.str();
    std::string candidate = base;
    int suffix = 1;
    while (namesById_.count(candidate) > 0) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

void SkillCatalog::add_internal(const std::string& id, const std::string& displayName, double weight, const std::string& description, bool persist) {
    std::string trimmedName = trim(displayName);
    if (trimmedName.empty()) return;
    const std::string norm = normalize(trimmedName);
    auto existingByName = idByName_.find(norm);
    if (existingByName != idByName_.end()) {
        const std::string existingId = existingByName->second;
        namesById_[existingId] = trimmedName;
        weightsById_[existingId] = clamp_weight(weight);
        descriptionsById_[existingId] = trim(description);
        if (persist) save();
        return;
    }

    if (namesById_.count(id) == 0) {
        orderedIds_.push_back(id);
    } else {
        const std::string& oldName = namesById_[id];
        if (!oldName.empty()) {
            idByName_.erase(normalize(oldName));
        }
    }
    namesById_[id] = trimmedName;
    idByName_[norm] = id;
    weightsById_[id] = clamp_weight(weight);
    descriptionsById_[id] = trim(description);

    if (persist) save();
}
