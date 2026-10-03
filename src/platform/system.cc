// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.system -- What the system does for the program: a link
// opened in what it opens links with.
export module mux.platform.system;

import std;
import sdl;

export namespace mux::platform::system {

// A link opened in what the system opens links with.
inline void open_url(const std::string& url) { sdl::SDL_OpenURL(url.c_str()); }

}  // namespace mux::platform::system
