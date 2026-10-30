// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.profiles.ui -- An account's Profile page: its name, pronouns
// and their language, its biography, a banner where its protocol has one;
// and its presence, of those its protocol has (profile_policy_of(state),
// the protocol's) -- among Manage accounts' pages (contract::account_pages).
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.profiles.ui;

import std;
import splice;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.widgets.model;
import skiff.nodes.text;
import mux.core;
import mux.config;
import mux.composition;
import mux.protocols;
import mux.ui.base;
import mux.ui.proto;
import mux.plugin.profiles.types;

export namespace mux::plugins::profiles {

using namespace ::mux::ui;

struct profile_draft {
  profile_edit edit;
  std::optional<user_profile> first;
};
struct profile_form_seed {
  account_id by;
  std::uint32_t round = 0;
  std::optional<user_profile> profile;
  friend bool operator==(const profile_form_seed& a, const profile_form_seed& b) {
    return a.by == b.by && a.round == b.round;
  }
};
auto profile_fields(const palette& colours, const profile_form_seed& seed, bool banners) {
  namespace c = skiff::compose;
  auto feedback = c::shown_for<account_page_read>([](const account_page_read& now) {
    return now && now->profile_said.has_value();
  }, c::text_for<account_page_read>([](const account_page_read& now) {
    return now ? now->profile_said.value_or("") : std::string{};
  }, c::styled({.fillX = true}, wrapped(nodes::Text("", 13.0f, colours.dim)))));
  auto submit = c::spec_for<profile_draft>([](const profile_draft& draft) {
    const auto changed = draft.first ? with_profile_edit(*draft.first, draft.edit) : user_profile{};
    return scene::Spec{.fillX = true, .height = 36.0f,
        .disabled = !draft.first || changed == *draft.first || (!changed.details.pronouns.empty() && draft.edit.language.empty())};
  }, c::asking_for<profile_draft>([by = seed.by](const profile_draft& draft) {
    return request::save_profile{by, draft.edit};
  }, button_for<scene::NoAction>(colours.widgets, "Save profile", {})));
  return SKIFF_NAMED(profile_fields, c::local<profile_draft>(
      c::spec_for<account_page_read>([](const account_page_read& now) {
        return scene::Spec{.disabled = now && now->profile_saving};
      }, c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          model_field<&profile_edit::name>(colours, "Display name", "Your name"),
          model_field<&profile_edit::language>(colours, "Pronouns language", "en"),
          model_area<&profile_edit::pronouns>(colours, "Pronouns", "One set per line", 3),
          model_area<&profile_edit::biography>(colours, "About you", "Your biography", 6),
          c::visible(banners, c::styled({.fillX = true, .height = 36.0f},
              button_for<sends<request::pick_profile_banner>>(colours.widgets, "Choose banner…", {seed.by}))),
          c::visible(banners, c::spec_for<account_page_read>([](const account_page_read& now) {
            return scene::Spec{.fillX = true, .height = 36.0f,
                .disabled = !now || !now->profile || !now->profile->details.banner};
          }, button_for<sends<request::remove_profile_banner>>(colours.widgets, "Remove banner", {seed.by}))),
          std::move(submit), std::move(feedback))),
      profile_draft{seed.profile ? profile_edit_of(*seed.profile) : profile_edit{}, seed.profile}));
}
struct presence_draft {
  std::size_t choice = 0;
  std::string status;
};
struct presence_form_seed {
  account_id by;
  std::optional<mux::presence> presence;
  friend bool operator==(const presence_form_seed& a, const presence_form_seed& b) {
    return a.by == b.by && a.presence.has_value() == b.presence.has_value();
  }
};
struct set_presence_choice {
  std::size_t index = 0;
  void operator()(presence_draft& draft) const { draft.choice = index; }
};
struct pick_presence_choice {
  using Answer = decltype(skiff::model::over<presence_draft>(set_presence_choice{}));
  Answer operator()(std::size_t index) const { return skiff::model::over<presence_draft>(set_presence_choice{index}); }
};
auto presence_fields(const palette& colours, const presence_form_seed& seed,
                     const std::vector<std::pair<std::string, availability_t>>& choices) {
  namespace c = skiff::compose;
  const auto selected = seed.presence ? std::ranges::find(choices, seed.presence->state,
      [](const auto& choice) { return choice.second; }) : choices.end();
  const auto index = selected == choices.end() ? std::size_t{0} : static_cast<std::size_t>(selected - choices.begin());
  return SKIFF_NAMED(presence_fields, c::local<presence_draft>(c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::projected<presence_draft>([](const presence_draft& draft) { return draft.choice; },
          make_choice_menu(colours, "Presence", choices | std::views::transform([](const auto& one) { return one.first; }) | std::ranges::to<std::vector>(), index, pick_presence_choice{})),
      model_field<&presence_draft::status>(colours, "Status", "What you are doing"),
      c::asking_for<presence_draft>([by = seed.by, choices](const presence_draft& draft) {
        return request::save_presence{by, {choices.at(draft.choice).second,
            draft.status.empty() ? std::nullopt : std::optional(draft.status)}};
      }, c::styled({.fillX = true, .height = 36.0f}, button_for<scene::NoAction>(colours.widgets, "Set presence", {})))),
      presence_draft{index, seed.presence ? seed.presence->status.value_or("") : std::string{}}));
}
MUX_INLINE_IN_RELEASE auto profile_view(const palette& colours, const account_page_facts& first, const profile_policy& policy) {
  namespace c = skiff::compose;
  using content = decltype(profile_fields(colours, profile_form_seed{}, false));
  using presence_content = decltype(presence_fields(colours, presence_form_seed{}, {}));
  return SKIFF_NAMED(profile_view, c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      nodes::Text("Profile", 20.0f, colours.text, true),
      c::projected<account_page_read>([](const account_page_read& now) {
        return now ? std::optional(profile_form_seed{now->by, now->profile_round, now->profile}) : std::nullopt;
      }, c::mount_now<content, profile_form_seed>([at = &colours, banners = policy.banners](const profile_form_seed& seed) {
        return std::tuple{profile_fields(*at, seed, banners)};
      }, profile_form_seed{first.by, first.profile_round, first.profile}, {.fillX = true, .autoSize = scene::axes::kY})),
      c::projected<account_page_read>([](const account_page_read& now) {
        return now ? std::optional(presence_form_seed{now->by, now->presence}) : std::nullopt;
      }, c::mount_now<presence_content, presence_form_seed>([at = &colours, choices = policy.presences](const presence_form_seed& seed) {
        return std::tuple{presence_fields(*at, seed, choices)};
      }, presence_form_seed{first.by, first.presence}, {.fillX = true, .autoSize = scene::axes::kY}))));
}

// What a protocol's profile holds, by its state: its own where it says
// (profile_policy_of, by ADL), else no banner and online or offline.
namespace policy_defaults {
inline ui::profile_policy profile_policy_of(const auto&) {
  return {false, {{"Online", availability::online{}}, {"Offline", availability::offline{}}}};
}
}  // namespace policy_defaults
[[nodiscard]] inline ui::profile_policy policy_of(const protocol_state_t& state) {
  return spl::visit([](const auto& now) {
    using policy_defaults::profile_policy_of;
    return profile_policy_of(now);
  }, state);
}

// The page, among an account's (contract::account_pages).
constexpr std::string_view page_title(profile_page) { return "Profile"; }
inline icon_t page_icon(profile_page) { return icon::info{}; }
MUX_INLINE_IN_RELEASE auto page_view(profile_page, const palette& colours, const account_page_facts& first) {
  return profile_view(colours, first, policy_of(first.state));
}
inline ui::request::refresh_profile asked_as_it_opens(profile_page, const account_id& by) { return {by}; }

}  // namespace mux::plugins::profiles
