// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.matrix -- Matrix's end-to-end encryption, as a plugin:
// the account made with it (encrypted_account, over the client's account),
// found where the account is made (mux.plugins.matrix).
export module mux.plugin.encryption.matrix;

export import :account;
export import :verification;
export import :keys;
export import :events;
export import :backup;
