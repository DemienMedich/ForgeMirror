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

// Actions supported by the confirmation modal.
enum class ConfirmAction {
    None,
    Archive,
    Restore,
    Delete,
    Block,
    Unblock
};
