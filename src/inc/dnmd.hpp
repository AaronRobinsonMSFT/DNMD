#ifndef _SRC_INC_DNMD_HPP_
#define _SRC_INC_DNMD_HPP_

#include "dnmd.h"
#include "dnmd_columns.hpp"
#include "dnmd_cursors.hpp"
#include "dnmd_read.hpp"
#include "dnmd_write.hpp"
#include "dnmd_search.hpp"
#include <memory>
#include <utility>

namespace dnmd
{

struct handle_deleter_t final
{
    using pointer = mdhandle_t;
    void operator()(mdhandle_t handle)
    {
        ::md_destroy_handle(handle);
    }
};

// C++ lifetime wrapper for mdhandle_t type
using handle_ptr = std::unique_ptr<mdhandle_t, handle_deleter_t>;

struct added_row_t final
{
private:
    mdcursor_t new_row{};
public:
    added_row_t() = default;
    explicit added_row_t(mdcursor_t row) : new_row{ row } {}
    added_row_t(added_row_t const& other) = delete;
    added_row_t(added_row_t&& other)
    {
        *this = std::move(other);
    }

    added_row_t& operator=(added_row_t const& other) = delete;
    added_row_t& operator=(added_row_t&& other)
    {
        new_row = other.new_row;
        other.new_row = {}; // Clear the other's row so we don't double-commit.
        return *this;
    }

    ~added_row_t()
    {
        detail::commit_row_add(new_row);
    }

    operator mdcursor_t()
    {
        return new_row;
    }

    mdcursor_t* operator&()
    {
        return &new_row;
    }
};

} // namespace dnmd

#endif // _SRC_INC_DNMD_HPP_
