#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <unordered_map>

class SkillCatalog {
public:
    explicit SkillCatalog(std::filesystem::path baseDir);

    const std::vector<std::string>& skills() const { return orderedSkills_; }
    bool contains(const std::string& skill) const;
    std::optional<std::string> canonical(const std::string& skill) const;
    double weight(const std::string& skill) const;
    std::string description(const std::string& skill) const;
    bool add_skill(const std::string& skill, double weight = 1.0, const std::string& description = {});
    bool remove_skill(const std::string& skill);

private:
    void load();
    void save() const;
    std::filesystem::path file_path() const;
    static std::string trim(std::string s);
    static std::string normalize(const std::string& s);
    void add_internal(const std::string& skill, double weight, const std::string& description, bool persist = false);

    std::filesystem::path baseDir_;
    std::vector<std::string> orderedSkills_;
    std::unordered_map<std::string, std::string> index_;
    std::unordered_map<std::string, double> weights_;
    std::unordered_map<std::string, std::string> descriptions_;
};
