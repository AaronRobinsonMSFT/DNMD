#include "pal.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#include <dncp.h>
#endif

void* LoadModule(char const* path)
{
#ifdef _WIN32
    return LoadLibraryA(path);
#else
    return dlopen(path, RTLD_LAZY);
#endif
}

void* GetSymbol(void* module, char const* name)
{
#ifdef _WIN32
    return GetProcAddress((HMODULE)module, name);
#else
    return dlsym(module, name);
#endif
}