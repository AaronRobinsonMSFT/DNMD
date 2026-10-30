#include "internal.hpp"
#include <dnmd_pdb.hpp>

namespace
{
    int report_error(dnmd::pdb_parse_error error, char const* kind,
        size_t offset, uint32_t length, uint32_t raw)
    {
        if (error == dnmd::pdb_parse_error::out_of_memory)
        {
            std::printf("Ran out of memory when parsing PDB blob.\n");
            return -1;
        }
        std::printf("Invalid PDB Blob (%s) Offset: %zu (len: %u) [%#x]|", kind, offset, length, raw);
        return 1;
    }

    char const* text(std::string_view value) noexcept
    {
        return value.empty() ? "" : value.data();
    }
}

namespace dnmd::detail
{
int dump_pdb_blob(mdcxt_t* context, mdtable_id_t table, col_index_t column,
    uint8_t const* blob, uint32_t length, uint32_t raw)
{
    size_t offset = blob == nullptr || context->blob_heap.ptr == nullptr
        ? 0 : static_cast<size_t>(blob - context->blob_heap.ptr);
    if (table == mdtid_Document && column == columns::Document_Name.column_index)
    {
        auto name = parse_document_name(context, blob, length);
        if (!name)
            return report_error(name.error(), "DocumentName", offset, length, raw);
        std::printf("DocumentName: '%s' [%#x]|", name->c_str(), raw);
        return 1;
    }
    if (table == mdtid_LocalConstant && column == columns::LocalConstant_Signature.column_index)
    {
        auto parsed = parse_local_constant_signature(context, blob, length);
        if (!parsed)
            return report_error(parsed.error(), "LocalConstantSig", offset, length, raw);
        std::printf("LocalConstantSig: ");
        for (auto const& modifier : parsed->custom_modifiers)
            std::printf("%s(0x%08x) ", modifier.required ? "modreq" : "modopt", modifier.type);
        std::visit([](auto const& value) {
            using type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<type, primitive_constant>)
                std::printf("Primitive: 0x%02x ", value.type_code);
            else if constexpr (std::is_same_v<type, enum_constant>)
                std::printf("Enum: 0x%02x{0x%08x (mdToken)} ", value.type_code, value.type);
            else
                std::printf("General: 0x%02x{0x%08x (mdToken)} ", static_cast<unsigned>(value.kind), value.type);
        }, parsed->type);
        std::printf("Value Offset: %zu (len: %zu) [%#x]|",
            static_cast<size_t>(parsed->value - context->blob_heap.ptr), parsed->value_size, raw);
        return 1;
    }
    if (table == mdtid_ImportScope && column == columns::ImportScope_Imports.column_index)
    {
        if (length == 0)
        {
            std::printf("Empty Imports: Offset: %zu (len: %u) [%#x]|", offset, length, raw);
            return 1;
        }
        auto parsed = parse_imports(context, blob, length);
        if (!parsed)
            return report_error(parsed.error(), "Imports", offset, length, raw);
        std::printf("{ ");
        bool first = true;
        for (auto const& value : *parsed)
        {
            if (!first)
                std::printf(", ");
            first = false;
            int alias_size = static_cast<int>(value.alias.size());
            int namespace_size = static_cast<int>(value.target_namespace.size());
            switch (value.kind)
            {
            case import_kind::import_namespace:
                std::printf("ns('%.*s')", namespace_size, text(value.target_namespace));
                break;
            case import_kind::import_assembly_namespace:
                std::printf("ns('%.*s' in 0x%08x (mdToken))",
                    namespace_size, text(value.target_namespace), value.assembly);
                break;
            case import_kind::import_type:
                std::printf("type(0x%08x (mdToken))", value.target_type);
                break;
            case import_kind::import_xml_namespace:
                std::printf("xml-alias('%.*s' for '%.*s')",
                    alias_size, text(value.alias), namespace_size, text(value.target_namespace));
                break;
            case import_kind::import_assembly_reference_alias:
                std::printf("import-alias('%.*s')", alias_size, text(value.alias));
                break;
            case import_kind::alias_assembly_reference:
                std::printf("alias('%.*s' for 0x%08x (mdToken))", alias_size, text(value.alias), value.assembly);
                break;
            case import_kind::alias_namespace:
                std::printf("alias('%.*s' for '%.*s')",
                    alias_size, text(value.alias), namespace_size, text(value.target_namespace));
                break;
            case import_kind::alias_assembly_namespace:
                std::printf("alias('%.*s' for '%.*s' in 0x%08x (mdToken))",
                    alias_size, text(value.alias), namespace_size, text(value.target_namespace), value.assembly);
                break;
            case import_kind::alias_type:
                std::printf("alias('%.*s' for  0x%08x (mdToken))", alias_size, text(value.alias), value.target_type);
                break;
            }
        }
        std::printf(" } [%#x]|", raw);
        return 1;
    }
    return 0;
}

bool dump_sequence_points(cursor<mdtid_MethodDebugInformation> method,
    uint8_t const* blob, uint32_t length, uint32_t raw)
{
    auto heap = CursorTable(method)->cxt->blob_heap.ptr;
    size_t offset = blob == nullptr || heap == nullptr ? 0 : static_cast<size_t>(blob - heap);
    if (length == 0)
    {
        std::printf("Empty SequencePoints: Offset: %zu (len: %u) [%#x]|", offset, length, raw);
        return true;
    }
    auto parsed = parse_sequence_points(method, blob, length);
    if (!parsed)
    {
        if (parsed.error() == sequence_point_parse_error::out_of_memory)
        {
            std::printf("Ran out of memory when parsing PDB blob.\n");
            return false;
        }
        std::printf("Invalid PDB Blob (SequencePoints) Offset: %zu (len: %u) [%#x]|", offset, length, raw);
        return true;
    }
    mdToken document;
    if (!parsed->document.get_token(document))
        return false;
    std::printf("SequencePoints: LocalSignature 0x%08x (mdToken) Document 0x%08x (mdToken) { ",
        parsed->signature, document);
    bool first = true;
    for (auto const& record : parsed->records)
    {
        if (!first)
            std::printf(", ");
        first = false;
        bool success = true;
        std::visit([&](auto const& value) {
            using type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<type, document_change>)
            {
                success = value.document.get_token(document);
                if (success)
                    std::printf("document-record: 0x%08x (mdToken)", document);
            }
            else if constexpr (std::is_same_v<type, hidden_sequence_point>)
                std::printf("hidden-sequence-point-record: %u", value.rolling_il_offset);
            else
                std::printf("sequence-point-record: (%u, %u, %" PRId64 ", %" PRId64 ", %" PRId64 ")",
                    value.rolling_il_offset, value.delta_lines, value.delta_columns,
                    value.rolling_start_line, value.rolling_start_column);
        }, record);
        if (!success)
            return false;
    }
    std::printf(" } [%#x]|", raw);
    return true;
}
}
