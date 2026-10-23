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
It includes the complete C++ API, `dnmd::handle_ptr`, and the global
`md_added_row_t` compatibility wrapper for C row additions. The typed API is
organized into these headers:

| Header | Contents |
| --- | --- |
| `dnmd_columns.hpp` | Column types, descriptors, and shared schema metadata |
| `dnmd_cursors.hpp` | Row/position cursors, conversions, navigation, and factories |
| `dnmd_read.hpp` | Row readers and read-only column iterators |
| `dnmd_write.hpp` | Row writers, cursor write methods, and typed row-add guards |
| `dnmd_search.hpp` | Typed searches and cursor parent lookup |

Each header includes its dependencies. When using individual headers instead of
the umbrella, include `dnmd_write.hpp` for cursor `set_column_value` methods and
`dnmd_search.hpp` for `get_parent_token`.

`dnmd::cursor<TableId>` represents an existing, dereferenceable row and cannot be
default-constructed or constructed directly from a C cursor.
`dnmd::raw_cursor<TableIds...>` and `dnmd::indirect_cursor<DirectTableId, IndirectTableId>`
represent positions, including nil and one-past-the-end positions. Test a position
before calling `as()` to obtain a row; calling `as()` on a non-dereferenceable
position violates its precondition.
Positions check dereferenceability inline using their table and RID bounds,
without calling a C API predicate.
Row, raw, and indirect cursors contain private table-pointer/RID state, not C
cursors. They cannot be default-constructed or converted to/from `mdcursor_t`.
Use `raw_cursor<TableIds...>::nil(handle)` or `end(handle)` for explicit
table-bound sentinels. `position.is<TableId>()` tests the bound table independently
of dereferenceability, including for nil and end positions. `position.is_nil()`
tests for RID zero; it is false for end positions even though they are not
dereferenceable. Narrowing to a different table returns that table's nil in the
same image. Checked `at(handle, row, table)` construction also validates the
table set and nil/live/end bounds; it does not expose an unchecked pointer/RID
constructor. Metadata handles must remain valid for the lifetime of these
borrowed positions.

Converting a row or raw position to a `raw_cursor` whose table targets are a
superset is implicit and nonthrowing; the conversion preserves its table, RID,
image, and nil/end state. Reserved `mdtid_Unused` tag slots do not count as
possible source tables. This lets typed rows be passed directly to heterogeneous
callbacks taking `dnmd::any_cursor`. Indirect-to-raw widening preserves the
source position, not the resolved target. Narrowing remains explicit and checked.
Prefer fixed-table or bounded-target cursors in helpers; `any_cursor` is for
genuinely heterogeneous callback and enumeration boundaries.

Indirect positions support nil/live/end source and target RIDs. `get_token`
describes the source; `get_target_token` describes a successfully resolved target,
including its nil/end sentinels. A malformed pointer reference fails resolution
without creating an invalid row cursor. Indirect positions can be constructed
from native raw positions; no C-cursor escape conversion is needed.

Every table slot retains its owning context, including empty tables.
`mdtable_t::is_initialized`, not a null context, distinguishes materialized table
storage from an absent/empty table. Binding nil does not initialize storage.

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

Construct a raw position directly with `dnmd::raw_cursor<TableIds...>{handle, token}`.
The handle must be valid; this precondition is asserted. Valid tokens must identify
an existing row in one of the permitted tables. Invalid tokens, including nil,
end, and wrong-table tokens, produce a nil position in the first non-reserved
table in the parameter pack. The nil position retains its table even if that table
is empty, without allocating or fully initializing the table.

`dnmd::md_token_to_cursor<TableIds...>` uses the same lookup and nil fallback for
valid handles. Like cursor constructors and factories, it requires a valid handle;
there is no unbound default position to return for an invalid handle.
Use `dnmd::md_is_valid_token` when only validation is needed. Both helpers use
the C++ token bounds checks rather than the C token-lookup API. Rows provide
`get_column_values` for bulk token reads and `get_parent_token` for list owners.
Typed range searches use column descriptors and return a raw cursor in the
searched table along with the range status and count. Their status type is
`dnmd::range_result`, an enum class declared in `dnmd_search.hpp` with values
`found`, `not_found`, and `not_supported`. Their numeric values match the C API's
`MD_RANGE_FOUND`, `MD_RANGE_NOT_FOUND`, and `MD_RANGE_NOT_SUPPORTED`, respectively;
the C API continues to use `md_range_result_t`.

The C++ `md_find_*` APIs search constant, reference, and list-position columns;
heap columns are not searchable. Reference search values are full tokens or typed
cursors, not bare RIDs. `column_iterator<TableId, ColumnType>` is a read-only
random-access iterator bound to a row and column. It yields stored `uint32_t`
values so coded indices retain encoded RID/tag order rather than token-type order.
Iterators borrow metadata storage and are invalidated by edits like row readers.
Default-constructed column iterators have the standard singular state for
comparison/assignment; they do not contain a null-table cursor or row reader.

Row searches return the first match at or after the supplied row, using
`std::lower_bound` for sorted primary keys and `std::find` otherwise. Range
searches use `std::equal_range` only for a sorted primary key with no pending row
addition; unsupported, missing, or invalid-key results leave output parameters
unchanged. The range is restricted to the supplied row's suffix.
Parent searches use typed list columns and Ptr reverse lookup, preserve empty
runs and nil starts, and project Event/Property maps back to TypeDef positions.
Loaded direct runs use `std::upper_bound` with nil-boundary normalization;
edited or indirected runs use a linear fallback rather than assume partitioning.
The legacy C entrypoints remain available for the C core.

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
This includes writes to other rows in the same image when heap growth widens
columns.

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
setters' access-context checks; allocation and Portable PDB reference tracking
remain shared with the core.

C++ sorted-state maintenance uses complete typed, ordered key sequences, including
GenericParam's Owner/Number keys and LocalScope's descending Length tie-breaker.
Key writes compare the row lexicographically against both neighbors; non-key
writes do not affect sortedness, and an unsorted table is not marked sorted again.
Validation is deferred while initializing a new row. Acquire a noncopyable
`dnmd::added_row<TableId>` using `append(handle)`, `insert(relative, after)`, or
typed list-add factories, access the pending row with asserted `as()`, and commit
on destruction or explicitly with `commit()`. Explicit commit consumes the guard
to a bound nil; a previously saved row cursor remains a normal borrowed row.
Moves transfer ownership and leave the source bound nil. A failed acquisition
returns a nil guard and finishes any partial row it acquired; metadata edits are
not rolled back.

```cpp
auto added = dnmd::added_row<mdtid_TypeRef>::append(handle.get());
if (!added)
    return false;
auto row = added.as();
if (!row.set_column_value(dnmd::columns::TypeRef_TypeName, "Example"))
    return false;
added.commit();
```

List additions infer the child table from the typed list column; sorted list
additions also require a constant sort column from that child table.
Only the global `md_added_row_t` retains C-cursor construction/conversion and the
C output-pointer hook, delegating its cleanup to the legacy C commit API.
Native typed guards never expose C output pointers.

Binary searches use only an ascending first
key from these same sequences, never a secondary key in isolation.
Legacy C validation and delta-only physical list sorting remain in the C core.

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

For shipping-representative disassembly on Windows, use the Release build in
`artifacts\release` with IPO enabled for both C and C++ targets:

```powershell
cmake -S . -B artifacts\release -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON `
    -DCMAKE_POLICY_DEFAULT_CMP0069=NEW
cmake --build artifacts\release --target dnmd_interfaces
```

The IPO setting enables MSVC `/GL` compilation and `/LTCG` linking. The policy
setting also enables IPO for the vendored dependency.

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