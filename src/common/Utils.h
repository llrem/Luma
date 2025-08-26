#pragma once

#include <string>
#include <windows.h>
#include <DirectXColors.h>
#include <comdef.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/euler_angles.hpp>

#include "src/common/Image.h"

class Exception {
public:
    Exception() = default;

    Exception(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber)
        : ErrorCode(hr), FunctionName(functionName), Filename(filename), LineNumber(lineNumber) {}
    
    std::wstring ToString() const {
        _com_error err(ErrorCode);
        std::wstring msg = err.ErrorMessage();
        return L"Failed to " + FunctionName + L" at " + Filename + L" Line:" + std::to_wstring(LineNumber) + L"\n" + msg;
    }

    HRESULT ErrorCode = S_OK;
    int LineNumber = -1; 
    std::wstring Filename;
    std::wstring FunctionName;
};

inline std::wstring AnsiToWString(const std::string &str) {
    WCHAR buffer[512];
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
    return std::wstring(buffer);
}

inline std::string WStringToAnsi(const std::wstring &wstr) {
    char buffer[512];
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, buffer, 512, nullptr, nullptr);
    return std::string(buffer);
}

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                               \
    {                                                  \
        HRESULT hr__ = (x);                            \
        std::wstring wfn = AnsiToWString(__FILE__);    \
        if (FAILED(hr__)) {                            \
            throw Exception(hr__, L#x, wfn, __LINE__); \
        }                                              \
    }
#endif

template <typename T> 
constexpr T mipmapLevels(T width, T height) { 
    T levels = 1;
    while ((width | height) >> levels) {
        ++levels;
    }
    return levels;
}

inline UINT AlignedByteSize(UINT dataSize, UINT alignSize) { 
    UINT alignMask = alignSize - 1;
    return (dataSize + alignMask) & ~alignMask;
}

inline bool IsPowerOfTwo(UINT value) {
	return value != 0 && (value & (value - 1)) == 0;
}

inline std::wstring ConvertToUTF16(const std::string &str) {
    const int bufferSize = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    const std::unique_ptr<wchar_t[]> buffer(new wchar_t[bufferSize]);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, buffer.get(), bufferSize);
    return std::wstring(buffer.get());
}