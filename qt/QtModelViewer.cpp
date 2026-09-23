#include "QtModelViewer.h"
#include <QtWidgets>
#include <ufbx.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
QString settingsPath(const fs::path& workspace) { return QString::fromUtf8((workspace / "meta/ui.ini").u8string()); }
bool safePath(const fs::path& path) {
    fs::path current;
    for (const auto& part : fs::absolute(path)) {
        current /= part;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#else
        std::error_code ec;
        if (fs::is_symlink(fs::symlink_status(current, ec))) return false;
#endif
    }
    return true;
}
float finiteClamp(float value, float minimum, float maximum, float fallback) {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
bool parseVec3(const std::string& text, QtModelViewer::Point3& point) {
    std::istringstream in(text); in.imbue(std::locale::classic());
    in >> point.x >> point.y >> point.z;
    return !in.fail() && std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}
}

QtModelSettings LoadQtModelSettings(const fs::path& workspace) {
    QtModelSettings out;
    if (!safePath(workspace / "meta/ui.ini")) return out;
    QFile file(settingsPath(workspace)); if (!file.open(QIODevice::ReadOnly)) return out;
    auto bytes = file.readAll(); if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    QString section;
    for (const auto& raw : QString::fromUtf8(bytes).split('\n')) {
        const auto line = raw.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) { section = line.mid(1, line.size() - 2); continue; }
        const auto key = line.section('=', 0, 0).trimmed(); const auto value = line.section('=', 1).trimmed();
        if (key.isEmpty()) continue;
        // Import legacy ImGui values as defaults, then let a Qt-specific section override them.
        if (section == "view3d" || section == "qt3d") {
            if (key == "modelPath") out.modelPath = value;
            else if (key == "yaw") out.yaw = value.toFloat();
            else if (key == "pitch") out.pitch = value.toFloat();
            else if (key == "zoom") out.zoom = value.toFloat();
            else if (key == "autoRotate") out.autoRotate = value == "1" || value.compare("true", Qt::CaseInsensitive) == 0;
            else if (key == "autoSpeed") out.autoSpeed = value.toFloat();
            else if (key == "color") {
                const auto pieces = value.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
                if (pieces.size() >= 3) {
                    const float r = pieces[0].toFloat(), g = pieces[1].toFloat(), b = pieces[2].toFloat();
                    const float a = pieces.size() > 3 ? pieces[3].toFloat() : 1.0f;
                    out.lineColor = QColor::fromRgbF(finiteClamp(r, 0, 1, .6f), finiteClamp(g, 0, 1, .85f), finiteClamp(b, 0, 1, 1), finiteClamp(a, 0, 1, 1));
                }
            }
        }
        if (section == "qt3d" && key == "lineColor") {
            const QColor color(value); if (color.isValid()) out.lineColor = color;
        }
    }
    out.yaw = finiteClamp(out.yaw, -50, 50, 0); out.pitch = finiteClamp(out.pitch, -1.57f, 1.57f, 0);
    out.zoom = finiteClamp(out.zoom, .3f, 3, 1); out.autoSpeed = finiteClamp(out.autoSpeed, 0, 3, .6f);
    return out;
}

bool SaveQtModelSettings(const fs::path& workspace, const QtModelSettings& settings) {
    const auto path = settingsPath(workspace), parent = QFileInfo(path).absolutePath();
    if (!safePath(workspace / "meta/ui.ini") || QFileInfo(parent).isSymLink() || QFileInfo(path).isSymLink()) return false;
    QFile input(path); QByteArray original; if (input.open(QIODevice::ReadOnly)) original = input.readAll(); input.close();
    const bool bom = original.startsWith("\xEF\xBB\xBF"); if (bom) original.remove(0, 3);
    auto lines = QString::fromUtf8(original).split('\n'); int begin = -1, end = lines.size();
    for (int i = 0; i < lines.size(); ++i) { const auto line = lines[i].trimmed(); if (line == "[qt3d]") { begin = i; continue; } if (begin >= 0 && i > begin && line.startsWith('[')) { end = i; break; } }
    if (begin < 0) { if (!lines.isEmpty() && !lines.back().isEmpty()) lines << ""; begin = lines.size(); lines << "[qt3d]"; end = lines.size(); }
    auto set = [&](const QString& key, const QString& value) { for (int i = begin + 1; i < end; ++i) if (lines[i].section('=', 0, 0).trimmed() == key) { lines[i] = key + '=' + value; return; } lines.insert(end++, key + '=' + value); };
    const auto color = settings.lineColor;
    set("modelPath", settings.modelPath); set("yaw", QString::number(finiteClamp(settings.yaw, -50, 50, 0), 'f', 5));
    set("pitch", QString::number(finiteClamp(settings.pitch, -1.57f, 1.57f, 0), 'f', 5));
    set("zoom", QString::number(finiteClamp(settings.zoom, .3f, 3, 1), 'f', 4));
    set("autoRotate", settings.autoRotate ? "1" : "0"); set("autoSpeed", QString::number(finiteClamp(settings.autoSpeed, 0, 3, .6f), 'f', 3));
    set("lineColor", color.name(QColor::HexArgb));
    QDir().mkpath(parent); QSaveFile output(path); output.setDirectWriteFallback(false);
    const auto bytes = (bom ? QByteArray("\xEF\xBB\xBF") : QByteArray()) + lines.join('\n').toUtf8();
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}

std::vector<QString> ListQtModels(const fs::path& workspace) {
    std::vector<QString> result; const auto directory = workspace / "models"; std::error_code ec;
    if (!fs::is_directory(directory, ec) || ec || !safePath(directory)) return result;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (ec || !entry.is_regular_file() || !safePath(entry.path())) continue;
        auto extension = QString::fromStdWString(entry.path().extension().wstring()).toLower();
        if (extension == ".obj" || extension == ".fbx") result.push_back(QString::fromStdWString(entry.path().filename().wstring()));
    }
    std::sort(result.begin(), result.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    return result;
}

QtModelViewer::QtModelViewer(QWidget* parent) : QWidget(parent) {
    setObjectName("modelViewport"); setMinimumSize(320, 240); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
}

void QtModelViewer::setModelPath(const fs::path& path) { loadModel(path); }

QtModelLoadResult QtModelViewer::loadModel(const fs::path& path) {
    triangles_.clear(); valid_ = false; QtModelLoadResult result;
    if (path.empty()) { result.error = QString::fromUtf8("Модель не выбрана."); update(); return result; }
    if (!safePath(path)) { result.error = QString::fromUtf8("Путь модели содержит символическую ссылку или junction."); update(); return result; }
    auto extension = QString::fromStdWString(path.extension().wstring()).toLower();
    if (extension == ".obj") {
        std::ifstream input(path, std::ios::binary);
        if (!input) { result.error = QString::fromUtf8("Не удалось открыть OBJ."); update(); return result; }
        std::vector<Point3> positions; std::string line;
        while (std::getline(input, line)) {
            if (line.size() < 2) continue;
            if (line[0] == 'v' && std::isspace(static_cast<unsigned char>(line[1]))) {
                Point3 point{}; if (parseVec3(line.substr(2), point)) positions.push_back(point);
            } else if (line[0] == 'f' && std::isspace(static_cast<unsigned char>(line[1]))) {
                std::istringstream row(line.substr(2)); std::vector<int> indices; std::string field;
                while (row >> field) { const auto slash = field.find('/'); const auto token = field.substr(0, slash); try { int index = std::stoi(token); if (!index) continue; if (index < 0) index = int(positions.size()) + index + 1; indices.push_back(index - 1); } catch (...) {} }
                for (size_t i = 1; i + 1 < indices.size(); ++i) {
                    const int a = indices[0], b = indices[i], c = indices[i + 1];
                    if (a < 0 || b < 0 || c < 0 || a >= int(positions.size()) || b >= int(positions.size()) || c >= int(positions.size())) continue;
                    triangles_.push_back({positions[size_t(a)], positions[size_t(b)], positions[size_t(c)]});
                }
            }
        }
    } else if (extension == ".fbx") {
        ufbx_error error{}; ufbx_load_opts options{}; options.ignore_missing_external_files = true; options.load_external_files = false;
        const auto pathUtf8 = path.u8string(); ufbx_scene* scene = ufbx_load_file(pathUtf8.c_str(), &options, &error);
        if (!scene) { result.error = error.description.data ? QString::fromUtf8(error.description.data, int(error.description.length)) : QString::fromUtf8("Не удалось прочитать FBX."); update(); return result; }
        std::vector<uint32_t> indices;
        for (size_t meshIndex = 0; meshIndex < scene->meshes.count; ++meshIndex) {
            const auto* mesh = scene->meshes.data[meshIndex]; if (!mesh->vertex_position.exists) continue;
            indices.resize(mesh->max_face_triangles * 3);
            for (size_t faceIndex = 0; faceIndex < mesh->faces.count; ++faceIndex) {
                const auto face = mesh->faces.data[faceIndex]; if (face.num_indices < 3) continue;
                const auto count = ufbx_triangulate_face(indices.data(), indices.size(), mesh, face);
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t values[3]{}; bool good = true;
                    for (int c = 0; c < 3; ++c) { const auto corner = indices[size_t(i) * 3 + size_t(c)]; if (corner >= mesh->vertex_position.indices.count) { good = false; break; } values[c] = mesh->vertex_position.indices.data[corner]; if (values[c] >= mesh->vertex_position.values.count) { good = false; break; } }
                    if (!good) continue;
                    const auto a = mesh->vertex_position.values.data[values[0]], b = mesh->vertex_position.values.data[values[1]], c = mesh->vertex_position.values.data[values[2]];
                    triangles_.push_back({{float(a.x),float(a.y),float(a.z)},{float(b.x),float(b.y),float(b.z)},{float(c.x),float(c.y),float(c.z)}});
                }
            }
        }
        ufbx_free_scene(scene);
    } else { result.error = QString::fromUtf8("Поддерживаются файлы OBJ и FBX."); update(); return result; }
    if (triangles_.empty()) { result.error = QString::fromUtf8("В файле не найдены треугольники."); update(); return result; }
    minimum_ = {std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
    maximum_ = {std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest()};
    auto bound = [&](const Point3& p) { minimum_.x=std::min(minimum_.x,p.x);minimum_.y=std::min(minimum_.y,p.y);minimum_.z=std::min(minimum_.z,p.z);maximum_.x=std::max(maximum_.x,p.x);maximum_.y=std::max(maximum_.y,p.y);maximum_.z=std::max(maximum_.z,p.z); };
    for (const auto& tri : triangles_) { bound(tri.a); bound(tri.b); bound(tri.c); }
    valid_ = true; result.ok = true; result.triangles = int(triangles_.size()); update(); return result;
}

void QtModelViewer::setSettings(const QtModelSettings& settings) { settings_ = settings; update(); }

void QtModelViewer::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    const QColor surface = palette().color(QPalette::Base); painter.fillRect(rect(), surface);
    painter.setPen(QPen(palette().color(QPalette::AlternateBase), 1)); painter.drawLine(0, 0, width(), 0);
    if (!valid_ || triangles_.empty()) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text)); painter.drawText(rect().adjusted(16,16,-16,-16), Qt::AlignCenter | Qt::TextWordWrap, QString::fromUtf8("Модель не загружена\nОткройте «Настройки 3D» и выберите OBJ или FBX.")); return;
    }
    const Point3 center{(minimum_.x+maximum_.x)*.5f,(minimum_.y+maximum_.y)*.5f,(minimum_.z+maximum_.z)*.5f};
    const float maxDim=std::max({maximum_.x-minimum_.x,maximum_.y-minimum_.y,maximum_.z-minimum_.z,.001f});
    const float scale=.44f*std::min(width(),height())/maxDim, distance=2.5f/std::max(.2f,settings_.zoom), aspect=float(width())/std::max(1,height());
    const QPointF centerScreen(width()*.5,height()*.5); const float cp=std::cos(settings_.pitch),sp=std::sin(settings_.pitch),cy=std::cos(settings_.yaw),sy=std::sin(settings_.yaw);
    auto project=[&](Point3 v) { v.x-=center.x;v.y-=center.y;v.z-=center.z; const float y=v.y*cp-v.z*sp,z=v.y*sp+v.z*cp,x=v.x*cy+z*sy,rz=-v.x*sy+z*cy+distance; if (rz<=.001f) return centerScreen; const float f=1.7320508f; return QPointF(centerScreen.x()+x*f/aspect/rz*scale,centerScreen.y()-y*f/rz*scale); };
    painter.setPen(QPen(settings_.lineColor,1));
    for (const auto& tri : triangles_) { const auto a=project(tri.a),b=project(tri.b),c=project(tri.c);painter.drawLine(a,b);painter.drawLine(b,c);painter.drawLine(c,a); }
}
void QtModelViewer::mousePressEvent(QMouseEvent* event) { if (event->button()==Qt::LeftButton) { dragging_=true;lastMouse_=event->pos();setCursor(Qt::ClosedHandCursor);event->accept();return;} QWidget::mousePressEvent(event); }
void QtModelViewer::mouseMoveEvent(QMouseEvent* event) { if (dragging_) { const auto delta=event->pos()-lastMouse_;lastMouse_=event->pos();settings_.yaw+=delta.x()*.01f;settings_.pitch=std::clamp(settings_.pitch+delta.y()*.01f,-1.57f,1.57f);settings_.autoRotate=false;update();event->accept();return;} QWidget::mouseMoveEvent(event); }
void QtModelViewer::mouseReleaseEvent(QMouseEvent* event) { if (event->button()==Qt::LeftButton && dragging_) { dragging_=false;unsetCursor();event->accept();return;} QWidget::mouseReleaseEvent(event); }
void QtModelViewer::wheelEvent(QWheelEvent* event) { settings_.zoom=std::clamp(settings_.zoom+event->angleDelta().y()/1200.0f,.3f,3.0f);update();event->accept(); }
