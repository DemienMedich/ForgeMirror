// Простой прототип геймифицированных профилей для команды моделлеров
// Возможности:
// - Навык (Skill) с уровнем и опытом (XP)
// - Профиль (Profile) с несколькими навыками и общим уровнем как средним по навыкам
// - Небольшое CLI‑демо: добавление XP и отображение повышения уровня

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <iomanip>

struct Skill {
    std::string name;
    int level = 1;
    int xp = 0;            // current xp within the level
    int xpToNext = 100;    // requirement for next level

    Skill() = default;
    explicit Skill(std::string n, int lvl = 1)
        : name(std::move(n)), level(lvl) {
        xpToNext = required_xp_for(level + 1);
    }

    static int required_xp_for(int targetLevel) {
        // Простая прогрессия сложности: 100 * L^1.3 (округление)
        // Ранние уровни быстрые, поздние — более значимые
        double base = 100.0;
        double curve = std::pow(targetLevel, 1.3);
        return static_cast<int>(base * curve);
    }

    bool add_xp(int amount) {
        if (amount <= 0) return false;
        bool leveled = false;
        xp += amount;
        // Преобразуем накопленный опыт в уровни по мере необходимости
        while (xp >= xpToNext) {
            xp -= xpToNext;
            level += 1;
            xpToNext = required_xp_for(level + 1);
            leveled = true;
        }
        return leveled;
    }
};

class Profile {
public:
    explicit Profile(std::string n) : name_(std::move(n)) {}

    void add_skill(const std::string& skillName, int startLevel = 1) {
        if (skills_.count(skillName) == 0) {
            skills_.emplace(skillName, Skill(skillName, startLevel));
        }
    }

    bool grant_xp(const std::string& skillName, int amount) {
        auto it = skills_.find(skillName);
        if (it == skills_.end()) return false;
        bool leveled = it->second.add_xp(amount);
        recompute_overall();
        return leveled;
    }

    int overall_level() const { return overallLevel_; }
    const std::string& name() const { return name_; }

    std::vector<Skill> list_skills() const {
        std::vector<Skill> out;
        out.reserve(skills_.size());
        for (const auto& kv : skills_) out.push_back(kv.second);
        return out;
    }

private:
    void recompute_overall() {
        // Общий уровень = целая часть среднего по уровням навыков, минимум 1
        if (skills_.empty()) { overallLevel_ = 1; return; }
        int sum = 0;
        for (const auto& kv : skills_) sum += kv.second.level;
        overallLevel_ = std::max(1, sum / static_cast<int>(skills_.size()));
    }

    std::string name_;
    std::unordered_map<std::string, Skill> skills_;
    int overallLevel_ = 1;
};

static void print_profile(const Profile& p) {
    std::cout << "=== Профиль: " << p.name() << " ===\n";
    std::cout << "Общий уровень: " << p.overall_level() << "\n";
    std::cout << "Навыки:\n";
    auto skills = p.list_skills();
    // sort-like stable order not necessary; just print
    for (const auto& s : skills) {
        std::cout << " - " << std::left << std::setw(12) << s.name
                  << " У" << s.level
                  << " | Опыт: " << s.xp << "/" << s.xpToNext
                  << "\n";
    }
}

int main() {
    // Демонстрационная настройка для участника команды моделлеров
    Profile alice("Алиса");
    // Базовые навыки по умолчанию (русские названия)
    alice.add_skill("Моделирование");
    alice.add_skill("Текстурирование");
    alice.add_skill("Риггинг");

    std::cout << "Добро пожаловать в профили команды!\n";
    print_profile(alice);

    std::cout << "\nНачисляем опыт: Моделирование +120, Текстурирование +60, Риггинг +300...\n";
    bool mUp = alice.grant_xp("Моделирование", 120);
    bool tUp = alice.grant_xp("Текстурирование", 60);
    bool rUp = alice.grant_xp("Риггинг", 300);

    if (mUp || tUp || rUp) {
        std::cout << "Повышение уровня как минимум в одном навыке!\n";
    }

    std::cout << "\nПосле начисления опыта:\n";
    print_profile(alice);

    // Простой интерактивный цикл (опционально)
    std::cout << "\nКоманды: добавить <навык> <число> | показать | выход\n";
    std::cout << "(Доступны и английские алиасы: addxp/show/quit)\n";
    std::string cmd;
    while (true) {
        std::cout << "> ";
        if (!(std::cin >> cmd)) break;
        if (cmd == "выход" || cmd == "quit" || cmd == "exit") break;
        if (cmd == "показать" || cmd == "show") {
            print_profile(alice);
            continue;
        }
        if (cmd == "добавить" || cmd == "addxp") {
            std::string skill; int amount;
            if (std::cin >> skill >> amount) {
                // Для удобства — создаём навык, если его ещё нет
                alice.add_skill(skill);
                bool up = alice.grant_xp(skill, amount);
                std::cout << (up ? "Повышение уровня!" : "Опыт начислен.") << "\n";
            } else {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "Неверный ввод.\n";
            }
            continue;
        }
        std::cout << "Неизвестная команда. Доступно: добавить / показать / выход\n";
    }

    std::cout << "До встречи!\n";
    return 0;
}
