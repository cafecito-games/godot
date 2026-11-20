#pragma once

namespace CafecitoExtensions {

void initialize_extensions(int p_level);
void uninitialize_extensions(int p_level);
const char *get_extension_build_id();

} // namespace CafecitoExtensions
