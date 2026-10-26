#ifndef _SRC_INC_DNMD_RESULT_HPP_
#define _SRC_INC_DNMD_RESULT_HPP_

#include <cassert>
#include <type_traits>
#include <utility>
#include <variant>

namespace dnmd
{
// TODO(C++23): Replace these compatibility types with std::unexpected and std::expected.
template<typename Error>
class unexpected final
{
    Error _error;

public:
    explicit unexpected(Error error) noexcept(std::is_nothrow_move_constructible_v<Error>)
        : _error{std::move(error)} {}

    Error& error() noexcept { return _error; }
    Error const& error() const noexcept { return _error; }
};

template<typename Value, typename Error>
class [[nodiscard]] expected final
{
    std::variant<Error, Value> _value;

public:
    using value_type = Value;
    using error_type = Error;

    expected() = delete;

    expected(unexpected<Error> error) noexcept(std::is_nothrow_move_constructible_v<Error>)
        : _value{std::in_place_index<0>, std::move(error.error())}
    {
    }

    template<typename T, std::enable_if_t<
        std::is_convertible_v<T&&, Value>
        && std::is_nothrow_constructible_v<Value, T&&>, int> = 0>
    expected(T&& value) noexcept
        : _value{std::in_place_index<1>, std::forward<T>(value)}
    {
    }

    bool has_value() const noexcept { return _value.index() == 1; }
    explicit operator bool() const noexcept { return has_value(); }

    Error& error() noexcept
    {
        auto value = std::get_if<0>(&_value);
        assert(value != nullptr);
        return *value;
    }

    Error const& error() const noexcept
    {
        auto value = std::get_if<0>(&_value);
        assert(value != nullptr);
        return *value;
    }

    Value& operator*() noexcept
    {
        auto value = std::get_if<1>(&_value);
        assert(value != nullptr);
        return *value;
    }

    Value const& operator*() const noexcept
    {
        auto value = std::get_if<1>(&_value);
        assert(value != nullptr);
        return *value;
    }

    Value* operator->() noexcept { return &**this; }
    Value const* operator->() const noexcept { return &**this; }
};
}

#endif // _SRC_INC_DNMD_RESULT_HPP_
