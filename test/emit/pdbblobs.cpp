#include <dnmd_pdb.hpp>
#include "internal.hpp"
#include <corhdr.h>
#include <gtest/gtest.h>
#include <array>
#include <type_traits>

namespace
{
    using namespace dnmd;
    using namespace dnmd::columns;
    using error = pdb_parse_error;

    static_assert(std::is_same_v<sequence_point_parse_error, pdb_parse_error>);
    static_assert(noexcept(parse_document_name(nullptr, nullptr, 0)));
    static_assert(noexcept(parse_local_constant_signature(nullptr, nullptr, 0)));
    static_assert(noexcept(parse_imports(nullptr, nullptr, 0)));

    void Append(std::vector<uint8_t>& bytes, uint32_t value)
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

    class PdbBlobs : public testing::Test
    {
    protected:
        handle_ptr handle;

        void SetUp() override
        {
            handle.reset(md_create_new_pdb_handle());
            ASSERT_NE(nullptr, handle.get());
        }

        uint32_t Blob(std::string_view value)
        {
            auto row = added_row<mdtid_Document>::append(handle.get());
            EXPECT_TRUE(row);
            EXPECT_TRUE(row.value().set_column_value(Document_Hash,
                reinterpret_cast<uint8_t const*>(value.data()), static_cast<uint32_t>(value.size())));
            return detail::row_storage_access::read(row_reader{row.value()}, Document_Hash);
        }

        std::vector<uint8_t> Import(uint32_t kind, uint32_t alias, uint32_t name)
        {
            std::vector<uint8_t> bytes;
            Append(bytes, kind);
            switch (static_cast<import_kind>(kind))
            {
            case import_kind::import_namespace:
                Append(bytes, name);
                break;
            case import_kind::import_assembly_namespace:
                Append(bytes, 37);
                Append(bytes, name);
                break;
            case import_kind::import_type:
                Append(bytes, (321u << 2) | 2);
                break;
            case import_kind::import_xml_namespace:
            case import_kind::alias_namespace:
                Append(bytes, alias);
                Append(bytes, name);
                break;
            case import_kind::import_assembly_reference_alias:
                Append(bytes, alias);
                break;
            case import_kind::alias_assembly_reference:
                Append(bytes, alias);
                Append(bytes, 37);
                break;
            case import_kind::alias_assembly_namespace:
                Append(bytes, alias);
                Append(bytes, 37);
                Append(bytes, name);
                break;
            case import_kind::alias_type:
                Append(bytes, alias);
                Append(bytes, (321u << 2) | 2);
                break;
            }
            return bytes;
        }
    };
}

TEST_F(PdbBlobs, DocumentNamesOwnTheirTextAndHandleSeparatorsAndEmptyParts)
{
    auto first = Blob("src");
    auto second = Blob("test.cs");
    auto result = [&] {
        std::vector<uint8_t> bytes{'/'};
        Append(bytes, first);
        Append(bytes, second);
        return parse_document_name(handle.get(), bytes.data(), bytes.size());
    }();
    ASSERT_TRUE(result);
    EXPECT_EQ("src/test.cs", *result);

    std::vector<uint8_t> bytes{0};
    Append(bytes, first);
    Append(bytes, second);
    auto joined = parse_document_name(handle.get(), bytes.data(), bytes.size());
    ASSERT_TRUE(joined);
    EXPECT_EQ("srctest.cs", *joined);

    bytes.assign(1, '/');
    Append(bytes, 0);
    Append(bytes, second);
    Append(bytes, 0);
    auto empty = parse_document_name(handle.get(), bytes.data(), bytes.size());
    ASSERT_TRUE(empty);
    EXPECT_EQ("/test.cs/", *empty);
    auto header = parse_document_name(handle.get(), bytes.data(), 1);
    ASSERT_TRUE(header);
    EXPECT_TRUE(header->empty());
    handle.reset();
    EXPECT_EQ("src/test.cs", *result);
}

TEST_F(PdbBlobs, DocumentNamesDecodeWideOffsetsAndPreserveUtf8)
{
    (void)Blob(std::string(17000, 'x'));
    auto offset = Blob("caf\xc3\xa9.cs");
    ASSERT_GT(offset, 16383u);
    std::vector<uint8_t> bytes{'\\'};
    Append(bytes, offset);
    auto result = parse_document_name(handle.get(), bytes.data(), bytes.size());
    ASSERT_TRUE(result);
    EXPECT_EQ("caf\xc3\xa9.cs", *result);
    for (size_t length = 2; length < bytes.size(); ++length)
    {
        auto truncated = parse_document_name(handle.get(), bytes.data(), length);
        ASSERT_FALSE(truncated);
        EXPECT_EQ(error::invalid_blob, truncated.error());
    }
}

TEST_F(PdbBlobs, LocalConstantsDecodeAllPrimitiveWidthsAndEnums)
{
    struct primitive
    {
        uint8_t type;
        size_t size;
        bool is_enum;
    };
    std::array<primitive, 12> cases{{
        {ELEMENT_TYPE_BOOLEAN, 1, true}, {ELEMENT_TYPE_CHAR, 2, true},
        {ELEMENT_TYPE_I1, 1, true}, {ELEMENT_TYPE_U1, 1, true},
        {ELEMENT_TYPE_I2, 2, true}, {ELEMENT_TYPE_U2, 2, true},
        {ELEMENT_TYPE_I4, 4, true}, {ELEMENT_TYPE_U4, 4, true},
        {ELEMENT_TYPE_I8, 8, true}, {ELEMENT_TYPE_U8, 8, true},
        {ELEMENT_TYPE_R4, 4, false}, {ELEMENT_TYPE_R8, 8, false}
    }};
    for (auto test : cases)
    {
        SCOPED_TRACE(test.type);
        std::vector<uint8_t> bytes(1 + test.size, 42);
        bytes[0] = test.type;
        auto result = parse_local_constant_signature(handle.get(), bytes.data(), bytes.size());
        ASSERT_TRUE(result);
        ASSERT_TRUE(std::holds_alternative<primitive_constant>(result->type));
        EXPECT_EQ(test.type, std::get<primitive_constant>(result->type).type_code);
        EXPECT_EQ(bytes.data() + 1, result->value);
        EXPECT_EQ(test.size, result->value_size);
        EXPECT_TRUE(result->custom_modifiers.empty());
        for (size_t length = 1; length < bytes.size(); ++length)
        {
            auto truncated = parse_local_constant_signature(handle.get(), bytes.data(), length);
            ASSERT_FALSE(truncated);
            EXPECT_EQ(error::invalid_blob, truncated.error());
        }
        if (test.is_enum)
        {
            Append(bytes, (300u << 2) | 1);
            auto enumeration = parse_local_constant_signature(handle.get(), bytes.data(), bytes.size());
            ASSERT_TRUE(enumeration);
            ASSERT_TRUE(std::holds_alternative<enum_constant>(enumeration->type));
            EXPECT_EQ(test.type, std::get<enum_constant>(enumeration->type).type_code);
            EXPECT_EQ(CreateTokenType(mdtid_TypeRef) | 300, std::get<enum_constant>(enumeration->type).type);
            EXPECT_EQ(test.size, enumeration->value_size);
            bytes.push_back(0);
            EXPECT_FALSE(parse_local_constant_signature(handle.get(), bytes.data(), bytes.size()));
        }
        else
        {
            bytes.push_back(0);
            EXPECT_FALSE(parse_local_constant_signature(handle.get(), bytes.data(), bytes.size()));
        }
    }
}

TEST_F(PdbBlobs, LocalConstantsDecodeModifiersBeforeTheValue)
{
    std::vector<uint8_t> bytes{ELEMENT_TYPE_CMOD_REQD};
    Append(bytes, (0x1234u << 2) | 2);
    bytes.push_back(ELEMENT_TYPE_CMOD_OPT);
    Append(bytes, (0x00ffffffu << 2) | 1);
    bytes.insert(bytes.end(), {ELEMENT_TYPE_I4, 42, 0, 0, 0});
    auto result = parse_local_constant_signature(handle.get(), bytes.data(), bytes.size());
    ASSERT_TRUE(result);
    ASSERT_EQ(2u, result->custom_modifiers.size());
    EXPECT_TRUE(result->custom_modifiers[0].required);
    EXPECT_EQ(CreateTokenType(mdtid_TypeSpec) | 0x1234u, result->custom_modifiers[0].type);
    EXPECT_FALSE(result->custom_modifiers[1].required);
    EXPECT_EQ(CreateTokenType(mdtid_TypeRef) | 0x00ffffffu, result->custom_modifiers[1].type);
    EXPECT_EQ(ELEMENT_TYPE_I4, std::get<primitive_constant>(result->type).type_code);
    EXPECT_EQ(bytes.data() + bytes.size() - 4, result->value);
    EXPECT_EQ(4u, result->value_size);
    EXPECT_EQ(42, result->value[0]);
    for (size_t length = 0; length < bytes.size(); ++length)
        EXPECT_FALSE(parse_local_constant_signature(handle.get(), bytes.data(), length));
}

TEST_F(PdbBlobs, LocalConstantsHandleStringsAndGeneralConstants)
{
    uint8_t string[] = {ELEMENT_TYPE_STRING, 'a', 0, 'b', 0};
    auto result = parse_local_constant_signature(handle.get(), string, sizeof(string));
    ASSERT_TRUE(result);
    EXPECT_EQ(ELEMENT_TYPE_STRING, std::get<primitive_constant>(result->type).type_code);
    EXPECT_EQ(string + 1, result->value);
    EXPECT_EQ(4u, result->value_size);
    for (uint8_t type : std::array<uint8_t, 3>{ELEMENT_TYPE_OBJECT, ELEMENT_TYPE_CLASS, ELEMENT_TYPE_VALUETYPE})
    {
        std::vector<uint8_t> bytes{type};
        if (type != ELEMENT_TYPE_OBJECT)
            Append(bytes, (123u << 2) | 2);
        auto offset = bytes.size();
        bytes.insert(bytes.end(), {1, 2, 3, 4});
        auto general = parse_local_constant_signature(handle.get(), bytes.data(), bytes.size());
        ASSERT_TRUE(general);
        ASSERT_TRUE(std::holds_alternative<general_constant>(general->type));
        auto const& value = std::get<general_constant>(general->type);
        EXPECT_EQ(type == ELEMENT_TYPE_OBJECT ? 0u : CreateTokenType(mdtid_TypeSpec) | 123, value.type);
        EXPECT_EQ(type == ELEMENT_TYPE_OBJECT ? general_constant_kind::object
            : type == ELEMENT_TYPE_CLASS ? general_constant_kind::class_type : general_constant_kind::value_type, value.kind);
        EXPECT_EQ(bytes.data() + offset, general->value);
        EXPECT_EQ(4u, general->value_size);
    }
}

TEST_F(PdbBlobs, ImportsDecodeAllNineFormsAndBorrowHeapStrings)
{
    (void)Blob(std::string(17000, 'x'));
    auto alias = Blob("Alias");
    auto name = Blob("Example.Namespace");
    auto result = [&] {
        std::vector<uint8_t> bytes;
        for (uint32_t kind = 1; kind <= 9; ++kind)
        {
            auto record = Import(kind, alias, name);
            bytes.insert(bytes.end(), record.begin(), record.end());
        }
        return parse_imports(handle.get(), bytes.data(), bytes.size());
    }();
    ASSERT_TRUE(result);
    ASSERT_EQ(9u, result->size());
    for (uint32_t kind = 1; kind <= 9; ++kind)
    {
        SCOPED_TRACE(kind);
        auto const& record = (*result)[kind - 1];
        EXPECT_EQ(static_cast<import_kind>(kind), record.kind);
        EXPECT_EQ(kind >= 4 ? "Alias" : "", record.alias);
        EXPECT_EQ(kind == 1 || kind == 2 || kind == 4 || kind == 7 || kind == 8
            ? "Example.Namespace" : "", record.target_namespace);
        EXPECT_EQ(kind == 2 || kind == 6 || kind == 8 ? CreateTokenType(mdtid_AssemblyRef) | 37 : 0,
            record.assembly);
        EXPECT_EQ(kind == 3 || kind == 9 ? CreateTokenType(mdtid_TypeSpec) | 321 : 0, record.target_type);
    }
    uint8_t const* namespace_data;
    uint32_t namespace_size;
    ASSERT_TRUE(detail::try_get_blob(&detail::context(handle.get()), name, &namespace_data, &namespace_size));
    EXPECT_EQ(reinterpret_cast<char const*>(namespace_data), (*result)[0].target_namespace.data());
    EXPECT_EQ(namespace_size, (*result)[0].target_namespace.size());
    uint8_t empty = 0;
    auto none = parse_imports(handle.get(), &empty, 0);
    ASSERT_TRUE(none);
    EXPECT_TRUE(none->empty());
}

TEST_F(PdbBlobs, RejectsInvalidArgumentsAndEmptyRequiredInput)
{
    uint8_t empty = 0;
    auto document = parse_document_name(nullptr, &empty, 1);
    auto constant = parse_local_constant_signature(nullptr, &empty, 1);
    auto imports = parse_imports(nullptr, &empty, 1);
    ASSERT_FALSE(document);
    ASSERT_FALSE(constant);
    ASSERT_FALSE(imports);
    EXPECT_EQ(error::invalid_argument, document.error());
    EXPECT_EQ(error::invalid_argument, constant.error());
    EXPECT_EQ(error::invalid_argument, imports.error());
    EXPECT_FALSE(parse_document_name(handle.get(), nullptr, 0));
    EXPECT_FALSE(parse_local_constant_signature(handle.get(), nullptr, 0));
    EXPECT_FALSE(parse_imports(handle.get(), nullptr, 0));
    auto no_name = parse_document_name(handle.get(), &empty, 0);
    auto no_type = parse_local_constant_signature(handle.get(), &empty, 0);
    ASSERT_FALSE(no_name);
    ASSERT_FALSE(no_type);
    EXPECT_EQ(error::invalid_blob, no_name.error());
    EXPECT_EQ(error::invalid_blob, no_type.error());
}

TEST_F(PdbBlobs, RejectsMalformedNamesConstantsAndTokenEncodings)
{
    for (auto bytes : {std::vector<uint8_t>{0x80}, {'/', 0xff}, {'/', 0x7f}})
    {
        auto result = parse_document_name(handle.get(), bytes.data(), bytes.size());
        ASSERT_FALSE(result);
        EXPECT_EQ(error::invalid_blob, result.error());
    }
    for (auto bytes : {std::vector<uint8_t>{0xff}, {ELEMENT_TYPE_VOID},
        {ELEMENT_TYPE_CMOD_OPT}, {ELEMENT_TYPE_CMOD_OPT, 3}, {ELEMENT_TYPE_CLASS, 3},
        {ELEMENT_TYPE_I4, 0, 0, 0, 0, 3}, {ELEMENT_TYPE_CLASS, 0x80}})
    {
        auto result = parse_local_constant_signature(handle.get(), bytes.data(), bytes.size());
        ASSERT_FALSE(result);
        EXPECT_EQ(error::invalid_blob, result.error());
    }
    for (uint32_t tag = 0; tag < 3; ++tag)
    {
        std::vector<uint8_t> bytes{ELEMENT_TYPE_CLASS};
        Append(bytes, (0x01000000u << 2) | tag);
        auto overflow = parse_local_constant_signature(handle.get(), bytes.data(), bytes.size());
        ASSERT_FALSE(overflow);
        EXPECT_EQ(error::invalid_blob, overflow.error());
    }
}

TEST_F(PdbBlobs, RejectsTruncatedImportsAndInvalidKindsReferencesAndStrings)
{
    (void)Blob(std::string(17000, 'x'));
    auto alias = Blob("Alias");
    auto name = Blob("Namespace");
    for (uint32_t kind = 1; kind <= 9; ++kind)
    {
        auto bytes = Import(kind, alias, name);
        for (size_t length = 1; length < bytes.size(); ++length)
        {
            SCOPED_TRACE(kind);
            SCOPED_TRACE(length);
            auto result = parse_imports(handle.get(), bytes.data(), length);
            ASSERT_FALSE(result);
            EXPECT_EQ(error::invalid_blob, result.error());
        }
    }
    for (auto bytes : {std::vector<uint8_t>{0}, {10}, {0xff},
        {1, 0xff}, {1, 0xdf, 0xff, 0xff, 0xff}, {3, 3}, {9, 0, 3}})
    {
        auto result = parse_imports(handle.get(), bytes.data(), bytes.size());
        ASSERT_FALSE(result);
        EXPECT_EQ(error::invalid_blob, result.error());
    }
    std::vector<uint8_t> bytes{2};
    Append(bytes, 0x01000000);
    Append(bytes, name);
    auto overflow = parse_imports(handle.get(), bytes.data(), bytes.size());
    ASSERT_FALSE(overflow);
    EXPECT_EQ(error::invalid_blob, overflow.error());
}

TEST_F(PdbBlobs, DumpsNativeNamesModifiersAndEveryImportForm)
{
    auto alias = Blob("Alias");
    auto name = Blob("Example.Namespace");
    auto path = Blob("test.cs");
    auto document = added_row<mdtid_Document>::append(handle.get());
    ASSERT_TRUE(document);
    std::vector<uint8_t> name_blob{'/'};
    Append(name_blob, path);
    ASSERT_TRUE(document.value().set_column_value(Document_Name, name_blob.data(),
        static_cast<uint32_t>(name_blob.size())));
    document.commit();

    uint8_t constant[] = {ELEMENT_TYPE_CMOD_REQD, 4, ELEMENT_TYPE_I4, 42, 0, 0, 0};
    auto local = added_row<mdtid_LocalConstant>::append(handle.get());
    ASSERT_TRUE(local);
    ASSERT_TRUE(local.value().set_column_value(LocalConstant_Signature, constant, sizeof(constant)));
    local.commit();

    std::vector<uint8_t> imports;
    for (uint32_t kind = 1; kind <= 9; ++kind)
    {
        auto record = Import(kind, alias, name);
        imports.insert(imports.end(), record.begin(), record.end());
    }
    auto scope = added_row<mdtid_ImportScope>::append(handle.get());
    ASSERT_TRUE(scope);
    ASSERT_TRUE(scope.value().set_column_value(ImportScope_Imports, imports.data(),
        static_cast<uint32_t>(imports.size())));
    scope.commit();

    testing::internal::CaptureStdout();
    bool document_success = md_dump_tables(handle.get(), mdtid_Document);
    bool local_success = md_dump_tables(handle.get(), mdtid_LocalConstant);
    bool imports_success = md_dump_tables(handle.get(), mdtid_ImportScope);
    auto output = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(document_success);
    EXPECT_TRUE(local_success);
    EXPECT_TRUE(imports_success);
    for (auto expected : {"DocumentName: 'test.cs'",
        "modreq(0x02000001) Primitive: 0x08",
        "ns('Example.Namespace')",
        "ns('Example.Namespace' in 0x23000025 (mdToken))",
        "type(0x1b000141 (mdToken))",
        "xml-alias('Alias' for 'Example.Namespace')",
        "import-alias('Alias')",
        "alias('Alias' for 0x23000025 (mdToken))",
        "alias('Alias' for 'Example.Namespace')",
        "alias('Alias' for 'Example.Namespace' in 0x23000025 (mdToken))",
        "alias('Alias' for  0x1b000141 (mdToken))"})
    {
        SCOPED_TRACE(expected);
        EXPECT_NE(std::string::npos, output.find(expected));
    }
}
