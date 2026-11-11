#include "internal.hpp"
#include <dnmd_pdb.hpp>
#include <corhdr.h>
#include <new>

namespace
{
    using namespace dnmd;

    class blob_reader
    {
        uint8_t const* _data;
        size_t _size;

    public:
        blob_reader(uint8_t const* data, size_t size) noexcept : _data{data}, _size{size} {}

        uint8_t const* data() const noexcept { return _data; }
        size_t size() const noexcept { return _size; }

        bool read(uint32_t& value) noexcept
        {
            return _size != 0 && decompress_u32(&_data, &_size, &value);
        }

        bool skip(size_t count) noexcept
        {
            if (count > _size)
                return false;
            _data += count;
            _size -= count;
            return true;
        }

        bool read_type(mdToken& value) noexcept
        {
            uint32_t encoded;
            mdtable_id_t table;
            uint32_t row;
            if (!read(encoded)
                || !detail::column_reference_codec<mdtid_TypeDef, mdtid_TypeRef, mdtid_TypeSpec>::decode(encoded, table, row)
                || row > 0x00ffffff)
                return false;
            value = CreateTokenType(table) | row;
            return true;
        }

        bool read_assembly(mdToken& value) noexcept
        {
            uint32_t row;
            if (!read(row) || row > 0x00ffffff)
                return false;
            value = CreateTokenType(mdtid_AssemblyRef) | row;
            return true;
        }

        bool read_string(mdcxt_t* context, std::string_view& value) noexcept
        {
            uint32_t offset;
            uint8_t const* data;
            uint32_t size;
            if (!read(offset) || !detail::try_get_blob(context, offset, &data, &size))
                return false;
            value = size == 0 ? std::string_view{} : std::string_view{reinterpret_cast<char const*>(data), size};
            return true;
        }
    };
}

namespace dnmd
{
document_name_result parse_document_name(mdhandle_t handle, uint8_t const* blob, size_t length) noexcept
{
    auto context = detail::extract_mdcxt(handle);
    if (context == nullptr || blob == nullptr)
        return unexpected{pdb_parse_error::invalid_argument};
    if (length == 0 || blob[0] > 0x7f)
        return unexpected{pdb_parse_error::invalid_blob};

    char separator = static_cast<char>(blob[0]);
    blob_reader reader{blob + 1, length - 1};
    try
    {
        std::string result;
        bool first = true;
        while (reader.size() != 0)
        {
            std::string_view part;
            if (!reader.read_string(context, part))
                return unexpected{pdb_parse_error::invalid_blob};
            if (!first && separator != 0)
            {
                if (result.size() == result.max_size())
                    return unexpected{pdb_parse_error::invalid_blob};
                result.push_back(separator);
            }
            first = false;
            if (part.size() > result.max_size() - result.size())
                return unexpected{pdb_parse_error::invalid_blob};
            if (!part.empty())
                result.append(part.data(), part.size());
        }
        return std::move(result);
    }
    catch (std::bad_alloc const&)
    {
        return unexpected{pdb_parse_error::out_of_memory};
    }
}

local_constant_result parse_local_constant_signature(mdhandle_t handle,
    uint8_t const* blob, size_t length) noexcept
{
    if (detail::extract_mdcxt(handle) == nullptr || blob == nullptr)
        return unexpected{pdb_parse_error::invalid_argument};

    blob_reader reader{blob, length};
    try
    {
        local_constant_signature result{};
        uint32_t type;
        if (!reader.read(type))
            return unexpected{pdb_parse_error::invalid_blob};
        while (type == ELEMENT_TYPE_CMOD_OPT || type == ELEMENT_TYPE_CMOD_REQD)
        {
            mdToken token;
            if (!reader.read_type(token)
                || result.custom_modifiers.size() >= UINT32_MAX - 1
                || result.custom_modifiers.size() == result.custom_modifiers.max_size())
                return unexpected{pdb_parse_error::invalid_blob};
            result.custom_modifiers.push_back({type == ELEMENT_TYPE_CMOD_REQD, token});
            if (!reader.read(type))
                return unexpected{pdb_parse_error::invalid_blob};
        }

        size_t width;
        switch (type)
        {
        case ELEMENT_TYPE_OBJECT:
            result.type = general_constant{general_constant_kind::object, 0};
            break;
        case ELEMENT_TYPE_CLASS:
        case ELEMENT_TYPE_VALUETYPE:
        {
            mdToken token;
            if (!reader.read_type(token))
                return unexpected{pdb_parse_error::invalid_blob};
            result.type = general_constant{type == ELEMENT_TYPE_CLASS
                ? general_constant_kind::class_type : general_constant_kind::value_type, token};
            break;
        }
        case ELEMENT_TYPE_R4:
        case ELEMENT_TYPE_R8:
            if (reader.size() != (type == ELEMENT_TYPE_R4 ? 4u : 8u))
                return unexpected{pdb_parse_error::invalid_blob};
            result.type = primitive_constant{static_cast<uint8_t>(type)};
            break;
        case ELEMENT_TYPE_STRING:
            result.type = primitive_constant{static_cast<uint8_t>(type)};
            break;
        case ELEMENT_TYPE_BOOLEAN:
        case ELEMENT_TYPE_CHAR:
        case ELEMENT_TYPE_I1:
        case ELEMENT_TYPE_U1:
        case ELEMENT_TYPE_I2:
        case ELEMENT_TYPE_U2:
        case ELEMENT_TYPE_I4:
        case ELEMENT_TYPE_U4:
        case ELEMENT_TYPE_I8:
        case ELEMENT_TYPE_U8:
        {
            switch (type)
            {
            case ELEMENT_TYPE_BOOLEAN:
            case ELEMENT_TYPE_I1:
            case ELEMENT_TYPE_U1: width = 1; break;
            case ELEMENT_TYPE_CHAR:
            case ELEMENT_TYPE_I2:
            case ELEMENT_TYPE_U2: width = 2; break;
            case ELEMENT_TYPE_I4:
            case ELEMENT_TYPE_U4: width = 4; break;
            default: width = 8; break;
            }
            result.value = reader.data();
            result.value_size = width;
            if (!reader.skip(width))
                return unexpected{pdb_parse_error::invalid_blob};
            if (reader.size() == 0)
                result.type = primitive_constant{static_cast<uint8_t>(type)};
            else
            {
                mdToken token;
                if (!reader.read_type(token) || reader.size() != 0)
                    return unexpected{pdb_parse_error::invalid_blob};
                result.type = enum_constant{static_cast<uint8_t>(type), token};
            }
            return std::move(result);
        }
        default:
            return unexpected{pdb_parse_error::invalid_blob};
        }
        result.value = reader.data();
        result.value_size = reader.size();
        return std::move(result);
    }
    catch (std::bad_alloc const&)
    {
        return unexpected{pdb_parse_error::out_of_memory};
    }
}

imports_result parse_imports(mdhandle_t handle, uint8_t const* blob, size_t length) noexcept
{
    auto context = detail::extract_mdcxt(handle);
    if (context == nullptr || blob == nullptr)
        return unexpected{pdb_parse_error::invalid_argument};

    blob_reader reader{blob, length};
    try
    {
        std::vector<import_record> result;
        while (reader.size() != 0)
        {
            uint32_t kind;
            if (!reader.read(kind) || kind < static_cast<uint32_t>(import_kind::import_namespace)
                || kind > static_cast<uint32_t>(import_kind::alias_type)
                || result.size() >= UINT32_MAX - 1 || result.size() == result.max_size())
                return unexpected{pdb_parse_error::invalid_blob};
            import_record record{static_cast<import_kind>(kind), {}, 0, {}, 0};
            switch (record.kind)
            {
            case import_kind::import_namespace:
                if (!reader.read_string(context, record.target_namespace))
                    return unexpected{pdb_parse_error::invalid_blob};
                break;
            case import_kind::import_assembly_namespace:
                if (!reader.read_assembly(record.assembly)
                    || !reader.read_string(context, record.target_namespace))
                    return unexpected{pdb_parse_error::invalid_blob};
                break;
            case import_kind::import_type:
                if (!reader.read_type(record.target_type))
                    return unexpected{pdb_parse_error::invalid_blob};
                break;
            case import_kind::import_xml_namespace:
            case import_kind::alias_namespace:
                if (!reader.read_string(context, record.alias)
                    || !reader.read_string(context, record.target_namespace))
                    return unexpected{pdb_parse_error::invalid_blob};
                break;
            case import_kind::import_assembly_reference_alias:
                if (!reader.read_string(context, record.alias))
                    return unexpected{pdb_parse_error::invalid_blob};
                break;
            case import_kind::alias_assembly_reference:
                if (!reader.read_string(context, record.alias)
                    || !reader.read_assembly(record.assembly))
                    return unexpected{pdb_parse_error::invalid_blob};
                break;
            case import_kind::alias_assembly_namespace:
                if (!reader.read_string(context, record.alias)
                    || !reader.read_assembly(record.assembly)
                    || !reader.read_string(context, record.target_namespace))
                    return unexpected{pdb_parse_error::invalid_blob};
                break;
            case import_kind::alias_type:
                if (!reader.read_string(context, record.alias)
                    || !reader.read_type(record.target_type))
                    return unexpected{pdb_parse_error::invalid_blob};
                break;
            }
            result.push_back(record);
        }
        return std::move(result);
    }
    catch (std::bad_alloc const&)
    {
        return unexpected{pdb_parse_error::out_of_memory};
    }
}
}
