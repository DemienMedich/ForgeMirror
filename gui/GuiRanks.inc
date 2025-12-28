struct RankOption {
    const char* label;
    int level;
};

struct RankSpan {
    const char* currentLabel = nullptr;
    int currentLevel = 1;
    const char* nextLabel = nullptr;
    int nextLevel = 0;
    bool hasNext = false;
};

const std::vector<RankOption>& RankOptions() {
    static const std::vector<RankOption> opts = {
        {u8"Стажёр", 1},
        {u8"Джуниор I", 10},
        {u8"Джуниор II", 20},
        {u8"Джуниор III", 30},
        {u8"Джуниор IV", 40},
        {u8"Мидл I", 50},
        {u8"Мидл II", 60},
        {u8"Мидл III", 70},
        {u8"Мидл IV", 80},
        {u8"Мидл V", 90},
        {u8"Мидл VI", 100},
        {u8"Сеньор I", 150},
        {u8"Сеньор II", 160},
        {u8"Сеньор III", 170},
        {u8"Сеньор IV", 180},
        {u8"Сеньор V", 190}
    };
    return opts;
}

RankSpan ComputeRankSpan(int level) {
    RankSpan span;
    const auto& opts = RankOptions();
    if (opts.empty()) return span;
    int currentIdx = 0;
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        if (level >= opts[i].level) {
            currentIdx = i;
        } else {
            break;
        }
    }
    span.currentLabel = opts[currentIdx].label;
    span.currentLevel = opts[currentIdx].level;
    if (currentIdx + 1 < static_cast<int>(opts.size())) {
        span.nextLabel = opts[currentIdx + 1].label;
        span.nextLevel = opts[currentIdx + 1].level;
        span.hasNext = true;
    }
    return span;
}

int RankIndexForLevel(int level) {
    const auto& opts = RankOptions();
    int bestIdx = 0;
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        if (level >= opts[i].level) {
            bestIdx = i;
        }
    }
    return bestIdx;
}

const char* RankLabelForLevel(int level) {
    const auto& opts = RankOptions();
    if (opts.empty()) return u8"—";
    int idx = RankIndexForLevel(level);
    idx = std::clamp(idx, 0, static_cast<int>(opts.size()) - 1);
    return opts[idx].label;
}
