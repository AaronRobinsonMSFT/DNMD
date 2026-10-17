# .NET MetaData &ndash; DNMD

DNMD represents a suites of tools for manipulating [ECMA-335][ecma_335] defined metadata. It designed to be written in unmanaged code (that is, C/C++) in a modern style. This doesn't mean it is intended to rely on the latest features or libraries. Rather it is written to use canonical C and C++ in a manner that is clear.

DNMD provides the following tools:

- `dnmd` - A static library with no external dependencies that represents the lowest level of reading ECMA-335.
- `dnmd_interfaces` - A shared library (`.dll`|`.dylib`|`.so`) that consumes `dnmd` and provides higher level .NET APIs. At present the following interfaces are provided:
  - [`IMetaDataDispenser`][api_dispenser]
  - [`IMetaDataImport`][api_import] / [`IMetaDataImport2`][api_import2]
  - [`IMetaDataAssemblyImport`][api_assemblyimport]
- `dnmd_interfaces_static` - A static library version of `dnmd_interfaces`.
- `mddump` - Utility for dumping ECMA-335 tables.

The primary goal of DNMD is to explore the benefits of a rewrite of the metadata APIs found in [dotnet/runtime](https://github.com/dotnet/runtime). The rewrite has the following constraints:

- Must be sharable across any existing .NET runtime implementation.
- Must be cross-platform with minimal OS abstraction layering.
- Represent scenarios that are relevant to modern .NET (that is, .NET 6+).

## Requirements (minimum)

- [CMake](https://cmake.org/download/) 3.10

- C11 and C++17 compliant compilers

## C++ API

`dnmd.hpp` provides typed cursors and column descriptors in the `dnmd` namespace.
The C API names and types in `dnmd.h` remain global.
`dnmd::cursor<TableId>` represents an existing, dereferenceable row and cannot be
default-constructed or constructed directly from a C cursor.
`dnmd::cursor_raw<TableIds...>` and `dnmd::cursor_indirect<DirectTableId, IndirectTableId>`
represent positions, including nil and one-past-the-end positions. Test a position
before calling `as()` to obtain a row; calling `as()` on a non-dereferenceable
position violates its precondition.

```cpp
dnmd::handle_ptr handle{md_create_new_handle()};
auto types = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get());
if (types)
{
    auto type = types.as();
    char const* name;
    if (!type.get_column_value(dnmd::columns::TypeDef_TypeName, &name))
        return false;
}
```

Pass a `uint32_t*` as the second argument when the row count is also needed.

Only single-table raw cursors expose `as()`. Narrow a multi-table position with
`get_as<TableId>()`, which returns a single-table raw cursor, or use `visit()` to
dispatch to the matching row type. `visit()` returns false without invoking the
callable for nil or end positions.

`dnmd::md_token_to_cursor<TableIds...>` also accepts multiple possible table IDs.
Use `dnmd::md_is_valid_token` when only validation is needed. Rows provide
`get_column_values` for bulk token reads and `get_parent_token` for list owners.
Typed range searches use column descriptors and return a raw cursor in the
searched table along with the range status and count.

Raw and indirect cursors expose `get_token(mdToken&)`, including for sentinel
positions. An indirect cursor additionally exposes `get_target_token(mdToken&)`
for its resolved target. Indirect cursors cache their resolved targets and
dereferenceability when constructed or advanced; reacquire them after metadata
edits that affect their source or target positions. Advance positions with
`dnmd::md_cursor_next` or `dnmd::md_cursor_move`, not row cursors. Typed row searches return a
raw cursor in the searched table, rather than the table referenced by the column.
The metadata handle must outlive its cursors.

## Build

> `git submodule update --init --recursive`

> `cmake -S . -B artifacts`

> `cmake --build artifacts --target install`

## Test

The `test/` directory contains all product tests. The native components for
DNMD should be built first. See the Build section.

The `DNMD.Tests.sln` file can be loaded in Visual Studio to run associated tests.
The managed tests will use the latest build of the DNMD libraries. Keep in mind
the native assets are built with a configuration independent of the tests.

Testing correctness defers to the current implementation of the relevant interface
defined in the .NET runtime the test is running on (for example, `IMetaDataImport`).
The approach is to pass identical arguments to the current implementation and the
implementation in this repo. The return argument and all out arguments are then
compared for equality. In some cases pointers are returned so the pointer is dereferenced
and then hashed.

# Additional Resources

[ECMA-335 specification][ecma_335]

<!-- Links -->
[ecma_335]: https://www.ecma-international.org/publications-and-standards/standards/ecma-335/

[api_dispenser]: https://learn.microsoft.com/dotnet/framework/unmanaged-api/metadata/imetadatadispenser-interface
[api_import]: https://learn.microsoft.com/dotnet/framework/unmanaged-api/metadata/imetadataimport-interface
[api_import2]: https://learn.microsoft.com/dotnet/framework/unmanaged-api/metadata/imetadataimport2-interface
[api_assemblyimport]: https://learn.microsoft.com/dotnet/framework/unmanaged-api/metadata/imetadataassemblyimport-interface