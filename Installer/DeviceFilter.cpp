#include "Setup.h"
#include <setupapi.h>
#include <cfgmgr32.h>
#include <vector>
#include <stdexcept>
#include <system_error>
#include <cwchar>
#include <algorithm>
namespace {
constexpr wchar_t Service[]=L"TitanUSBTrace";
struct List {HDEVINFO h=INVALID_HANDLE_VALUE;~List(){if(h!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(h);}};
std::wstring Message(DWORD e){return L"Windows error "+std::to_wstring(e)+L". USB traffic was not intentionally blocked.";}
void Require(bool ok){if(!ok)throw std::system_error(static_cast<int>(GetLastError()?GetLastError():ERROR_GEN_FAILURE),std::system_category());}
std::wstring Text(HDEVINFO h,SP_DEVINFO_DATA& d,DWORD property){DWORD type=0,bytes=0;SetupDiGetDeviceRegistryPropertyW(h,&d,property,&type,nullptr,0,&bytes);if(!bytes||bytes>65536||type!=REG_SZ)return {};std::vector<wchar_t> value(bytes/2+1,0);if(!SetupDiGetDeviceRegistryPropertyW(h,&d,property,&type,reinterpret_cast<BYTE*>(value.data()),bytes,nullptr))return {};value.back()=0;return value.data();}
std::wstring Instance(HDEVINFO h,SP_DEVINFO_DATA& d){wchar_t value[1024]{};if(!SetupDiGetDeviceInstanceIdW(h,&d,value,1024,nullptr))return {};return value;}
std::vector<std::wstring> Filters(HDEVINFO h,SP_DEVINFO_DATA& d){
 DWORD type=0,bytes=0;if(!SetupDiGetDeviceRegistryPropertyW(h,&d,SPDRP_LOWERFILTERS,&type,nullptr,0,&bytes)){DWORD error=GetLastError();if(error==ERROR_INVALID_DATA||error==ERROR_FILE_NOT_FOUND)return {};if(error!=ERROR_INSUFFICIENT_BUFFER){SetLastError(error);Require(false);}}
 if(type!=REG_MULTI_SZ||bytes<4||bytes>65536||bytes%2){SetLastError(ERROR_INVALID_DATA);Require(false);}std::vector<wchar_t> value(bytes/2,0);Require(SetupDiGetDeviceRegistryPropertyW(h,&d,SPDRP_LOWERFILTERS,&type,reinterpret_cast<BYTE*>(value.data()),bytes,nullptr)!=FALSE);
 if(value.size()<2||value[value.size()-1]||value[value.size()-2]){SetLastError(ERROR_INVALID_DATA);Require(false);}std::vector<std::wstring> result;size_t position=0;while(position<value.size()&&value[position]){size_t end=position;while(end<value.size()&&value[end])++end;if(end==value.size()){SetLastError(ERROR_INVALID_DATA);Require(false);}result.emplace_back(value.data()+position,end-position);position=end+1;}return result;
}
bool Has(const std::vector<std::wstring>& list){for(auto& s:list)if(!_wcsicmp(s.c_str(),Service))return true;return false;}
bool Eligible(HDEVINFO h,SP_DEVINFO_DATA& d,const std::wstring& id){
 if(id.size()<12||id.size()>255||_wcsnicmp(id.c_str(),L"USB\\VID_",8))return false;
 auto service=Text(h,d,SPDRP_SERVICE);if(!_wcsicmp(service.c_str(),L"USBHUB")||!_wcsicmp(service.c_str(),L"USBHUB3")||!_wcsicmp(service.c_str(),L"usbhub"))return false;
 return true;
}
void Write(HDEVINFO h,SP_DEVINFO_DATA& d,const std::vector<std::wstring>& values){
 if(values.empty()){Require(SetupDiSetDeviceRegistryPropertyW(h,&d,SPDRP_LOWERFILTERS,nullptr,0)!=FALSE);return;}
 std::vector<wchar_t> bytes;for(auto& value:values){bytes.insert(bytes.end(),value.begin(),value.end());bytes.push_back(0);}bytes.push_back(0);Require(SetupDiSetDeviceRegistryPropertyW(h,&d,SPDRP_LOWERFILTERS,reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()*sizeof(wchar_t)))!=FALSE);
}
bool RestartUSB(HDEVINFO h,SP_DEVINFO_DATA& d,bool& reboot){
 ULONG status=0,problem=0;CONFIGRET cr=CM_Get_DevNode_Status(&status,&problem,d.DevInst,0);if(cr==CR_NO_SUCH_DEVNODE)return true;
 SP_PROPCHANGE_PARAMS change{};change.ClassInstallHeader.cbSize=sizeof(SP_CLASSINSTALL_HEADER);change.ClassInstallHeader.InstallFunction=DIF_PROPERTYCHANGE;change.StateChange=DICS_PROPCHANGE;change.Scope=DICS_FLAG_GLOBAL;
 if(!SetupDiSetClassInstallParamsW(h,&d,&change.ClassInstallHeader,sizeof(change))||!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE,h,&d))return false;
 SP_DEVINSTALL_PARAMS_W params{};params.cbSize=sizeof(params);if(!SetupDiGetDeviceInstallParamsW(h,&d,&params))return false;reboot=(params.Flags&(DI_NEEDREBOOT|DI_NEEDRESTART))!=0;return true;
}
}
std::vector<Setup::USBNode> Setup::USBNodes(bool presentOnly){
 List list;list.h=SetupDiGetClassDevsW(nullptr,L"USB",nullptr,DIGCF_ALLCLASSES|(presentOnly?DIGCF_PRESENT:0));Require(list.h!=INVALID_HANDLE_VALUE);std::vector<USBNode> result;
 for(DWORD i=0;;++i){SP_DEVINFO_DATA d{};d.cbSize=sizeof(d);if(!SetupDiEnumDeviceInfo(list.h,i,&d)){if(GetLastError()!=ERROR_NO_MORE_ITEMS)Require(false);break;}auto id=Instance(list.h,d);if(!Eligible(list.h,d,id))continue;auto filters=Filters(list.h,d);auto description=Text(list.h,d,SPDRP_FRIENDLYNAME);if(description.empty())description=Text(list.h,d,SPDRP_DEVICEDESC);result.push_back({std::move(id),std::move(description),Has(filters)});}
 return result;
}
Setup::Result Setup::ChangeFilter(const std::wstring& exactInstance,bool attach) noexcept{
 HANDLE mutex=nullptr;bool locked=false;
 try{
  mutex=CreateMutexW(nullptr,FALSE,L"Global\\TitanUSBTraceSetup");Require(mutex!=nullptr);DWORD wait=WaitForSingleObject(mutex,0);if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){CloseHandle(mutex);return {ERROR_BUSY,false,L"Another USB Trace maintenance operation is already running."};}locked=true;
  if(attach&&Installed()!=1){ReleaseMutex(mutex);CloseHandle(mutex);return {ERROR_SERVICE_DOES_NOT_EXIST,false,L"Install the signed USB Trace control driver before attaching a USB filter."};}
  List list;list.h=SetupDiGetClassDevsW(nullptr,L"USB",nullptr,DIGCF_ALLCLASSES);Require(list.h!=INVALID_HANDLE_VALUE);SP_DEVINFO_DATA selected{};bool found=false;
  for(DWORD i=0;;++i){SP_DEVINFO_DATA d{};d.cbSize=sizeof(d);if(!SetupDiEnumDeviceInfo(list.h,i,&d)){if(GetLastError()!=ERROR_NO_MORE_ITEMS)Require(false);break;}auto id=Instance(list.h,d);if(!_wcsicmp(id.c_str(),exactInstance.c_str())&&Eligible(list.h,d,id)){selected=d;found=true;break;}}
  if(!found){ReleaseMutex(mutex);CloseHandle(mutex);return {ERROR_NOT_FOUND,false,L"The exact non-hub USB device was not found. No filters were changed."};}
  auto previous=Filters(list.h,selected);auto next=previous;bool has=Has(previous);
  if(attach&&!has)next.emplace_back(Service);if(!attach)next.erase(std::remove_if(next.begin(),next.end(),[](auto& s){return !_wcsicmp(s.c_str(),Service);}),next.end());
  bool reboot=false;if(next!=previous){Write(list.h,selected,next);if(!RestartUSB(list.h,selected,reboot)){DWORD error=GetLastError();Write(list.h,selected,previous);bool rollbackReboot=false;bool restored=RestartUSB(list.h,selected,rollbackReboot);ReleaseMutex(mutex);CloseHandle(mutex);return {error,reboot||rollbackReboot||!restored,L"USB filter restart failed. The original filter list was restored. "+Message(error)};}}
  ReleaseMutex(mutex);CloseHandle(mutex);return {0,reboot,attach?L"USB Trace is associated with this exact device. Other filter entries were preserved. Capture stays inactive until explicitly started.":L"Only USB Trace was removed from this device's filter list. Other entries were preserved."};
 }catch(const std::system_error& e){if(mutex){if(locked)ReleaseMutex(mutex);CloseHandle(mutex);}return {static_cast<DWORD>(e.code().value()),true,Message(static_cast<DWORD>(e.code().value()))};}
 catch(...){if(mutex){if(locked)ReleaseMutex(mutex);CloseHandle(mutex);}return {ERROR_GEN_FAILURE,true,L"USB filter maintenance did not complete. Refresh the device list and inspect the reported association before retrying."};}
}
