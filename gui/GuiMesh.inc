struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Triangle {
    Vec3 a;
    Vec3 b;
    Vec3 c;
};

struct MeshData {
    std::vector<Triangle> triangles;
    Vec3 min;
    Vec3 max;
    bool valid = false;
};

void ResetMeshBounds(MeshData& mesh) {
    mesh.min = { std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max() };
    mesh.max = { std::numeric_limits<float>::lowest(),
                 std::numeric_limits<float>::lowest(),
                 std::numeric_limits<float>::lowest() };
}

void UpdateBounds(MeshData& mesh, const Vec3& v) {
    mesh.min.x = std::min(mesh.min.x, v.x);
    mesh.min.y = std::min(mesh.min.y, v.y);
    mesh.min.z = std::min(mesh.min.z, v.z);
    mesh.max.x = std::max(mesh.max.x, v.x);
    mesh.max.y = std::max(mesh.max.y, v.y);
    mesh.max.z = std::max(mesh.max.z, v.z);
}

bool LoadObjMesh(const std::filesystem::path& path, MeshData& mesh, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "Не удалось открыть OBJ.";
        return false;
    }
    std::vector<Vec3> positions;
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && std::isspace(static_cast<unsigned char>(line[1]))) {
            std::istringstream ss(line.substr(2));
            Vec3 v{};
            ss >> v.x >> v.y >> v.z;
            if (!ss.fail()) positions.push_back(v);
        } else if (line[0] == 'f' && std::isspace(static_cast<unsigned char>(line[1]))) {
            std::istringstream ss(line.substr(2));
            std::vector<int> indices;
            std::string part;
            while (ss >> part) {
                auto slash = part.find('/');
                std::string idxStr = slash == std::string::npos ? part : part.substr(0, slash);
                if (idxStr.empty()) continue;
                int idx = 0;
                try {
                    idx = std::stoi(idxStr);
                } catch (...) {
                    idx = 0;
                }
                if (idx == 0) continue;
                if (idx < 0) idx = static_cast<int>(positions.size()) + idx + 1;
                indices.push_back(idx - 1);
            }
            if (indices.size() >= 3) {
                for (size_t i = 1; i + 1 < indices.size(); ++i) {
                    const int ia = indices[0];
                    const int ib = indices[i];
                    const int ic = indices[i + 1];
                    if (ia < 0 || ib < 0 || ic < 0) continue;
                    if (ia >= static_cast<int>(positions.size()) ||
                        ib >= static_cast<int>(positions.size()) ||
                        ic >= static_cast<int>(positions.size())) {
                        continue;
                    }
                    Triangle tri{positions[ia], positions[ib], positions[ic]};
                    mesh.triangles.push_back(tri);
                }
            }
        }
    }
    if (mesh.triangles.empty()) {
        error = "OBJ не содержит треугольников.";
        return false;
    }
    ResetMeshBounds(mesh);
    for (const auto& tri : mesh.triangles) {
        UpdateBounds(mesh, tri.a);
        UpdateBounds(mesh, tri.b);
        UpdateBounds(mesh, tri.c);
    }
    mesh.valid = true;
    return true;
}

bool LoadFbxMesh(const std::filesystem::path& path, MeshData& mesh, std::string& error) {
    ufbx_error uerr;
    ufbx_load_opts opts{};
    opts.ignore_missing_external_files = true;
    opts.load_external_files = false;
    ufbx_scene* scene = ufbx_load_file(path.string().c_str(), &opts, &uerr);
    if (!scene) {
        if (uerr.description.data && uerr.description.length > 0) {
            error.assign(uerr.description.data, uerr.description.length);
        } else {
            error = "Не удалось прочитать FBX.";
        }
        return false;
    }
    std::vector<uint32_t> triIndices;
    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh* umesh = scene->meshes.data[mi];
        if (!umesh->vertex_position.exists) continue;
        const size_t maxTris = umesh->max_face_triangles * 3;
        if (maxTris == 0) continue;
        triIndices.resize(maxTris);
        for (size_t fi = 0; fi < umesh->faces.count; ++fi) {
            ufbx_face face = umesh->faces.data[fi];
            if (face.num_indices < 3) continue;
            const uint32_t triCount = ufbx_triangulate_face(triIndices.data(), triIndices.size(), umesh, face);
            for (uint32_t ti = 0; ti < triCount; ++ti) {
                uint32_t ia = triIndices[ti * 3 + 0];
                uint32_t ib = triIndices[ti * 3 + 1];
                uint32_t ic = triIndices[ti * 3 + 2];
                if (ia >= umesh->vertex_position.indices.count ||
                    ib >= umesh->vertex_position.indices.count ||
                    ic >= umesh->vertex_position.indices.count) {
                    continue;
                }
                uint32_t va = umesh->vertex_position.indices.data[ia];
                uint32_t vb = umesh->vertex_position.indices.data[ib];
                uint32_t vc = umesh->vertex_position.indices.data[ic];
                if (va >= umesh->vertex_position.values.count ||
                    vb >= umesh->vertex_position.values.count ||
                    vc >= umesh->vertex_position.values.count) {
                    continue;
                }
                ufbx_vec3 p0 = umesh->vertex_position.values.data[va];
                ufbx_vec3 p1 = umesh->vertex_position.values.data[vb];
                ufbx_vec3 p2 = umesh->vertex_position.values.data[vc];
                Triangle tri{{static_cast<float>(p0.x), static_cast<float>(p0.y), static_cast<float>(p0.z)},
                             {static_cast<float>(p1.x), static_cast<float>(p1.y), static_cast<float>(p1.z)},
                             {static_cast<float>(p2.x), static_cast<float>(p2.y), static_cast<float>(p2.z)}};
                mesh.triangles.push_back(tri);
            }
        }
    }
    ufbx_free_scene(scene);
    if (mesh.triangles.empty()) {
        error = "FBX не содержит треугольников.";
        return false;
    }
    ResetMeshBounds(mesh);
    for (const auto& tri : mesh.triangles) {
        UpdateBounds(mesh, tri.a);
        UpdateBounds(mesh, tri.b);
        UpdateBounds(mesh, tri.c);
    }
    mesh.valid = true;
    return true;
}

bool LoadMeshFromFile(const std::filesystem::path& path, MeshData& mesh, std::string& error) {
    mesh.triangles.clear();
    mesh.valid = false;
    error.clear();
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (ext == ".obj") return LoadObjMesh(path, mesh, error);
    if (ext == ".fbx") return LoadFbxMesh(path, mesh, error);
    error = "Неподдерживаемый формат.";
    return false;
}

MeshData MakeCubeMesh() {
    MeshData mesh;
    const float s = 0.5f;
    const Vec3 v0{-s, -s, -s};
    const Vec3 v1{ s, -s, -s};
    const Vec3 v2{ s,  s, -s};
    const Vec3 v3{-s,  s, -s};
    const Vec3 v4{-s, -s,  s};
    const Vec3 v5{ s, -s,  s};
    const Vec3 v6{ s,  s,  s};
    const Vec3 v7{-s,  s,  s};
    mesh.triangles = {
        {v0, v1, v2}, {v0, v2, v3},
        {v4, v6, v5}, {v4, v7, v6},
        {v0, v4, v5}, {v0, v5, v1},
        {v1, v5, v6}, {v1, v6, v2},
        {v2, v6, v7}, {v2, v7, v3},
        {v3, v7, v4}, {v3, v4, v0}
    };
    ResetMeshBounds(mesh);
    for (const auto& tri : mesh.triangles) {
        UpdateBounds(mesh, tri.a);
        UpdateBounds(mesh, tri.b);
        UpdateBounds(mesh, tri.c);
    }
    mesh.valid = true;
    return mesh;
}

Vec3 RotateX(const Vec3& v, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
}

Vec3 RotateY(const Vec3& v, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

ImVec2 ProjectPoint(const Vec3& v, float fov, float aspect, float distance, const ImVec2& center, float scale) {
    const float z = v.z + distance;
    if (z <= 0.001f) return center;
    const float f = 1.0f / std::tan(fov * 0.5f);
    const float x = (v.x * f / aspect) / z;
    const float y = (v.y * f) / z;
    return ImVec2(center.x + x * scale, center.y - y * scale);
}

void DrawMeshWireframe(const MeshData& mesh, const UiSettings& settings, ImDrawList* drawList,
                       const ImVec2& pos, const ImVec2& size) {
    if (!mesh.valid || mesh.triangles.empty()) return;
    Vec3 center{
        (mesh.min.x + mesh.max.x) * 0.5f,
        (mesh.min.y + mesh.max.y) * 0.5f,
        (mesh.min.z + mesh.max.z) * 0.5f
    };
    const float dx = mesh.max.x - mesh.min.x;
    const float dy = mesh.max.y - mesh.min.y;
    const float dz = mesh.max.z - mesh.min.z;
    const float maxDim = std::max({dx, dy, dz, 0.001f});
    const float scale = 0.45f * std::min(size.x, size.y) / maxDim;
    const float fov = 60.0f * 3.1415926535f / 180.0f;
    const float aspect = size.x > 0.0f ? size.x / size.y : 1.0f;
    const float distance = 2.5f / std::max(0.2f, settings.modelZoom);
    const ImVec2 screenCenter(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);

    const ImU32 color = ImGui::GetColorU32(settings.modelColor);
    for (const auto& tri : mesh.triangles) {
        Vec3 a = tri.a;
        Vec3 b = tri.b;
        Vec3 c = tri.c;
        a.x -= center.x; a.y -= center.y; a.z -= center.z;
        b.x -= center.x; b.y -= center.y; b.z -= center.z;
        c.x -= center.x; c.y -= center.y; c.z -= center.z;
        a = RotateY(RotateX(a, settings.modelPitch), settings.modelYaw);
        b = RotateY(RotateX(b, settings.modelPitch), settings.modelYaw);
        c = RotateY(RotateX(c, settings.modelPitch), settings.modelYaw);
        ImVec2 pa = ProjectPoint(a, fov, aspect, distance, screenCenter, scale);
        ImVec2 pb = ProjectPoint(b, fov, aspect, distance, screenCenter, scale);
        ImVec2 pc = ProjectPoint(c, fov, aspect, distance, screenCenter, scale);
        drawList->AddLine(pa, pb, color, 1.0f);
        drawList->AddLine(pb, pc, color, 1.0f);
        drawList->AddLine(pc, pa, color, 1.0f);
    }
}
