#ifndef _TEST_REGTEST_FIXTURES_H_
#define _TEST_REGTEST_FIXTURES_H_

#include <gtest/gtest.h>
#include "baseline.h"
#include <dncp.h>

#include "cor.h"
#include "corsym.h"
#include <vector>

template<typename TInterface>
struct Regression
{
    std::string modulePath;
    dncp::com_ptr<TInterface> baseline;
    dncp::com_ptr<TInterface> test;
};

using MetadataRegresssion = Regression<IMetaDataImport2>;

using SymbolRegression = Regression<ISymUnmanagedReader>;

std::vector<MetadataRegresssion> ReadOnlyMetadataInDirectory(std::string directory);
std::vector<MetadataRegresssion> ReadWriteMetadataInDirectory(std::string directory);

class MetaDataRegressionTest : public ::testing::TestWithParam<MetadataRegresssion>
{
};

class SymbolRegressionTest : public ::testing::TestWithParam<SymbolRegression>
{
};

#endif // !_TEST_REGTEST_FIXTURES_H_