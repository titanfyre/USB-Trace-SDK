#include "USBTrace.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <array>
#include <cstring>
#include <utility>
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"crypt32.lib")

namespace {
struct Http {
    HINTERNET value = nullptr;
    explicit Http(HINTERNET handle) : value(handle) {}
    ~Http() { if (value) WinHttpCloseHandle(value); }
    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;
};
std::string Failure(const char* stage) {
    return std::string(stage) + " failed (Windows error " +
        std::to_string(GetLastError()) + ").";
}
bool ValidKey(std::string_view key) {
    if (key.size() != 81 || key.substr(0, 5) != "app1_" || key[37] != '_')
        return false;
    for (size_t i = 5; i != 37; ++i)
        if (!((key[i] >= '0' && key[i] <= '9') ||
              (key[i] >= 'a' && key[i] <= 'f'))) return false;
    for (size_t i = 38; i != key.size(); ++i)
        if (!((key[i] >= 'a' && key[i] <= 'z') ||
              (key[i] >= 'A' && key[i] <= 'Z') ||
              (key[i] >= '0' && key[i] <= '9') ||
               key[i] == '_' || key[i] == '-')) return false;
    return true;
}
}

namespace {
std::string RequestDriverAuthorization(std::string_view key,
    const TITAN_AUTHORIZATION_CHALLENGE& challenge,
    TITAN_AUTHORIZATION_TOKEN& token) {
    token = {};
    if (!ValidKey(key)) return "Enter a valid application access key (app1_...).";
    if (challenge.magic != TITAN_AUTHORIZATION_MAGIC ||
        challenge.version != TITAN_AUTHORIZATION_VERSION ||
        challenge.duration_seconds != 0 ||
        (challenge.product != TITAN_AUTHORIZATION_USB_TRACE))
        return "The driver returned an invalid authorization challenge.";
    const char* sku = "usb-trace-sdk";
    std::string nonce;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char value : challenge.nonce) {
        nonce += hex[value >> 4]; nonce += hex[value & 15];
    }
    std::string body = "{\"key\":\"" + std::string(key) +
        "\",\"sku\":\"" + sku + "\",\"nonce\":\"" + nonce + "\"}";
    Http session(WinHttpOpen(L"Titan Driver SDK/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) { SecureZeroMemory(body.data(), body.size()); return Failure("HTTPS initialization"); }
    WinHttpSetTimeouts(session.value, 5000, 5000, 10000, 10000);
    Http connection(WinHttpConnect(session.value, L"licences.configcsa.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.value) { SecureZeroMemory(body.data(), body.size()); return Failure("HTTPS connection"); }
    Http request(WinHttpOpenRequest(connection.value, L"POST",
        L"/portfolio/api/v1/driver-authorize", nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value) { SecureZeroMemory(body.data(), body.size()); return Failure("Authorization request"); }
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE,
        &disabled, sizeof(disabled))) {
        SecureZeroMemory(body.data(), body.size()); return Failure("HTTPS policy");
    }
    const BOOL sent = WinHttpSendRequest(request.value,
        L"Content-Type: application/json\r\n", static_cast<DWORD>(-1),
        body.data(), static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);
    const DWORD sendError = sent ? ERROR_SUCCESS : GetLastError();
    SecureZeroMemory(body.data(), body.size());
    if (!sent) return "Authorization request failed (Windows error " + std::to_string(sendError) + ").";
    if (!WinHttpReceiveResponse(request.value, nullptr)) return Failure("Authorization response");
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE |
        WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
        &length, WINHTTP_NO_HEADER_INDEX)) return Failure("HTTP status");
    std::string response;
    std::array<char, 1024> chunk{};
    for (;;) {
        DWORD count = 0;
        if (!WinHttpReadData(request.value, chunk.data(),
            static_cast<DWORD>(chunk.size()), &count)) return Failure("Response read");
        if (!count) break;
        if (response.size() + count > 8192) return "Authorization response is too large.";
        response.append(chunk.data(), count);
    }
    if (status != 200) {
        for (char& c : response) if (static_cast<unsigned char>(c) < 32) c = ' ';
        return "License server refused authorization (HTTP " +
            std::to_string(status) + "): " + response;
    }
    // Only extract a bounded base64 field. The kernel verifies every byte of
    // the decoded permission and its signature before enabling any producer.
    const auto field = response.find("\"grant_base64\"");
    if (field == std::string::npos) return "Missing authorization grant in server response.";
    const auto colon = response.find(':', field + 14);
    const auto start = colon == std::string::npos ? std::string::npos : response.find('"', colon + 1);
    const auto end = start == std::string::npos ? std::string::npos : response.find('"', start + 1);
    if (end == std::string::npos || end - start - 1 != 152)
        return "Invalid authorization grant encoding.";
    const std::string grant = response.substr(start + 1, end - start - 1);
    DWORD bytes = sizeof(token);
    if (!CryptStringToBinaryA(grant.c_str(), static_cast<DWORD>(grant.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
        reinterpret_cast<BYTE*>(&token), &bytes, nullptr, nullptr) ||
        bytes != sizeof(token)) {
        token = {}; return "Invalid authorization grant length or encoding.";
    }
    if (token.permission.magic != challenge.magic ||
        token.permission.version != challenge.version ||
        token.permission.product != challenge.product ||
        token.permission.duration_seconds == 0 ||
        token.permission.duration_seconds > TITAN_AUTHORIZATION_MAX_SECONDS ||
        std::memcmp(token.permission.nonce, challenge.nonce, sizeof(challenge.nonce))) {
        token = {}; return "Authorization grant does not match this driver session.";
    }
    return {};
}

} // private HTTPS implementation


#include <stdexcept>
namespace {
void Check(DWORD error,const char* stage){if(error)throw std::runtime_error(std::string(stage)+": "+USBTrace::Error(error));}
}
std::string USBTrace::Error(DWORD code){
 char* text=nullptr;FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),reinterpret_cast<char*>(&text),0,nullptr);
 std::string result=text?text:"Windows operation failed";if(text)LocalFree(text);while(!result.empty()&&(result.back()=='\r'||result.back()=='\n'))result.pop_back();return result+" (Windows error "+std::to_string(code)+")";
}
void USBTrace::Open(){if(device_!=INVALID_HANDLE_VALUE)return;device_=CreateFileW(L"\\\\.\\TitanUSBTrace",GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(device_==INVALID_HANDLE_VALUE)Check(GetLastError(),"Open USB Trace driver (install signed driver and run as administrator)");}
HANDLE USBTrace::NativeHandle(){Open();return device_;}
DWORD USBTrace::Call(DWORD code,const void* input,DWORD inputBytes,void* output,DWORD outputBytes) noexcept {
 DWORD bytes=0;if(device_==INVALID_HANDLE_VALUE)return ERROR_INVALID_HANDLE;if(!DeviceIoControl(device_,code,const_cast<void*>(input),inputBytes,output,outputBytes,&bytes,nullptr))return GetLastError();return bytes==outputBytes?ERROR_SUCCESS:ERROR_INVALID_DATA;
}
USBTrace::~USBTrace(){Stop();if(device_!=INVALID_HANDLE_VALUE)CloseHandle(device_);}
void USBTrace::Stop() noexcept {if(device_!=INVALID_HANDLE_VALUE)(void)Call(UT_STOP,nullptr,0,nullptr,0);}
std::vector<UT_MODULE> USBTrace::Modules(){Open();UT_MODULES modules{};Check(Call(UT_ENUM,nullptr,0,&modules,sizeof(modules)),"Enumerate USB filter modules");if(modules.version!=UT_VERSION||modules.count>UT_MAX_MODULES)throw std::runtime_error("Invalid module inventory response");for(unsigned n=0;n<modules.count;++n){auto& m=modules.modules[n];if(!m.id||m.ready>1||m.reserved||m.instance[255]||m.description[127])throw std::runtime_error("Invalid USB module identity");}return {modules.modules,modules.modules+modules.count};}
std::string USBTrace::Authorize(std::string_view key){try{Open();TITAN_AUTHORIZATION_CHALLENGE challenge{};TITAN_AUTHORIZATION_TOKEN token{};Check(Call(UT_CHALLENGE,nullptr,0,&challenge,sizeof(challenge)),"Driver challenge");auto error=RequestDriverAuthorization(key,challenge,token);if(!error.empty())return error;DWORD code=Call(UT_AUTHORIZE,&token,sizeof(token),nullptr,0);SecureZeroMemory(&token,sizeof(token));return code?"Driver authorization: "+Error(code):std::string{};}catch(const std::exception& e){return e.what();}}
std::string USBTrace::Start(std::uint64_t module){try{Open();UT_SELECTION selection{};selection.module=module;Check(Call(UT_START,&selection,sizeof(selection),nullptr,0),"Start selected USB trace (authorization required)");return {};}catch(const std::exception& e){return e.what();}}
UT_STATE USBTrace::Status(){Open();UT_STATE s{};Check(Call(UT_STATUS,nullptr,0,&s,sizeof(s)),"Read USB trace status");if(s.version!=UT_VERSION||s.size!=sizeof(s)||s.active>1||s.authorized>1||s.queued>UT_RING_CAPACITY||s.pending>UT_MAX_PENDING)throw std::runtime_error("Invalid USB trace status response");return s;}
std::vector<UT_EVENT> USBTrace::Receive(){Open();UT_BATCH batch{};Check(Call(UT_READ,nullptr,0,&batch,sizeof(batch)),"Read USB observations");if(batch.version!=UT_VERSION||batch.count>UT_BATCH_CAPACITY)throw std::runtime_error("Invalid observation batch");for(unsigned n=0;n<batch.count;++n)if(!ValidUSBTraceEvent(batch.events[n]))throw std::runtime_error("Invalid USB observation record");return {batch.events,batch.events+batch.count};}
