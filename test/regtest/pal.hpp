#ifndef _TEST_REGTEST_PAL_H_
#define _TEST_REGTEST_PAL_H_

void* LoadModule(char const* path);
void* GetSymbol(void* module, char const* name);

#endif // !_TEST_REGTEST_PAL_H_