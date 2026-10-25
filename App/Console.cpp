#include "../SDK/USBTrace.h"
#include <iostream>
int main(int argc,char** argv){
 if(argc==2&&std::string(argv[1])=="--preview"){std::cout<<"USB Trace SDK - offline preview. No driver or device opened.\nPer-device USB request observations with bounded payload samples.\n";return 0;}
 try{USBTrace trace;auto modules=trace.Modules();std::cout<<"Attached USB filter modules (capture starts only explicitly):\n";for(const auto& m:modules)std::wcout<<m.id<<L"  "<<m.instance<<L'\n';auto s=trace.Status();std::cout<<"Active: "<<s.active<<", observed completions: "<<s.completed<<", queue: "<<s.queued<<'\n';}
 catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
