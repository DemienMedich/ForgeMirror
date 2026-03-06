#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TaskParticipant {
    std::string profileId;
    int percent = 0;
    int globalXp = 0;
    int skillXp = 0;
};

struct TaskEntry {
    std::string id;
    std::string projectId;
    std::string project;
    std::string title;
    std::string description;
    std::int64_t deadlineAt = 0;
    int status = 0;
    int priority = 1;
    int category = 0;
    int score = 0;
    int baseXp = 0;
    int basePool = 0;
    std::int64_t createdAt = 0;
    std::vector<std::string> assignees;
    std::vector<TaskParticipant> participants;
};

struct TaskAuditEntry {
    std::int64_t timestamp = 0;
    std::string actor;
    std::string taskId;
    std::string field;
    std::string oldValue;
    std::string newValue;
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

struct PipelineStep {
    std::string title;
    std::string description;
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
