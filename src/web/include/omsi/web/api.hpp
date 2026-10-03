// SPDX-License-Identifier: MIT
//
// The launcher's JSON API.
//
// One endpoint, /api/library, is enough for the whole front end: it returns the maps,
// vehicles and timetables in one payload so the page does not have to stitch three
// requests together and can never show a map list from one scan beside a vehicle list
// from another.

#pragma once

#include <string>

#include "omsi/content/library.hpp"
#include "omsi/web/json.hpp"

namespace omsi::web {

// Registers /api/library on `server`, reading from `library`.
void registerLibraryRoutes(class Server& server, const omsi::content::Library& library);

// The payload, exposed separately so it can be tested without a socket.
std::string libraryJson(const omsi::content::Library& library);

}  // namespace omsi::web