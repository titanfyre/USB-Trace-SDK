#include "Setup.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){
    if(argc==3&&(std::wstring(argv[1])==L"/attach"||std::wstring(argv[1])==L"/detach")){auto result=Setup::ChangeFilter(argv[2],std::wstring(argv[1])==L"/attach");std::wcout<<result.detail<<L'\n';return result.error?static_cast<int>(result.error):(result.reboot?3010:0);}
    if(argc==2&&std::wstring(argv[1])==L"/devices"){try{for(const auto& node:Setup::USBNodes())std::wcout<<(node.attached?L"[attached] ":L"[available] ")<<node.instance<<L"\n  "<<node.description<<L'\n';return 0;}catch(...){std::cerr<<"USB device enumeration failed.\n";return 1;}}
    if(argc!=2||(std::wstring(argv[1])!=L"/install"&&std::wstring(argv[1])!=L"/uninstall")){
        std::wcout<<L"USB Trace Studio Setup\nUsage: USBTraceSetupConsole.exe /install | /uninstall\nAdministrator rights required. No automatic restart.\n";return 2;
    }
    auto result=Setup::Execute(std::wstring(argv[1])==L"/install");std::wcout<<result.detail<<L'\n';
    if(result.reboot)std::wcout<<L"Restart Windows to complete this operation.\n";
    return result.error?static_cast<int>(result.error):(result.reboot?3010:0);
}
