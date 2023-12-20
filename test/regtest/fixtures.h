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


// TODO: Can't pre-create here as dncp::com_ptr is non-copyable, need to create in the test I guess. Change discovery to be a function that returns a vector of spans of the metadata in PE.
template<typename TInterface>
struct Regression
{
    using ParamType = Regression;

    std::string modulePath;
    dncp::com_ptr<TInterface> baseline;
    dncp::com_ptr<TInterface> test;
};

using MetadataRegresssion = Regression<IMetaDataImport2>;

using SymbolRegression = Regression<ISymUnmanagedReader>;

std::vector<MetadataRegresssion> ReadOnlyMetadataInDirectory(std::string directory);
std::vector<MetadataRegresssion> ReadWriteMetadataInDirectory(std::string directory);

std::string FindFrameworkInstall(std::string version);

std::string GetBaselineDirectory();

void SetBaselineModulePath(std::string path);

class MetaDataRegressionTest : public ::testing::TestWithParam<MetadataRegresssion>
{
};

class SymbolRegressionTest : public ::testing::TestWithParam<SymbolRegression>
{
};

#endif // !_TEST_REGTEST_FIXTURES_H_