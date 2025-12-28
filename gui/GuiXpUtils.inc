int TotalSkillXpGui(const Skill& skill) {
    int total = skill.xp;
    for (int lvl = 2; lvl <= skill.level; ++lvl) {
        total += Skill::required_xp_for(lvl);
    }
    return total;
}

// Reset Add Experience sheet: one row per catalog skill, even split of 100%.
void PrepareXpEntries(GuiState& state, const SkillCatalog& catalog) {
    const auto& skills = catalog.skills();
    state.xpEntries.assign(skills.size(), {});
    state.taskScore = 10;
    state.taskCategoryIndex = 0;
    const int count = static_cast<int>(skills.size());
    if (count <= 0) return;
    const int base = 100 / count;
    int remainder = 100 - base * count;
    for (int i = 0; i < count; ++i) {
        state.xpEntries[i].percent = base;
        if (remainder > 0) {
            state.xpEntries[i].percent += 1;
            --remainder;
        }
    }
}


// Maintain a small rolling activity feed per profile.
void AppendLog(GuiState& state, const std::string& profileId, const std::string& message) {
    auto& log = state.activityLogs[profileId];
    if (log.size() == 3) {
        log.pop_front();
    }
    log.push_back(message);
}

// Generic clamp used all over the GUI logic.
int ClampToRange(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

int NormalizeCategoryIndex(int index) {
    if (index < 0) return 0;
    if (index >= Profile::kCategoryCount) return Profile::kCategoryCount - 1;
    return index;
}

const char* ClassificationLabel(int categoryIndex) {
    return Profile::kCategoryLabels[NormalizeCategoryIndex(categoryIndex)];
}

// Utility functions below keep the Add Experience sliders in sync.
int SumPercentages(const std::vector<XpEntry>& entries) {
    int total = 0;
    for (const auto& entry : entries) total += entry.percent;
    return total;
}

// Reduce percentages on non-selected rows until the sum drops back to 100%.
void ReduceOthers(std::vector<XpEntry>& entries, int fixedIndex, int overflow) {
    if (overflow <= 0) return;
    const int size = static_cast<int>(entries.size());
    if (size == 0) return;
    std::vector<int> indices;
    indices.reserve(size);
    for (int i = 0; i < size; ++i) {
        if (i == fixedIndex) continue;
        if (entries[i].percent > 0) indices.push_back(i);
    }
    if (indices.empty()) {
        entries[fixedIndex].percent = std::max(0, entries[fixedIndex].percent - overflow);
        return;
    }
    while (overflow > 0) {
        bool changed = false;
        int positiveCount = 0;
        for (int idx : indices) {
            if (entries[idx].percent > 0) ++positiveCount;
        }
        if (positiveCount == 0) {
            entries[fixedIndex].percent = std::max(0, entries[fixedIndex].percent - overflow);
            break;
        }
        int share = std::max(1, overflow / positiveCount);
        for (int idx : indices) {
            if (overflow <= 0) break;
            if (entries[idx].percent <= 0) continue;
            int delta = std::min(entries[idx].percent, share);
            if (delta <= 0) continue;
            entries[idx].percent -= delta;
            overflow -= delta;
            changed = true;
        }
        if (!changed) {
            for (int idx : indices) {
                if (overflow <= 0) break;
                if (entries[idx].percent <= 0) continue;
                entries[idx].percent -= 1;
                --overflow;
                changed = true;
            }
        }
        if (!changed) {
            entries[fixedIndex].percent = std::max(0, entries[fixedIndex].percent - overflow);
            break;
        }
    }
}

// Distribute remaining percent points across other rows to reach 100%.
void IncreaseOthers(std::vector<XpEntry>& entries, int fixedIndex, int deficit) {
    if (deficit <= 0) return;
    const int size = static_cast<int>(entries.size());
    if (size == 0) return;
    std::vector<int> indices;
    indices.reserve(size);
    for (int i = 0; i < size; ++i) {
        if (i == fixedIndex) continue;
        indices.push_back(i);
    }
    if (indices.empty()) {
        entries[fixedIndex].percent = ClampToRange(entries[fixedIndex].percent + deficit, 0, 100);
        return;
    }
    std::size_t cursor = 0;
    int safety = deficit * static_cast<int>(indices.size() + 1) + 100;
    auto allFull = [&]() {
        for (int idx : indices) {
            if (entries[idx].percent < 100) return false;
        }
        return true;
    };
    while (deficit > 0 && safety-- > 0) {
        if (allFull()) {
            entries[fixedIndex].percent = ClampToRange(entries[fixedIndex].percent + deficit, 0, 100);
            break;
        }
        int idx = indices[cursor % indices.size()];
        cursor++;
        if (entries[idx].percent < 100) {
            entries[idx].percent += 1;
            --deficit;
        }
    }
}

// High-level balancer that ensures the sheet always totals 100%.
void BalancePercentages(std::vector<XpEntry>& entries, int fixedIndex) {
    if (entries.empty()) return;
    if (fixedIndex < 0 || fixedIndex >= static_cast<int>(entries.size())) {
        fixedIndex = 0;
    }
    int total = SumPercentages(entries);
    if (total > 100) {
        ReduceOthers(entries, fixedIndex, total - 100);
    } else if (total < 100) {
        IncreaseOthers(entries, fixedIndex, 100 - total);
    }
    int adjustedTotal = SumPercentages(entries);
    if (adjustedTotal != 100) {
        int diff = adjustedTotal - 100;
        entries[fixedIndex].percent = ClampToRange(entries[fixedIndex].percent - diff, 0, 100);
        adjustedTotal = SumPercentages(entries);
        if (adjustedTotal != 100 && !entries.empty()) {
            int correction = 100 - adjustedTotal;
            entries[0].percent = ClampToRange(entries[0].percent + correction, 0, 100);
        }
    }
}

// Entry point invoked when the user drags a slider.
void AdjustSkillShare(GuiState& state, int index, int desiredPercent) {
    if (index < 0 || index >= static_cast<int>(state.xpEntries.size())) return;
    desiredPercent = ClampToRange(desiredPercent, 0, 100);
    state.xpEntries[index].percent = desiredPercent;
    BalancePercentages(state.xpEntries, index);
}
