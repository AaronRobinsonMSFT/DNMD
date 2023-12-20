#ifndef _TEST_REGTEST_FIXTURES_H_
#define _TEST_REGTEST_FIXTURES_H_

#include <gtest/gtest.h>

#if defined(_MSC_VER)
#define NOMINMAX
#include <Windows.h>
#endif
#include <dncp.h>

#include <cor.h>
#include <corsym.h>

#include <vector>

#include <internal/span.hpp>


// TODO: Can't pre-create here as dncp::com_ptr is non-copyable, need to create in the test I guess. Change discovery to be a function that returns a vector of spans of the metadata in PE.
struct FileBlob
{
    std::string path;
    std::vector<uint8_t> blob;
};

inline std::string PrintFileBlob(testing::TestParamInfo<FileBlob> info)
{
    return info.param.path;
}

std::vector<FileBlob> MetadataInDirectory(std::string directory);

std::string FindFrameworkInstall(std::string version);

std::string GetBaselineDirectory();

void SetBaselineModulePath(std::string path);

class RegressionTest : public ::testing::TestWithParam<FileBlob>
{
};

#endif // !_TEST_REGTEST_FIXTURES_H_