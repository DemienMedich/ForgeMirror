constexpr const char* kAdminPassword = "admin123";

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

// Simple description struct for pipeline hints shown in the UI sidebar.
struct PipelineStep {
    const char* title;
    const char* description;
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
