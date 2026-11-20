#pragma once

#include "core/crypto/cafecito_security.h"
#include "core/string/ustring.h"

namespace CafecitoSecurity {

const RuntimeOps *get_runtime_ops();
String key_fingerprint();

} // namespace CafecitoSecurity
