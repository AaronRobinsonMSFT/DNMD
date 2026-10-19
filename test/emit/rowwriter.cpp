#include <dnmd.hpp>
#include <gtest/gtest.h>
#include <array>
#include <string>
#include <vector>

namespace
{
    template<typename Writer, typename Column, typename... Values>
    auto SupportsWrite(int) -> decltype(std::declval<Writer const&>().write(
        std::declval<Column>(), std::declval<Values>()...), std::true_type{});

    template<typename Writer, typename Column, typename... Values>
    auto SupportsWrite(...) -> std::false_type;

    template<typename Writer, typename Column, typename... Values>
    inline constexpr bool can_write_v = decltype(SupportsWrite<Writer, Column, Values...>(0))::value;

    using type_writer = dnmd::row_writer<mdtid_TypeDef>;
    static_assert(!std::is_default_constructible_v<type_writer>);
    static_assert(std::is_trivially_copyable_v<type_writer>);
    static_assert(sizeof(type_writer) == sizeof(mdcursor_t));
    static_assert(std::is_constructible_v<type_writer, dnmd::cursor<mdtid_TypeDef> const&>);
    static_assert(!std::is_constructible_v<type_writer, dnmd::raw_cursor<mdtid_TypeDef>>);
    static_assert(!std::is_constructible_v<type_writer, mdcursor_t>);
    static_assert(!std::is_constructible_v<type_writer, dnmd::cursor<mdtid_Module>>);
    static_assert(can_write_v<type_writer, decltype(dnmd::columns::TypeDef_Flags), uint32_t>);
    static_assert(can_write_v<type_writer, decltype(dnmd::columns::TypeDef_TypeName), char const*>);
    static_assert(can_write_v<type_writer, decltype(dnmd::columns::TypeDef_Extends), mdToken>);
    static_assert(can_write_v<type_writer, decltype(dnmd::columns::TypeDef_Extends), dnmd::cursor<mdtid_TypeRef>>);
    static_assert(can_write_v<type_writer, decltype(dnmd::columns::TypeDef_Extends),
        dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef>>);
    static_assert(!can_write_v<type_writer, decltype(dnmd::columns::TypeDef_Extends), dnmd::cursor<mdtid_Module>>);
    static_assert(!can_write_v<type_writer, decltype(dnmd::columns::TypeDef_Extends),
        dnmd::raw_cursor<mdtid_TypeDef, mdtid_Module>>);
    static_assert(!can_write_v<type_writer, decltype(dnmd::columns::Module_Name), char const*>);
    static_assert(!can_write_v<type_writer, decltype(dnmd::columns::TypeDef_TypeName), mdguid_t>);
    static_assert(!can_write_v<type_writer, decltype(dnmd::columns::TypeDef_Flags), char const*>);
    static_assert(!can_write_v<type_writer, decltype(dnmd::columns::TypeDef_Extends), mdcursor_t>);
    static_assert(can_write_v<type_writer, decltype(dnmd::columns::TypeDef_MethodList),
        dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>>);
    static_assert(!can_write_v<type_writer, decltype(dnmd::columns::TypeDef_MethodList),
        dnmd::indirect_cursor<mdtid_Field, mdtid_FieldPtr>>);
    static_assert(!can_write_v<type_writer, decltype(dnmd::columns::TypeDef_MethodList), dnmd::cursor<mdtid_Module>>);
    static_assert(can_write_v<dnmd::row_writer<mdtid_Module>, decltype(dnmd::columns::Module_Mvid), mdguid_t>);
    static_assert(can_write_v<dnmd::row_writer<mdtid_TypeSpec>, decltype(dnmd::columns::TypeSpec_Signature),
        uint8_t const*, uint32_t>);
    static_assert(!can_write_v<dnmd::row_writer<mdtid_TypeSpec>, decltype(dnmd::columns::TypeSpec_Signature), char const*>);
    static_assert(can_write_v<dnmd::row_writer<mdtid_Module>,
        dnmd::column<mdtid_Module, dnmd::column_type_userstring>, char16_t const*>);

    class CppRowWriter : public testing::Test
    {
    protected:
        dnmd::handle_ptr handle;

        void SetUp() override
        {
#ifdef DNMD_PORTABLE_PDB
            handle.reset(md_create_new_pdb_handle());
#else
            handle.reset(md_create_new_handle());
#endif
            ASSERT_NE(nullptr, handle.get());
        }

        template<mdtable_id_t TableId>
        dnmd::cursor<TableId> GetRow()
        {
            auto position = dnmd::md_create_cursor<TableId>(handle.get());
            if (!position)
            {
                mdcursor_t raw{};
                EXPECT_TRUE(md_append_row(handle.get(), TableId, &raw));
                md_commit_row_add(raw);
                position = dnmd::raw_cursor<TableId>{raw};
            }
            return position.as();
        }

        void Save(std::vector<uint8_t>& bytes)
        {
            size_t size = 0;
            EXPECT_FALSE(md_write_to_buffer(handle.get(), nullptr, &size));
            ASSERT_GT(size, 0u);
            bytes.resize(size);
            ASSERT_TRUE(md_write_to_buffer(handle.get(), bytes.data(), &size));
        }

        template<mdtable_id_t TableId, mdtable_id_t... Targets>
        void CheckReferences(dnmd::column<TableId, dnmd::raw_cursor<Targets...>> column)
        {
            auto row = GetRow<TableId>();
            dnmd::row_writer<TableId> writer{row};
            auto check = [&](mdtable_id_t table) {
                if (table == mdtid_Unused)
                    return;
                for (uint32_t index : {0u, 3u})
                {
                    mdToken token = CreateTokenType(table) | index;
                    ASSERT_TRUE(writer.write(column, token));
                    mdToken actual;
                    ASSERT_TRUE(md_get_column_value_as_token(row, column.column_index, &actual));
                    EXPECT_EQ(token, actual);
                    ASSERT_TRUE(row.get_column_value(column, actual));
                    EXPECT_EQ(token, actual);
                }
            };
            (check(Targets), ...);
        }
    };
}

using namespace dnmd::columns;

TEST_F(CppRowWriter, ReusesWriterAcrossColumnsAndStringGrowth)
{
    auto module = GetRow<mdtid_Module>();
    dnmd::row_writer<mdtid_Module> const writer{module};
    static_assert(noexcept(writer.write(Module_Name, "")));
    ASSERT_TRUE(writer.write(Module_Generation, 0x12345));
    ASSERT_TRUE(writer.write(Module_Name, "Writer.dll"));
    mdguid_t guid{0x12345678, 0x1234, 0x5678, {0, 1, 2, 3, 4, 5, 6, 7}};
    ASSERT_TRUE(writer.write(Module_Mvid, guid));
    std::string large(70000, 'x');
    ASSERT_TRUE(writer.write(Module_Name, large.c_str()));
    char const* name;
    ASSERT_TRUE(module.get_column_value(Module_Name, &name));
    EXPECT_EQ(large.size(), std::strlen(name));
    EXPECT_EQ(0, std::memcmp(large.data(), name, large.size()));
    ASSERT_TRUE(writer.write(Module_Name, "AfterGrowth.dll"));
    ASSERT_TRUE(module.get_column_value(Module_Name, &name));
    EXPECT_STREQ("AfterGrowth.dll", name);
    ASSERT_TRUE(writer.write(Module_Mvid, guid));
    mdguid_t actual;
    ASSERT_TRUE(module.get_column_value(Module_Mvid, &actual));
    EXPECT_EQ(0, std::memcmp(&guid, &actual, sizeof(guid)));
    uint32_t generation;
    ASSERT_TRUE(module.get_column_value(Module_Generation, &generation));
    EXPECT_EQ(0x2345u, generation);
    ASSERT_TRUE(writer.write(Module_Mvid, mdguid_t{}));
    ASSERT_TRUE(module.get_column_value(Module_Mvid, &actual));
    EXPECT_EQ(0, std::memcmp(&dnmd::detail::empty_guid, &actual, sizeof(actual)));
}

TEST_F(CppRowWriter, ReusesWriterAcrossBlobGrowth)
{
    auto spec = GetRow<mdtid_TypeSpec>();
    dnmd::row_writer<mdtid_TypeSpec> writer{spec};
    std::vector<uint8_t> large(70000, 0x5a);
    ASSERT_TRUE(writer.write(TypeSpec_Signature, large.data(), static_cast<uint32_t>(large.size())));
    uint8_t const* bytes;
    uint32_t length;
    ASSERT_TRUE(spec.get_column_value(TypeSpec_Signature, &bytes, &length));
    ASSERT_EQ(large.size(), length);
    EXPECT_EQ(0, std::memcmp(large.data(), bytes, length));
    uint8_t small[] = {0x12, 0x34};
    ASSERT_TRUE(writer.write(TypeSpec_Signature, small, sizeof(small)));
    ASSERT_TRUE(spec.get_column_value(TypeSpec_Signature, &bytes, &length));
    ASSERT_EQ(sizeof(small), length);
    EXPECT_EQ(0, std::memcmp(small, bytes, length));
    ASSERT_TRUE(writer.write(TypeSpec_Signature, nullptr, 0));
    ASSERT_TRUE(spec.get_column_value(TypeSpec_Signature, &bytes, &length));
    EXPECT_EQ(0u, length);
}

TEST_F(CppRowWriter, PreservesReadOnlyInput)
{
    auto original = GetRow<mdtid_TypeDef>();
    ASSERT_TRUE(original.set_column_value(TypeDef_TypeName, "Original"));
    ASSERT_TRUE(original.set_column_value(TypeDef_Flags, 0));
    std::vector<uint8_t> image;
    ASSERT_NO_FATAL_FAILURE(Save(image));
    auto before = image;
    mdhandle_t raw;
    ASSERT_TRUE(md_create_handle(image.data(), image.size(), &raw));
    dnmd::handle_ptr loaded{raw};
    auto row = dnmd::md_create_cursor<mdtid_TypeDef>(loaded.get()).as();
    dnmd::row_writer<mdtid_TypeDef> writer{row};
    ASSERT_TRUE(writer.write(TypeDef_Flags, 0x12345678));
    std::string large(70000, 'y');
    ASSERT_TRUE(writer.write(TypeDef_TypeName, large.c_str()));
    ASSERT_TRUE(writer.write(TypeDef_TypeNamespace, "Changed"));
    uint32_t flags;
    ASSERT_TRUE(row.get_column_value(TypeDef_Flags, &flags));
    EXPECT_EQ(0x12345678u, flags);
    char const* name;
    ASSERT_TRUE(row.get_column_value(TypeDef_TypeNamespace, &name));
    EXPECT_STREQ("Changed", name);
    EXPECT_EQ(before, image);
    ASSERT_TRUE(original.get_column_value(TypeDef_TypeName, &name));
    EXPECT_STREQ("Original", name);
}

TEST_F(CppRowWriter, EncodesDirectAndCodedReferences)
{
    ASSERT_NO_FATAL_FAILURE(CheckReferences(InterfaceImpl_Class));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(TypeDef_Extends));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(Constant_Parent));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(CustomAttribute_Parent));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(CustomAttribute_Type));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(FieldMarshal_Parent));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(DeclSecurity_Parent));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(MemberRef_Class));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(MethodSemantics_Association));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(MethodImpl_MethodBody));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(ImplMap_MemberForwarded));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(ExportedType_Implementation));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(TypeRef_ResolutionScope));
    ASSERT_NO_FATAL_FAILURE(CheckReferences(GenericParam_Owner));
}

TEST_F(CppRowWriter, AcceptsRowsAndNullablePositions)
{
    auto row = GetRow<mdtid_TypeDef>();
    dnmd::row_writer<mdtid_TypeDef> writer{row};
    ASSERT_TRUE(writer.write(TypeDef_Extends, row));
    dnmd::raw_cursor<mdtid_TypeDef> raw{row};
    ASSERT_TRUE(writer.write(TypeDef_Extends, raw));
    dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef> multiple{row};
    ASSERT_TRUE(writer.write(TypeDef_Extends, multiple));
    ASSERT_TRUE(writer.write(TypeDef_Extends, CreateTokenType(mdtid_TypeDef)));
    decltype(TypeDef_Extends)::value_type nil;
    ASSERT_TRUE(row.get_column_value(TypeDef_Extends, nil));
    ASSERT_TRUE(writer.write(TypeDef_Extends, nil));
    ASSERT_TRUE(dnmd::md_cursor_next(raw));
    EXPECT_FALSE(raw);
    ASSERT_TRUE(writer.write(TypeDef_Extends, raw));
    mdToken token;
    ASSERT_TRUE(row.get_column_value(TypeDef_Extends, token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 2, token);
    EXPECT_FALSE(writer.write(TypeDef_Extends, CreateTokenType(mdtid_Field) | 1));
    EXPECT_FALSE(writer.write(TypeDef_Extends, dnmd::raw_cursor<mdtid_TypeRef>{}));
    ASSERT_TRUE(row.get_column_value(TypeDef_Extends, token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 2, token);
}

TEST_F(CppRowWriter, DistinguishesIndirectSourcesAndTargets)
{
    auto owner = GetRow<mdtid_TypeDef>();
    mdcursor_t first;
    ASSERT_TRUE(md_add_new_row_to_list(owner, TypeDef_MethodList.column_index, &first));
    md_commit_row_add(first);
    mdcursor_t other_owner;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_TypeDef, &other_owner));
    md_commit_row_add(other_owner);
    mdcursor_t method;
    ASSERT_TRUE(md_add_new_row_to_list(other_owner, TypeDef_MethodList.column_index, &method));
    md_commit_row_add(method);
    ASSERT_TRUE(md_add_new_row_to_list(owner, TypeDef_MethodList.column_index, &method));
    md_commit_row_add(method);

    dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr> methods;
    ASSERT_TRUE(owner.get_column_value(TypeDef_MethodList, methods));
    ASSERT_TRUE(methods);
    dnmd::row_writer<mdtid_TypeDef> writer{owner};
    ASSERT_TRUE(writer.write(TypeDef_MethodList, methods));
    ASSERT_TRUE(writer.write(TypeDef_MethodList, dnmd::raw_cursor<mdtid_MethodPtr>{methods}.as()));
    EXPECT_FALSE(writer.write(TypeDef_MethodList, methods.as()));
    ASSERT_TRUE(writer.write(TypeDef_MethodList, dnmd::raw_cursor<mdtid_MethodDef, mdtid_MethodPtr>{methods}));
    mdToken source;
    mdToken target;
    ASSERT_TRUE(methods.get_token(source));
    ASSERT_TRUE(methods.get_target_token(target));
    mdToken actual;
    ASSERT_TRUE(owner.get_column_value(TypeDef_MethodList, actual));
    EXPECT_EQ(source, actual);
    auto impl = GetRow<mdtid_MethodImpl>();
    dnmd::row_writer<mdtid_MethodImpl> impl_writer{impl};
    ASSERT_TRUE(impl_writer.write(MethodImpl_Class, owner));
    ASSERT_TRUE(impl_writer.write(MethodImpl_MethodBody, methods));
    ASSERT_TRUE(impl.get_column_value(MethodImpl_MethodBody, actual));
    EXPECT_EQ(target, actual);
    EXPECT_FALSE(impl_writer.write(MethodImpl_MethodBody,
        dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>{}));
    auto end = methods;
    ASSERT_TRUE(dnmd::md_cursor_move(end, 3));
    EXPECT_FALSE(end);
    ASSERT_TRUE(writer.write(TypeDef_MethodList, end));
    ASSERT_TRUE(impl_writer.write(MethodImpl_MethodBody, end));
    ASSERT_TRUE(impl.get_column_value(MethodImpl_MethodBody, actual));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 4, actual);
    ASSERT_TRUE(writer.write(TypeDef_MethodList, CreateTokenType(mdtid_MethodPtr)));
    dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr> nil;
    ASSERT_TRUE(owner.get_column_value(TypeDef_MethodList, nil));
    EXPECT_FALSE(nil);
    ASSERT_TRUE(writer.write(TypeDef_MethodList, nil));
    ASSERT_TRUE(impl_writer.write(MethodImpl_MethodBody, nil));
    ASSERT_TRUE(impl.get_column_value(MethodImpl_MethodBody, actual));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), actual);
}

TEST_F(CppRowWriter, MaintainsSortedTableBookkeeping)
{
    std::array<mdcursor_t, 3> layouts{};
    std::array<mdcursor_t, 3> parameters{};
    for (uint32_t i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(md_append_row(handle.get(), mdtid_ClassLayout, &layouts[i]));
        auto layout = dnmd::raw_cursor<mdtid_ClassLayout>{layouts[i]}.as();
        auto table = dnmd::detail::CursorTable(layouts[i]);
        table->is_sorted = true;
        ASSERT_TRUE(layout.set_column_value(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | (i + 1)));
        EXPECT_TRUE(table->is_adding_new_row);
        EXPECT_TRUE(table->is_sorted);
        md_commit_row_add(layouts[i]);

        ASSERT_TRUE(md_append_row(handle.get(), mdtid_GenericParam, &parameters[i]));
        auto parameter = dnmd::raw_cursor<mdtid_GenericParam>{parameters[i]}.as();
        ASSERT_TRUE(parameter.set_column_value(GenericParam_Owner, CreateTokenType(mdtid_TypeDef) | 1));
        ASSERT_TRUE(parameter.set_column_value(GenericParam_Number, i));
        md_commit_row_add(parameters[i]);
    }

    auto table = dnmd::detail::CursorTable(layouts[0]);
    table->is_sorted = true;
    dnmd::row_writer<mdtid_ClassLayout> layout_writer{dnmd::raw_cursor<mdtid_ClassLayout>{layouts[0]}.as()};
    ASSERT_TRUE(layout_writer.write(ClassLayout_ClassSize, 32));
    EXPECT_TRUE(table->is_sorted);
    ASSERT_TRUE(layout_writer.write(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | 4));
    EXPECT_FALSE(table->is_sorted);
    ASSERT_TRUE(layout_writer.write(ClassLayout_Parent, CreateTokenType(mdtid_TypeDef) | 1));
    EXPECT_FALSE(table->is_sorted);

    table = dnmd::detail::CursorTable(parameters[0]);
    table->is_sorted = true;
    dnmd::row_writer<mdtid_GenericParam> parameter_writer{dnmd::raw_cursor<mdtid_GenericParam>{parameters[0]}.as()};
    ASSERT_TRUE(parameter_writer.write(GenericParam_Flags, 1));
    EXPECT_TRUE(table->is_sorted);
    ASSERT_TRUE(parameter_writer.write(GenericParam_Number, 4));
    EXPECT_FALSE(table->is_sorted);
}

#ifdef DNMD_PORTABLE_PDB
TEST(CppRowWriterPdb, TracksExternalReferencesAcrossSchemaGrowth)
{
    std::array<uint8_t, 116> image{};
    uint8_t const signature[] = {'B', 'S', 'J', 'B'};
    std::memcpy(image.data(), signature, sizeof(signature));
    image[4] = 1;
    image[6] = 1;
    image[12] = 12;
    std::memcpy(image.data() + 16, "PDB v1.0", 8);
    image[30] = 2;
    image[32] = 60;
    image[36] = 32;
    std::memcpy(image.data() + 40, "#Pdb", 4);
    image[48] = 92;
    image[52] = 24;
    std::memcpy(image.data() + 56, "#~", 2);
    image[96] = 2;
    image[99] = 1;
    mdhandle_t raw;
    ASSERT_TRUE(md_create_handle(image.data(), image.size(), &raw));
    dnmd::handle_ptr handle{raw};
    mdcursor_t added;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_LocalScope, &added));
    auto scope = dnmd::raw_cursor<mdtid_LocalScope>{added}.as();
    dnmd::row_writer<mdtid_LocalScope> writer{scope};
    mdcursor_t debug_row;
    ASSERT_TRUE(md_append_row(handle.get(), mdtid_CustomDebugInformation, &debug_row));
    md_commit_row_add(debug_row);
    auto debug = dnmd::raw_cursor<mdtid_CustomDebugInformation>{debug_row}.as();
    dnmd::row_writer<mdtid_CustomDebugInformation> debug_writer{debug};
    auto table = dnmd::detail::CursorTable(added);
    ASSERT_TRUE(table->column_details[static_cast<uint8_t>(mdtLocalScope_Method)] & mdtc_b2);
    ASSERT_TRUE(writer.write(LocalScope_Method, CreateTokenType(mdtid_MethodDef) | 0x10000));
    ASSERT_TRUE(writer.write(LocalScope_StartOffset, 0x12345678));
    md_commit_row_add(added);
    mdToken method;
    ASSERT_TRUE(scope.get_column_value(LocalScope_Method, method));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 0x10000, method);
    uint32_t offset;
    ASSERT_TRUE(scope.get_column_value(LocalScope_StartOffset, &offset));
    EXPECT_EQ(0x12345678u, offset);
    EXPECT_TRUE(table->column_details[static_cast<uint8_t>(mdtLocalScope_Method)] & mdtc_b4);
    ASSERT_TRUE(writer.write(LocalScope_Method, CreateTokenType(mdtid_MethodDef) | 1));
    EXPECT_TRUE(table->column_details[static_cast<uint8_t>(mdtLocalScope_Method)] & mdtc_b4);
    ASSERT_TRUE(scope.get_column_value(LocalScope_Method, method));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 1, method);
    ASSERT_TRUE(scope.get_column_value(LocalScope_StartOffset, &offset));
    EXPECT_EQ(0x12345678u, offset);
    for (auto reference : {mdtid_MethodDef, mdtid_Field, mdtid_TypeRef, mdtid_TypeDef, mdtid_Param,
        mdtid_InterfaceImpl, mdtid_MemberRef, mdtid_Module, mdtid_DeclSecurity, mdtid_Property,
        mdtid_Event, mdtid_StandAloneSig, mdtid_ModuleRef, mdtid_TypeSpec})
    {
        mdToken token = CreateTokenType(reference) | 3;
        ASSERT_TRUE(debug_writer.write(CustomDebugInformation_Parent, token));
        mdToken actual;
        ASSERT_TRUE(debug.get_column_value(CustomDebugInformation_Parent, actual));
        EXPECT_EQ(token, actual);
    }
    ASSERT_TRUE(writer.write(LocalScope_Method, CreateTokenType(mdtid_MethodDef) | 0x20000));
    ASSERT_TRUE(scope.get_column_value(LocalScope_Method, method));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | 0x20000, method);
}
#endif
