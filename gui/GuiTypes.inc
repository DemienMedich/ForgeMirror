// View-model representing a loaded profile + storage id for the GUI.
struct GuiProfile {
    Profile profile;
    std::string id;
};

struct RankOption;

// User-entered share for a skill in the Add Experience sheet.
struct XpEntry {
    int percent = 0;
    int rating = 0;
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
    std::string projectId;
    std::string project;
    std::string title;
    std::string description;
    std::int64_t deadlineAt = 0;
    int status = 0; // 0=Новая, 1=В работе, 2=Выполнена
    int priority = 1; // 0=Низкий, 1=Средний, 2=Высокий, 3=Критический
    int category = 0;
    int score = 0;
    int baseXp = 0;
    int basePool = 0;
    std::int64_t createdAt = 0;
    std::vector<std::string> assignees;
    std::vector<TaskParticipant> participants;
};

struct ProjectEntry {
    std::string id;
    std::string name;
    std::string description;
    std::int64_t createdAt = 0;
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
    Delete,
    Block,
    Unblock
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

