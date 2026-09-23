#pragma once
#include <QColor>
#include <QWidget>
#include <filesystem>
#include <string>
#include <vector>

struct QtModelSettings {
    QString modelPath;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float zoom = 1.0f;
    bool autoRotate = true;
    float autoSpeed = 0.6f;
    QColor lineColor = QColor(153, 217, 255);
};

struct QtModelLoadResult {
    bool ok = false;
    int triangles = 0;
    QString error;
};

QtModelSettings LoadQtModelSettings(const std::filesystem::path& workspaceDirectory);
bool SaveQtModelSettings(const std::filesystem::path& workspaceDirectory, const QtModelSettings& settings);
std::vector<QString> ListQtModels(const std::filesystem::path& workspaceDirectory);

class QtModelViewer : public QWidget {
public:
    struct Point3 { float x, y, z; };
    struct Triangle { Point3 a, b, c; };
    explicit QtModelViewer(QWidget* parent = nullptr);
    void setModelPath(const std::filesystem::path& path);
    QtModelLoadResult loadModel(const std::filesystem::path& path);
    void setSettings(const QtModelSettings& settings);
    const QtModelSettings& settings() const { return settings_; }
    int triangleCount() const { return int(triangles_.size()); }
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
private:
    QtModelSettings settings_;
    std::vector<Triangle> triangles_;
    Point3 minimum_{}, maximum_{};
    bool valid_ = false;
    QPoint lastMouse_;
    bool dragging_ = false;
};
