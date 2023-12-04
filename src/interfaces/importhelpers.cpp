#include "importhelpers.hpp"
#include "pal.hpp"
#include <cassert>
#include <stack>
#include <algorithm>
#include <cctype>

// ALG_ID crackers
#define GET_ALG_CLASS(x)                (x & (7 << 13))
#define GET_ALG_SID(x)                  (x & (511))

#define ALG_CLASS_SIGNATURE             (1 << 13)
#define ALG_CLASS_HASH                  (4 << 13)

#define ALG_SID_SHA1                    4

#define PUBLICKEYBLOB           0x6

#define RETURN_IF_FAILED(exp) \
{ \
    hr = (exp); \
    if (FAILED(hr)) \
    { \
        return hr; \
    } \
}

namespace
{
    HRESULT GetMvid(mdhandle_t image, mdguid_t* mvid)
    {
        mdcursor_t c;
        uint32_t count;
        if (!md_create_cursor(image, mdtid_Module, &c, &count))
            return CLDB_E_FILE_CORRUPT;
        
        if (1 != md_get_column_value_as_guid(c, mdtModule_Mvid, 1, mvid))
            return CLDB_E_FILE_CORRUPT;
        
        return S_OK;
    }

    namespace StrongNameKeys
    {
        constexpr size_t TokenSize = 8;

        const uint8_t Microsoft[] =
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

        const uint8_t MicrosoftToken[] = {0xb0,0x3f,0x5f,0x7f,0x11,0xd5,0x0a,0x3a};


        const uint8_t SilverlightPlatform[] =
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

        const uint8_t SilverlightPlatformToken[] = {0x7c,0xec,0x85,0xd7,0xbe,0xa7,0x79,0x8e};

        const uint8_t Silverlight[] =
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

        const uint8_t SilverlightToken[] = {0x31,0xBF,0x38,0x56,0xAD,0x36,0x4E,0x35};

        const uint8_t Ecma[] = { 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0 };
        const uint8_t EcmaToken[] = { 0xb7, 0x7a, 0x5c, 0x56, 0x19, 0x34, 0xe0, 0x89 };


    }

    struct PublicKeyBlob
    {
        uint32_t SigAlgID;
        uint32_t HashAlgID;
        uint32_t PublicKeyLength;
        uint8_t  PublicKey[];
    };

    HRESULT StrongNameTokenFromPublicKey(span<const uint8_t> publicKeyBlob, malloc_span<uint8_t>* strongNameTokenBuffer)
    {
        if (publicKeyBlob.size() < sizeof(PublicKeyBlob))
            return CORSEC_E_INVALID_PUBLICKEY;
        
        PublicKeyBlob const* publicKey = reinterpret_cast<PublicKeyBlob const*>((uint8_t const*)publicKeyBlob);

        if (publicKey->PublicKeyLength != publicKeyBlob.size() - sizeof(PublicKeyBlob))
            return CORSEC_E_INVALID_PUBLICKEY;
        
        if (publicKeyBlob.size() == sizeof(StrongNameKeys::Ecma)
            && std::memcmp(publicKeyBlob, StrongNameKeys::Ecma, sizeof(StrongNameKeys::Ecma)) == 0)
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
        
        *strongNameTokenBuffer = { (uint8_t*)::malloc(StrongNameKeys::TokenSize), StrongNameKeys::TokenSize };
        if (!strongNameTokenBuffer)
            return E_OUTOFMEMORY;
        
        // Check well-known keys first.
        if (publicKeyBlob.size() == sizeof(StrongNameKeys::Microsoft)
            && std::memcmp(publicKeyBlob, StrongNameKeys::Microsoft, sizeof(StrongNameKeys::Microsoft)) == 0)
        {
            std::memcpy(strongNameTokenBuffer, StrongNameKeys::MicrosoftToken, StrongNameKeys::TokenSize);
            return S_OK;
        }

        if (publicKeyBlob.size() == sizeof(StrongNameKeys::SilverlightPlatform)
            && std::memcmp(publicKeyBlob, StrongNameKeys::SilverlightPlatform, sizeof(StrongNameKeys::SilverlightPlatform)) == 0)
        {
            std::memcpy(strongNameTokenBuffer, StrongNameKeys::SilverlightPlatformToken, StrongNameKeys::TokenSize);
            return S_OK;
        }

        if (publicKeyBlob.size() == sizeof(StrongNameKeys::Silverlight)
            && std::memcmp(publicKeyBlob, StrongNameKeys::Silverlight, sizeof(StrongNameKeys::Silverlight)) == 0)
        {
            std::memcpy(strongNameTokenBuffer, StrongNameKeys::SilverlightToken, StrongNameKeys::TokenSize);
            return S_OK;
        }

        std::array<uint8_t, pal::SHA1_HASH_SIZE> hash;
        if (!pal::ComputeSha1Hash(publicKeyBlob, hash))
            return CORSEC_E_INVALID_PUBLICKEY;

        // Take the last few bytes of the hash value for our token.
        // These are the low order bytes from a network byte order point of view.
        // Reverse the order of these bytes in the output buffer to get host byte order.
        for (size_t i = 0; i < StrongNameKeys::TokenSize; i++)
        {
            hash[StrongNameKeys::TokenSize - (i + 1)] = hash[i + pal::SHA1_HASH_SIZE - StrongNameKeys::TokenSize];
        }

        return S_OK;
    }
}

namespace
{
    bool CaseInsensitiveEquals(char const* a, char const* b)
    {
        while (*a != '\0' && *b != '\0')
        {
            if (std::tolower(*a) != std::tolower(*b))
                return false;
            
            a++;
            b++;
        }

        return *a == '\0' && *b == '\0';
    }

    HRESULT FindAssemblyRef(
        mdhandle_t targetModule,
        uint32_t majorVersion,
        uint32_t minorVersion,
        uint32_t buildNumber,
        uint32_t revisionNumber,
        uint32_t flags,
        char const* name,
        char const* culture,
        span<uint8_t> publicKeyOrToken,
        mdcursor_t* assemblyRef)
    {
        HRESULT hr;
        // We will never generate a full public key for an assembly ref, only a token,
        // so we should never be looking for a full public key here.
        assert(!IsAfPublicKey(flags));

        // Search the assembly ref table for a matching row.
        mdcursor_t c;
        uint32_t count;
        if (!md_create_cursor(targetModule, mdtid_AssemblyRef, &c, &count))
            return E_FAIL;
        
        // COMPAT: CoreCLR resolves all references to mscorlib and Microsoft.VisualC to the same assembly ref ignoring the build and revision version.
        bool ignoreBuildRevisionVersion = CaseInsensitiveEquals(name, "mscorlib") || CaseInsensitiveEquals(name, "microsoft.visualc");
        
        for (uint32_t i = 0; i < count; i++)
        {
            // Search the table linearly my manually reading the columns.

            uint32_t temp;
            if (1 != md_get_column_value_as_constant(c, mdtAssemblyRef_MajorVersion, 1, &temp))
                return CLDB_E_FILE_CORRUPT;
            
            if (temp != majorVersion)
                continue;
            
            if (1 != md_get_column_value_as_constant(c, mdtAssemblyRef_MinorVersion, 1, &temp))
                return CLDB_E_FILE_CORRUPT;
            
            if (temp != minorVersion)
                continue;
            
            if (!ignoreBuildRevisionVersion)
            {
                if (1 != md_get_column_value_as_constant(c, mdtAssemblyRef_BuildNumber, 1, &temp))
                    return CLDB_E_FILE_CORRUPT;
                
                if (temp != buildNumber)
                    continue;
                
                if (1 != md_get_column_value_as_constant(c, mdtAssemblyRef_RevisionNumber, 1, &temp))
                    return CLDB_E_FILE_CORRUPT;
                
                if (temp != revisionNumber)
                    continue;
            }
            
            char const* tempString;
            if (1 != md_get_column_value_as_utf8(c, mdtAssemblyRef_Name, 1, &tempString))
                return CLDB_E_FILE_CORRUPT;
            
            if (std::strcmp(tempString, name) != 0)
                continue;
            
            if (1 != md_get_column_value_as_utf8(c, mdtAssemblyRef_Culture, 1, &tempString))
                return CLDB_E_FILE_CORRUPT;
            
            if (std::strcmp(tempString, culture) != 0)
                continue;
            
            uint8_t const* tempBlob;
            uint32_t tempBlobLength;
            if (1 != md_get_column_value_as_blob(c, mdtAssemblyRef_PublicKeyOrToken, 1, &tempBlob, &tempBlobLength))
                return CLDB_E_FILE_CORRUPT;
            
            if (tempBlobLength)
            {
                // Handle the case when a ref may be using a full public key instead of a token.
                malloc_ptr<uint8_t> tempPublicKeyTokenBuffer{nullptr};
                span<uint8_t const> refPublicKeyToken{nullptr, 0};

                uint32_t assemblyRefFlags;
                if (1 != md_get_column_value_as_constant(c, mdtAssemblyRef_Flags, 1, &assemblyRefFlags))
                    return CLDB_E_FILE_CORRUPT;
                
                if (!IsAfPublicKey(assemblyRefFlags))
                {
                    refPublicKeyToken = { tempBlob, tempBlobLength };
                }
                else
                {
                    // This AssemblyRef row has a full public key. We need to get the token from the key.
                    malloc_span<uint8_t> tempPublicKeyToken;
                    RETURN_IF_FAILED(StrongNameTokenFromPublicKey({ tempBlob, tempBlobLength }, &tempPublicKeyToken));
                    refPublicKeyToken = tempPublicKeyToken;
                    tempPublicKeyTokenBuffer.reset(tempPublicKeyToken.release());
                }

                if (publicKeyOrToken.size() != refPublicKeyToken.size() || std::memcmp(publicKeyOrToken, refPublicKeyToken, refPublicKeyToken.size()) != 0)
                    continue;
            }
            
            *assemblyRef = c;
            return S_OK;
        }

        return S_FALSE;
    }

    HRESULT ImportReferenceToAssembly(
        mdcursor_t sourceAssembly,
        span<const uint8_t> sourceAssemblyHash,
        mdhandle_t targetModule,
        std::function<void(mdcursor_t)> onRowAdded,
        mdcursor_t* targetAssembly)
    {
        HRESULT hr;
        uint32_t flags;
        if (1 != md_get_column_value_as_constant(sourceAssembly, mdtAssembly_Flags, 1, &flags))
            return E_FAIL;

        uint8_t const* publicKey;
        uint32_t publicKeyLength;
        if (1 != md_get_column_value_as_blob(sourceAssembly, mdtAssembly_PublicKey, 1, &publicKey, &publicKeyLength))
            return E_FAIL;
        
        malloc_span<uint8_t> publicKeyToken {nullptr, 0};
        if (publicKey != nullptr)
        {
            assert(IsAfPublicKey(flags));
            flags &= ~afPublicKey;
            RETURN_IF_FAILED(StrongNameTokenFromPublicKey({ publicKey, publicKeyLength }, &publicKeyToken));
        }
        else
        {
            assert(!IsAfPublicKey(flags));
        }

        uint32_t majorVersion;
        if (1 != md_get_column_value_as_constant(sourceAssembly, mdtAssembly_MajorVersion, 1, &majorVersion))
            return E_FAIL;
        
        uint32_t minorVersion;
        if (1 != md_get_column_value_as_constant(sourceAssembly, mdtAssembly_MinorVersion, 1, &minorVersion))
            return E_FAIL;
        
        uint32_t buildNumber;
        if (1 != md_get_column_value_as_constant(sourceAssembly, mdtAssembly_BuildNumber, 1, &buildNumber))
            return E_FAIL;
        
        uint32_t revisionNumber;
        if (1 != md_get_column_value_as_constant(sourceAssembly, mdtAssembly_RevisionNumber, 1, &revisionNumber))
            return E_FAIL;
        
        char const* assemblyName;
        if (1 != md_get_column_value_as_utf8(sourceAssembly, mdtAssembly_Name, 1, &assemblyName))
            return E_FAIL;
        
        char const* assemblyCulture;
        if (1 != md_get_column_value_as_utf8(sourceAssembly, mdtAssembly_Culture, 1, &assemblyCulture))
            return E_FAIL;

        RETURN_IF_FAILED(FindAssemblyRef(
            targetModule,
            majorVersion,
            minorVersion,
            buildNumber,
            revisionNumber,
            flags,
            assemblyName,
            assemblyCulture,
            publicKeyToken,
            targetAssembly));

        if (hr == S_OK)
        {
            return S_OK;
        }

        md_added_row_t assemblyRef;
        if (!md_append_row(targetModule, mdtid_AssemblyRef, &assemblyRef))
            return E_FAIL;
        
        onRowAdded(assemblyRef);
  
        if (1 != md_set_column_value_as_constant(assemblyRef, mdtAssemblyRef_MajorVersion, 1, &majorVersion))
            return E_FAIL;

        if (1 != md_set_column_value_as_constant(assemblyRef, mdtAssemblyRef_MinorVersion, 1, &minorVersion))
            return E_FAIL;

        if (1 != md_set_column_value_as_constant(assemblyRef, mdtAssemblyRef_BuildNumber, 1, &buildNumber))
            return E_FAIL;

        if (md_set_column_value_as_constant(assemblyRef, mdtAssemblyRef_RevisionNumber, 1, &revisionNumber))
            return E_FAIL;

        if (1 != md_set_column_value_as_constant(assemblyRef, mdtAssemblyRef_Flags, 1, &flags))
            return E_FAIL;
        
        if (1 != md_set_column_value_as_utf8(assemblyRef, mdtAssemblyRef_Name, 1, &assemblyName))
            return E_FAIL;

        if (1 != md_set_column_value_as_utf8(assemblyRef, mdtAssemblyRef_Culture, 1, &assemblyCulture))
            return E_FAIL;

        uint8_t const* hash = sourceAssemblyHash;
        uint32_t hashLength = (uint32_t)sourceAssemblyHash.size();
        if (1 != md_set_column_value_as_blob(assemblyRef, mdtAssemblyRef_HashValue, 1, &hash, &hashLength))
            return E_FAIL;

        uint8_t const* publicKeyTokenBlob = publicKeyToken;
        uint32_t publicKeyTokenLength = (uint32_t)publicKeyToken.size();
        if (1 != md_set_column_value_as_blob(assemblyRef, mdtAssemblyRef_PublicKeyOrToken, 1, &publicKeyTokenBlob, &publicKeyTokenLength))
            return E_FAIL;
        
        *targetAssembly = assemblyRef;
        return S_OK;
    }
}

HRESULT ImportReferenceToTypeDef(
    mdcursor_t sourceTypeDef,
    mdhandle_t sourceAssembly,
    span<const uint8_t> sourceAssemblyHash,
    mdhandle_t targetAssembly,
    mdhandle_t targetModule,
    std::function<void(mdcursor_t)> onRowAdded,
    mdcursor_t* targetTypeDef)
{
    HRESULT hr;
    mdhandle_t sourceModule = md_extract_handle_from_cursor(sourceTypeDef);

    mdguid_t thisMvid = { 0 };
    mdguid_t targetAssemblyMvid = { 0 };
    mdguid_t sourceAssemblyMvid = { 0 };
    mdguid_t importMvid = { 0 };
    RETURN_IF_FAILED(GetMvid(targetModule, &thisMvid));
    RETURN_IF_FAILED(GetMvid(targetAssembly, &targetAssemblyMvid));
    RETURN_IF_FAILED(GetMvid(sourceAssembly, &sourceAssemblyMvid));
    RETURN_IF_FAILED(GetMvid(sourceModule, &importMvid));

    bool sameModuleMvid = std::memcmp(&thisMvid, &importMvid, sizeof(mdguid_t)) == 0;
    bool sameAssemblyMvid = std::memcmp(&targetAssemblyMvid, &sourceAssemblyMvid, sizeof(mdguid_t)) == 0;

    mdcursor_t resolutionScope;
    if (sameAssemblyMvid && sameModuleMvid)
    {
        uint32_t count;
        if (!md_create_cursor(targetModule, mdtid_Module, &resolutionScope, &count))
            return E_FAIL;
    }
    else if (sameAssemblyMvid && !sameModuleMvid)
    {
        char const* importName;
        mdcursor_t importModule;
        uint32_t count;
        if (!md_create_cursor(sourceModule, mdtid_Module, &importModule, &count)
            || 1 != md_get_column_value_as_utf8(importModule, mdtModule_Name, 1, &importName))
        {
            return E_FAIL;
        }

        md_added_row_t moduleRef;
        if (!md_append_row(targetModule, mdtid_ModuleRef, &moduleRef)
            || 1 != md_set_column_value_as_utf8(moduleRef, mdtModuleRef_Name, 1, &importName))
        {
            return E_FAIL;
        }

        resolutionScope = moduleRef;
        onRowAdded(moduleRef);
    }
    else if (sameModuleMvid)
    {
        // The import can't be the same module and different assemblies.
        // COMPAT-BREAK: CoreCLR allows this for cases where there is no source assembly, with a TODO from FX-era
        // relating to building the core library using multiple modules.
        // CoreCLR doesn't support multimodule assemblies, so we are okay taking a breaking change here.
        return E_INVALIDARG;
    }
    else
    {
        mdcursor_t importAssembly;
        uint32_t count;
        if (!md_create_cursor(sourceModule, mdtid_Assembly, &importAssembly, &count))
            return E_FAIL;
        
        // Add a reference to the assembly in the target module.
        RETURN_IF_FAILED(ImportReferenceToAssembly(importAssembly, sourceAssemblyHash, targetModule, onRowAdded, &resolutionScope));

        // Also add a reference to the assembly in the target assembly.
        // In most cases, the target module will be the same as the target assembly, so this will be a no-op.
        // However, if the target module is a netmodule, then the target assembly will be the main assembly.
        // CoreCLR doesn't support multi-module assemblies, but they're still valid in ECMA-335.
        if (targetModule != targetAssembly)
        {
            mdcursor_t ignored;
            RETURN_IF_FAILED(ImportReferenceToAssembly(importAssembly, sourceAssemblyHash, targetAssembly, onRowAdded, &ignored));
        }
    }

    std::stack<mdcursor_t> typesForTypeRefs;

    mdcursor_t importType;
    if (!md_token_to_cursor(sourceModule, tdImport, &importType))
        return CLDB_E_FILE_CORRUPT;
    
    typesForTypeRefs.push(importType);
    
    mdcursor_t nestedClasses;
    uint32_t nestedClassCount;
    if (!md_create_cursor(sourceModule, mdtid_NestedClass, &nestedClasses, &nestedClassCount))
        return E_FAIL;
    
    mdToken nestedTypeToken = tdImport;
    mdcursor_t nestedClass;
    while (md_find_row_from_cursor(nestedClasses, mdtNestedClass_NestedClass, RidFromToken(nestedTypeToken), &nestedClass))
    {
        mdcursor_t enclosingClass;
        if (1 != md_get_column_value_as_cursor(nestedClass, mdtNestedClass_EnclosingClass, 1, &enclosingClass))
            return E_FAIL;
        
        typesForTypeRefs.push(enclosingClass);
        if (!md_cursor_to_token(enclosingClass, &nestedTypeToken))
            return E_FAIL;
    }

    for (; !typesForTypeRefs.empty(); typesForTypeRefs.pop())
    {
        mdcursor_t typeDef = typesForTypeRefs.top();
        md_added_row_t typeRef;
        if (!md_append_row(targetModule, mdtid_TypeRef, &typeRef))
            return E_FAIL;
        
        if (1 != md_set_column_value_as_cursor(typeRef, mdtTypeRef_ResolutionScope, 1, &resolutionScope))
            return E_FAIL;
        
        char const* typeName;
        if (1 != md_get_column_value_as_utf8(typeDef, mdtTypeDef_TypeName, 1, &typeName)
            || 1 != md_set_column_value_as_utf8(typeRef, mdtTypeRef_TypeName, 1, &typeName))
        {
            return E_FAIL;
        }
        
        char const* typeNamespace;
        if (1 != md_get_column_value_as_utf8(typeDef, mdtTypeDef_TypeNamespace, 1, &typeNamespace)
            || 1 != md_set_column_value_as_utf8(typeRef, mdtTypeRef_TypeNamespace, 1, &typeNamespace))
        {
            return E_FAIL;
        }

        resolutionScope = typeRef;
        onRowAdded(typeRef);
    }

    *targetTypeDef = resolutionScope;

    return S_OK;
}