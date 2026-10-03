// SPDX-License-Identifier: MIT

#include "omsi/web/api.hpp"

#include "omsi/web/server.hpp"

namespace omsi::web {
namespace {

// A Windows path as the front end shows it: backslashes kept, but the folder itself is
// the last component so the UI can name it.
std::string folderName(const std::filesystem::path& path) {
    return path.filename().string();
}

}  // namespace

std::string libraryJson(const omsi::content::Library& library) {
    Json j;
    j.beginObject();
    j.member("root", library.root().string());

    j.key("maps").beginArray();
    for (const auto& map : library.maps()) {
        j.beginObject();
        // The folder name is what the game writes to its log and what a user recognises,
        // so it is the identifier; the title is only a nicer label when there is one.
        j.member("id", map.folderName);
        j.member("title", map.title.empty() ? map.folderName : map.title);
        j.member("folder", map.directory.string());
        j.member("globalCfg", map.globalCfg.string());
        j.endObject();
    }
    j.endArray();

    j.key("vehicles").beginArray();
    for (const auto& vehicle : library.vehicles()) {
        j.beginObject();
        j.member("id", vehicle.manufacturer + "/" + vehicle.model);
        j.member("name", vehicle.displayName());
        j.member("manufacturer", vehicle.manufacturer);
        j.member("model", vehicle.model);
        j.member("kind", vehicle.kind);
        j.member("file", vehicle.file.string());
        j.endObject();
    }
    j.endArray();

    j.key("timetables").beginArray();
    for (const auto& table : library.timetables()) {
        j.beginObject();
        j.member("id", table.map + "/" + table.name);
        j.member("name", table.displayName());
        j.member("map", table.map);
        j.member("model", table.name);
        j.member("kind", table.kind);
        j.member("file", table.file.string());
        j.endObject();
    }
    j.endArray();

    j.key("problems").beginArray();
    for (const auto& problem : library.problems()) {
        j.value(problem);
    }
    j.endArray();

    j.endObject();
    return j.take();
}

void registerLibraryRoutes(Server& server, const omsi::content::Library& library) {
    server.route("/api/library", [&library](const Request&, Response& response) {
        response.status = 200;
        response.contentType = "application/json; charset=utf-8";
        response.body = libraryJson(library);
        return true;
    });
}

}  // namespace omsi::web