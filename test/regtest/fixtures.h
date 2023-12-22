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

struct MetadataFile final
{
    enum class Kind
    {
        OnDisk,
        Generated
    } kind;

    MetadataFile(Kind kind, std::string pathOrKey)
    : pathOrKey(std::move(pathOrKey)), kind(kind) {}

    std::string pathOrKey;
    
    bool operator==(const MetadataFile& rhs) const noexcept
    {
        return kind == rhs.kind && pathOrKey == rhs.pathOrKey;
    }
};

inline static std::string DeltaImageKey = "DeltaImage";

std::string PrintName(testing::TestParamInfo<MetadataFile> info);

std::vector<MetadataFile> MetadataFilesInDirectory(std::string directory);

std::vector<MetadataFile> CoreLibFiles();

span<uint8_t> GetMetadataForFile(MetadataFile file);

malloc_span<uint8_t> GetRegressionAssemblyMetadata();

std::string FindFrameworkInstall(std::string version);

std::string GetBaselineDirectory();

void SetBaselineModulePath(std::string path);

void SetRegressionAssemblyPath(std::string path);

class RegressionTest : public ::testing::TestWithParam<MetadataFile>
{
protected:
    using TokenList = std::vector<uint32_t>;
};

#endif // !_TEST_REGTEST_FIXTURES_H_