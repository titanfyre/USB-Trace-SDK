#pragma once
#ifdef _KERNEL_MODE
#include <ntddk.h>
typedef ULONG UT_U32;typedef ULONGLONG UT_U64;typedef USHORT UT_U16;typedef UCHAR UT_U8;typedef WCHAR UT_WCHAR;
#else
#include <windows.h>
#include <winioctl.h>
#include <cstdint>
typedef uint32_t UT_U32;typedef uint64_t UT_U64;typedef uint16_t UT_U16;typedef uint8_t UT_U8;typedef wchar_t UT_WCHAR;
#endif
#define UT_VERSION 1u
#define UT_SAMPLE_BYTES 512u
#define UT_RING_CAPACITY 256u
#define UT_BATCH_CAPACITY 16u
#define UT_MAX_MODULES 32u
#define UT_MAX_PENDING 128u
#define UT_DEVICE_TYPE 0x8339u
#define UT_IOCTL(n) CTL_CODE(UT_DEVICE_TYPE,0x900+(n),METHOD_BUFFERED,FILE_READ_DATA|FILE_WRITE_DATA)
#define UT_ENUM UT_IOCTL(0)
#define UT_STATUS UT_IOCTL(1)
#define UT_START UT_IOCTL(2)
#define UT_STOP UT_IOCTL(3)
#define UT_READ UT_IOCTL(4)
#define UT_CHALLENGE UT_IOCTL(5)
#define UT_AUTHORIZE UT_IOCTL(6)
#define UT_IN 1u
#define UT_OUT 2u
#define UT_METADATA 0u
#define UT_PAYLOAD_PRESENT 1u
#define UT_PAYLOAD_TRUNCATED 2u
#define UT_PAYLOAD_UNAVAILABLE 4u
#define UT_ENDPOINT_UNKNOWN 8u
#define UT_ACTUAL_UNKNOWN 16u
#define UT_SUBMITTED_BEFORE_CAPTURE 32u
typedef struct UT_MODULE {UT_U64 id;UT_U32 ready,reserved;UT_WCHAR instance[256],description[128];} UT_MODULE;
typedef struct UT_MODULES {UT_U32 version,count;UT_MODULE modules[UT_MAX_MODULES];} UT_MODULES;
typedef struct UT_SELECTION {UT_U64 module;UT_U32 reserved[2];} UT_SELECTION;
typedef struct UT_STATE {UT_U32 version,size,active,authorized;UT_U64 selected,frequency,submitted,completed,overwritten,unobserved,unavailable;UT_U32 queued,pending;} UT_STATE;
typedef struct UT_EVENT {
 UT_U64 sequence,module,submit_qpc,complete_qpc;
 UT_U32 nt_status,usb_status,requested,actual,captured,flags;
 UT_U16 function;UT_U8 direction,endpoint,pipe_type,setup_valid,reserved[2];
 UT_U8 setup[8],data[UT_SAMPLE_BYTES];
} UT_EVENT;
typedef struct UT_BATCH {UT_U32 version,count;UT_STATE state;UT_EVENT events[UT_BATCH_CAPACITY];} UT_BATCH;
#ifdef __cplusplus
static_assert(sizeof(UT_MODULE)==784);static_assert(sizeof(UT_STATE)==80);static_assert(sizeof(UT_EVENT)==584);
inline bool ValidUSBTraceEvent(const UT_EVENT& e) noexcept {return e.captured<=UT_SAMPLE_BYTES && e.direction<=UT_OUT && e.setup_valid<=1 && e.reserved[0]==0 && e.reserved[1]==0 && (e.flags&~63u)==0;}
#else
C_ASSERT(sizeof(UT_MODULE)==784);C_ASSERT(sizeof(UT_STATE)==80);C_ASSERT(sizeof(UT_EVENT)==584);
#endif
