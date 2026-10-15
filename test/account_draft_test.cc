// SPDX-License-Identifier: AGPL-3.0-only
import std;
import mux.proto.matrix.kept;
import mux.proto.xmpp.kept;
import gtest;

#include "gtest/gtest-macros.h"

TEST(AccountDraft, MatrixPasswordAndBrowserRegistrationKeepTheirDifferentRequirements) {
  namespace p = mux::proto::matrix;
  p::account_draft draft;
  draft.user_id = "@user:example.org";
  draft.password = "example password";
  draft.creating = true;
  draft.agrees = true;
  draft.registration_token = "invitation";
  auto kept = p::account_from(draft);
  ASSERT_TRUE(kept);
  EXPECT_TRUE(kept->create.value_or(false));
  EXPECT_TRUE(kept->accept_terms.value_or(false));
  EXPECT_EQ(kept->registration_token, "invitation");
  draft.password.clear();
  EXPECT_FALSE(p::account_from(draft));
  draft.in_browser = true;
  kept = p::account_from(draft);
  ASSERT_TRUE(kept);
  EXPECT_TRUE(kept->oauth.value_or(false));
  EXPECT_TRUE(kept->password.empty());
  EXPECT_EQ(p::draft_of(*kept), draft);
}

TEST(AccountDraft, XmppRejectsMalformedPortsWithoutChangingTheRawDraft) {
  namespace p = mux::proto::xmpp;
  p::account_draft draft;
  draft.address = "user@example.org";
  draft.password = "example password";
  draft.port = "5222 trailing";
  EXPECT_FALSE(p::account_from(draft));
  EXPECT_EQ(draft.port, "5222 trailing");
  draft.port = "65536";
  EXPECT_FALSE(p::account_from(draft));
  draft.port = "5223";
  draft.host = "xmpp.example.org";
  draft.resource = "desktop";
  draft.plain_without_tls = true;
  auto kept = p::account_from(draft);
  ASSERT_TRUE(kept);
  EXPECT_EQ(kept->port, 5223);
  EXPECT_EQ(kept->host, "xmpp.example.org");
  EXPECT_TRUE(kept->plain_without_tls);
}

TEST(AccountDraft, XmppRegistrationAnswersSurviveDraftConversion) {
  namespace p = mux::proto::xmpp;
  p::kept saved{.address = "user@example.org", .password = "example password"};
  saved.create = true;
  saved.answers = std::vector<p::registration_answer>{{"hidden", "server-token"}, {"captcha", "typed"}};
  auto draft = p::draft_of(saved);
  EXPECT_TRUE(draft.creating);
  const auto rebuilt = p::account_from(draft);
  ASSERT_TRUE(rebuilt);
  EXPECT_EQ(rebuilt->answers, saved.answers);
  draft.password.clear();
  EXPECT_FALSE(p::account_from(draft));
}
