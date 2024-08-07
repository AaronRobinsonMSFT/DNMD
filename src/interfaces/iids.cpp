#include <cstddef>
#include <cstdint>

#define DNCP_DEFINE_GUID
#include <dncp.h>

#define MIDL_DEFINE_GUID(name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
        EXTERN_GUID(name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8)

// Define the IMetaDataImport IID here - cor.h provides the declaration.
MIDL_DEFINE_GUID(IID_IMetaDataDispenser,0x809c652e,0x7396,0x11d2,0x97,0x71,0x00,0xa0,0xc9,0xb4,0xd5,0x0c);
MIDL_DEFINE_GUID(IID_IMetaDataDispenserEx, 0x31bcfce2, 0xdafb, 0x11d2, 0x9f, 0x81, 0x0, 0xc0, 0x4f, 0x79, 0xa0, 0xa3);
MIDL_DEFINE_GUID(IID_IMetaDataImport,0x7dac8207,0xd3ae,0x4c75,0x9b,0x67,0x92,0x80,0x1a,0x49,0x7d,0x44);
MIDL_DEFINE_GUID(IID_IMetaDataImport2,0xfce5efa0,0x8bba,0x4f8e,0xa0,0x36,0x8f,0x20,0x22,0xb0,0x84,0x66);
MIDL_DEFINE_GUID(IID_IMetaDataAssemblyImport,0xee62470b,0xe94b,0x424e,0x9b,0x7c,0x2f,0x00,0xc9,0x24,0x9f,0x93);
MIDL_DEFINE_GUID(IID_IMetaDataEmit, 0xba3fee4c, 0xecb9, 0x4e41, 0x83, 0xb7, 0x18, 0x3f, 0xa4, 0x1c, 0xd8, 0x59);
MIDL_DEFINE_GUID(IID_IMetaDataEmit2, 0xf5dd9950, 0xf693, 0x42e6, 0x83, 0xe, 0x7b, 0x83, 0x3e, 0x81, 0x46, 0xa9);
MIDL_DEFINE_GUID(IID_IMetaDataAssemblyEmit, 0x211ef15b, 0x5317, 0x4438, 0xb1, 0x96, 0xde, 0xc8, 0x7b, 0x88, 0x76, 0x93);

// Define the ISymUnmanaged* IIDs here - corsym.h provides the declaration.
MIDL_DEFINE_GUID(IID_ISymUnmanagedBinder, 0xaa544d42, 0x28cb, 0x11d3, 0xbd, 0x22, 0x00, 0x00, 0xf8, 0x08, 0x49, 0xbd);

// Define option IIDs here - cor.h provides the declaration.
MIDL_DEFINE_GUID(MetaDataThreadSafetyOptions, 0xf7559806, 0xf266, 0x42ea, 0x8c, 0x63, 0xa, 0xdb, 0x45, 0xe8, 0xb2, 0x34);
MIDL_DEFINE_GUID(CLSID_CLR_v2_MetaData, 0xefea471a, 0x44fd, 0x4862, 0x92, 0x92, 0xc, 0x58, 0xd4, 0x6e, 0x1f, 0x3a);

// Define an IID for our own marker interface
MIDL_DEFINE_GUID(IID_IDNMDOwner, 0x250ebc02, 0x1a92, 0x4638, 0xaa, 0x6c, 0x3d, 0x0f, 0x98, 0xb3, 0xa6, 0xfb);
