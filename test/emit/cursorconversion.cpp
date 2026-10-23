#include <dnmd.hpp>
#include <gtest/gtest.h>
#include <functional>
#include <vector>

namespace
{
    using method = dnmd::raw_cursor<mdtid_MethodDef>;
    using methods_and_fields = dnmd::raw_cursor<mdtid_Field, mdtid_MethodDef>;
    using method_or_ref = dnmd::raw_cursor<mdtid_MethodDef, mdtid_MemberRef>;
    using constructor_reference = dnmd::raw_cursor<mdtid_Unused, mdtid_Unused,
        mdtid_MethodDef, mdtid_MemberRef, mdtid_Unused>;
    using indirect_method = dnmd::indirect_cursor<mdtid_MethodDef, mdtid_MethodPtr>;

    static_assert(std::is_convertible_v<method, methods_and_fields>);
    static_assert(std::is_convertible_v<dnmd::cursor<mdtid_MethodDef>, methods_and_fields>);
    static_assert(std::is_convertible_v<constructor_reference, method_or_ref>);
    static_assert(std::is_convertible_v<constructor_reference, dnmd::any_cursor>);
    static_assert(std::is_convertible_v<indirect_method, dnmd::any_cursor>);
    static_assert(std::is_nothrow_constructible_v<dnmd::any_cursor, method>);
    static_assert(!std::is_convertible_v<methods_and_fields, method>);
    static_assert(!std::is_convertible_v<method, dnmd::raw_cursor<mdtid_Field>>);
    static_assert(!std::is_convertible_v<indirect_method, method>);
    static_assert(std::is_constructible_v<method, methods_and_fields>);

    class CppCursorConversion : public testing::Test
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

        dnmd::cursor<mdtid_TypeDef> AddOwner()
        {
            auto added = dnmd::added_row<mdtid_TypeDef>::append(handle.get());
            EXPECT_TRUE(added);
            return added.as();
        }

        dnmd::cursor<mdtid_MethodDef> AddMethod(dnmd::cursor<mdtid_TypeDef> owner)
        {
            auto added = dnmd::md_add_new_row_to_list(owner, dnmd::columns::TypeDef_MethodList);
            EXPECT_TRUE(added);
            return added.as();
        }
    };
}

TEST_F(CppCursorConversion, WideningPreservesNilLiveEndAndOwner)
{
    auto row = AddMethod(AddOwner());
    method positions[] = {method::nil(handle.get()), method{row}, method::end(handle.get())};
    std::vector<mdToken> callback_tokens;
    std::function<void(dnmd::any_cursor)> callback = [&](dnmd::any_cursor value) {
        mdToken token;
        ASSERT_TRUE(value.get_token(token));
        callback_tokens.push_back(token);
        EXPECT_EQ(handle.get(), value.get_handle());
    };
    for (auto const& original : positions)
    {
        methods_and_fields widened = original;
        dnmd::any_cursor erased = widened;
        mdToken expected;
        mdToken actual;
        ASSERT_TRUE(original.get_token(expected));
        ASSERT_TRUE(widened.get_token(actual));
        EXPECT_EQ(expected, actual);
        ASSERT_TRUE(erased.get_token(actual));
        EXPECT_EQ(expected, actual);
        EXPECT_EQ(static_cast<bool>(original), static_cast<bool>(widened));
        EXPECT_EQ(handle.get(), widened.get_handle());
        callback(original);
        EXPECT_EQ(expected, callback_tokens.back());
    }
    callback(row);
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef) | dnmd::detail::CursorRow(row), callback_tokens.back());
    method explicitly_narrowed{methods_and_fields{row}};
    EXPECT_TRUE(explicitly_narrowed);
}

TEST_F(CppCursorConversion, ReservedSlotsDoNotPreventWidening)
{
    constructor_reference nil = constructor_reference::nil(handle.get());
    method_or_ref widened = nil;
    mdToken token;
    ASSERT_TRUE(widened.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_MethodDef), token);
    EXPECT_EQ(handle.get(), widened.get_handle());
    EXPECT_FALSE(widened);
}

TEST_F(CppCursorConversion, TypedCreationAssignsSupersetAndPreservesEmptyTableIdentity)
{
    auto owner = AddOwner();
    uint32_t count = 0x12345678;
    dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef, mdtid_TypeRef> position =
        dnmd::md_create_cursor<mdtid_TypeDef>(handle.get(), &count);
    ASSERT_TRUE(position);
    EXPECT_EQ(dnmd::detail::CursorTable(owner)->row_count, count);
    mdToken token;
    ASSERT_TRUE(position.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeDef) | 1, token);
    EXPECT_EQ(handle.get(), position.get_handle());

    count = 0x12345678;
    position = dnmd::md_create_cursor<mdtid_TypeRef>(handle.get(), &count);
    EXPECT_FALSE(position);
    EXPECT_EQ(0x12345678u, count);
    ASSERT_TRUE(position.get_token(token));
    EXPECT_EQ(CreateTokenType(mdtid_TypeRef), token);
    EXPECT_EQ(handle.get(), position.get_handle());

    position = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get());
    EXPECT_TRUE(position);
}

TEST_F(CppCursorConversion, IndirectWideningRetainsSourceRatherThanTarget)
{
    auto first = AddOwner();
    (void)AddMethod(first);
    auto second = AddOwner();
    (void)AddMethod(second);
    (void)AddMethod(first);
    auto current = indirect_method::nil(handle.get());
    uint32_t count;
    ASSERT_TRUE(first.get_column_value_as_range(dnmd::columns::TypeDef_MethodList, current, &count));
    ASSERT_EQ(2u, count);
    indirect_method positions[] = {
        indirect_method::nil(handle.get()), current, indirect_method::end(handle.get())};
    for (auto const& original : positions)
    {
        dnmd::any_cursor widened = original;
        mdToken source;
        mdToken target;
        mdToken actual;
        ASSERT_TRUE(original.get_token(source));
        ASSERT_TRUE(original.get_target_token(target));
        ASSERT_TRUE(widened.get_token(actual));
        EXPECT_EQ(source, actual);
        EXPECT_EQ(mdtid_MethodPtr, ExtractTokenType(actual));
        EXPECT_EQ(mdtid_MethodDef, ExtractTokenType(target));
        EXPECT_EQ(handle.get(), widened.get_handle());
    }
}
