#pragma once

template <typename T>
inline void SafeRelease(T **p)
{
    if (*p)
    {
        (*p)->Release();
        *p = nullptr;
    }
}

#define WIN_CHECK(x)                                                                                                                                           \
    {                                                                                                                                                          \
        HRESULT ret = x;                                                                                                                                       \
        assert(ret == S_OK);                                                                                                                                   \
    }