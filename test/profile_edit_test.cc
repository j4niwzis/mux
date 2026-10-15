// SPDX-License-Identifier: AGPL-3.0-only
import std;
import mux.core.ids;
import gtest;

#include "gtest/gtest-macros.h"

TEST(ProfileEdit, EditingOneLanguageRetainsOtherPronounsAndUntouchedHtml) {
  mux::user_profile current;
  current.name = "Old name";
  current.avatar = "avatar";
  current.details.banner = "banner";
  current.details.pronouns = {{"fr", "elle"}, {"en", "they/them"}};
  current.details.biography = {"Biography", "<b>Biography</b>"};
  mux::profile_edit edit{.name = "New name", .language = "en", .pronouns = "she/her\n\n", .biography = "Biography"};
  const auto changed = mux::with_profile_edit(current, edit);
  ASSERT_EQ(changed.details.pronouns.size(), 2u);
  EXPECT_EQ(changed.details.pronouns[0], (mux::pronouns{"fr", "elle"}));
  EXPECT_EQ(changed.details.pronouns[1], (mux::pronouns{"en", "she/her"}));
  EXPECT_EQ(changed.details.biography.html, current.details.biography.html);
  EXPECT_EQ(changed.avatar, current.avatar);
  EXPECT_EQ(changed.details.banner, current.details.banner);
  edit.biography = "Changed biography";
  EXPECT_FALSE(mux::with_profile_edit(current, edit).details.biography.html);
}

TEST(ProfileEdit, SavingATypedDraftKeepsBannerAndMetadataThatArrivedLater) {
  mux::user_profile before;
  before.name = "Name";
  before.details.banner = "old-banner";
  before.details.pronouns = {{"en", "they/them"}};
  auto edit = mux::profile_edit_of(before);
  edit.name = "Edited name";
  auto latest = before;
  latest.avatar = "new-avatar";
  latest.details.banner = "new-banner";
  latest.details.pronouns.push_back({"es", "elle"});
  const auto saved = mux::with_profile_edit(latest, edit);
  EXPECT_EQ(saved.name, "Edited name");
  EXPECT_EQ(saved.avatar, "new-avatar");
  EXPECT_EQ(saved.details.banner, "new-banner");
  EXPECT_EQ(saved.details.pronouns, latest.details.pronouns);
}
