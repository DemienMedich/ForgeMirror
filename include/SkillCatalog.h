#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <unordered_map>

class SkillCatalog {
public:
    explicit SkillCatalog(std::filesystem::path baseDir);

    const std::vector<std::string>& skills() const { return orderedIds_; }
    bool contains_id(const std::string& id) const;
    bool contains_name(const std::string& name) const;
    std::optional<std::string> id_for_name(const std::string& name) const;
    void reload();
    double weight(const std::string& skill) const;
    std::vector<std::string> professions(const std::string& id) const;
    bool has_profession(const std::string& id, const std::string& professionId) const;
    std::string category(const std::string& id) const;
    std::string display_name(const std::string& id) const;
    std::string description(const std::string& id) const;
    bool add_skill(const std::string& skill, double weight = 1.0, const std::string& description = {},
                   const std::string& category = {}, const std::vector<std::string>& professions = {});
    bool update_skill(const std::string& id, const std::string& displayName, double weight,
                      const std::string& description, const std::string& category,
                      const std::vector<std::string>& professions);
    bool remove_skill(const std::string& idOrName);

private:
    void load();
    void save() const;
    std::filesystem::path file_path() const;
    static std::string trim(std::string s);
    static std::string normalize(const std::string& s);
    void add_internal(const std::string& id, const std::string& displayName, double weight,
                      const std::string& description, const std::string& category,
                      const std::vector<std::string>& professions, bool persist = false);
    std::optional<std::string> resolve_id(const std::string& idOrName) const;
    std::string make_id(const std::string& displayName) const;

    std::filesystem::path baseDir_;
    std::vector<std::string> orderedIds_;
    std::unordered_map<std::string, std::string> idByName_;
    std::unordered_map<std::string, std::string> namesById_;
    std::unordered_map<std::string, double> weightsById_;
    std::unordered_map<std::string, std::string> descriptionsById_;
    std::unordered_map<std::string, std::string> categoriesById_;
    std::unordered_map<std::string, std::vector<std::string>> professionsById_;
};
