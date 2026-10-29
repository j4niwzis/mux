// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.changes -- What an encrypted account tells the
// program of its own: an emoji verification, as it goes -- joined into
// mux::change_t (mux.plugins.changes), and taken by the plugin's part.
// Light: under mux.core, on its ids alone.
export module mux.plugin.encryption.changes;

import std;
import mux.core.ids;

export namespace mux::plugins::encryption {

struct verification_changed {
  account_id by;
  std::string txn;
  std::string user;
  std::string device;
  verification_step_t step;
};

using changes = mux::change_list<verification_changed>;

inline std::string describe(const verification_changed& one) {
  return std::format("verifying {} {}: {}", one.user, one.device, one.txn);
}

}  // namespace mux::plugins::encryption
