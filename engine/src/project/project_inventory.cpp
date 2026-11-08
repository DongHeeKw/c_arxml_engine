#include "studio/project_inventory.hpp"
#include "studio/digest.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace studio {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}
bool is_link(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(path.c_str());
    if (attributes==INVALID_FILE_ATTRIBUTES) throw std::runtime_error("Cannot inspect project file attributes");
    return (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
#else
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path));
#endif
}
}
Json inspect_eclipse_project(const std::string& root_text) {
    if (root_text.find('\0')!=std::string::npos) throw std::invalid_argument("Project path contains NUL");
    const std::u8string encoded(root_text.begin(),root_text.end());
    const std::filesystem::path requested(encoded);
    if (!requested.is_absolute() || !std::filesystem::is_directory(requested))
        throw std::invalid_argument("Project root must be an existing absolute directory");
    if (is_link(requested)) throw std::invalid_argument("Project root cannot be a symlink or reparse point");
    const auto root=std::filesystem::canonical(requested);
    std::map<std::string,Json> files;
    std::set<std::string> folders;
    Json::Array blocked;
    for (std::filesystem::recursive_directory_iterator it(root),end;it!=end;++it) {
        const auto relative=utf8(it->path().lexically_relative(root));
        if (files.size()+folders.size()+blocked.size()>=10000)
            throw std::runtime_error("Project inventory exceeds the 10000-entry safety limit");
        if (is_link(it->path())) {
            it.disable_recursion_pending();
            blocked.emplace_back(Json::Object{{"code","PROJECT_LINK_NOT_FOLLOWED"},
                {"severity","error"},{"path",relative},{"message","Symlink/reparse point was not followed"}});
            continue;
        }
        if (it->is_directory()) { folders.insert(relative);continue; }
        if (!it->is_regular_file()) throw std::runtime_error("Project contains a non-regular file: "+relative);
        const auto size=it->file_size(); const auto modified=it->last_write_time();
        std::ifstream input(it->path(),std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read project file: "+relative);
        const auto digest=sha256(input);
        if (size!=it->file_size() || modified!=it->last_write_time())
            throw std::runtime_error("Project changed while inventory was being read: "+relative);
        files.emplace(relative,Json::Object{{"path",relative},{"sha256",digest},{"sizeBytes",static_cast<double>(size)}});
    }
    Json::Array inventory, directories, missing;
    for (auto& [name,value] : files) inventory.push_back(std::move(value));
    for (const auto& name : folders) directories.emplace_back(name);
    for (const auto* metadata : {".project",".cproject"})
        if (!files.contains(metadata)) missing.emplace_back(metadata);
    return Json::Object{{"root",utf8(root)},{"readOnly",true},{"files",std::move(inventory)},
        {"folders",std::move(directories)},{"issues",std::move(blocked)},
        {"missingMetadata",missing},{"eclipseProject",missing.empty()},
        {"buildExecuted",false},{"legacyBaseline",false}};
}
}
