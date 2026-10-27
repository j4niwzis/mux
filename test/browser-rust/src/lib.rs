// SPDX-License-Identifier: AGPL-3.0-only
use std::sync::Mutex;

static VALUES: Mutex<Vec<u32>> = Mutex::new(Vec::new());

#[no_mangle]
pub extern "C" fn mux_browser_rust_check() -> u32 {
    let mut values = VALUES.lock().unwrap();
    values.extend([7, 11]);
    values.iter().sum()
}
