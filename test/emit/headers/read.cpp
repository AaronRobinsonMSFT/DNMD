#include <dnmd_read.hpp>
#include <dnmd_read.hpp>
#include "fixture.hpp"

TEST_F(CppHeader, StandaloneRead)
{
    auto module = dnmd::raw_cursor<mdtid_Module>{Row<mdtid_Module>()}.as();
    dnmd::row_reader reader{module};
    uint32_t generation;
    ASSERT_TRUE(reader.read(dnmd::columns::Module_Generation, &generation));
    EXPECT_EQ(0u, generation);
    char const* name;
    ASSERT_TRUE(reader.read(dnmd::columns::Module_Name, &name));
    EXPECT_NE(nullptr, name);
    dnmd::column_iterator first{module, dnmd::columns::Module_Generation};
    EXPECT_EQ(generation, *first);
    EXPECT_EQ(1, first.end() - first);
    EXPECT_EQ(first, std::find(first, first.end(), generation));
}
