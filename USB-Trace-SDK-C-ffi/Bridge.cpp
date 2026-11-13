#include "SDK/USBTrace.h"
#include "WireTypes.h"
#include <map>
struct ProductSession{
USBTrace client;
Json Invoke(const std::string& op,const Json& a){

if(op=="check"){auto event=FromWire<UT_EVENT>(a.at("event"));return {{"valid",ValidUSBTraceEvent(event)},{"value",EncodeWire(event)}};}
if(op=="authorize"){CheckMessage(client.Authorize(a.at("key").get<std::string>()));return Json::object();}
if(op=="modules")return VectorWire(client.Modules());
if(op=="start"){CheckMessage(client.Start(Identifier(a,"module")));return Json::object();}
if(op=="stop"){client.Stop();return Json::object();}
if(op=="status")return EncodeWire(client.Status());
if(op=="receive")return VectorWire(client.Receive());

throw SdkError(50,"Unknown operation");}
};
static Runtime<ProductSession> runtime;
extern "C" __declspec(dllexport) unsigned usb_trace_sdk_json_open(std::uint64_t* id)noexcept{return runtime.Open(id);}
extern "C" __declspec(dllexport) unsigned usb_trace_sdk_json_close(std::uint64_t id)noexcept{return runtime.Close(id);}
extern "C" __declspec(dllexport) char* usb_trace_sdk_json_call(std::uint64_t id,const char* request,std::uint32_t length)noexcept{return runtime.Call(id,request,length);}
extern "C" __declspec(dllexport) void usb_trace_sdk_json_free(char* response)noexcept{delete[] response;}
