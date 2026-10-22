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

struct md_added_row_t final
{
private:
    mdcursor_t new_row{};

public:
    md_added_row_t() = default;
    explicit md_added_row_t(mdcursor_t row) noexcept : new_row{row} {}
    md_added_row_t(md_added_row_t const&) = delete;
    md_added_row_t& operator=(md_added_row_t const&) = delete;
    md_added_row_t(md_added_row_t&& other) noexcept : new_row{other.new_row} { other.new_row = {}; }
    md_added_row_t& operator=(md_added_row_t&& other) noexcept
    {
        if (this != std::addressof(other))
        {
            ::md_commit_row_add(new_row);
            new_row = other.new_row;
            other.new_row = {};
        }
        return *this;
    }
    ~md_added_row_t() { ::md_commit_row_add(new_row); }
    operator mdcursor_t() const noexcept { return new_row; }
    mdcursor_t* operator&() noexcept { return &new_row; }
};

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
