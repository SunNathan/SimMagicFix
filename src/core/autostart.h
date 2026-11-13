#pragma once

#include "core/common.h"

namespace simagicfix {

bool is_autostart_enabled();
bool enable_autostart(const fs::path& exe);
bool disable_autostart();

bool create_start_menu_shortcut(const fs::path& target);
bool remove_start_menu_shortcut();

}  // namespace simagicfix
