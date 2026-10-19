#pragma once

// The plugin host's side of the editor API (EditorApi.cpp): served on the bus while it is open.
// Only for VpxHost.cpp and EditorApi.cpp, which see the plugin SDK.

#include <cstdint>

#include "plugins/MsgPlugin.h"

namespace colorizing::vpx {

void installEditorApi(const MsgPluginAPI* api, uint32_t hostEndpoint);
void uninstallEditorApi();

}  // namespace colorizing::vpx
