#pragma once

#include "studio/project.hpp"
#include "studio/dbc.hpp"

#include <cstdint>
#include <optional>
#include <string>

struct sqlite3;

namespace studio {

struct StoredProject {
    std::optional<ProjectDefinition> definition;
    std::uint64_t revision = 0;
};

struct DbcImport {
    std::string uri;
    std::string name;
    std::string channel;
    std::string text;
    bool can_fd = false;
};

class ProjectStore {
public:
    explicit ProjectStore(const std::string& database_path);
    ~ProjectStore();

    ProjectStore(const ProjectStore&) = delete;
    ProjectStore& operator=(const ProjectStore&) = delete;

    [[nodiscard]] StoredProject load() const;
    [[nodiscard]] std::uint64_t save(const ProjectDefinition& project);
    [[nodiscard]] Json import_dbcs(const std::vector<DbcImport>& sources);
    [[nodiscard]] Json dbc_sources() const;
    [[nodiscard]] Json ecu_view(const std::string& channel, const std::string& ecu) const;

private:
    sqlite3* database_ = nullptr;
};

} // namespace studio
