#include "internal.hpp"
#include <dnmd_pdb.hpp>
#include <new>

namespace dnmd
{
sequence_points_result parse_sequence_points(cursor<mdtid_MethodDebugInformation> method,
    uint8_t const* blob, size_t length) noexcept
{
    using error = sequence_point_parse_error;
    if (blob == nullptr)
        return unexpected{error::invalid_argument};

    auto read_unsigned = [&](uint32_t& value) {
        return length != 0 && decompress_u32(&blob, &length, &value);
    };
    auto read_signed = [&](int32_t& value) {
        return length != 0 && decompress_i32(&blob, &length, &value);
    };
    mdToken signature;
    if (!read_unsigned(signature))
        return unexpected{error::invalid_blob};
    auto handle = method.get_handle();
    auto document = raw_cursor<mdtid_Document>::nil(handle);
    if (!row_reader{method}.read(columns::MethodDebugInformation_Document, document))
        return unexpected{error::invalid_blob};

    auto initial_document = raw_cursor<mdtid_Document>::nil(handle);
    if (document.is_nil())
    {
        uint32_t rid;
        if (!read_unsigned(rid) || rid > 0x00ffffff)
            return unexpected{error::invalid_blob};
        if (rid != 0)
        {
            initial_document = raw_cursor<mdtid_Document>{handle, CreateTokenType(mdtid_Document) | rid};
            if (!initial_document)
                return unexpected{error::invalid_blob};
        }
    }

    try
    {
        sequence_points result{signature, initial_document};
        bool seen_non_hidden = false;
        while (length != 0)
        {
            if (result.records.size() >= UINT32_MAX - 1
                || result.records.size() == result.records.max_size())
                return unexpected{error::invalid_blob};

            uint32_t il_offset;
            if (!read_unsigned(il_offset))
                return unexpected{error::invalid_blob};
            if (!result.records.empty() && il_offset == 0)
            {
                uint32_t rid;
                if (!read_unsigned(rid) || rid > 0x00ffffff)
                    return unexpected{error::invalid_blob};
                auto next_document = raw_cursor<mdtid_Document>{handle, CreateTokenType(mdtid_Document) | rid};
                if (!next_document)
                    return unexpected{error::invalid_blob};
                result.records.emplace_back(document_change{next_document});
                continue;
            }

            uint32_t delta_lines;
            if (!read_unsigned(delta_lines))
                return unexpected{error::invalid_blob};
            int64_t delta_columns;
            if (delta_lines == 0)
            {
                uint32_t value;
                if (!read_unsigned(value))
                    return unexpected{error::invalid_blob};
                delta_columns = value;
            }
            else
            {
                int32_t value;
                if (!read_signed(value))
                    return unexpected{error::invalid_blob};
                delta_columns = value;
            }
            if (delta_lines == 0 && delta_columns == 0)
            {
                result.records.emplace_back(hidden_sequence_point{il_offset});
                continue;
            }

            int64_t start_line;
            int64_t start_column;
            if (!seen_non_hidden)
            {
                uint32_t line;
                uint32_t column;
                if (!read_unsigned(line) || !read_unsigned(column))
                    return unexpected{error::invalid_blob};
                start_line = line;
                start_column = column;
                seen_non_hidden = true;
            }
            else
            {
                int32_t line;
                int32_t column;
                if (!read_signed(line) || !read_signed(column))
                    return unexpected{error::invalid_blob};
                start_line = line;
                start_column = column;
            }
            result.records.emplace_back(sequence_point{
                il_offset, delta_lines, delta_columns, start_line, start_column});
        }
        return std::move(result);
    }
    catch (std::bad_alloc const&)
    {
        return unexpected{error::out_of_memory};
    }
}
}
