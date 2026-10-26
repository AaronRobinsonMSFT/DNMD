#ifndef _SRC_INC_DNMD_PDB_HPP_
#define _SRC_INC_DNMD_PDB_HPP_

#include "dnmd_read.hpp"
#include "dnmd_result.hpp"
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#ifdef DNMD_PORTABLE_PDB
namespace dnmd
{
enum class pdb_parse_error
{
    invalid_argument,
    invalid_blob,
    out_of_memory
};

using sequence_point_parse_error = pdb_parse_error;

using document_name_result = expected<std::string, pdb_parse_error>;

document_name_result parse_document_name(mdhandle_t handle,
    uint8_t const* blob, size_t length) noexcept;

struct custom_modifier final
{
    bool required;
    mdToken type;
};

struct primitive_constant final
{
    uint8_t type_code;
};

struct enum_constant final
{
    uint8_t type_code;
    mdToken type;
};

enum class general_constant_kind
{
    value_type,
    class_type,
    object
};

struct general_constant final
{
    general_constant_kind kind;
    mdToken type;
};

struct local_constant_signature final
{
    std::variant<primitive_constant, enum_constant, general_constant> type;
    uint8_t const* value;
    size_t value_size;
    std::vector<custom_modifier> custom_modifiers;
};

using local_constant_result = expected<local_constant_signature, pdb_parse_error>;

// Modifier storage is owned; value bytes borrow the supplied blob.
local_constant_result parse_local_constant_signature(mdhandle_t handle,
    uint8_t const* blob, size_t length) noexcept;

enum class import_kind : uint8_t
{
    import_namespace = 1,
    import_assembly_namespace,
    import_type,
    import_xml_namespace,
    import_assembly_reference_alias,
    alias_assembly_reference,
    alias_namespace,
    alias_assembly_namespace,
    alias_type
};

struct import_record final
{
    import_kind kind;
    std::string_view alias;
    mdToken assembly = 0;
    std::string_view target_namespace;
    mdToken target_type = 0;
};

using imports_result = expected<std::vector<import_record>, pdb_parse_error>;

// Record storage is owned; alias and namespace views borrow the metadata image.
imports_result parse_imports(mdhandle_t handle, uint8_t const* blob, size_t length) noexcept;

struct document_change final
{
    raw_cursor<mdtid_Document> document;
};

struct sequence_point final
{
    uint32_t rolling_il_offset;
    uint32_t delta_lines;
    int64_t delta_columns;
    int64_t rolling_start_line;
    int64_t rolling_start_column;
};

struct hidden_sequence_point final
{
    uint32_t rolling_il_offset;
};

using sequence_point_record = std::variant<document_change, sequence_point, hidden_sequence_point>;

struct sequence_points final
{
    mdToken signature;
    // The blob's InitialDocument, or nil when the document is supplied by its row.
    raw_cursor<mdtid_Document> document;
    std::vector<sequence_point_record> records;

    sequence_points(mdToken sig, raw_cursor<mdtid_Document> doc) noexcept
        : signature{sig}, document{doc} {}
    sequence_points(sequence_points const&) = delete;
    sequence_points& operator=(sequence_points const&) = delete;
    sequence_points(sequence_points&&) noexcept = default;
    sequence_points& operator=(sequence_points&&) noexcept = default;
};

using sequence_points_result = expected<sequence_points, sequence_point_parse_error>;

// Records are owned by the result; document cursors borrow the supplied metadata image.
sequence_points_result parse_sequence_points(cursor<mdtid_MethodDebugInformation> method,
    uint8_t const* blob, size_t length) noexcept;
}
#endif // DNMD_PORTABLE_PDB

#endif // _SRC_INC_DNMD_PDB_HPP_
