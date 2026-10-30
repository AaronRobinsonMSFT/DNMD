#include <dnmd_pdb.hpp>
#include <dnmd.hpp>
#include <gtest/gtest.h>
#include <type_traits>

namespace
{
    using namespace dnmd;
    using namespace dnmd::columns;
    using error = sequence_point_parse_error;

    static_assert(!std::is_default_constructible_v<sequence_points>);
    static_assert(!std::is_default_constructible_v<document_change>);
    static_assert(!std::is_copy_constructible_v<sequence_points_result>);
    static_assert(std::is_nothrow_move_constructible_v<sequence_points_result>);
    static_assert(noexcept(parse_sequence_points(
        std::declval<cursor<mdtid_MethodDebugInformation>>(), nullptr, 0)));

    void AppendUnsigned(std::vector<uint8_t>& bytes, uint32_t value)
    {
        if (value <= 0x7f)
            bytes.push_back(static_cast<uint8_t>(value));
        else if (value <= 0x3fff)
        {
            bytes.push_back(static_cast<uint8_t>(0x80 | (value >> 8)));
            bytes.push_back(static_cast<uint8_t>(value));
        }
        else
        {
            bytes.push_back(static_cast<uint8_t>(0xc0 | (value >> 24)));
            bytes.push_back(static_cast<uint8_t>(value >> 16));
            bytes.push_back(static_cast<uint8_t>(value >> 8));
            bytes.push_back(static_cast<uint8_t>(value));
        }
    }

    void AppendSigned(std::vector<uint8_t>& bytes, int32_t value)
    {
        uint32_t mask = value >= -64 && value <= 63 ? 0x3fu
            : value >= -8192 && value <= 8191 ? 0x1fffu : 0x0fffffffu;
        uint32_t encoded = ((static_cast<uint32_t>(value) & mask) << 1) | (value < 0 ? 1u : 0u);
        if (mask == 0x3f)
            bytes.push_back(static_cast<uint8_t>(encoded));
        else if (mask == 0x1fff)
        {
            bytes.push_back(static_cast<uint8_t>(0x80 | (encoded >> 8)));
            bytes.push_back(static_cast<uint8_t>(encoded));
        }
        else
        {
            bytes.push_back(static_cast<uint8_t>(0xc0 | (encoded >> 24)));
            bytes.push_back(static_cast<uint8_t>(encoded >> 16));
            bytes.push_back(static_cast<uint8_t>(encoded >> 8));
            bytes.push_back(static_cast<uint8_t>(encoded));
        }
    }

    class PdbSequencePoints : public testing::Test
    {
    protected:
        handle_ptr handle;

        void SetUp() override
        {
            handle.reset(md_create_new_pdb_handle());
            ASSERT_NE(nullptr, handle.get());
        }

        raw_cursor<mdtid_Document> Document()
        {
            auto added = added_row<mdtid_Document>::append(handle.get());
            EXPECT_TRUE(added);
            return added.position();
        }

        raw_cursor<mdtid_MethodDebugInformation> Method(raw_cursor<mdtid_Document> document)
        {
            auto added = added_row<mdtid_MethodDebugInformation>::append(handle.get());
            EXPECT_TRUE(added);
            EXPECT_TRUE(added.value().set_column_value(MethodDebugInformation_Document, document));
            return added.position();
        }
    };
}

TEST_F(PdbSequencePoints, OwnsTypedRecordsAndPreservesDocumentAndSignedFields)
{
    ASSERT_TRUE(Document());
    ASSERT_TRUE(Document());
    auto method = Method(raw_cursor<mdtid_Document>::nil(handle.get()));
    ASSERT_TRUE(method);
    auto parsed = [&] {
        std::vector<uint8_t> bytes{11, 1, 0, 0, 0, 0, 2, 5, 1};
        AppendSigned(bytes, -1);
        AppendUnsigned(bytes, 100);
        AppendUnsigned(bytes, 9);
        bytes.insert(bytes.end(), {3, 0, 2});
        AppendSigned(bytes, -3);
        AppendSigned(bytes, 5);
        bytes.insert(bytes.end(), {7, 0, 0});
        return parse_sequence_points(method.value(), bytes.data(), bytes.size());
    }();
    ASSERT_TRUE(parsed);
    EXPECT_EQ(11u, parsed->signature);
    mdToken document;
    ASSERT_TRUE(parsed->document.get_token(document));
    EXPECT_EQ(CreateTokenType(mdtid_Document) | 1, document);
    ASSERT_EQ(5u, parsed->records.size());
    ASSERT_TRUE(std::holds_alternative<hidden_sequence_point>(parsed->records[0]));
    EXPECT_EQ(0u, std::get<hidden_sequence_point>(parsed->records[0]).rolling_il_offset);
    ASSERT_TRUE(std::holds_alternative<document_change>(parsed->records[1]));
    ASSERT_TRUE(std::get<document_change>(parsed->records[1]).document.get_token(document));
    EXPECT_EQ(CreateTokenType(mdtid_Document) | 2, document);
    auto const& first = std::get<sequence_point>(parsed->records[2]);
    EXPECT_EQ(5u, first.rolling_il_offset);
    EXPECT_EQ(1u, first.delta_lines);
    EXPECT_EQ(-1, first.delta_columns);
    EXPECT_EQ(100, first.rolling_start_line);
    EXPECT_EQ(9, first.rolling_start_column);
    auto const& second = std::get<sequence_point>(parsed->records[3]);
    EXPECT_EQ(8u, second.rolling_il_offset);
    EXPECT_EQ(0u, second.delta_lines);
    EXPECT_EQ(2, second.delta_columns);
    EXPECT_EQ(97, second.rolling_start_line);
    EXPECT_EQ(14, second.rolling_start_column);
    EXPECT_EQ(15u, std::get<hidden_sequence_point>(parsed->records[4]).rolling_il_offset);
}

TEST_F(PdbSequencePoints, SupportsColumnDocumentsAndHeaderOnlyBlobs)
{
    auto document = Document();
    ASSERT_TRUE(document);
    auto method = Method(document);
    ASSERT_TRUE(method);
    uint8_t bytes[] = {0};
    auto result = parse_sequence_points(method.value(), bytes, sizeof(bytes));
    ASSERT_TRUE(result);
    mdToken expected_document;
    mdToken actual_document;
    ASSERT_TRUE(document.get_token(expected_document));
    ASSERT_TRUE(result->document.get_token(actual_document));
    EXPECT_EQ(expected_document, actual_document);
    EXPECT_TRUE(result->records.empty());
    method = Method(raw_cursor<mdtid_Document>::nil(handle.get()));
    uint8_t initial[] = {0, 1};
    auto explicit_document = parse_sequence_points(method.value(), initial, sizeof(initial));
    ASSERT_TRUE(explicit_document);
    EXPECT_TRUE(explicit_document->document);
    EXPECT_TRUE(explicit_document->records.empty());
}

TEST_F(PdbSequencePoints, DecodesCompressedIntegerBoundaries)
{
    auto document = Document();
    ASSERT_TRUE(document);
    auto method = Method(document);
    ASSERT_TRUE(method);
    for (uint32_t value : {0u, 127u, 128u, 16383u, 16384u, 0x1fffffffu})
    {
        for (int32_t signed_value : {-0x10000000, -8193, -8192, -65, -64, -1, 0, 63, 64, 8191, 8192, 0x0fffffff})
        {
            SCOPED_TRACE(value);
            SCOPED_TRACE(signed_value);
            std::vector<uint8_t> bytes;
            AppendUnsigned(bytes, value);
            AppendUnsigned(bytes, value);
            AppendUnsigned(bytes, 1);
            AppendSigned(bytes, signed_value);
            AppendUnsigned(bytes, value);
            AppendUnsigned(bytes, value);
            bytes.insert(bytes.end(), {1, 1});
            AppendSigned(bytes, signed_value);
            AppendSigned(bytes, signed_value);
            AppendSigned(bytes, signed_value);
            auto result = parse_sequence_points(method.value(), bytes.data(), bytes.size());
            ASSERT_TRUE(result);
            EXPECT_EQ(value, result->signature);
            ASSERT_EQ(2u, result->records.size());
            auto const& first = std::get<sequence_point>(result->records[0]);
            EXPECT_EQ(value, first.rolling_il_offset);
            EXPECT_EQ(signed_value, first.delta_columns);
            EXPECT_EQ(value, first.rolling_start_line);
            auto const& second = std::get<sequence_point>(result->records[1]);
            EXPECT_EQ(int64_t{value} + signed_value, second.rolling_start_line);
            EXPECT_EQ(int64_t{value} + signed_value, second.rolling_start_column);
        }
    }
}

TEST_F(PdbSequencePoints, RejectsRollingIlOffsetOverflow)
{
    auto document = Document();
    ASSERT_TRUE(document);
    auto method = Method(document);
    ASSERT_TRUE(method);
    std::vector<uint8_t> bytes{0};
    for (unsigned index = 0; index != 8; ++index)
    {
        AppendUnsigned(bytes, 0x1fffffff);
        bytes.insert(bytes.end(), {0, 0});
    }
    auto valid = parse_sequence_points(method.value(), bytes.data(), bytes.size());
    ASSERT_TRUE(valid);
    EXPECT_EQ(UINT32_MAX - 7, std::get<hidden_sequence_point>(valid->records.back()).rolling_il_offset);
    AppendUnsigned(bytes, 0x1fffffff);
    bytes.insert(bytes.end(), {0, 0});
    auto overflow = parse_sequence_points(method.value(), bytes.data(), bytes.size());
    ASSERT_FALSE(overflow);
    EXPECT_EQ(error::invalid_blob, overflow.error());
}

TEST_F(PdbSequencePoints, RejectsTruncatedInvalidAndOutOfRangeInput)
{
    auto document = Document();
    ASSERT_TRUE(document);
    auto method = Method(document);
    ASSERT_TRUE(method);
    auto null = parse_sequence_points(method.value(), nullptr, 0);
    ASSERT_FALSE(null);
    EXPECT_EQ(error::invalid_argument, null.error());
    std::vector<uint8_t> bytes;
    AppendUnsigned(bytes, 16384);
    auto header_size = bytes.size();
    AppendUnsigned(bytes, 16384);
    AppendUnsigned(bytes, 1);
    AppendSigned(bytes, -8193);
    AppendUnsigned(bytes, 16384);
    AppendUnsigned(bytes, 9);
    for (size_t length = 0; length < bytes.size(); ++length)
    {
        if (length == header_size)
            continue;
        SCOPED_TRACE(length);
        auto result = parse_sequence_points(method.value(), bytes.data(), length);
        ASSERT_FALSE(result);
        EXPECT_EQ(error::invalid_blob, result.error());
    }
    uint8_t prefix[] = {0xff};
    auto invalid = parse_sequence_points(method.value(), prefix, sizeof(prefix));
    ASSERT_FALSE(invalid);
    EXPECT_EQ(error::invalid_blob, invalid.error());

    method = Method(raw_cursor<mdtid_Document>::nil(handle.get()));
    for (uint32_t rid : {2u, 0x10000001u})
    {
        std::vector<uint8_t> bad{0};
        AppendUnsigned(bad, rid);
        auto result = parse_sequence_points(method.value(), bad.data(), bad.size());
        ASSERT_FALSE(result);
        EXPECT_EQ(error::invalid_blob, result.error());
    }
}
