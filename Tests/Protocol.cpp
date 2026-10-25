#include "../SDK/USBTraceProtocol.h"
#include "../Installer/Setup.h"
#include <iostream>
#include <filesystem>
#include <fstream>
int main(){
 unsigned checks=0;auto require=[&](bool value){++checks;if(!value)throw std::runtime_error("Bounded public protocol check failed");};
 try{UT_EVENT e{};require(ValidUSBTraceEvent(e));e.captured=UT_SAMPLE_BYTES;require(ValidUSBTraceEvent(e));
  for(unsigned n=0;n<100000;++n){e={};e.captured=UT_SAMPLE_BYTES+1+n;require(!ValidUSBTraceEvent(e));e={};e.flags=64u+(n<<7);require(!ValidUSBTraceEvent(e));}
  e={};e.direction=3;require(!ValidUSBTraceEvent(e));e={};e.setup_valid=2;require(!ValidUSBTraceEvent(e));e={};e.reserved[1]=1;require(!ValidUSBTraceEvent(e));
  auto path=std::filesystem::temp_directory_path()/std::filesystem::path("USBTrace-unsigned-policy-test-"+std::to_string(GetCurrentProcessId())+".inf");{std::ofstream f(path);f<<"[Version]\nSignature=\"$Windows NT$\"\nClass=System\nClassGUID={4D36E97D-E325-11CE-BFC1-08002BE10318}\nProvider=Titanfyre\nCatalogFile=Wrong.cat\n";}require(!Setup::PackageMatches(path.wstring()));require(!Setup::CatalogTrusted(path.wstring()));std::filesystem::remove(path);
  std::cout<<checks<<" ABI mutation and read-only installer refusal checks passed; no driver opened.\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
