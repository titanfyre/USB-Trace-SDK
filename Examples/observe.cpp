#include "../SDK/USBTrace.h"
#include <iostream>
int main(){
 try{
  USBTrace trace;auto modules=trace.Modules();
  if(modules.empty()){std::cout<<"Attach USB Trace to one exact USB device with setup first.\n";return 1;}
  std::wcout<<L"Selected attached device: "<<modules.front().instance<<L'\n';
  std::string key;std::cout<<"Read bounded USB observations from this device; original traffic passes unchanged.\nAccess key: ";std::getline(std::cin,key);
  auto error=trace.Authorize(key);if(error.empty())error=trace.Start(modules.front().id);if(!error.empty()){std::cerr<<error<<'\n';return 1;}
  std::cout<<"Observing the first attached module for 10 seconds.\n";
  const auto end=GetTickCount64()+10000;
  while(GetTickCount64()<end){for(const auto& e:trace.Receive())std::cout<<"USB function "<<e.function<<", "<<e.captured<<" sampled bytes, status "<<e.nt_status<<'\n';Sleep(20);}
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
