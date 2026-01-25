// View-model representing a loaded profile + storage id for the GUI.
struct GuiProfile {
    Profile profile;
    std::string id;
};

struct RankOption;

// User-entered share for a skill in the Add Experience sheet.
struct XpEntry {
    int percent = 0;
};

// Profile contribution share for a multi-profile task.
struct TaskProfileShare {
    std::string id;
    std::string name;
    int percent = 0;
    bool selected = false;
    bool archived = false;
};

// Stored task participant result.
struct TaskParticipant {
    std::string profileId;
    int percent = 0;
    int globalXp = 0;
    int skillXp = 0;
};

// Stored task metadata.
struct TaskEntry {
    std::string id;
    std::string project;
    std::string title;
    std::string description;
    int category = 0;
    int score = 0;
    int baseXp = 0;
    int basePool = 0;
    std::int64_t createdAt = 0;
    std::vector<TaskParticipant> participants;
};

struct ShortcutEntry {
    std::string id;
    std::string label;
    std::string path;
};

struct ProfessionEntry {
    std::string id;
    std::string name;
    std::string description;
};

// Simple description struct for pipeline hints shown in the UI sidebar.
struct PipelineStep {
    std::string title;
    std::string description;
};

// Actions supported by the confirmation modal.
enum class ConfirmAction {
    None,
    Archive,
    Restore,
    Delete
};

enum class AppLogLevel {
    Info,
    Warning,
    Error
};

struct AppLogEntry {
    std::int64_t timestamp = 0;
    AppLogLevel level = AppLogLevel::Info;
    std::string source;
    std::string message;
};
