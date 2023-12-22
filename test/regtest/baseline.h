#ifndef _TEST_REGTEST_BASELINE_H_
#define _TEST_REGTEST_BASELINE_H_

#if defined(_MSC_VER)
#define NOMINMAX
#include <Windows.h>
#endif
#include <dncp.h>

#include <cor.h>
#include <corsym.h>

namespace TestBaseline
{
    extern dncp::com_ptr<IMetaDataDispenser> Metadata;
    extern dncp::com_ptr<IMetaDataDispenserEx> DeltaMetadataBuilder;
    extern dncp::com_ptr<ISymUnmanagedBinder> Symbol;
}

#endif // !_TEST_REGTEST_BASELINE_H_