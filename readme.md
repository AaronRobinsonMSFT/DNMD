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
`dnmd::raw_cursor<TableIds...>` and `dnmd::indirect_cursor<DirectTableId, IndirectTableId>`
represent positions, including nil and one-past-the-end positions. Test a position
before calling `as()` to obtain a row; calling `as()` on a non-dereferenceable
position violates its precondition.

```cpp
dnmd::handle_ptr handle{md_create_new_handle()};
auto types = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get());
if (types)
{
    auto type = types.as();
    dnmd::row_reader<mdtid_TypeDef> reader{type};
    uint32_t flags;
    char const* name;
    if (!reader.read(dnmd::columns::TypeDef_Flags, &flags)
        || !reader.read(dnmd::columns::TypeDef_TypeName, &name))
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

`dnmd::row_reader<TableId>` is a borrowed, read-only row access context
constructed from a dereferenceable `cursor<TableId>`. Reuse it across columns of
that row with `reader.read(column, output)`; overload resolution enforces the
source table, column value kind, and output type without category assertions.
It contains no writable data pointer. Cursor reads, including ranges and bulk
token reads, use this context rather than the C API's `create_access_context`
path. Heap offsets, encoded tags, and referenced row bounds are still validated.
For list columns, `reader.read(column, position, &count)` returns the range
without rebinding the reader.
Recreate readers after metadata edits that can invalidate their borrowed storage.

For reference columns, a single table ID denotes a direct row index. Multiple
table IDs specify a coded index in encoded tag order, including `mdtid_Unused`
placeholders for reserved tags. Reserved and out-of-range tags fail the read.
List columns select their direct or Ptr target from `mdtable_t::has_indirect_table`
on the element table, not from column target bits. The flag is initialized when
loading an image and set when editing introduces its Ptr table. Keeping it on
the element table lets TypeDef field and method lists select independently.

Rows provide matching `set_column_value` overloads for constants, UTF-8 strings,
blobs, GUIDs, user strings, and table references. They delegate to the corresponding
`dnmd::row_writer<TableId>::write(column, value)` overloads. Construct a writer from
a dereferenceable `cursor<TableId>` and reuse it for multiple columns of the row.
Writes return `bool` without exception-based error handling and retain the C
setters' input preconditions.
References accept tokens, typed rows, and raw positions, including nil and end
positions. An indirect value supplies its resolved target for a reference column,
but its source position for a list column so pointer-table indices are preserved.

Writers keep a row position rather than a cached writable pointer, so their own
heap writes can grow heaps and widen columns safely. They reacquire the current
layout and writable storage for each write, preserving copy-on-write behavior,
sorted-table bookkeeping, and Portable PDB reference tracking. Reacquire writers
after edits that change the row's identity, such as inserting rows before it.
Column encoding and storage writes use the typed C++ path rather than the C
setters' access-context checks; allocation and metadata bookkeeping remain shared
with the core.

Raw and indirect cursors expose `get_token(mdToken&)`, including for sentinel
positions. An indirect cursor additionally exposes `get_target_token(mdToken&)`
for its resolved target. Indirect cursors cache their resolved targets and
dereferenceability when constructed or advanced. Pointer-table rows are resolved
through `row_reader` using the typed Ptr-table columns in `dnmd::detail`;
nil/end positions map to the corresponding target-table sentinels without
reading a pointer row. Reacquire indirect cursors after metadata
edits that affect their source or target positions. Advance positions with
`dnmd::md_cursor_next` or `dnmd::md_cursor_move`, not row cursors. Typed row searches return a
raw cursor in the searched table, rather than the table referenced by the column.
The metadata handle must outlive its cursors, readers, and writers.

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