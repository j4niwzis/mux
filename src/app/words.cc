// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.words: The motion setting read into its variant.
export module mux.app.words;

import std;
import skiff.paint;

export namespace mux::app {

// How much moves, as the accounts file says: "none", "reduced" or "full"
// (and full where it says nothing, or what it says is none of these).
skiff::paint::Motion motion_of(const std::optional<std::string>& said) {
  if (!said)
    return skiff::paint::motion::full{};
  static const std::unordered_map<std::string_view, skiff::paint::Motion> known = {
      {"full", skiff::paint::motion::full{}},
      {"reduced", skiff::paint::motion::reduced{}},
      {"none", skiff::paint::motion::none{}}};
  if (const auto found = known.find(*said); found != known.end())
    return found->second;
  std::println(std::cerr, "[mux] motion is \"none\", \"reduced\" or \"full\", not \"{}\": full it is", *said);
  return skiff::paint::motion::full{};
}

}  // namespace mux::app
