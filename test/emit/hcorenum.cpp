#include <dnmd.hpp>
#include "emit.hpp"
#include "hcorenum.hpp"

namespace
{
    class CppEnumResolution : public testing::Test
    {
    protected:
        dnmd::handle_ptr handle;

        void SetUp() override
        {
            handle.reset(md_create_new_handle());
            ASSERT_NE(nullptr, handle.get());
        }

        template<mdtable_id_t Owner>
        dnmd::cursor<Owner> AddOwner()
        {
            if constexpr (Owner == mdtid_MethodDef)
            {
                auto type = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get()).value();
                auto added = dnmd::md_add_new_row_to_list(type, dnmd::columns::TypeDef_MethodList);
                EXPECT_TRUE(added);
                return added.value();
            }
            else
            {
                auto added = dnmd::added_row<Owner>::append(handle.get());
                EXPECT_TRUE(added);
                return added.value();
            }
        }

        HCORENUMImpl_ptr Enumerator(dnmd::any_cursor source, uint32_t count)
        {
            HCORENUMImpl* raw = nullptr;
            EXPECT_EQ(S_OK, HCORENUMImpl::CreateTableEnum(handle.get(), 1, &raw));
            HCORENUMImpl_ptr result{raw};
            HCORENUMImpl::InitTableEnum(*result, 0, source, count);
            return result;
        }

        template<mdtable_id_t Owner, mdtable_id_t Direct, mdtable_id_t Ptr>
        void CheckFamily(dnmd::column<Owner, dnmd::indirect_cursor<Direct, Ptr>> list)
        {
            auto first = AddOwner<Owner>();
            mdToken expected[2];
            {
                auto added = dnmd::md_add_new_row_to_list(first, list);
                ASSERT_TRUE(added);
                ASSERT_TRUE(added.get_token(expected[0]));
            }
            auto second = AddOwner<Owner>();
            {
                auto added = dnmd::md_add_new_row_to_list(second, list);
                ASSERT_TRUE(added);
            }
            {
                auto added = dnmd::md_add_new_row_to_list(first, list);
                ASSERT_TRUE(added);
                ASSERT_TRUE(added.get_token(expected[1]));
            }
            auto source = dnmd::indirect_cursor<Direct, Ptr>::nil(handle.get());
            uint32_t count;
            ASSERT_TRUE(first.get_column_value_as_range(list, source, &count));
            ASSERT_EQ(2u, count);
            ASSERT_EQ(Ptr, dnmd::detail::CursorTable(source)->table_id);
            auto pointer_position = dnmd::raw_cursor<Ptr>{source};
            ASSERT_TRUE(pointer_position.value().set_column_value(dnmd::detail::pointer_column<Ptr>(), expected[0]));
            ASSERT_TRUE(pointer_position.move(1));
            ASSERT_TRUE(pointer_position.value().set_column_value(dnmd::detail::pointer_column<Ptr>(), expected[1]));
            auto enumeration = Enumerator(source, count);
            mdToken tokens[2];
            ULONG read;
            ASSERT_EQ(S_OK, enumeration->ReadTokens(tokens, 2, &read));
            ASSERT_EQ(2u, read);
            EXPECT_EQ(expected[0], tokens[0]);
            EXPECT_EQ(expected[1], tokens[1]);
            ASSERT_EQ(S_OK, enumeration->Reset(0));
            ASSERT_EQ(S_OK, enumeration->ReadTokens(tokens, 1, &read));
            ASSERT_EQ(1u, read);
            EXPECT_EQ(expected[0], tokens[0]);
            ASSERT_EQ(S_OK, enumeration->ReadTokens(tokens, 1, &read));
            ASSERT_EQ(1u, read);
            EXPECT_EQ(expected[1], tokens[0]);

            dnmd::raw_cursor<Ptr> sentinels[] = {
                dnmd::raw_cursor<Ptr>::nil(handle.get()), dnmd::raw_cursor<Ptr>::end(handle.get())};
            mdToken targets[] = {CreateTokenType(Direct),
                CreateTokenType(Direct) | (dnmd::detail::context(handle.get()).tables[Direct].row_count + 1)};
            for (uint32_t i = 0; i < 2; ++i)
            {
                for (ULONG capacity : {1u, 2u})
                {
                    auto sentinel = Enumerator(sentinels[i], 1);
                    ASSERT_EQ(S_OK, sentinel->ReadTokens(tokens, capacity, &read));
                    ASSERT_EQ(1u, read);
                    EXPECT_EQ(targets[i], tokens[0]);
                }
            }

            auto pointer = dnmd::raw_cursor<Ptr>{source}.value();
            for (mdToken target : targets)
            {
                ASSERT_TRUE(pointer.set_column_value(dnmd::detail::pointer_column<Ptr>(), target));
                for (ULONG capacity : {1u, 2u})
                {
                    auto sentinel_target = Enumerator(source, 1);
                    ASSERT_EQ(S_OK, sentinel_target->ReadTokens(tokens, capacity, &read));
                    ASSERT_EQ(1u, read);
                    EXPECT_EQ(target, tokens[0]);
                }
            }
            ASSERT_TRUE(pointer.set_column_value(dnmd::detail::pointer_column<Ptr>(),
                CreateTokenType(Direct) | 100));
            for (ULONG capacity : {1u, 2u})
            {
                auto malformed = Enumerator(source, 1);
                tokens[0] = 0x12345678;
                EXPECT_EQ(CLDB_E_FILE_CORRUPT, malformed->ReadTokens(tokens, capacity, &read));
                EXPECT_EQ(0u, read);
                EXPECT_EQ(0x12345678u, tokens[0]);
            }
            ASSERT_TRUE(pointer.set_column_value(dnmd::detail::pointer_column<Ptr>(), expected[0]));
        }
    };
}

TEST_F(CppEnumResolution, AllPointerTableFamilies)
{
    using namespace dnmd::columns;
    ASSERT_NO_FATAL_FAILURE(CheckFamily(TypeDef_FieldList));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(TypeDef_MethodList));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(MethodDef_ParamList));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(EventMap_EventList));
    ASSERT_NO_FATAL_FAILURE(CheckFamily(PropertyMap_PropertyList));
}

TEST_F(CppEnumResolution, DirectNilLiveEndPositionsPassThrough)
{
    auto first = dnmd::md_create_cursor<mdtid_TypeDef>(handle.get());
    dnmd::raw_cursor<mdtid_TypeDef> positions[] = {
        dnmd::raw_cursor<mdtid_TypeDef>::nil(handle.get()), first,
        dnmd::raw_cursor<mdtid_TypeDef>::end(handle.get())};
    for (auto position : positions)
    {
        auto enumeration = Enumerator(position, 1);
        mdToken expected;
        ASSERT_TRUE(position.get_token(expected));
        mdToken actual;
        ULONG read;
        ASSERT_EQ(S_OK, enumeration->ReadTokens(&actual, 1, &read));
        EXPECT_EQ(expected, actual);
        EXPECT_EQ(1u, read);
    }
}
