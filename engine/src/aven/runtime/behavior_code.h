#pragma once

#include "aven/core/json.h"

#include <string>

namespace aven {

// The EasyScript that does the same job as a behavior component, using its current settings
// (`values` is the component saved as JSON). Used by the editor to show how behaviors work
// and to turn a behavior into an editable script. Returns "" for components that aren't behaviors.
std::string behaviorAsEasyScript(const std::string& component, const Json& values);

} // namespace aven
