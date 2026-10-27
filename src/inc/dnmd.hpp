#ifndef _SRC_INC_DNMD_HPP_
#define _SRC_INC_DNMD_HPP_

#include "dnmd.h"
#include "dnmd_columns.hpp"
#include "dnmd_cursors.hpp"
#include "dnmd_read.hpp"
#include "dnmd_write.hpp"
#include "dnmd_search.hpp"
#include "dnmd_result.hpp"
#include <memory>

namespace dnmd
{
struct handle_deleter_t final
{
    using pointer = mdhandle_t;
    void operator()(mdhandle_t handle) { ::md_destroy_handle(handle); }
};
using handle_ptr = std::unique_ptr<mdhandle_t, handle_deleter_t>;
}

#endif // _SRC_INC_DNMD_HPP_
