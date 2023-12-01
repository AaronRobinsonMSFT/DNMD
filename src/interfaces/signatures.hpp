
#include <internal/dnmd_platform.hpp>
#include <internal/span.hpp>

#include <external/cor.h>

#include <cstdint>

malloc_span<uint8_t> GetMethodDefSigFromMethodRefSig(span<uint8_t> methodRefSig);
