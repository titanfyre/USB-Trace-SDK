#pragma once
#include "USBTraceProtocol.h"
#include "AuthorizationProtocol.h"
#include <string>
#include <string_view>
#include <vector>
class USBTrace {
 HANDLE device_=INVALID_HANDLE_VALUE;
 void Open();
public:
 USBTrace()=default;
 ~USBTrace();
 USBTrace(const USBTrace&)=delete;
 USBTrace& operator=(const USBTrace&)=delete;
 static std::string Error(DWORD code);
 std::vector<UT_MODULE> Modules();
 std::string Authorize(std::string_view accessKey);
 std::string Start(std::uint64_t module);
 void Stop() noexcept;
 UT_STATE Status();
 std::vector<UT_EVENT> Receive();
 HANDLE NativeHandle();
 DWORD Call(DWORD code,const void* input,DWORD inputBytes,void* output,DWORD outputBytes) noexcept;
};
