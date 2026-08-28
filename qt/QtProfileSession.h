#pragma once
#include "IJobStorage.h"
#include <QByteArray>

// Local UI authorization only; never persists credentials or unlock state.
class QtProfileSession {
public:
    bool unlock(IJobStorage& storage, const std::string& id, const std::string& password);
    bool isUnlocked(IJobStorage& storage, const std::string& id);
    void lock() { id_.clear(); fingerprint_.clear(); }
private:
    std::string id_;
    QByteArray fingerprint_;
};
