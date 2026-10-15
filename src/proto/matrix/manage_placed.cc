// SPDX-License-Identifier: AGPL-3.0-only
// Dispatch to the page's own implementation without deducing its tree here.
module mux.ui.proto.matrix;

import std;
import skiff.compose;

namespace mux::proto::matrix::manage_detail {
void manage_page_placed(manage_box& box, manage::erased_page& page) {
  box.draft = std::monostate{};
  (void)(manage::place_general(box, page) ||
          manage::place_members(box, page) ||
          manage::place_permissions(box, page) ||
          manage::place_packs(box, page) ||
          manage::place_developer(box, page));
}
}
namespace mux::ui {
void preserve_manage_input(proto::matrix::manage::erased_page& page) {
  namespace manage = proto::matrix::manage;
  (void)(manage::preserve_general(page) ||
          manage::preserve_members(page) ||
          manage::preserve_permissions(page) ||
          manage::preserve_packs(page) ||
          manage::preserve_developer(page));
}
}
