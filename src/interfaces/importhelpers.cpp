#include "importhelpers.hpp"
#include "signatures.hpp"
#include "pal.hpp"
#include <cassert>
#include <stack>
#include <cctype>
#include <array>
#include <algorithm>
#include <cstring>

using namespace dnmd::columns;

// Macros from wincrypt.h that we need avaliable on all platforms
// for strong-name parsing.
// Get the class (hash, signature, encryption, etc) of an algorithm from an ALG_ID
#define GET_ALG_CLASS(x)                (x & (7 << 13))
// Get the sub-identifier of an algorithm (like SHA1) from an ALG_ID
#define GET_ALG_SID(x)                  (x & (511))

#define ALG_CLASS_SIGNATURE             (1 << 13)
#define ALG_CLASS_HASH                  (4 << 13)

#define ALG_SID_SHA1                    4

// Blob definitions from wincrypt.h
#define PUBLICKEYBLOB           0x6

#define RETURN_IF_FAILED(exp) \
{ \
    hr = (exp); \
    if (FAILED(hr)) \
    { \
        return hr; \
    } \
}

#define RETURN_UNEXPECTED_IF_FAILED(exp) \
{ \
    hr = (exp); \
    if (FAILED(hr)) \
    { \
        return import_helpers::unexpected{hr}; \
    } \
}

namespace
{
    using resolution_scope_cursor = dnmd::raw_cursor<mdtid_Module, mdtid_ModuleRef, mdtid_AssemblyRef, mdtid_TypeRef>;
    using imported_scope_cursor = dnmd::raw_cursor<mdtid_Module, mdtid_ModuleRef, mdtid_AssemblyRef>;
    using source_scope_cursor = dnmd::raw_cursor<mdtid_Module, mdtid_ModuleRef, mdtid_AssemblyRef, mdtid_TypeRef, mdtid_File>;

    HRESULT GetMvid(mdhandle_t image, mdguid_t* mvid)
    {
        auto c = dnmd::md_create_cursor<mdtid_Module>(image);
        if (!c)
            return CLDB_E_FILE_CORRUPT;

        if (!c.value().get_column_value(Module_Mvid, mvid))
            return CLDB_E_FILE_CORRUPT;

        return S_OK;
    }

    // The strong name token is the last 8 bytes of the SHA1 hash of the public key.
    // See II.6.3
    constexpr size_t StrongNameTokenSize = 8;

    using StrongNameToken = std::array<uint8_t, StrongNameTokenSize>;

    namespace StrongNameKeys
    {
        // The byte values of the real public keys and their corresponding tokens
        // for assemblies the .NET SDK ships.
        // These blobs allow us to skip the token calculation for these assemblies.
        // Each of these keys corresponds to the public key in a file in the .NET Arcade SDK.

        // The byte values of the ECMA pseudo public key and its token.
        // Arcade SDK StrongNameKeyId: ECMA
        // See II.6.2.1.3 for the definition of this key.
        uint8_t const EcmaPublicKey[] = { 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0 };
        StrongNameToken const EcmaToken = { 0xb7, 0x7a, 0x5c, 0x56, 0x19, 0x34, 0xe0, 0x89 };

        // Arcade SDK StrongNameKeyId: Microsoft
        uint8_t const Microsoft[] =
        {
            0x00,0x24,0x00,0x00,0x04,0x80,0x00,0x00,0x94,0x00,0x00,0x00,0x06,0x02,0x00,0x00,
            0x00,0x24,0x00,0x00,0x52,0x53,0x41,0x31,0x00,0x04,0x00,0x00,0x01,0x00,0x01,0x00,
            0x07,0xd1,0xfa,0x57,0xc4,0xae,0xd9,0xf0,0xa3,0x2e,0x84,0xaa,0x0f,0xae,0xfd,0x0d,
            0xe9,0xe8,0xfd,0x6a,0xec,0x8f,0x87,0xfb,0x03,0x76,0x6c,0x83,0x4c,0x99,0x92,0x1e,
            0xb2,0x3b,0xe7,0x9a,0xd9,0xd5,0xdc,0xc1,0xdd,0x9a,0xd2,0x36,0x13,0x21,0x02,0x90,
            0x0b,0x72,0x3c,0xf9,0x80,0x95,0x7f,0xc4,0xe1,0x77,0x10,0x8f,0xc6,0x07,0x77,0x4f,
            0x29,0xe8,0x32,0x0e,0x92,0xea,0x05,0xec,0xe4,0xe8,0x21,0xc0,0xa5,0xef,0xe8,0xf1,
            0x64,0x5c,0x4c,0x0c,0x93,0xc1,0xab,0x99,0x28,0x5d,0x62,0x2c,0xaa,0x65,0x2c,0x1d,
            0xfa,0xd6,0x3d,0x74,0x5d,0x6f,0x2d,0xe5,0xf1,0x7e,0x5e,0xaf,0x0f,0xc4,0x96,0x3d,
            0x26,0x1c,0x8a,0x12,0x43,0x65,0x18,0x20,0x6d,0xc0,0x93,0x34,0x4d,0x5a,0xd2,0x93
        };

        StrongNameToken const MicrosoftToken = {0xb0,0x3f,0x5f,0x7f,0x11,0xd5,0x0a,0x3a};

        // Arcade SDK StrongNameKeyId: SilverlightPlatform
        uint8_t const SilverlightPlatform[] =
        {
            0x00,0x24,0x00,0x00,0x04,0x80,0x00,0x00,0x94,0x00,0x00,0x00,0x06,0x02,0x00,0x00,
            0x00,0x24,0x00,0x00,0x52,0x53,0x41,0x31,0x00,0x04,0x00,0x00,0x01,0x00,0x01,0x00,
            0x8d,0x56,0xc7,0x6f,0x9e,0x86,0x49,0x38,0x30,0x49,0xf3,0x83,0xc4,0x4b,0xe0,0xec,
            0x20,0x41,0x81,0x82,0x2a,0x6c,0x31,0xcf,0x5e,0xb7,0xef,0x48,0x69,0x44,0xd0,0x32,
            0x18,0x8e,0xa1,0xd3,0x92,0x07,0x63,0x71,0x2c,0xcb,0x12,0xd7,0x5f,0xb7,0x7e,0x98,
            0x11,0x14,0x9e,0x61,0x48,0xe5,0xd3,0x2f,0xba,0xab,0x37,0x61,0x1c,0x18,0x78,0xdd,
            0xc1,0x9e,0x20,0xef,0x13,0x5d,0x0c,0xb2,0xcf,0xf2,0xbf,0xec,0x3d,0x11,0x58,0x10,
            0xc3,0xd9,0x06,0x96,0x38,0xfe,0x4b,0xe2,0x15,0xdb,0xf7,0x95,0x86,0x19,0x20,0xe5,
            0xab,0x6f,0x7d,0xb2,0xe2,0xce,0xef,0x13,0x6a,0xc2,0x3d,0x5d,0xd2,0xbf,0x03,0x17,
            0x00,0xae,0xc2,0x32,0xf6,0xc6,0xb1,0xc7,0x85,0xb4,0x30,0x5c,0x12,0x3b,0x37,0xab
        };

        StrongNameToken const SilverlightPlatformToken = {0x7c,0xec,0x85,0xd7,0xbe,0xa7,0x79,0x8e};

        // Arcade SDK StrongNameKeyId: MicrosoftShared
        uint8_t const Silverlight[] =
        {
            0x00,0x24,0x00,0x00,0x04,0x80,0x00,0x00,0x94,0x00,0x00,0x00,0x06,0x02,0x00,0x00,
            0x00,0x24,0x00,0x00,0x52,0x53,0x41,0x31,0x00,0x04,0x00,0x00,0x01,0x00,0x01,0x00,
            0xb5,0xfc,0x90,0xe7,0x02,0x7f,0x67,0x87,0x1e,0x77,0x3a,0x8f,0xde,0x89,0x38,0xc8,
            0x1d,0xd4,0x02,0xba,0x65,0xb9,0x20,0x1d,0x60,0x59,0x3e,0x96,0xc4,0x92,0x65,0x1e,
            0x88,0x9c,0xc1,0x3f,0x14,0x15,0xeb,0xb5,0x3f,0xac,0x11,0x31,0xae,0x0b,0xd3,0x33,
            0xc5,0xee,0x60,0x21,0x67,0x2d,0x97,0x18,0xea,0x31,0xa8,0xae,0xbd,0x0d,0xa0,0x07,
            0x2f,0x25,0xd8,0x7d,0xba,0x6f,0xc9,0x0f,0xfd,0x59,0x8e,0xd4,0xda,0x35,0xe4,0x4c,
            0x39,0x8c,0x45,0x43,0x07,0xe8,0xe3,0x3b,0x84,0x26,0x14,0x3d,0xae,0xc9,0xf5,0x96,
            0x83,0x6f,0x97,0xc8,0xf7,0x47,0x50,0xe5,0x97,0x5c,0x64,0xe2,0x18,0x9f,0x45,0xde,
            0xf4,0x6b,0x2a,0x2b,0x12,0x47,0xad,0xc3,0x65,0x2b,0xf5,0xc3,0x08,0x05,0x5d,0xa9
        };

        StrongNameToken const SilverlightToken = {0x31,0xBF,0x38,0x56,0xAD,0x36,0x4E,0x35};

        // Arcade SDK StrongNameKeyId: MicrosoftAspNetCore
        uint8_t const AspNetCore[] =
        {
            0x00,0x24,0x00,0x00,0x04,0x80,0x00,0x00,0x94,0x00,0x00,0x00,0x06,0x02,0x00,0x00,
            0x00,0x24,0x00,0x00,0x52,0x53,0x41,0x31,0x00,0x04,0x00,0x00,0x01,0x00,0x01,0x00,
            0xF3,0x3A,0x29,0x04,0x4F,0xA9,0xD7,0x40,0xC9,0xB3,0x21,0x3A,0x93,0xE5,0x7C,0x84,
            0xB4,0x72,0xC8,0x4E,0x0B,0x8A,0x0E,0x1A,0xE4,0x8E,0x67,0xA9,0xF8,0xF6,0xDE,0x9D,
            0x5F,0x7F,0x3D,0x52,0xAC,0x23,0xE4,0x8A,0xC5,0x18,0x01,0xF1,0xDC,0x95,0x0A,0xBE,
            0x90,0x1D,0xA3,0x4D,0x2A,0x9E,0x3B,0xAA,0xDB,0x14,0x1A,0x17,0xC7,0x7E,0xF3,0xC5,
            0x65,0xDD,0x5E,0xE5,0x05,0x4B,0x91,0xCF,0x63,0xBB,0x3C,0x6A,0xB8,0x3F,0x72,0xAB,
            0x3A,0xAF,0xE9,0x3D,0x0F,0xC3,0xC2,0x34,0x8B,0x76,0x4F,0xAF,0xB0,0xB1,0xC0,0x73,
            0x3D,0xE5,0x14,0x59,0xAE,0xAB,0x46,0x58,0x03,0x84,0xBF,0x9D,0x74,0xC4,0xE2,0x81,
            0x64,0xB7,0xCD,0xE2,0x47,0xF8,0x91,0xBA,0x07,0x89,0x1C,0x9D,0x87,0x2A,0xD2,0xBB
        };

        StrongNameToken const AspNetCoreToken = {0xad, 0xb9, 0x79, 0x38, 0x29, 0xdd, 0xae, 0x60};

        // Arcade SDK StrongNameKeyId: Open
        uint8_t const Open[] =
        {
            0x00,0x24,0x00,0x00,0x04,0x80,0x00,0x00,0x94,0x00,0x00,0x00,0x06,0x02,0x00,0x00,
            0x00,0x24,0x00,0x00,0x52,0x53,0x41,0x31,0x00,0x04,0x00,0x00,0x01,0x00,0x01,0x00,
            0x4B,0x86,0xC4,0xCB,0x78,0x54,0x9B,0x34,0xBA,0xB6,0x1A,0x3B,0x18,0x00,0xE2,0x3B,
            0xFE,0xB5,0xB3,0xEC,0x39,0x00,0x74,0x04,0x15,0x36,0xA7,0xE3,0xCB,0xD9,0x7F,0x5F,
            0x04,0xCF,0x0F,0x85,0x71,0x55,0xA8,0x92,0x8E,0xAA,0x29,0xEB,0xFD,0x11,0xCF,0xBB,
            0xAD,0x3B,0xA7,0x0E,0xFE,0xA7,0xBD,0xA3,0x22,0x6C,0x6A,0x8D,0x37,0x0A,0x4C,0xD3,
            0x03,0xF7,0x14,0x48,0x6B,0x6E,0xBC,0x22,0x59,0x85,0xA6,0x38,0x47,0x1E,0x6E,0xF5,
            0x71,0xCC,0x92,0xA4,0x61,0x3C,0x00,0xB8,0xFA,0x65,0xD6,0x1C,0xCE,0xE0,0xCB,0xE5,
            0xF3,0x63,0x30,0xC9,0xA0,0x1F,0x41,0x83,0x55,0x9F,0x1B,0xEF,0x24,0xCC,0x29,0x17,
            0xC6,0xD9,0x13,0xE3,0xA5,0x41,0x33,0x3A,0x1D,0x05,0xD9,0xBE,0xD2,0x2B,0x38,0xCB
        };

        StrongNameToken const OpenToken = {0xcc, 0x7b, 0x13, 0xff, 0xcd, 0x2d, 0xdd, 0x51};

        struct WellKnownKey final
        {
            uint8_t const* const PublicKey;
            size_t const PublicKeyLen;
            StrongNameToken const& Token;
        };

        static WellKnownKey const WellKnownKeys[] =
        {
            { EcmaPublicKey, sizeof(EcmaPublicKey), EcmaToken },
            { Microsoft, sizeof(Microsoft), MicrosoftToken },
            { SilverlightPlatform, sizeof(SilverlightPlatform), SilverlightPlatformToken },
            { Silverlight, sizeof(Silverlight), SilverlightToken },
            { AspNetCore, sizeof(AspNetCore), AspNetCoreToken },
            { Open, sizeof(Open), OpenToken },
        };

        bool GetTokenForWellKnownKey(uint8_t const* key, size_t keyLength, StrongNameToken* token)
        {
            for (size_t i = 0; i < ARRAY_SIZE(WellKnownKeys); i++)
            {
                if (keyLength == WellKnownKeys[i].PublicKeyLen
                    && std::memcmp(key, WellKnownKeys[i].PublicKey, keyLength) == 0)
                {
                    *token = WellKnownKeys[i].Token;
                    return true;
                }
            }

            return false;
        }
    }

    struct PublicKeyBlob final
    {
        uint32_t SigAlgID;
        uint32_t HashAlgID;
        uint32_t PublicKeyLength;
        uint8_t  PublicKey[];
    };

    HRESULT StrongNameTokenFromPublicKey(span<uint8_t const> publicKeyBlob, StrongNameToken& strongNameTokenBuffer)
    {
        if (publicKeyBlob.size() < sizeof(PublicKeyBlob))
            return CORSEC_E_INVALID_PUBLICKEY;

        PublicKeyBlob const* publicKey = reinterpret_cast<PublicKeyBlob const*>((uint8_t const*)publicKeyBlob);

        if (publicKey->PublicKeyLength != publicKeyBlob.size() - sizeof(PublicKeyBlob))
            return CORSEC_E_INVALID_PUBLICKEY;

        if (publicKeyBlob.size() == sizeof(StrongNameKeys::EcmaPublicKey)
            && std::memcmp((uint8_t const*)publicKeyBlob, StrongNameKeys::EcmaPublicKey, sizeof(StrongNameKeys::EcmaPublicKey)) == 0)
        {
            return S_OK;
        }

        if (publicKey->HashAlgID != 0)
        {
            if (GET_ALG_CLASS(publicKey->HashAlgID) != ALG_CLASS_HASH)
                return CORSEC_E_INVALID_PUBLICKEY;

            if (GET_ALG_SID(publicKey->HashAlgID) < ALG_SID_SHA1)
                return CORSEC_E_INVALID_PUBLICKEY;
        }

        if (publicKey->SigAlgID != 0 && GET_ALG_CLASS(publicKey->SigAlgID) != ALG_CLASS_SIGNATURE)
            return CORSEC_E_INVALID_PUBLICKEY;

        if (publicKey->PublicKeyLength == 0 || publicKey->PublicKey[0] != PUBLICKEYBLOB)
            return CORSEC_E_INVALID_PUBLICKEY;

        // Check well-known keys first.
        if (StrongNameKeys::GetTokenForWellKnownKey(publicKey->PublicKey, publicKey->PublicKeyLength, &strongNameTokenBuffer))
            return S_OK;

        std::array<uint8_t, pal::SHA1_HASH_SIZE> hash;
        if (!pal::ComputeSha1Hash(publicKeyBlob, hash))
            return CORSEC_E_INVALID_PUBLICKEY;

        // Take the last few bytes of the hash value for our token.
        // These are the low order bytes from a big-endian point of view.
        // Reverse the order of these bytes in the output buffer to get little-endian byte order.
        // The byte order of the strong name token is not specified in ECMA-335, but is what CLR, CoreCLR, and Mono Desktop have always done.
        std::reverse_copy(hash.begin() + pal::SHA1_HASH_SIZE - StrongNameTokenSize, hash.end(), strongNameTokenBuffer.begin());

        return S_OK;
    }
}

namespace
{
    struct AssemblyVersionMatcher
    {
        bool(*IsApplicable)(char const* name);
        HRESULT(*Match)(dnmd::row_reader<mdtid_AssemblyRef> const& reader, uint32_t majorVersion, uint32_t minorVersion, uint32_t buildNumber, uint32_t revisionNumber);
    };

    std::array<AssemblyVersionMatcher, 2> const AssemblyVersionMatchers =
    {
        {
            // COMPAT: CoreCLR resolves all references to mscorlib and Microsoft.VisualC to the same assembly ref ignoring the build and revision version.
            {
                [](char const* name) -> bool
                {
                    auto AsciiCaseInsensitiveEquals = [](char const* a, char const* b)
                    {
                        while (*a != '\0' && *b != '\0')
                        {
                            if (std::tolower(*a) != std::tolower(*b))
                                return false;

                            a++;
                            b++;
                        }

                        return *a == '\0' && *b == '\0';
                    };

                    return AsciiCaseInsensitiveEquals(name, "mscorlib")
                        || AsciiCaseInsensitiveEquals(name, "microsoft.visualc");
                },
                [](dnmd::row_reader<mdtid_AssemblyRef> const& reader, uint32_t majorVersion, uint32_t minorVersion, uint32_t buildNumber, uint32_t revisionNumber)
                {
                    UNREFERENCED_PARAMETER(buildNumber);
                    UNREFERENCED_PARAMETER(revisionNumber);
                    uint32_t temp;
                    if (!reader.read(AssemblyRef_MajorVersion, &temp))
                        return CLDB_E_FILE_CORRUPT;

                    if (temp != majorVersion)
                        return S_FALSE;

                    if (!reader.read(AssemblyRef_MinorVersion, &temp))
                        return CLDB_E_FILE_CORRUPT;

                    if (temp != minorVersion)
                        return S_FALSE;

                    return S_OK;
                }
            },
            // Otherwise, we'll compare all of the version components.
            {
                [](char const* name)
                {
                    UNREFERENCED_PARAMETER(name);
                    return true;
                },
                [](dnmd::row_reader<mdtid_AssemblyRef> const& reader, uint32_t majorVersion, uint32_t minorVersion, uint32_t buildNumber, uint32_t revisionNumber)
                {
                    uint32_t temp;
                    if (!reader.read(AssemblyRef_MajorVersion, &temp))
                        return CLDB_E_FILE_CORRUPT;

                    if (temp != majorVersion)
                        return S_FALSE;

                    if (!reader.read(AssemblyRef_MinorVersion, &temp))
                        return CLDB_E_FILE_CORRUPT;

                    if (temp != minorVersion)
                        return S_FALSE;
                    if (!reader.read(AssemblyRef_BuildNumber, &temp))
                        return CLDB_E_FILE_CORRUPT;

                    if (temp != buildNumber)
                        return S_FALSE;

                    if (!reader.read(AssemblyRef_RevisionNumber, &temp))
                        return CLDB_E_FILE_CORRUPT;

                    if (temp != revisionNumber)
                        return S_FALSE;

                    return S_OK;
                }
            }
        }
    };

    AssemblyVersionMatcher const& GetAssemblyVersionMatcher(char const* name)
    {
        for (AssemblyVersionMatcher const& matcher : AssemblyVersionMatchers)
        {
            if (matcher.IsApplicable(name))
                return matcher;
        }

        // The final matcher should always be applicable.
        // If it isn't, we have a bug in our code.
        assert(false);
        return AssemblyVersionMatchers[AssemblyVersionMatchers.size() - 1];
    }

    cursor_result<dnmd::raw_cursor<mdtid_AssemblyRef>> FindAssemblyRef(
        mdhandle_t targetModule,
        uint32_t majorVersion,
        uint32_t minorVersion,
        uint32_t buildNumber,
        uint32_t revisionNumber,
        uint32_t flags,
        char const* name,
        char const* culture,
        span<const uint8_t> publicKeyOrToken)
    {
        HRESULT hr;

        bool calculatedPublicKeyToken = false;
        StrongNameToken publicKeyToken{};
        if (IsAfPublicKeyToken(flags) && publicKeyOrToken.size() == StrongNameTokenSize)
        {
            std::copy(publicKeyOrToken.begin(), publicKeyOrToken.end(), publicKeyToken.begin());
            calculatedPublicKeyToken = true;
        }

        // Search the assembly ref table for a matching row.
        uint32_t count;
        auto c = dnmd::md_create_cursor<mdtid_AssemblyRef>(targetModule, &count);
        if (!c)
            return import_helpers::unexpected{E_FAIL};

        AssemblyVersionMatcher const& matcher = GetAssemblyVersionMatcher(name);

        for (uint32_t i = 0; i < count; i++, c.move(1))
        {
            if (!c)
                return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};
            dnmd::row_reader reader{c.value()};
            // Search the table linearly by manually reading the columns.
            hr = matcher.Match(reader, majorVersion, minorVersion, buildNumber, revisionNumber);
            RETURN_UNEXPECTED_IF_FAILED(hr);
            if (hr == S_FALSE)
                continue;

            char const* tempString;
            if (!reader.read(AssemblyRef_Name, &tempString))
                return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

            if (std::strcmp(tempString, name) != 0)
                continue;

            if (!reader.read(AssemblyRef_Culture, &tempString))
                return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

            if (std::strcmp(tempString, culture) != 0)
                continue;

            uint8_t const* tempBlob;
            uint32_t tempBlobLength;
            if (!reader.read(AssemblyRef_PublicKeyOrToken, &tempBlob, &tempBlobLength))
                return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

            // If our source has a public key or token, we can only match against an AssemblyRef that has a public key or token.
            // If our source doesn't have a public key or token, we can only match against an AssemblyRef that doesn't have a public key or token.
            if ((publicKeyOrToken.size() == 0) != (tempBlobLength == 0))
                continue;

            if (tempBlobLength != 0)
            {
                // Handle the case when a ref may be using a full public key instead of a token.
                StrongNameToken refPublicKeyToken;

                uint32_t assemblyRefFlags;
                if (!reader.read(AssemblyRef_Flags, &assemblyRefFlags))
                    return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

                if (IsAfPublicKey(flags) == IsAfPublicKey(assemblyRefFlags))
                {
                    // If the source and destination either both have a full key or both have a key token, we can compare them directly.
                    if (tempBlobLength != publicKeyOrToken.size() || !std::equal(publicKeyOrToken.begin(), publicKeyOrToken.end(), tempBlob))
                        continue;
                }
                else if (IsAfPublicKey(assemblyRefFlags))
                {
                    // This AssemblyRef row has a full public key and our source has a token.
                    // We need to get the token from the key.
                    RETURN_UNEXPECTED_IF_FAILED(StrongNameTokenFromPublicKey({ tempBlob, tempBlobLength }, refPublicKeyToken));
                }
                else
                {
                    // This AssemblyRef row has a token and our source has a full public key.
                    // We need to get the token from the key.
                    if (!calculatedPublicKeyToken)
                    {
                        RETURN_UNEXPECTED_IF_FAILED(StrongNameTokenFromPublicKey(publicKeyOrToken, publicKeyToken));
                        calculatedPublicKeyToken = true;
                    }
                }

                // At this point, we have a token for both our source and the AssemblyRef we are checking against.

                // If our source started with a public key token, we should have initialized publicKeyToken to it
                // and set calculatedPublicKeyToken to true.
                // If our source started with a public key, then we should have calculated the token above and
                // set calculatedPublicKeyToken to true.
                assert(calculatedPublicKeyToken);
                if (publicKeyToken != refPublicKeyToken)
                    continue;
            }

            return c;
        }

        return import_helpers::unexpected{S_FALSE};
    }

    cursor_result<dnmd::raw_cursor<mdtid_AssemblyRef>> ImportReferenceToAssemblyRef(
        dnmd::raw_cursor<mdtid_AssemblyRef> sourceAssemblyRef,
        mdhandle_t targetModule,
        std::function<void(dnmd::any_cursor)> onRowAdded)
    {
        auto sourceRow = sourceAssemblyRef.as<mdtid_AssemblyRef>();
        if (!sourceRow)
            return import_helpers::unexpected{E_FAIL};
        dnmd::row_reader reader{sourceRow.value()};
        uint32_t flags;
        if (!reader.read(AssemblyRef_Flags, &flags))
            return import_helpers::unexpected{E_FAIL};

        uint8_t const* publicKey;
        uint32_t publicKeyLength;
        if (!reader.read(AssemblyRef_PublicKeyOrToken, &publicKey, &publicKeyLength))
            return import_helpers::unexpected{E_FAIL};

        uint32_t majorVersion;
        if (!reader.read(AssemblyRef_MajorVersion, &majorVersion))
            return import_helpers::unexpected{E_FAIL};

        uint32_t minorVersion;
        if (!reader.read(AssemblyRef_MinorVersion, &minorVersion))
            return import_helpers::unexpected{E_FAIL};

        uint32_t buildNumber;
        if (!reader.read(AssemblyRef_BuildNumber, &buildNumber))
            return import_helpers::unexpected{E_FAIL};

        uint32_t revisionNumber;
        if (!reader.read(AssemblyRef_RevisionNumber, &revisionNumber))
            return import_helpers::unexpected{E_FAIL};

        char const* assemblyName;
        if (!reader.read(AssemblyRef_Name, &assemblyName))
            return import_helpers::unexpected{E_FAIL};

        char const* assemblyCulture;
        if (!reader.read(AssemblyRef_Culture, &assemblyCulture))
            return import_helpers::unexpected{E_FAIL};

        auto existing = FindAssemblyRef(
            targetModule,
            majorVersion,
            minorVersion,
            buildNumber,
            revisionNumber,
            flags,
            assemblyName,
            assemblyCulture,
            { publicKey, publicKeyLength });
        if (existing)
            return existing;
        if (existing.error() != S_FALSE)
            return import_helpers::unexpected{existing.error()};

        auto assemblyRef = dnmd::added_row<mdtid_AssemblyRef>::append(targetModule);
        if (!assemblyRef)
            return import_helpers::unexpected{E_FAIL};

        onRowAdded(assemblyRef.value());

        dnmd::row_writer writer{assemblyRef.value()};
        if (!writer.write(AssemblyRef_MajorVersion, majorVersion))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_MinorVersion, minorVersion))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_BuildNumber, buildNumber))
            return import_helpers::unexpected{E_FAIL};

        if (writer.write(AssemblyRef_RevisionNumber, revisionNumber))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_Flags, flags))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_Name, assemblyName))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_Culture, assemblyCulture))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_PublicKeyOrToken, publicKey, publicKeyLength))
            return import_helpers::unexpected{E_FAIL};

        return assemblyRef.position();
    }

    // Add a reference to sourceAssembly
    // in the AssemblyRef tables in targetModule and targetAssembly.
    // Returns the resulting cursor into targetModule's AssemblyRef table.
    cursor_result<dnmd::raw_cursor<mdtid_AssemblyRef>> ImportReferenceToAssemblyRef(
        dnmd::raw_cursor<mdtid_AssemblyRef> sourceAssemblyRef,
        mdhandle_t targetModule,
        mdhandle_t targetAssembly,
        std::function<void(dnmd::any_cursor)> onRowAdded)
    {
        // Add a reference to the assembly in the target module.
        auto reference = ImportReferenceToAssemblyRef(sourceAssemblyRef, targetModule, onRowAdded);
        if (!reference)
            return import_helpers::unexpected{reference.error()};

        // Also add a reference to the assembly in the target assembly.
        // In most cases, the target module will be the same as the target assembly, so this will be a no-op.
        // However, if the target module is a netmodule, then the target assembly will be the main assembly.
        // CoreCLR doesn't support multi-module assemblies, but they're still valid in ECMA-335.
        if (targetModule != targetAssembly)
        {
            auto assemblyReference = ImportReferenceToAssemblyRef(sourceAssemblyRef, targetAssembly, onRowAdded);
            if (!assemblyReference)
                return import_helpers::unexpected{assemblyReference.error()};
        }

        return reference;
    }

    cursor_result<dnmd::raw_cursor<mdtid_AssemblyRef>> ImportReferenceToAssembly(
        dnmd::raw_cursor<mdtid_Assembly> sourceAssembly,
        span<const uint8_t> sourceAssemblyHash,
        mdhandle_t targetModule,
        std::function<void(dnmd::any_cursor)> onRowAdded)
    {
        HRESULT hr;
        auto sourceRow = sourceAssembly.as<mdtid_Assembly>();
        if (!sourceRow)
            return import_helpers::unexpected{E_FAIL};
        dnmd::row_reader reader{sourceRow.value()};
        uint32_t flags;
        if (!reader.read(Assembly_Flags, &flags))
            return import_helpers::unexpected{E_FAIL};

        uint8_t const* publicKey;
        uint32_t publicKeyLength;
        if (!reader.read(Assembly_PublicKey, &publicKey, &publicKeyLength))
            return import_helpers::unexpected{E_FAIL};

        span<const uint8_t> publicKeyTokenSpan;
        StrongNameToken publicKeyToken;
        if (publicKey != nullptr)
        {
            assert(IsAfPublicKey(flags));
            flags &= ~afPublicKey;
            RETURN_UNEXPECTED_IF_FAILED(StrongNameTokenFromPublicKey({ publicKey, publicKeyLength }, publicKeyToken));
            publicKeyTokenSpan = { publicKeyToken.data(), publicKeyToken.size() };
        }
        else
        {
            assert(!IsAfPublicKey(flags));
        }

        uint32_t majorVersion;
        if (!reader.read(Assembly_MajorVersion, &majorVersion))
            return import_helpers::unexpected{E_FAIL};

        uint32_t minorVersion;
        if (!reader.read(Assembly_MinorVersion, &minorVersion))
            return import_helpers::unexpected{E_FAIL};

        uint32_t buildNumber;
        if (!reader.read(Assembly_BuildNumber, &buildNumber))
            return import_helpers::unexpected{E_FAIL};

        uint32_t revisionNumber;
        if (!reader.read(Assembly_RevisionNumber, &revisionNumber))
            return import_helpers::unexpected{E_FAIL};

        char const* assemblyName;
        if (!reader.read(Assembly_Name, &assemblyName))
            return import_helpers::unexpected{E_FAIL};

        char const* assemblyCulture;
        if (!reader.read(Assembly_Culture, &assemblyCulture))
            return import_helpers::unexpected{E_FAIL};

        auto existing = FindAssemblyRef(
            targetModule,
            majorVersion,
            minorVersion,
            buildNumber,
            revisionNumber,
            flags,
            assemblyName,
            assemblyCulture,
            publicKeyTokenSpan);
        if (existing)
            return existing;
        if (existing.error() != S_FALSE)
            return import_helpers::unexpected{existing.error()};

        auto assemblyRef = dnmd::added_row<mdtid_AssemblyRef>::append(targetModule);
        if (!assemblyRef)
            return import_helpers::unexpected{E_FAIL};

        onRowAdded(assemblyRef.value());

        dnmd::row_writer writer{assemblyRef.value()};
        if (!writer.write(AssemblyRef_MajorVersion, majorVersion))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_MinorVersion, minorVersion))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_BuildNumber, buildNumber))
            return import_helpers::unexpected{E_FAIL};

        if (writer.write(AssemblyRef_RevisionNumber, revisionNumber))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_Flags, flags))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_Name, assemblyName))
            return import_helpers::unexpected{E_FAIL};

        if (!writer.write(AssemblyRef_Culture, assemblyCulture))
            return import_helpers::unexpected{E_FAIL};

        uint8_t const* hash = (uint8_t const*)sourceAssemblyHash;
        uint32_t hashLength = (uint32_t)sourceAssemblyHash.size();
        if (!writer.write(AssemblyRef_HashValue, hash, hashLength))
            return import_helpers::unexpected{E_FAIL};

        uint8_t const* publicKeyTokenBlob = publicKeyToken.data();
        uint32_t publicKeyTokenLength = (uint32_t)publicKeyToken.size();
        if (!writer.write(AssemblyRef_PublicKeyOrToken, publicKeyTokenBlob, publicKeyTokenLength))
            return import_helpers::unexpected{E_FAIL};

        return assemblyRef.position();
    }

    // Add a reference to sourceAssembly
    // in the AssemblyRef tables in targetModule and targetAssembly.
    // Returns the resulting cursor into targetModule's AssemblyRef table.
    cursor_result<dnmd::raw_cursor<mdtid_AssemblyRef>> ImportReferenceToAssembly(
        mdhandle_t sourceAssembly,
        span<const uint8_t> sourceAssemblyHash,
        mdhandle_t targetModule,
        mdhandle_t targetAssembly,
        std::function<void(dnmd::any_cursor)> onRowAdded)
    {
        dnmd::raw_cursor<mdtid_Assembly> importAssembly{sourceAssembly, TokenFromRid(1, mdtAssembly)};
        if (!importAssembly)
            return import_helpers::unexpected{E_FAIL};

        // Add a reference to the assembly in the target module.
        auto reference = ImportReferenceToAssembly(importAssembly, sourceAssemblyHash, targetModule, onRowAdded);
        if (!reference)
            return import_helpers::unexpected{reference.error()};

        // Also add a reference to the assembly in the target assembly.
        // In most cases, the target module will be the same as the target assembly, so this will be a no-op.
        // However, if the target module is a netmodule, then the target assembly will be the main assembly.
        // CoreCLR doesn't support multi-module assemblies, but they're still valid in ECMA-335.
        if (targetModule != targetAssembly)
        {
            auto assemblyReference = ImportReferenceToAssembly(importAssembly, sourceAssemblyHash, targetAssembly, onRowAdded);
            if (!assemblyReference)
                return import_helpers::unexpected{assemblyReference.error()};
        }

        return reference;
    }
}

cursor_result<dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef>> ImportReferenceToTypeDef(
    dnmd::raw_cursor<mdtid_TypeDef> sourceTypeDef,
    mdhandle_t sourceAssembly,
    span<const uint8_t> sourceAssemblyHash,
    mdhandle_t targetAssembly,
    mdhandle_t targetModule,
    bool alwaysImport,
    std::function<void(dnmd::any_cursor)> onRowAdded)
{
    HRESULT hr;
    mdhandle_t sourceModule = sourceTypeDef.get_handle();

    mdguid_t targetModuleMvid = {};
    mdguid_t targetAssemblyMvid = {};
    mdguid_t sourceAssemblyMvid = {};
    mdguid_t sourceModuleMvid = {};
    RETURN_UNEXPECTED_IF_FAILED(GetMvid(targetModule, &targetModuleMvid));
    RETURN_UNEXPECTED_IF_FAILED(GetMvid(targetAssembly, &targetAssemblyMvid));
    RETURN_UNEXPECTED_IF_FAILED(GetMvid(sourceModule, &sourceModuleMvid));
    RETURN_UNEXPECTED_IF_FAILED(GetMvid(sourceAssembly, &sourceAssemblyMvid));

    bool sameModuleMvid = std::memcmp(&targetModuleMvid, &sourceModuleMvid, sizeof(mdguid_t)) == 0;
    bool sameAssemblyMvid = std::memcmp(&targetAssemblyMvid, &sourceAssemblyMvid, sizeof(mdguid_t)) == 0;

    resolution_scope_cursor resolutionScope{targetModule, 0};
    if (sameAssemblyMvid && sameModuleMvid)
    {
        if (!alwaysImport)
        {
            // If we don't need to always import the TypeDef,
            // we can resolve it to an existing TypeDef.
            mdToken token;
            if (!sourceTypeDef.get_token(token))
                return import_helpers::unexpected{E_FAIL};

            dnmd::raw_cursor<mdtid_TypeDef> targetCursor{targetModule, token};

            // All images with the same MVID should have the same metadata tables.
            if (!targetCursor)
                return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

            return targetCursor;
        }
        resolutionScope = dnmd::md_create_cursor<mdtid_Module>(targetModule);
        if (!resolutionScope)
            return import_helpers::unexpected{E_FAIL};
    }
    else if (sameAssemblyMvid && !sameModuleMvid)
    {
        char const* importName;
        auto importModule = dnmd::md_create_cursor<mdtid_Module>(sourceModule);
        if (!importModule || !importModule.value().get_column_value(Module_Name, &importName))
        {
            return import_helpers::unexpected{E_FAIL};
        }

        auto moduleRef = dnmd::added_row<mdtid_ModuleRef>::append(targetModule);
        if (!moduleRef
            || !moduleRef.value().set_column_value(ModuleRef_Name, importName))
        {
            return import_helpers::unexpected{E_FAIL};
        }

        resolutionScope = moduleRef.position();
        onRowAdded(moduleRef.value());
    }
    else if (sameModuleMvid)
    {
        // The import can't be the same module and different assemblies.
        // COMPAT-BREAK: CoreCLR allows this for cases where there is no source assembly open, with a TODO from FX-era
        // relating to using a sample compiler from the .NET Framework SDK from before VS6.0.
        // This tool never shipped, so we don't need to account for this bug here.
        return import_helpers::unexpected{E_INVALIDARG};
    }
    else
    {
        auto reference = ImportReferenceToAssembly(sourceAssembly, sourceAssemblyHash, targetModule, targetAssembly, onRowAdded);
        if (!reference)
            return import_helpers::unexpected{reference.error()};
        resolutionScope = *reference;
    }

    try
    {
        std::stack<dnmd::raw_cursor<mdtid_TypeDef>> typesForTypeRefs;

        typesForTypeRefs.push(sourceTypeDef);

        auto nestedClasses = dnmd::md_create_cursor<mdtid_NestedClass>(sourceModule);
        mdToken nestedTypeToken;
        if (!sourceTypeDef.get_token(nestedTypeToken))
            return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};
        while (nestedClasses)
        {
            auto nestedClass = dnmd::md_find_row_from_cursor(
                nestedClasses.value(), NestedClass_NestedClass, nestedTypeToken);
            if (!nestedClass)
                break;
            dnmd::raw_cursor<mdtid_TypeDef> enclosingClass{sourceModule, 0};
            if (!nestedClass || !nestedClass.value().get_column_value(NestedClass_EnclosingClass, enclosingClass))
                return import_helpers::unexpected{E_FAIL};

            typesForTypeRefs.push(enclosingClass);
            if (!enclosingClass.get_token(nestedTypeToken))
                return import_helpers::unexpected{E_FAIL};
        }

        for (; !typesForTypeRefs.empty(); typesForTypeRefs.pop())
        {
            auto typeDef = typesForTypeRefs.top().as<mdtid_TypeDef>();
            auto typeRef = dnmd::added_row<mdtid_TypeRef>::append(targetModule);
            if (!typeRef)
                return import_helpers::unexpected{E_FAIL};

            dnmd::row_writer writer{typeRef.value()};
            if (!writer.write(TypeRef_ResolutionScope, resolutionScope))
                return import_helpers::unexpected{E_FAIL};

            if (!typeDef)
                return import_helpers::unexpected{E_FAIL};
            dnmd::row_reader reader{typeDef.value()};
            char const* typeName;
            if (!reader.read(TypeDef_TypeName, &typeName)
                || !writer.write(TypeRef_TypeName, typeName))
            {
                return import_helpers::unexpected{E_FAIL};
            }

            if (sourceModule == targetModule)
                reader = dnmd::row_reader{typeDef.value()};
            char const* typeNamespace;
            if (!reader.read(TypeDef_TypeNamespace, &typeNamespace)
                || !writer.write(TypeRef_TypeNamespace, typeNamespace))
            {
                return import_helpers::unexpected{E_FAIL};
            }

            resolutionScope = typeRef.position();
            onRowAdded(typeRef.value());
        }

        return resolutionScope.as<mdtid_TypeRef>();
    }
    catch (std::bad_alloc const&)
    {
        return import_helpers::unexpected{E_OUTOFMEMORY};
    }
}

namespace
{
    dnmd::raw_cursor<mdtid_ModuleRef> FindModuleRef(mdhandle_t image, char const* moduleName)
    {
        uint32_t count;
        auto c = dnmd::md_create_cursor<mdtid_ModuleRef>(image, &count);
        if (!c)
            return dnmd::raw_cursor<mdtid_ModuleRef>::nil(image);

        for (uint32_t i = 0; i < count; i++, c.move(1))
        {
            char const* name;
            if (!c || !c.value().get_column_value(ModuleRef_Name, &name))
                return dnmd::raw_cursor<mdtid_ModuleRef>::nil(image);

            if (std::strcmp(name, moduleName) == 0)
            {
                return c;
            }
        }

        return dnmd::raw_cursor<mdtid_ModuleRef>::nil(image);
    }

    // Given a type name and type namespace for a type in a (possibly multi-module) assembly,
    // import a reference to the type into the target module.
    // This function also handles type forwards into the target assembly.
    cursor_result<imported_scope_cursor> ImportScopeForTypeByNameInAssembly(
        char const* typeName,
        char const* typeNamespace,
        mdhandle_t module,
        mdhandle_t assembly,
        std::function<void(dnmd::any_cursor)> onRowAdded)
    {
        // Search the ExportedType table in the targetAssembly for a type with the given name or namespace.
        // An empty ExportedType table is okay.
        uint32_t count;
        auto exportedType = dnmd::md_create_cursor<mdtid_ExportedType>(assembly, &count);
        bool foundExportedType = false;
        if (exportedType)
        {
            for (uint32_t i = 0; i < count; ++i, exportedType.move(1))
            {
                if (!exportedType)
                    return import_helpers::unexpected{E_FAIL};
                dnmd::row_reader reader{exportedType.value()};
                char const* exportedTypeName;
                if (!reader.read(ExportedType_TypeName, &exportedTypeName))
                    return import_helpers::unexpected{E_FAIL};

                char const* exportedTypeNamespace;
                if (!reader.read(ExportedType_TypeNamespace, &exportedTypeNamespace))
                    return import_helpers::unexpected{E_FAIL};

                if (std::strcmp(typeName, exportedTypeName) == 0 && std::strcmp(typeNamespace, exportedTypeNamespace) == 0)
                {
                    foundExportedType = true;
                    break;
                }
            }
        }

        if (foundExportedType)
        {
            // If we found an ExportedType, then the type is defined in another module or is forwarded to another assembly.
            // We need to find the imported scope for the type.
            dnmd::raw_cursor<mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType> implementation{assembly, 0};
            if (!exportedType || !exportedType.value().get_column_value(ExportedType_Implementation, implementation))
                return import_helpers::unexpected{E_FAIL};

            cursor_result<imported_scope_cursor> result = import_helpers::unexpected{E_FAIL};
            if (!implementation.visit([&](auto row) {
                constexpr auto table = dnmd::cursor_tableid_v<decltype(row)>;
                // If the ExportedType.Implementation is a File:
                // - If the File refers to module's module, then we can use the module cursor in module as the imported scope.
                // - If the File refers to another module, then we'll create a ModuleRef to that module.
                if constexpr (table == mdtid_File)
                {
                    char const* fileName;
                    if (!row.get_column_value(File_Name, &fileName))
                        return;

                    dnmd::raw_cursor<mdtid_Module> moduleCursor{module, TokenFromRid(1, mdtModule)};
                    if (!moduleCursor)
                        return;

                    char const* moduleName;
                    if (!moduleCursor || !moduleCursor.value().get_column_value(Module_Name, &moduleName))
                        return;

                    if (std::strcmp(fileName, moduleName) == 0)
                    {
                        result = moduleCursor;
                    }
                    else if (auto existing = FindModuleRef(module, fileName))
                    {
                        result = existing;
                    }
                    else
                    {
                        auto moduleRef = dnmd::added_row<mdtid_ModuleRef>::append(module);
                        if (!moduleRef)
                            return;

                        if (!moduleRef.value().set_column_value(ModuleRef_Name, fileName))
                            return;

                        result = moduleRef.position();
                        onRowAdded(moduleRef.value());
                    }
                }
                // If the ExportedType.Implementation is an AssemblyRef, then we'll use that as the imported scope.
                // COMPAT-BREAK: CoreCLR does not support this case (it assumes that this ExportedType entry is never a type forwarder).
                else if constexpr (table == mdtid_AssemblyRef)
                {
                    auto reference = ImportReferenceToAssemblyRef(row, module, assembly, onRowAdded);
                    if (!reference)
                    {
                        result = import_helpers::unexpected{reference.error()};
                        return;
                    }
                    result = *reference;
                }
                // If the ExportedType.Implementation is an ExportedType, then we're in an error scenario.
                else if constexpr (table == mdtid_ExportedType)
                {
                    result = import_helpers::unexpected{E_FAIL};
                }
            }))
                return import_helpers::unexpected{E_FAIL};
            return result;
        }

        // If we couldn't find an ExportedType, then we need to search the TypeDef table in the assembly.
        // We must be able to find the type here, otherwise the metadata is invalid as we can't make a reference to a type we can't find.
        auto typeDef = dnmd::md_create_cursor<mdtid_TypeDef>(assembly, &count);
        if (!typeDef)
            return import_helpers::unexpected{E_FAIL};

        for (uint32_t i = 0; i < count; ++i, typeDef.move(1))
        {
            if (!typeDef)
                return import_helpers::unexpected{E_FAIL};
            dnmd::row_reader reader{typeDef.value()};
            char const* typeDefName;
            if (!reader.read(TypeDef_TypeName, &typeDefName))
                return import_helpers::unexpected{E_FAIL};

            char const* typeDefNamespace;
            if (!reader.read(TypeDef_TypeNamespace, &typeDefNamespace))
                return import_helpers::unexpected{E_FAIL};

            if (std::strcmp(typeName, typeDefName) == 0 && std::strcmp(typeNamespace, typeDefNamespace) == 0)
            {
                // Make sure that this type is not nested.
                // For this to be the same type, it must not be a nested type.
                auto nestedType = dnmd::md_create_cursor<mdtid_NestedClass>(assembly);
                if (!nestedType)
                    return import_helpers::unexpected{E_FAIL};

                mdToken typeDefToken;
                if (!typeDef.get_token(typeDefToken))
                    return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

                if (dnmd::md_find_row_from_cursor(nestedType.value(), NestedClass_NestedClass, typeDefToken))
                    return import_helpers::unexpected{E_FAIL};

                // If we found the type defined in the assembly, then the correct imported scope is the assembly module.
                // COMPAT-BREAK: CLR and CoreCLR always use the module token as the ResolutionScope here,
                // which is invalid as the type lives in the Assembly manifest module, which may not be the current module.
                // When the assembly module is the manifest module, it ends up being correct,
                // but when the assembly manifest module is a different module, the TypeRef will not resolve.
                dnmd::raw_cursor<mdtid_Module> assemblyModule{assembly, TokenFromRid(1, mdtModule)};
                if (!assemblyModule)
                    return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

                char const* assemblyModuleName;
                if (!assemblyModule || !assemblyModule.value().get_column_value(Module_Name, &assemblyModuleName))
                    return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

                dnmd::raw_cursor<mdtid_Module> moduleCursor{module, TokenFromRid(1, mdtModule)};
                if (!moduleCursor)
                    return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

                char const* moduleName;
                if (!moduleCursor || !moduleCursor.value().get_column_value(Module_Name, &moduleName))
                    return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

                if (std::strcmp(assemblyModuleName, moduleName) == 0)
                {
                    // If the assembly module has the same name as the current module,
                    // assume that the assembly manifest module is the same module as the current module.
                    return moduleCursor;
                }
                else if (auto existing = FindModuleRef(module, assemblyModuleName))
                {
                    return existing;
                }
                else
                {
                    auto moduleRef = dnmd::added_row<mdtid_ModuleRef>::append(module);
                    if (!moduleRef)
                        return import_helpers::unexpected{E_FAIL};

                    if (!moduleRef.value().set_column_value(ModuleRef_Name, assemblyModuleName))
                        return import_helpers::unexpected{E_FAIL};

                    onRowAdded(moduleRef.value());
                    return moduleRef.position();
                }
            }
        }
        return import_helpers::unexpected{CLDB_E_RECORD_NOTFOUND};
    }

    HRESULT AssemblyRefPointsToAssembly(
        dnmd::raw_cursor<mdtid_AssemblyRef> assemblyRef,
        dnmd::raw_cursor<mdtid_Assembly> assembly)
    {
        HRESULT hr;
        auto refRow = assemblyRef.as<mdtid_AssemblyRef>();
        auto assemblyRow = assembly.as<mdtid_Assembly>();
        if (!refRow || !assemblyRow)
            return CLDB_E_FILE_CORRUPT;
        dnmd::row_reader refReader{refRow.value()};
        dnmd::row_reader assemblyReader{assemblyRow.value()};
        // Compare version, Name, Locale, and PublicKeyOrToken (possibly creating token from the assembly's key if needed)
        uint32_t refMajorVersion;
        if (!refReader.read(AssemblyRef_MajorVersion, &refMajorVersion))
            return CLDB_E_FILE_CORRUPT;

        uint32_t majorVersion;
        if (!assemblyReader.read(Assembly_MajorVersion, &majorVersion))
            return CLDB_E_FILE_CORRUPT;

        if (refMajorVersion != majorVersion)
            return S_FALSE;

        uint32_t refMinorVersion;
        if (!refReader.read(AssemblyRef_MinorVersion, &refMinorVersion))
            return CLDB_E_FILE_CORRUPT;

        uint32_t minorVersion;
        if (!assemblyReader.read(Assembly_MinorVersion, &minorVersion))
            return CLDB_E_FILE_CORRUPT;

        if (refMinorVersion != minorVersion)
            return S_FALSE;

        uint32_t refBuildNumber;
        if (!refReader.read(AssemblyRef_BuildNumber, &refBuildNumber))
            return CLDB_E_FILE_CORRUPT;

        uint32_t buildNumber;
        if (!assemblyReader.read(Assembly_BuildNumber, &buildNumber))
            return CLDB_E_FILE_CORRUPT;

        if (refBuildNumber != buildNumber)
            return S_FALSE;

        uint32_t refRevisionNumber;
        if (!refReader.read(AssemblyRef_RevisionNumber, &refRevisionNumber))
            return CLDB_E_FILE_CORRUPT;

        uint32_t revisionNumber;
        if (!assemblyReader.read(Assembly_RevisionNumber, &revisionNumber))
            return CLDB_E_FILE_CORRUPT;

        if (refRevisionNumber != revisionNumber)
            return S_FALSE;

        char const* refName;
        if (!refReader.read(AssemblyRef_Name, &refName))
            return CLDB_E_FILE_CORRUPT;

        char const* name;
        if (!assemblyReader.read(Assembly_Name, &name))
            return CLDB_E_FILE_CORRUPT;

        if (std::strcmp(refName, name) != 0)
            return S_FALSE;

        char const* refCulture;
        if (!refReader.read(AssemblyRef_Culture, &refCulture))
            return CLDB_E_FILE_CORRUPT;

        char const* culture;
        if (!assemblyReader.read(Assembly_Culture, &culture))
            return CLDB_E_FILE_CORRUPT;

        if (std::strcmp(refCulture, culture) != 0)
            return S_FALSE;

        uint8_t const* refPublicKeyOrToken;
        uint32_t refPublicKeyOrTokenLength;
        if (!refReader.read(AssemblyRef_PublicKeyOrToken, &refPublicKeyOrToken, &refPublicKeyOrTokenLength))
            return CLDB_E_FILE_CORRUPT;

        uint8_t const* publicKey;
        uint32_t publicKeyLength;
        if (!assemblyReader.read(Assembly_PublicKey, &publicKey, &publicKeyLength))
            return CLDB_E_FILE_CORRUPT;

        if ((refPublicKeyOrTokenLength == 0) != (publicKeyLength == 0))
            return S_FALSE;

        if (refPublicKeyOrTokenLength != 0)
        {
            uint32_t refFlags;
            if (!refReader.read(AssemblyRef_Flags, &refFlags))
                return CLDB_E_FILE_CORRUPT;

            if (IsAfPublicKey(refFlags))
            {
                // If we have a full public key for the reference, then we can compare the full public key.
                if (refPublicKeyOrTokenLength != publicKeyLength || std::memcmp(refPublicKeyOrToken, publicKey, publicKeyLength) != 0)
                    return S_FALSE;

                return S_OK;
            }

            StrongNameToken asmPublicKeyToken;
            RETURN_IF_FAILED(StrongNameTokenFromPublicKey({ publicKey, publicKeyLength }, asmPublicKeyToken));

            if (refPublicKeyOrTokenLength != asmPublicKeyToken.size() || !std::equal(asmPublicKeyToken.begin(), asmPublicKeyToken.end(), refPublicKeyOrToken))
                return S_FALSE;

            return S_OK;
        }
        return S_OK;
    }

    cursor_result<dnmd::raw_cursor<mdtid_TypeDef, mdtid_TypeRef>> ImportReferenceToTypeRef(
        dnmd::raw_cursor<mdtid_TypeRef> sourceTypeRef,
        mdhandle_t sourceAssembly,
        span<const uint8_t> sourceAssemblyHash,
        mdhandle_t targetAssembly,
        mdhandle_t targetModule,
        std::function<void(dnmd::any_cursor)> onRowAdded)
    {
        assert(sourceAssembly != nullptr && targetAssembly != nullptr && targetModule != nullptr);

        HRESULT hr;
        std::stack<resolution_scope_cursor> typesForTypeRefs;
        typesForTypeRefs.push(sourceTypeRef);

        source_scope_cursor scope = sourceTypeRef;
        while (scope.is<mdtid_TypeRef>())
        {
            auto typeRefScope = scope.as<mdtid_TypeRef>();
            dnmd::raw_cursor<mdtid_Module, mdtid_ModuleRef, mdtid_AssemblyRef, mdtid_TypeRef> resolutionScope{scope.get_handle(), 0};
            if (!typeRefScope || !typeRefScope.value().get_column_value(TypeRef_ResolutionScope, resolutionScope))
                return import_helpers::unexpected{E_FAIL};

            typesForTypeRefs.push(resolutionScope);
            scope = resolutionScope;
        }

        mdhandle_t sourceModule = sourceTypeRef.get_handle();
        mdguid_t targetModuleMvid = {};
        mdguid_t targetAssemblyMvid = {};
        mdguid_t sourceAssemblyMvid = {};
        mdguid_t sourceModuleMvid = {};
        RETURN_UNEXPECTED_IF_FAILED(GetMvid(targetModule, &targetModuleMvid));
        RETURN_UNEXPECTED_IF_FAILED(GetMvid(targetAssembly, &targetAssemblyMvid));
        RETURN_UNEXPECTED_IF_FAILED(GetMvid(sourceModule, &sourceModuleMvid));
        RETURN_UNEXPECTED_IF_FAILED(GetMvid(sourceAssembly, &sourceAssemblyMvid));

        bool sameModuleMvid = std::memcmp(&targetModuleMvid, &sourceModuleMvid, sizeof(mdguid_t)) == 0;
        bool sameAssemblyMvid = std::memcmp(&targetAssemblyMvid, &sourceAssemblyMvid, sizeof(mdguid_t)) == 0;

        // II.22.38 1. Valid ResolutionScope values
        // - null
        // - TypeRef token
        // - ModuleRef token
        // - Module token
        // - AssemblyRef token
        resolution_scope_cursor targetOutermostScope{targetModule, 0};
        if (sameAssemblyMvid && sameModuleMvid)
        {
            mdToken token;
            if (!sourceTypeRef.get_token(token))
                return import_helpers::unexpected{E_FAIL};

            dnmd::raw_cursor<mdtid_TypeRef> typeRef{targetModule, token};

            if (!typeRef)
                return import_helpers::unexpected{CLDB_E_FILE_CORRUPT};

            return typeRef;
        }
        else if (sameAssemblyMvid && !sameModuleMvid)
        {
            if (scope.is_nil())
            {
                // A Nil ResolutionScope means a reference to an ExportedType entry
                // in the assembly.
                // Since the source and target assemblies have the same identity,
                // we can use the Nil token and we don't have to resolve the ExportedType
                // as the target and source assemblies are the same.
                targetOutermostScope = resolution_scope_cursor::nil(targetModule);
            }
            else
            {
                HRESULT result = E_INVALIDARG;
                if (!scope.visit([&](auto row) {
                    constexpr auto table = dnmd::cursor_tableid_v<decltype(row)>;
                    if constexpr (table == mdtid_Module)
                    {
                        // Create a ModuleRef from the target module to the source module.
                        char const* moduleName;
                        result = CLDB_E_FILE_CORRUPT;
                        if (!row.get_column_value(Module_Name, &moduleName))
                            return;

                        result = E_FAIL;
                        if (auto existing = FindModuleRef(targetModule, moduleName))
                        {
                            targetOutermostScope = existing;
                        }
                        else
                        {
                            auto moduleRef = dnmd::added_row<mdtid_ModuleRef>::append(targetModule);
                            if (!moduleRef)
                                return;

                            if (!moduleRef.value().set_column_value(ModuleRef_Name, moduleName))
                                return;

                            targetOutermostScope = moduleRef.position();
                            onRowAdded(moduleRef.value());
                        }
                        result = S_OK;
                    }
                    else if constexpr (table == mdtid_ModuleRef)
                    {
                        // If this ModuleRef points from the source module into the target module,
                        // then we can use the Module token as the outermost scope.
                        // otherwise, create ModuleRef to the module that the source ModuleRef points to.
                        char const* moduleName;
                        result = CLDB_E_FILE_CORRUPT;
                        if (!row.get_column_value(ModuleRef_Name, &moduleName))
                            return;

                        result = E_FAIL;
                        auto targetModuleCursor = dnmd::md_create_cursor<mdtid_Module>(targetModule);
                        if (!targetModuleCursor)
                            return;

                        char const* targetModuleName;
                        if (!targetModuleCursor.value().get_column_value(Module_Name, &targetModuleName))
                            return;

                        if (std::strcmp(moduleName, targetModuleName) == 0)
                        {
                            targetOutermostScope = targetModuleCursor;
                        }
                        else if (auto existing = FindModuleRef(targetModule, moduleName))
                        {
                            targetOutermostScope = existing;
                        }
                        else
                        {
                            auto moduleRef = dnmd::added_row<mdtid_ModuleRef>::append(targetModule);
                            if (!moduleRef)
                                return;

                            if (!moduleRef.value().set_column_value(ModuleRef_Name, moduleName))
                                return;

                            targetOutermostScope = moduleRef.position();
                            onRowAdded(moduleRef.value());
                        }
                        result = S_OK;
                    }
                    else if constexpr (table == mdtid_AssemblyRef)
                    {
                        // Copy the AssemblyRef from the source module to the target module.
                        auto reference = ImportReferenceToAssemblyRef(row, targetModule, targetAssembly, onRowAdded);
                        if (!reference)
                        {
                            result = reference.error();
                            return;
                        }
                        result = S_OK;
                        targetOutermostScope = *reference;
                    }
                }))
                {
                    if (scope.is<mdtid_Module>() || scope.is<mdtid_ModuleRef>())
                        result = CLDB_E_FILE_CORRUPT;
                    else if (scope.is<mdtid_AssemblyRef>())
                        result = E_FAIL;
                }
                RETURN_UNEXPECTED_IF_FAILED(result);
            }
        }
        else if (!sameAssemblyMvid)
        {
            assert(!sameModuleMvid);

            auto importSourceAssembly = [&]() -> HRESULT {
                auto reference = ImportReferenceToAssembly(sourceAssembly, sourceAssemblyHash, targetModule, targetAssembly, onRowAdded);
                if (!reference)
                    return reference.error();
                targetOutermostScope = *reference;
                return S_OK;
            };

            if (scope.is_nil())
            {
                // Lookup ExportedType entry in the source assembly for this type.
                uint32_t count;
                auto exportedType = dnmd::md_create_cursor<mdtid_ExportedType>(sourceAssembly, &count);
                bool foundExportedType = false;
                dnmd::raw_cursor<mdtid_File, mdtid_AssemblyRef, mdtid_ExportedType> implementation{sourceAssembly, 0};
                if (exportedType)
                {
                    auto outermostTypeRef = typesForTypeRefs.top().as<mdtid_TypeRef>();
                    if (!outermostTypeRef)
                        return import_helpers::unexpected{E_FAIL};
                    dnmd::row_reader reader{outermostTypeRef.value()};
                    char const* typeName;
                    if (!reader.read(TypeRef_TypeName, &typeName))
                        return import_helpers::unexpected{E_FAIL};

                    char const* typeNamespace;
                    if (!reader.read(TypeRef_TypeNamespace, &typeNamespace))
                        return import_helpers::unexpected{E_FAIL};

                    // If we can't find an ExportedType entry for this type, we'll just move over the TypeRef with a Nil ResolutionScope.
                    for (uint32_t i = 0; i < count; ++i, exportedType.move(1))
                    {
                        if (!exportedType)
                            return import_helpers::unexpected{E_FAIL};
                        dnmd::row_reader exportedTypeReader{exportedType.value()};
                        char const* exportedTypeName;
                        if (!exportedTypeReader.read(ExportedType_TypeName, &exportedTypeName))
                            return import_helpers::unexpected{E_FAIL};

                        char const* exportedTypeNamespace;
                        if (!exportedTypeReader.read(ExportedType_TypeNamespace, &exportedTypeNamespace))
                            return import_helpers::unexpected{E_FAIL};

                        if (std::strcmp(typeName, exportedTypeName) == 0 && std::strcmp(typeNamespace, exportedTypeNamespace) == 0)
                        {
                            if (!exportedTypeReader.read(ExportedType_Implementation, implementation))
                                return import_helpers::unexpected{E_FAIL};

                            foundExportedType = true;
                            break;
                        }
                    }
                }

                if (foundExportedType)
                {
                    HRESULT result = E_FAIL;
                    if (!implementation.visit([&](auto row) {
                        constexpr auto table = dnmd::cursor_tableid_v<decltype(row)>;
                        if constexpr (table == mdtid_File)
                        {
                            // This type is from a file in the source assembly, so we need to create an AssemblyRef to the source assembly.
                            result = importSourceAssembly();
                            if (FAILED(result))
                                return;
                            scope = row;
                        }
                        else if constexpr (table == mdtid_AssemblyRef)
                        {
                            // This type is a type-forward from another assembly.
                            // Set the scope to this AssemblyRef.
                            // If this is a type forward from the target assembly, we want to resolve it to the target assembly to avoid a self-referential
                            // AssemblyRef.
                            scope = row;
                            result = S_OK;
                        }
                        else if constexpr (table == mdtid_ExportedType)
                        {
                            assert(false && "We should be looking at the outermost type already. Therefore, the ExportedType entry for this type should not be enclosed in another type.");
                            result = E_FAIL;
                        }
                    }))
                        return import_helpers::unexpected{E_FAIL};
                    RETURN_UNEXPECTED_IF_FAILED(result);
                }
                else
                {
                    // COMPAT-BREAK: CoreCLR and CLR treat a type that is not found in the ExportedType table as though it is an imported type from the target assembly.
                    // This is incorrect per the spec as the type is not defined in the target assembly.
                    // Early .NET compilers wouldn't always have an AssemblyRef to the core library (mscorlib), so we could end up in a situation where we'd be importing
                    // a TypeRef to mscorlib from a module that doesn't have a reference to mscorlib.
                    // In this case, this branch would treat the ResolutionScope as the Nil token.
                    // Nowadays, all of the managed code compilers correctly emit references to all types, including the core library (which in many cases now is not mscorlib).
                    // Additionally, multimodule assemblies aren't supported by CoreCLR, so we won't even reach this branch anyway (the nil-scope branch will only happen in multimodule scenarios).

                    // If we can't find the type in the source assembly, then we can't import it as we can't find the definition anywhere.
                    uint32_t sourceAssemblyTypeDefCount;
                    auto sourceAssemblyTypeDef = dnmd::md_create_cursor<mdtid_TypeDef>(
                        sourceAssembly, &sourceAssemblyTypeDefCount);
                    if (!sourceAssemblyTypeDef)
                        return import_helpers::unexpected{E_FAIL};

                    auto outermostTypeRef = typesForTypeRefs.top().as<mdtid_TypeRef>();
                    if (!outermostTypeRef)
                        return import_helpers::unexpected{E_FAIL};
                    dnmd::row_reader reader{outermostTypeRef.value()};
                    char const* typeName;
                    if (!reader.read(TypeRef_TypeName, &typeName))
                        return import_helpers::unexpected{E_FAIL};

                    char const* typeNamespace;
                    if (!reader.read(TypeRef_TypeNamespace, &typeNamespace))
                        return import_helpers::unexpected{E_FAIL};

                    bool found = false;
                    for (uint32_t i = 0; i < sourceAssemblyTypeDefCount; ++i, sourceAssemblyTypeDef.move(1))
                    {
                        if (!sourceAssemblyTypeDef)
                            return import_helpers::unexpected{E_FAIL};
                        dnmd::row_reader sourceTypeDefReader{sourceAssemblyTypeDef.value()};
                        char const* sourceAssemblyTypeDefName;
                        if (!sourceTypeDefReader.read(TypeDef_TypeName, &sourceAssemblyTypeDefName))
                            return import_helpers::unexpected{E_FAIL};

                        char const* sourceAssemblyTypeDefNamespace;
                        if (!sourceTypeDefReader.read(TypeDef_TypeNamespace, &sourceAssemblyTypeDefNamespace))
                            return import_helpers::unexpected{E_FAIL};

                        if (std::strcmp(typeName, sourceAssemblyTypeDefName) != 0 && std::strcmp(typeNamespace, sourceAssemblyTypeDefNamespace) != 0)
                            continue;

                        auto sourceAssemblyTypeDefEnclosingClass = dnmd::md_create_cursor<mdtid_NestedClass>(sourceAssembly);
                        if (sourceAssemblyTypeDefEnclosingClass
                            && dnmd::md_find_row_from_cursor(sourceAssemblyTypeDefEnclosingClass.value(),
                                NestedClass_NestedClass, TokenFromRid(i + 1, mdtTypeDef)))
                        {
                            // If the type is nested, then it can't be the right type as we're already at the outermost scope.
                            continue;
                        }

                        // If we found the type defined in the source assembly, then the correct imported scope is the assembly module.
                        dnmd::raw_cursor<mdtid_Assembly> importAssembly{sourceAssembly, TokenFromRid(1, mdtAssembly)};
                        if (!importAssembly)
                            return import_helpers::unexpected{E_FAIL};

                        // Add a reference to the assembly in the target module and assembly.
                        RETURN_UNEXPECTED_IF_FAILED(importSourceAssembly());
                        found = true;
                        break;
                    }

                    if (!found)
                        return import_helpers::unexpected{CLDB_E_RECORD_NOTFOUND};
                }
            }
            else if (scope.is<mdtid_Module>())
            {
                // Create an AssemblyRef from the destination assembly to the source assembly.
                RETURN_UNEXPECTED_IF_FAILED(importSourceAssembly());
            }

            // A nil scope can resolve through an ExportedType to an AssemblyRef.
            HRESULT result = E_FAIL;
            if (!scope.visit([&](auto row) {
                constexpr auto table = dnmd::cursor_tableid_v<decltype(row)>;
                if constexpr (table == mdtid_AssemblyRef)
                {
                    // Convert from AssemblyRef to Assembly if the source AssemblyRef points to the target assembly.
                    auto targetAssemblyCursor = dnmd::md_create_cursor<mdtid_Assembly>(targetModule);
                    if (!targetAssemblyCursor)
                        return;

                    HRESULT match = AssemblyRefPointsToAssembly(row, targetAssemblyCursor);
                    if (FAILED(match))
                    {
                        result = match;
                        return;
                    }
                    if (match == S_OK)
                    {
                        // The type is defined in the target assembly, so we need to correctly define its scope.
                        auto outermostTypeRef = typesForTypeRefs.top().as<mdtid_TypeRef>();
                        if (!outermostTypeRef)
                            return;
                        dnmd::row_reader reader{outermostTypeRef.value()};
                        char const* typeName;
                        if (!reader.read(TypeRef_TypeName, &typeName))
                            return;

                        char const* typeNamespace;
                        if (!reader.read(TypeRef_TypeNamespace, &typeNamespace))
                            return;

                        auto importedScope = ImportScopeForTypeByNameInAssembly(
                            typeName,
                            typeNamespace,
                            targetModule,
                            targetAssembly,
                            onRowAdded);
                        if (!importedScope)
                        {
                            result = importedScope.error();
                            return;
                        }
                        result = S_OK;
                        targetOutermostScope = *importedScope;
                    }
                    else
                    {
                        // The type is defined in another assembly. We need to create an AssemblyRef to that assembly.
                        assert(match == S_FALSE);
                        auto reference = ImportReferenceToAssemblyRef(row, targetModule, targetAssembly, onRowAdded);
                        if (!reference)
                        {
                            result = reference.error();
                            return;
                        }
                        result = S_OK;
                        targetOutermostScope = *reference;
                    }
                }
                else if constexpr (table == mdtid_ModuleRef)
                {
                    // Module references are only valid within assembly boundaries,
                    // so reference the source assembly instead of its other module.
                    result = importSourceAssembly();
                }
            }))
            {
                if (scope.is<mdtid_ModuleRef>())
                    result = importSourceAssembly();
                else if (scope.is<mdtid_AssemblyRef>())
                    result = dnmd::md_create_cursor<mdtid_Assembly>(targetModule) ? CLDB_E_FILE_CORRUPT : E_FAIL;
            }
            RETURN_UNEXPECTED_IF_FAILED(result);
        }

        assert(targetOutermostScope.get_handle() == targetModule);

        if (targetOutermostScope.is<mdtid_Module>() && !targetOutermostScope.is_nil())
        {
            // Find a nested TypeDef in the target module that matches the name and namespace of the source TypeRef.
            // We've resolved the TypeRef's outermost scope to be in the target module,
            // so the TypeDef must be in the target module.
            dnmd::raw_cursor<mdtid_Module, mdtid_TypeDef> enclosingScope{targetOutermostScope};

            auto targetTypeDef = dnmd::md_create_cursor<mdtid_TypeDef>(targetModule);
            if (!targetTypeDef)
                return import_helpers::unexpected{E_FAIL};
            for (; !typesForTypeRefs.empty(); typesForTypeRefs.pop())
            {
                auto sourceEnclosingTypeRef = typesForTypeRefs.top().as<mdtid_TypeRef>();
                if (!sourceEnclosingTypeRef)
                    return import_helpers::unexpected{E_FAIL};
                dnmd::row_reader reader{sourceEnclosingTypeRef.value()};

                char const* typeName;
                if (!reader.read(TypeRef_TypeName, &typeName))
                    return import_helpers::unexpected{E_FAIL};

                char const* typeNamespace;
                if (!reader.read(TypeRef_TypeNamespace, &typeNamespace))
                    return import_helpers::unexpected{E_FAIL};

                mdToken enclosingScopeToken;
                if (!enclosingScope.get_token(enclosingScopeToken))
                    return import_helpers::unexpected{E_FAIL};

                bool shouldHaveEnclosingType = enclosingScope.is<mdtid_TypeDef>() && !enclosingScope.is_nil();
                // The TypeDef table must be sorted such that enclosing types are defined before nesting types.
                // Therefore, we can search the table linearly.
                // See the commentary in II.22 before II.22.1 for more information.
                bool found = false;
                do
                {
                    if (!targetTypeDef)
                        return import_helpers::unexpected{E_FAIL};
                    dnmd::row_reader targetTypeDefReader{targetTypeDef.value()};
                    char const* targetTypeName;
                    if (!targetTypeDefReader.read(TypeDef_TypeName, &targetTypeName))
                        return import_helpers::unexpected{E_FAIL};

                    char const* targetTypeNamespace;
                    if (!targetTypeDefReader.read(TypeDef_TypeNamespace, &targetTypeNamespace))
                        return import_helpers::unexpected{E_FAIL};

                    // Check the name of the type.
                    if (std::strcmp(typeName, targetTypeName) != 0 || std::strcmp(typeNamespace, targetTypeNamespace) != 0)
                        continue;

                    // Now that we've validated that the target TypeDef has an enclosing type,
                    // we need to validate that the enclosing type matches the source TypeRef's enclosing type.
                    mdToken targetTypeDefToken;
                    if (!targetTypeDef.get_token(targetTypeDefToken))
                        return import_helpers::unexpected{E_FAIL};

                    auto targetNestedClass = dnmd::md_create_cursor<mdtid_NestedClass>(targetModule);
                    if (shouldHaveEnclosingType !=
                        (targetNestedClass
                            && (targetNestedClass = dnmd::md_find_row_from_cursor(targetNestedClass.value(),
                                NestedClass_NestedClass, targetTypeDefToken))))
                    {
                        // If the source TypeRef has an enclosing type, then the target TypeDef must have an enclosing type and vice versa.
                        continue;
                    }

                    if (shouldHaveEnclosingType)
                    {
                        mdToken targetEnclosingType;
                        if (!targetNestedClass || !targetNestedClass.value().get_column_value(NestedClass_EnclosingClass, targetEnclosingType))
                            return import_helpers::unexpected{E_FAIL};

                        // If the enclosing type doesn't match, then we are in a failure state.
                        if (enclosingScopeToken != targetTypeDefToken)
                            return import_helpers::unexpected{CLDB_E_RECORD_NOTFOUND};
                    }

                    found = true;
                    break;
                } while (targetTypeDef.move(1));

                if (!found)
                    return import_helpers::unexpected{CLDB_E_RECORD_NOTFOUND};

                enclosingScope = targetTypeDef;
            }
            return enclosingScope.as<mdtid_TypeDef>();
        }

        resolution_scope_cursor resolutionScope = targetOutermostScope;
        for (; !typesForTypeRefs.empty(); typesForTypeRefs.pop())
        {
            auto sourceEnclosingTypeRef = typesForTypeRefs.top().as<mdtid_TypeRef>();
            auto targetEnclosingTypeRef = dnmd::added_row<mdtid_TypeRef>::append(targetModule);
            if (!targetEnclosingTypeRef)
                return import_helpers::unexpected{E_FAIL};

            dnmd::row_writer writer{targetEnclosingTypeRef.value()};
            if (!writer.write(TypeRef_ResolutionScope, resolutionScope))
                return import_helpers::unexpected{E_FAIL};

            if (!sourceEnclosingTypeRef)
                return import_helpers::unexpected{E_FAIL};
            dnmd::row_reader reader{sourceEnclosingTypeRef.value()};
            char const* typeName;
            if (!reader.read(TypeRef_TypeName, &typeName)
                || !writer.write(TypeRef_TypeName, typeName))
            {
                return import_helpers::unexpected{E_FAIL};
            }

            if (sourceModule == targetModule)
                reader = dnmd::row_reader{sourceEnclosingTypeRef.value()};
            char const* typeNamespace;
            if (!reader.read(TypeRef_TypeNamespace, &typeNamespace)
                || !writer.write(TypeRef_TypeNamespace, typeNamespace))
            {
                return import_helpers::unexpected{E_FAIL};
            }

            resolutionScope = targetEnclosingTypeRef.position();
            onRowAdded(targetEnclosingTypeRef.value());
        }

        return resolutionScope.as<mdtid_TypeRef>();
    }
}

HRESULT ImportReferenceToTypeDefOrRefOrSpec(
    mdhandle_t sourceAssembly,
    mdhandle_t sourceModule,
    span<const uint8_t> sourceAssemblyHash,
    mdhandle_t targetAssembly,
    mdhandle_t targetModule,
    std::function<void(dnmd::any_cursor)> onRowAdded,
    mdToken* importedToken)
{
    HRESULT hr;
    if (!dnmd::md_is_valid_token(sourceModule, *importedToken))
        return CLDB_E_FILE_CORRUPT;

    switch (ExtractTokenType(*importedToken))
    {
        case mdtid_TypeDef:
        {
            auto targetCursor = ImportReferenceToTypeDef(dnmd::raw_cursor<mdtid_TypeDef>{sourceModule, *importedToken}, sourceAssembly, sourceAssemblyHash, targetAssembly, targetModule, true, onRowAdded);
            if (!targetCursor)
                return targetCursor.error();
            if (!targetCursor->get_token(*importedToken))
                return E_FAIL;

            return S_OK;
        }
        case mdtid_TypeRef:
        {
            auto targetCursor = ImportReferenceToTypeRef(dnmd::raw_cursor<mdtid_TypeRef>{sourceModule, *importedToken}, sourceAssembly, sourceAssemblyHash, targetAssembly, targetModule, onRowAdded);
            if (!targetCursor)
                return targetCursor.error();
            if (!targetCursor->get_token(*importedToken))
                return E_FAIL;

            return S_OK;
        }
        case mdtid_TypeSpec:
        {
            auto sourceTypeSpec = dnmd::raw_cursor<mdtid_TypeSpec>{sourceModule, *importedToken};
            uint8_t const* signature;
            uint32_t signatureLength;
            if (!sourceTypeSpec || !sourceTypeSpec.value().get_column_value(TypeSpec_Signature, &signature, &signatureLength))
                return E_FAIL;

            inline_span<uint8_t> importedSignature;
            RETURN_IF_FAILED(ImportTypeSpecBlob(sourceAssembly, sourceModule, sourceAssemblyHash, targetAssembly, targetModule, {signature, signatureLength}, onRowAdded, importedSignature));

            auto typeSpec = dnmd::added_row<mdtid_TypeSpec>::append(targetModule);
            if (!typeSpec)
                return E_FAIL;

            uint8_t const* importedSignatureData = (uint8_t const*)importedSignature;
            uint32_t importedSignatureLength = (uint32_t)importedSignature.size();
            if (!typeSpec.value().set_column_value(TypeSpec_Signature, importedSignatureData, importedSignatureLength))
                return E_FAIL;

            if (!typeSpec.get_token(*importedToken))
                return E_FAIL;

            return S_OK;
        }
        default:
            return E_INVALIDARG;
    }
}

HRESULT DefineImportMember(
    IMetaDataEmit* emit,                // [In] Module into which the Member is imported.
    IMetaDataAssemblyImport *pAssemImport,  // [IN] Assembly containing the Member.
    void const  *pbHashValue,           // [IN] Hash Blob for Assembly.
    ULONG        cbHashValue,           // [IN] Count of bytes.
    IMetaDataImport *pImport,           // [IN] Import scope, with member.
    mdToken     mbMember,               // [IN] Member in import scope.
    IMetaDataAssemblyEmit *pAssemEmit,  // [IN] Assembly into which the Member is imported.
    mdToken     tkImport,               // [IN] Classref or classdef in emit scope.
    mdMemberRef *pmr)                   // [OUT] Put member ref here.
{
    HRESULT hr;
    assert(pImport && pmr);
    assert(ExtractTokenType(tkImport) == mdtid_TypeRef || ExtractTokenType(tkImport) == mdtid_ModuleRef ||
                IsNilToken(tkImport) || ExtractTokenType(tkImport) == mdtid_TypeSpec);
    assert((ExtractTokenType(mbMember) == mdtid_MethodDef && mbMember != mdMethodDefNil) ||
             (ExtractTokenType(mbMember) == mdtid_Field && mbMember != mdFieldDefNil));

    size_t memberNameSize = 128;
    std::unique_ptr<WCHAR[]> memberName { new WCHAR[memberNameSize] }; // Name of the imported member.
    GUID        mvidImport;             // MVID of the import module.
    GUID        mvidEmit;               // MVID of the emit module.
    PCCOR_SIGNATURE pvSig;              // Member's signature.
    ULONG       cbSig;                  // Length of member's signature.
    ULONG       translatedSigLength;        // Length of translated signature.

    if (ExtractTokenType(mbMember) == mdtid_MethodDef)
    {
        ULONG acutalNameLength;
        for (;;)
        {
            hr = pImport->GetMethodProps(mbMember, nullptr, memberName.get(), (DWORD)memberNameSize, &acutalNameLength,
                nullptr, &pvSig, &cbSig, nullptr, nullptr);
            if (hr == CLDB_S_TRUNCATION)
            {
                memberName.reset(new WCHAR[acutalNameLength]);
                memberNameSize = (size_t)acutalNameLength;
                continue;
            }
            break;
        }
    }
    else    // ExtractTokenType(mbMember) == mdtid_Field
    {
        ULONG acutalNameLength;
        for (;;)
        {
            hr = pImport->GetMethodProps(mbMember, nullptr, memberName.get(),(DWORD)memberNameSize, &acutalNameLength,
                nullptr, &pvSig,&cbSig, nullptr, nullptr);
            if (hr == CLDB_S_TRUNCATION)
            {
                memberName.reset(new WCHAR[acutalNameLength]);
                memberNameSize = (size_t)acutalNameLength;
                continue;
            }
            break;
        }
    }
    RETURN_IF_FAILED(hr);

    ULONG sigSizeMax = cbSig * 3;  // Set translated signature buffer size conservatively.
    std::unique_ptr<uint8_t[]> translatedSig { new uint8_t[sigSizeMax] };

    RETURN_IF_FAILED(emit->TranslateSigWithScope(
        pAssemImport,
        pbHashValue,
        cbHashValue,
        pImport,
        pvSig,
        cbSig,
        pAssemEmit,
        emit,
        translatedSig.get(),
        sigSizeMax,
        &translatedSigLength));

    // Define ModuleRef for imported Member functions

    // Check if the Member being imported is a global function.
    dncp::com_ptr<IMetaDataImport> pEmitImport;
    RETURN_IF_FAILED(emit->QueryInterface(IID_IMetaDataImport, (void**)&pEmitImport));
    RETURN_IF_FAILED(pEmitImport->GetScopeProps(nullptr, 0, nullptr, &mvidEmit));

    DWORD scopeNameSize;
    RETURN_IF_FAILED(pImport->GetScopeProps(nullptr, 0, &scopeNameSize, &mvidImport));
    if (mvidEmit != mvidImport && IsNilToken(tkImport))
    {
        std::unique_ptr<WCHAR[]> scopeName { new WCHAR[scopeNameSize] }; // Name of the imported member's scope.
        RETURN_IF_FAILED(pImport->GetScopeProps(scopeName.get(), scopeNameSize,
                                        nullptr, nullptr));
        RETURN_IF_FAILED(emit->DefineModuleRef(scopeName.get(), &tkImport));
    }

    // Define MemberRef base on the name, sig, and parent
    RETURN_IF_FAILED(emit->DefineMemberRef(
        tkImport,
        memberName.get(),
        translatedSig.get(),
        translatedSigLength,
        pmr));

    return S_OK;
}
