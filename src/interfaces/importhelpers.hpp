#ifndef _SRC_INTERFACES_IMPORTHELPERS_HPP
#define _SRC_INTERFACES_IMPORTHELPERS_HPP

#include <internal/dnmd_platform.hpp>
#include <internal/span.hpp>
#include <cassert>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

// TODO(C++23): Replace these internal types with std::unexpected and std::expected<Cursor, HRESULT>.
namespace import_helpers
{
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
}

// Boolean conversion means a cursor is present; S_FALSE is a cursor-less lookup outcome.
template<typename Cursor>
class [[nodiscard]] cursor_result final
{
    std::variant<HRESULT, Cursor> _value;

public:
    using value_type = Cursor;
    using error_type = HRESULT;

    cursor_result() = delete;

    cursor_result(import_helpers::unexpected<HRESULT> error) noexcept
        : _value{std::in_place_index<0>, error.error()}
    {
        assert(error.error() != S_OK);
    }

    template<typename Position, std::enable_if_t<
        std::is_convertible_v<Position const&, Cursor>
        && std::is_nothrow_constructible_v<Cursor, Position const&>, int> = 0>
    cursor_result(Position const& cursor) noexcept
        : _value{std::in_place_index<1>, cursor}
    {
    }

    bool has_value() const noexcept
    {
        return std::holds_alternative<Cursor>(_value);
    }

    explicit operator bool() const noexcept { return has_value(); }

    HRESULT& error() noexcept
    {
        auto error = std::get_if<HRESULT>(&_value);
        assert(error != nullptr);
        return *error;
    }

    HRESULT const& error() const noexcept
    {
        auto error = std::get_if<HRESULT>(&_value);
        assert(error != nullptr);
        return *error;
    }

    Cursor& operator*() noexcept
    {
        auto cursor = std::get_if<Cursor>(&_value);
        assert(cursor != nullptr);
        return *cursor;
    }

    Cursor const& operator*() const noexcept
    {
        auto cursor = std::get_if<Cursor>(&_value);
        assert(cursor != nullptr);
        return *cursor;
    }

    Cursor* operator->() noexcept { return &**this; }
    Cursor const* operator->() const noexcept { return &**this; }
};

// Import a reference to a TypeDef row from one module and assembly pair to another.
cursor_result<dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef>> ImportReferenceToTypeDef(
    dnmd::raw_cursor<mdtid_TypeDef> sourceTypeDef,
    mdhandle_t sourceAssembly,
    span<uint8_t const> sourceAssemblyHash,
    mdhandle_t targetAssembly,
    mdhandle_t targetModule,
    bool alwaysImport, // Always import a reference to the TypeDef, even if the source and destination modules are the same.
    std::function<void(dnmd::any_cursor)> onRowAdded);

// Import a reference to a TypeDef, TypeRef, or TypeSpec row from one module and assembly pair to another, and return a TypeDef or TypeRef or TypeSpec token
// that can be used to refer to the imported type.
HRESULT ImportReferenceToTypeDefOrRefOrSpec(
    mdhandle_t sourceAssembly,
    mdhandle_t sourceModule,
    span<uint8_t const> sourceAssemblyHash,
    mdhandle_t targetAssembly,
    mdhandle_t targetModule,
    std::function<void(dnmd::any_cursor)> onRowAdded,
    mdToken* importedToken);

// Import a reference to a MemberRef row from one module and assembly pair to another.
// This method works at the IMetadataEmit/Import level as it is implementation-agnostic.
HRESULT DefineImportMember(
    IMetaDataEmit* emit,
    IMetaDataAssemblyImport *pAssemImport,
    void const  *pbHashValue,
    ULONG        cbHashValue,
    IMetaDataImport *pImport,
    mdToken     mbMember,
    IMetaDataAssemblyEmit *pAssemEmit,
    mdToken     tkImport,
    mdMemberRef *pmr);

#endif // _SRC_INTERFACES_IMPORTHELPERS_HPP
