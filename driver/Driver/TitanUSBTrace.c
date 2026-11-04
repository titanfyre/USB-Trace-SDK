#include <ntifs.h>
#include <wdf.h>
#include <usb.h>
#include <usbioctl.h>
#include <initguid.h>
#include <devpkey.h>
#include <ntstrsafe.h>
#include "../SDK/USBTraceProtocol.h"
#include "../Common/DriverAuthorization.h"
#define TAG 'tUtT'
#define MAX_PIPES 32u
typedef struct PIPE {USBD_PIPE_HANDLE handle;UCHAR endpoint,type,interface_number;} PIPE;
typedef struct DEVICE {LIST_ENTRY link;UT_MODULE info;PIPE pipes[MAX_PIPES];ULONG count;BOOLEAN control,listed,ready;} DEVICE;
typedef struct FILE_CONTEXT {PEPROCESS process;TITAN_AUTHORIZATION_STATE auth;BOOLEAN closing;} FILE_CONTEXT;
typedef struct REQUEST_CONTEXT {PURB urb;WDFDEVICE device;USBD_PIPE_HANDLE pipe;UT_EVENT event;ULONGLONG generation;ULONG initial,urb_length;PVOID buffer;PMDL mdl;BOOLEAN payload,configuration,capture,recording;} REQUEST_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE,DeviceContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILE_CONTEXT,FileContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT,RequestContext)
static KSPIN_LOCK Lock;static LIST_ENTRY Devices;static LONG64 NextId;static ULONG ModuleCount,Pending,Head,Count;
static UT_EVENT Ring[UT_RING_CAPACITY];static WDFDEVICE Control;static WDFFILEOBJECT Owner;
static ULONGLONG Deadline,Selected,Generation,Sequence,Submitted,Completed,Overwritten,Unobserved,Unavailable,Frequency;
static BOOLEAN Active;
static REGHANDLE InitProvider;
static const GUID InitGuid={0x70cc1948,0xaafd,0x4e2a,{0x90,0xb6,0xb6,0x6c,0x39,0xf3,0xf8,0x91}};
DRIVER_INITIALIZE DriverEntry;EVT_WDF_DRIVER_DEVICE_ADD AddDevice;
EVT_WDF_DRIVER_UNLOAD DriverUnload;
EVT_WDF_OBJECT_CONTEXT_CLEANUP CleanupDevice;
EVT_WDF_DEVICE_FILE_CREATE CreateFile;EVT_WDF_FILE_CLEANUP CleanupFile;EVT_WDF_FILE_CLOSE CloseFile;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL ControlIo;EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL InternalIo;
EVT_WDF_REQUEST_COMPLETION_ROUTINE Complete;
EVT_WDF_DEVICE_D0_ENTRY D0Entry;EVT_WDF_DEVICE_D0_EXIT D0Exit;
static VOID InitTrace(ULONG stage,BOOLEAN root,NTSTATUS status){WCHAR text[128];if(InitProvider&&NT_SUCCESS(RtlStringCchPrintfW(text,RTL_NUMBER_OF(text),L"Stage=%lu Root=%lu Status=0x%08X",stage,(ULONG)root,(ULONG)status)))EtwWriteString(InitProvider,4,1,NULL,text);}
static BOOLEAN Authorized(VOID){return Owner&&Deadline&&KeQueryInterruptTime()<Deadline;}
static VOID Clear(VOID){Active=FALSE;Selected=0;Head=Count=0;++Generation;RtlZeroMemory(Ring,sizeof(Ring));}
static VOID State(UT_STATE* s){if(Active&&!Authorized())Clear();RtlZeroMemory(s,sizeof(*s));s->version=UT_VERSION;s->size=sizeof(*s);s->authorized=Authorized();s->active=Active;s->selected=Selected;s->frequency=Frequency;s->submitted=Submitted;s->completed=Completed;s->overwritten=Overwritten;s->unobserved=Unobserved;s->unavailable=Unavailable;s->queued=Count;s->pending=Pending;}
static DEVICE* Find(ULONGLONG id){PLIST_ENTRY p;for(p=Devices.Flink;p!=&Devices;p=p->Flink){DEVICE* d=CONTAINING_RECORD(p,DEVICE,link);if(d->info.id==id)return d;}return NULL;}
static VOID Sample(REQUEST_CONTEXT* c,ULONG bytes){
 PVOID buffer=c->buffer;ULONG n=min(bytes,UT_SAMPLE_BYTES);c->event.captured=0;
 if(!n)return;
 if(c->mdl){if(c->mdl->Next){c->event.flags|=UT_PAYLOAD_UNAVAILABLE;return;}n=min(n,MmGetMdlByteCount(c->mdl));buffer=MmGetSystemAddressForMdlSafe(c->mdl,NormalPagePriority|MdlMappingNoExecute);}
 if(!buffer||(ULONG_PTR)buffer<=(ULONG_PTR)MmHighestUserAddress){c->event.flags|=UT_PAYLOAD_UNAVAILABLE;return;}
 __try {RtlCopyMemory(c->event.data,buffer,n);c->event.captured=n;c->event.flags|=UT_PAYLOAD_PRESENT;if(n<bytes)c->event.flags|=UT_PAYLOAD_TRUNCATED;}
 __except(EXCEPTION_EXECUTE_HANDLER){RtlZeroMemory(c->event.data,sizeof(c->event.data));c->event.flags|=UT_PAYLOAD_UNAVAILABLE;}
}
static VOID Parse(PURB urb,REQUEST_CONTEXT* c){
 c->urb=urb;c->urb_length=urb->UrbHeader.Length;c->event.function=urb->UrbHeader.Function;c->event.endpoint=0xff;c->event.pipe_type=0xff;c->event.flags=UT_ENDPOINT_UNKNOWN;
 if(c->event.function==URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER&&c->urb_length>=sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER)){
  c->initial=urb->UrbBulkOrInterruptTransfer.TransferBufferLength;c->buffer=urb->UrbBulkOrInterruptTransfer.TransferBuffer;c->mdl=urb->UrbBulkOrInterruptTransfer.TransferBufferMDL;c->pipe=urb->UrbBulkOrInterruptTransfer.PipeHandle;c->payload=TRUE;c->event.direction=(urb->UrbBulkOrInterruptTransfer.TransferFlags&USBD_TRANSFER_DIRECTION_IN)?UT_IN:UT_OUT;
 }else if(c->event.function==URB_FUNCTION_CONTROL_TRANSFER&&c->urb_length>=sizeof(struct _URB_CONTROL_TRANSFER)){
  c->initial=urb->UrbControlTransfer.TransferBufferLength;c->buffer=urb->UrbControlTransfer.TransferBuffer;c->mdl=urb->UrbControlTransfer.TransferBufferMDL;c->payload=TRUE;c->event.direction=(urb->UrbControlTransfer.TransferFlags&USBD_TRANSFER_DIRECTION_IN)?UT_IN:UT_OUT;RtlCopyMemory(c->event.setup,urb->UrbControlTransfer.SetupPacket,8);c->event.setup_valid=1;
 }else if(c->event.function==URB_FUNCTION_CONTROL_TRANSFER_EX&&c->urb_length>=sizeof(struct _URB_CONTROL_TRANSFER_EX)){
  c->initial=urb->UrbControlTransferEx.TransferBufferLength;c->buffer=urb->UrbControlTransferEx.TransferBuffer;c->mdl=urb->UrbControlTransferEx.TransferBufferMDL;c->payload=TRUE;c->event.direction=(urb->UrbControlTransferEx.TransferFlags&USBD_TRANSFER_DIRECTION_IN)?UT_IN:UT_OUT;RtlCopyMemory(c->event.setup,urb->UrbControlTransferEx.SetupPacket,8);c->event.setup_valid=1;
 }else if((c->event.function==URB_FUNCTION_CLASS_DEVICE||c->event.function==URB_FUNCTION_CLASS_INTERFACE||c->event.function==URB_FUNCTION_CLASS_ENDPOINT||c->event.function==URB_FUNCTION_CLASS_OTHER||c->event.function==URB_FUNCTION_VENDOR_DEVICE||c->event.function==URB_FUNCTION_VENDOR_INTERFACE||c->event.function==URB_FUNCTION_VENDOR_ENDPOINT||c->event.function==URB_FUNCTION_VENDOR_OTHER)&&c->urb_length>=sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST)){
  UCHAR recipient=3,type=0x20;struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST* u=&urb->UrbControlVendorClassRequest;
  switch(c->event.function){case URB_FUNCTION_CLASS_DEVICE:recipient=0;break;case URB_FUNCTION_CLASS_INTERFACE:recipient=1;break;case URB_FUNCTION_CLASS_ENDPOINT:recipient=2;break;case URB_FUNCTION_VENDOR_DEVICE:type=0x40;recipient=0;break;case URB_FUNCTION_VENDOR_INTERFACE:type=0x40;recipient=1;break;case URB_FUNCTION_VENDOR_ENDPOINT:type=0x40;recipient=2;break;case URB_FUNCTION_VENDOR_OTHER:type=0x40;break;default:break;}
  c->initial=u->TransferBufferLength;c->buffer=u->TransferBuffer;c->mdl=u->TransferBufferMDL;c->payload=TRUE;c->event.direction=(u->TransferFlags&USBD_TRANSFER_DIRECTION_IN)?UT_IN:UT_OUT;c->event.setup_valid=1;c->event.setup[0]=(UCHAR)(type|recipient|(c->event.direction==UT_IN?0x80:0));c->event.setup[1]=u->Request;c->event.setup[2]=(UCHAR)u->Value;c->event.setup[3]=(UCHAR)(u->Value>>8);c->event.setup[4]=(UCHAR)u->Index;c->event.setup[5]=(UCHAR)(u->Index>>8);c->event.setup[6]=(UCHAR)c->initial;c->event.setup[7]=(UCHAR)(c->initial>>8);
 }
 if(c->payload&&c->event.function!=URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER){c->event.endpoint=0;c->event.pipe_type=UsbdPipeTypeControl;c->event.flags&=~UT_ENDPOINT_UNKNOWN;}
 if(!c->payload)c->event.flags|=UT_ACTUAL_UNKNOWN;
 c->configuration=c->event.function==URB_FUNCTION_SELECT_CONFIGURATION||c->event.function==URB_FUNCTION_SELECT_INTERFACE;c->event.requested=c->initial;
}
static ULONG Actual(REQUEST_CONTEXT* c){
 if(!c->payload)return 0;
 if(c->event.function==URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER)return c->urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
 if(c->event.function==URB_FUNCTION_CONTROL_TRANSFER)return c->urb->UrbControlTransfer.TransferBufferLength;
 if(c->event.function==URB_FUNCTION_CONTROL_TRANSFER_EX)return c->urb->UrbControlTransferEx.TransferBufferLength;
 return c->urb->UrbControlVendorClassRequest.TransferBufferLength;
}
static BOOLEAN Supported(USHORT function){switch(function){case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:case URB_FUNCTION_CONTROL_TRANSFER:case URB_FUNCTION_CONTROL_TRANSFER_EX:case URB_FUNCTION_CLASS_DEVICE:case URB_FUNCTION_CLASS_INTERFACE:case URB_FUNCTION_CLASS_ENDPOINT:case URB_FUNCTION_CLASS_OTHER:case URB_FUNCTION_VENDOR_DEVICE:case URB_FUNCTION_VENDOR_INTERFACE:case URB_FUNCTION_VENDOR_ENDPOINT:case URB_FUNCTION_VENDOR_OTHER:return TRUE;default:return FALSE;}}
static VOID Pipes(DEVICE* d,REQUEST_CONTEXT* c){
 ULONG offset;UCHAR* base=(UCHAR*)c->urb;ULONG length=c->urb_length;
 if(!USBD_SUCCESS(c->urb->UrbHeader.Status))return;
 if(c->event.function==URB_FUNCTION_SELECT_CONFIGURATION){offset=FIELD_OFFSET(struct _URB_SELECT_CONFIGURATION,Interface);d->count=0;}
 else offset=FIELD_OFFSET(struct _URB_SELECT_INTERFACE,Interface);
 while(offset<=length&&length-offset>=(ULONG)FIELD_OFFSET(USBD_INTERFACE_INFORMATION,Pipes)){
  PUSBD_INTERFACE_INFORMATION in=(PUSBD_INTERFACE_INFORMATION)(base+offset);ULONG n,remaining=length-offset;
  if(in->Length<FIELD_OFFSET(USBD_INTERFACE_INFORMATION,Pipes)||in->Length>remaining||in->NumberOfPipes>MAX_PIPES||in->NumberOfPipes>(in->Length-FIELD_OFFSET(USBD_INTERFACE_INFORMATION,Pipes))/sizeof(USBD_PIPE_INFORMATION))break;
  for(n=0;n<d->count;){if(d->pipes[n].interface_number==in->InterfaceNumber)d->pipes[n]=d->pipes[--d->count];else ++n;}
  for(n=0;n<in->NumberOfPipes&&d->count<MAX_PIPES;++n){PIPE* p=&d->pipes[d->count++];p->handle=in->Pipes[n].PipeHandle;p->endpoint=in->Pipes[n].EndpointAddress;p->type=(UCHAR)in->Pipes[n].PipeType;p->interface_number=in->InterfaceNumber;}
  offset+=in->Length;if(c->event.function==URB_FUNCTION_SELECT_INTERFACE)break;
 }
}
static VOID Finish(WDFREQUEST request,NTSTATUS status,ULONG_PTR information){
 REQUEST_CONTEXT* c=RequestContext(request);DEVICE* d=DeviceContext(c->device);KIRQL old;BOOLEAN compatible=FALSE,eligible=FALSE;ULONGLONG capture_generation=0;
 c->event.complete_qpc=KeQueryPerformanceCounter(NULL).QuadPart;c->event.nt_status=(ULONG)status;
 __try {c->event.usb_status=c->urb->UrbHeader.Status;
  if(NT_SUCCESS(status)&&c->urb->UrbHeader.Function==c->event.function&&c->urb->UrbHeader.Length>=c->urb_length){c->event.actual=Actual(c);compatible=TRUE;}
  else {c->event.actual=0;c->event.flags|=UT_ACTUAL_UNKNOWN;if(c->event.direction==UT_IN)c->event.flags|=UT_PAYLOAD_UNAVAILABLE;}
 }
 __except(EXCEPTION_EXECUTE_HANDLER){c->event.flags|=UT_PAYLOAD_UNAVAILABLE|UT_ACTUAL_UNKNOWN;}
 KeAcquireSpinLock(&Lock,&old);
 if(c->configuration&&NT_SUCCESS(status)){__try {if(c->urb->UrbHeader.Function==c->event.function&&c->urb->UrbHeader.Length>=c->urb_length)Pipes(d,c);else d->count=0;}__except(EXCEPTION_EXECUTE_HANDLER){d->count=0;}}
 if(Active&&Authorized()&&Selected==d->info.id&&(!c->capture||Generation==c->generation)){
  eligible=TRUE;capture_generation=Generation;
  if(!c->capture){c->event.flags|=UT_SUBMITTED_BEFORE_CAPTURE;if(c->event.direction==UT_OUT){c->event.flags|=UT_PAYLOAD_UNAVAILABLE;c->event.captured=0;RtlZeroMemory(c->event.data,sizeof(c->event.data));}}
 }
 KeReleaseSpinLock(&Lock,old);
 if(eligible&&c->payload&&c->event.direction==UT_IN&&compatible&&USBD_SUCCESS(c->event.usb_status))Sample(c,min(c->event.actual,c->initial));
 KeAcquireSpinLock(&Lock,&old);--Pending;
 if(eligible&&Active&&Authorized()&&Selected==d->info.id&&Generation==capture_generation){ULONG slot=(Head+Count)%UT_RING_CAPACITY;++Completed;if(c->event.flags&UT_PAYLOAD_UNAVAILABLE)++Unavailable;Ring[slot]=c->event;if(Count==UT_RING_CAPACITY){Head=(Head+1)%UT_RING_CAPACITY;++Overwritten;}else ++Count;}
 KeReleaseSpinLock(&Lock,old);WdfRequestCompleteWithInformation(request,status,information);
}
VOID Complete(WDFREQUEST request,WDFIOTARGET target,PWDF_REQUEST_COMPLETION_PARAMS params,WDFCONTEXT context){UNREFERENCED_PARAMETER(target);UNREFERENCED_PARAMETER(context);Finish(request,params->IoStatus.Status,params->IoStatus.Information);}
VOID InternalIo(WDFQUEUE queue,WDFREQUEST request,size_t output,size_t input,ULONG code){
 DEVICE* d=DeviceContext(WdfIoQueueGetDevice(queue));PIO_STACK_LOCATION stack=IoGetCurrentIrpStackLocation(WdfRequestWdmGetIrp(request));PURB urb=NULL;BOOLEAN track=FALSE,capture=FALSE,config=FALSE;ULONGLONG generation=0,submit_qpc=0;KIRQL old;REQUEST_CONTEXT* c=NULL;NTSTATUS status;WDF_OBJECT_ATTRIBUTES a;
 UNREFERENCED_PARAMETER(output);UNREFERENCED_PARAMETER(input);
 if(code==IOCTL_INTERNAL_USB_SUBMIT_URB){
  urb=(PURB)stack->Parameters.Others.Argument1;
  /* The INTERNAL major is kernel-only. HID can retain UserMode in the original
     IRP while constructing a kernel URB; that mode is not an URB-origin test. */
  __try {if(urb&&(ULONG_PTR)urb>(ULONG_PTR)MmHighestUserAddress&&urb->UrbHeader.Length>=sizeof(struct _URB_HEADER)){USHORT function=urb->UrbHeader.Function;config=function==URB_FUNCTION_SELECT_CONFIGURATION||function==URB_FUNCTION_SELECT_INTERFACE;KeAcquireSpinLock(&Lock,&old);if(Active&&!Authorized())Clear();capture=Active&&d->ready&&Selected==d->info.id;generation=Generation;if(capture||config||Supported(function)){if(Pending<UT_MAX_PENDING){submit_qpc=KeQueryPerformanceCounter(NULL).QuadPart;++Pending;track=TRUE;}else ++Unobserved;}KeReleaseSpinLock(&Lock,old);}}
  __except(EXCEPTION_EXECUTE_HANDLER){track=FALSE;}
 }
 if(track){
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&a,REQUEST_CONTEXT);status=WdfObjectAllocateContext(request,&a,(PVOID*)&c);
  if(!NT_SUCCESS(status)&&status!=STATUS_OBJECT_NAME_EXISTS){KeAcquireSpinLock(&Lock,&old);--Pending;++Unobserved;KeReleaseSpinLock(&Lock,old);track=FALSE;}
  else{RtlZeroMemory(c,sizeof(*c));c->device=WdfIoQueueGetDevice(queue);__try{Parse(urb,c);}__except(EXCEPTION_EXECUTE_HANDLER){c->urb=urb;c->configuration=FALSE;c->payload=FALSE;c->event.flags=UT_PAYLOAD_UNAVAILABLE;}
   KeAcquireSpinLock(&Lock,&old);c->capture=capture;c->recording=capture&&Active&&Authorized()&&Selected==d->info.id&&Generation==generation;c->generation=generation;c->event.sequence=++Sequence;c->event.module=d->info.id;if(c->recording)++Submitted;
   if(c->payload&&c->event.function==URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER){ULONG n;for(n=0;n<d->count;++n)if(d->pipes[n].handle==c->pipe){c->event.endpoint=d->pipes[n].endpoint;c->event.pipe_type=d->pipes[n].type;c->event.flags&=~UT_ENDPOINT_UNKNOWN;break;}}
   KeReleaseSpinLock(&Lock,old);c->event.submit_qpc=submit_qpc;if(c->recording&&c->payload&&c->event.direction==UT_OUT)Sample(c,c->initial);
  }
 }
 WdfRequestFormatRequestUsingCurrentType(request);
 if(track){WdfRequestSetCompletionRoutine(request,Complete,NULL);if(!WdfRequestSend(request,WdfDeviceGetIoTarget(WdfIoQueueGetDevice(queue)),NULL))Finish(request,WdfRequestGetStatus(request),0);}
 else {WDF_REQUEST_SEND_OPTIONS options;WDF_REQUEST_SEND_OPTIONS_INIT(&options,WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);if(!WdfRequestSend(request,WdfDeviceGetIoTarget(WdfIoQueueGetDevice(queue)),&options))WdfRequestComplete(request,WdfRequestGetStatus(request));}
}
VOID CreateFile(WDFDEVICE device,WDFREQUEST request,WDFFILEOBJECT file){
 PIO_STACK_LOCATION s=IoGetCurrentIrpStackLocation(WdfRequestWdmGetIrp(request));FILE_CONTEXT* f=FileContext(file);KIRQL old;NTSTATUS status=STATUS_SUCCESS;UNREFERENCED_PARAMETER(device);
 if(!s->Parameters.Create.SecurityContext||(s->Parameters.Create.SecurityContext->DesiredAccess&(FILE_READ_DATA|FILE_WRITE_DATA))!=(FILE_READ_DATA|FILE_WRITE_DATA)||WdfFileObjectGetFileName(file)->Length){WdfRequestComplete(request,STATUS_ACCESS_DENIED);return;}
 f->process=IoGetRequestorProcess(WdfRequestWdmGetIrp(request));if(!f->process){WdfRequestComplete(request,STATUS_ACCESS_DENIED);return;}ObReferenceObject(f->process);TitanAuthorizationInitialize(&f->auth,TITAN_AUTHORIZATION_USB_TRACE);
 KeAcquireSpinLock(&Lock,&old);if(Owner)status=STATUS_SHARING_VIOLATION;else{Owner=file;Deadline=0;Clear();Submitted=Completed=Overwritten=Unobserved=Unavailable=0;}KeReleaseSpinLock(&Lock,old);if(!NT_SUCCESS(status)){ObDereferenceObject(f->process);f->process=NULL;}WdfRequestComplete(request,status);
}
VOID CleanupFile(WDFFILEOBJECT file){KIRQL old;KeAcquireSpinLock(&Lock,&old);FileContext(file)->closing=TRUE;if(Owner==file){Clear();Owner=NULL;Deadline=0;}KeReleaseSpinLock(&Lock,old);}
VOID CloseFile(WDFFILEOBJECT file){FILE_CONTEXT* f=FileContext(file);if(f->process)ObDereferenceObject(f->process);}
VOID ControlIo(WDFQUEUE queue,WDFREQUEST request,size_t output,size_t input,ULONG code){
 WDFFILEOBJECT file=WdfRequestGetFileObject(request);FILE_CONTEXT* f=file?FileContext(file):NULL;PVOID buffer=NULL;size_t done=0;NTSTATUS status=STATUS_INVALID_PARAMETER;KIRQL old;UNREFERENCED_PARAMETER(queue);
 if(!f||f->closing||f->process!=IoGetRequestorProcess(WdfRequestWdmGetIrp(request))){WdfRequestComplete(request,STATUS_ACCESS_DENIED);return;}
 if(code==UT_CHALLENGE&&input==0&&output==sizeof(TITAN_AUTHORIZATION_CHALLENGE)){status=WdfRequestRetrieveOutputBuffer(request,output,&buffer,NULL);if(NT_SUCCESS(status)){status=TitanAuthorizationChallenge(&f->auth,buffer);if(NT_SUCCESS(status))done=output;}}
 else if(code==UT_AUTHORIZE&&input==sizeof(TITAN_AUTHORIZATION_TOKEN)&&output==0){status=WdfRequestRetrieveInputBuffer(request,input,&buffer,NULL);if(NT_SUCCESS(status)){status=TitanAuthorizationAccept(&f->auth,buffer);if(NT_SUCCESS(status)){KeAcquireSpinLock(&Lock,&old);if(Owner==file&&!f->closing)Deadline=(ULONGLONG)f->auth.deadline;else status=STATUS_FILE_CLOSED;KeReleaseSpinLock(&Lock,old);}}}
 else{
  if(input){status=WdfRequestRetrieveInputBuffer(request,input,&buffer,NULL);}else if(output){status=WdfRequestRetrieveOutputBuffer(request,output,&buffer,NULL);}else status=STATUS_SUCCESS;
  if(NT_SUCCESS(status)){KeAcquireSpinLock(&Lock,&old);
   if(f->closing||Owner!=file)status=STATUS_FILE_CLOSED;
   else if(code==UT_ENUM&&input==0&&output==sizeof(UT_MODULES)){UT_MODULES* m=buffer;PLIST_ENTRY p;RtlZeroMemory(m,sizeof(*m));m->version=UT_VERSION;for(p=Devices.Flink;p!=&Devices&&m->count<UT_MAX_MODULES;p=p->Flink)m->modules[m->count++]=CONTAINING_RECORD(p,DEVICE,link)->info;done=sizeof(*m);}
   else if(code==UT_STATUS&&input==0&&output==sizeof(UT_STATE)){State(buffer);done=sizeof(UT_STATE);}
   else if(code==UT_STOP&&input==0&&output==0){Clear();}
   else if(code==UT_START&&input==sizeof(UT_SELECTION)&&output==0){UT_SELECTION* s=buffer;DEVICE* d=Find(s->module);if(!Authorized())status=STATUS_ACCESS_DENIED;else if(s->reserved[0]||s->reserved[1])status=STATUS_INVALID_PARAMETER;else if(!d||!d->ready)status=STATUS_DEVICE_NOT_READY;else{Clear();Selected=s->module;Active=TRUE;}}
   else if(code==UT_READ&&input==0&&output==sizeof(UT_BATCH)){UT_BATCH* b=buffer;ULONG n;RtlZeroMemory(b,sizeof(*b));b->version=UT_VERSION;if(!Authorized()){Clear();status=STATUS_ACCESS_DENIED;}else{b->count=min(Count,UT_BATCH_CAPACITY);for(n=0;n<b->count;++n){b->events[n]=Ring[Head];RtlZeroMemory(&Ring[Head],sizeof(Ring[Head]));Head=(Head+1)%UT_RING_CAPACITY;--Count;}State(&b->state);done=sizeof(*b);}}
   else status=STATUS_INVALID_PARAMETER;
   KeReleaseSpinLock(&Lock,old);
  }
 }
 WdfRequestCompleteWithInformation(request,status,NT_SUCCESS(status)?done:0);
}
NTSTATUS D0Entry(WDFDEVICE device,WDF_POWER_DEVICE_STATE previous){DEVICE* d=DeviceContext(device);KIRQL old;UNREFERENCED_PARAMETER(previous);KeAcquireSpinLock(&Lock,&old);d->ready=TRUE;d->info.ready=1;KeReleaseSpinLock(&Lock,old);return STATUS_SUCCESS;}
NTSTATUS D0Exit(WDFDEVICE device,WDF_POWER_DEVICE_STATE next){DEVICE* d=DeviceContext(device);KIRQL old;UNREFERENCED_PARAMETER(next);KeAcquireSpinLock(&Lock,&old);d->ready=FALSE;d->info.ready=0;if(Selected==d->info.id)Clear();KeReleaseSpinLock(&Lock,old);return STATUS_SUCCESS;}
VOID CleanupDevice(WDFOBJECT object){DEVICE* d=DeviceContext(object);KIRQL old;KeAcquireSpinLock(&Lock,&old);if(d->listed){RemoveEntryList(&d->link);--ModuleCount;d->listed=FALSE;if(Selected==d->info.id)Clear();}if(d->control&&Control==(WDFDEVICE)object){Control=NULL;Clear();Owner=NULL;Deadline=0;}KeReleaseSpinLock(&Lock,old);}
static BOOLEAN Root(PWDFDEVICE_INIT init){WCHAR ids[512];ULONG bytes=0;NTSTATUS s=WdfFdoInitQueryProperty(init,DevicePropertyHardwareID,sizeof(ids),ids,&bytes);ULONG pos=0;if(!NT_SUCCESS(s)||bytes>sizeof(ids)||bytes%sizeof(WCHAR))return FALSE;while(pos<bytes/sizeof(WCHAR)&&ids[pos]){UNICODE_STRING value;ULONG n=0;while(pos+n<bytes/sizeof(WCHAR)&&ids[pos+n])++n;if(pos+n==bytes/sizeof(WCHAR))break;RtlInitUnicodeString(&value,ids+pos);{UNICODE_STRING match=RTL_CONSTANT_STRING(L"ROOT\\TitanUSBTrace");if(RtlEqualUnicodeString(&value,&match,TRUE))return TRUE;}pos+=n+1;}return FALSE;}
NTSTATUS AddDevice(WDFDRIVER driver,PWDFDEVICE_INIT init){
 BOOLEAN root=Root(init);WDF_OBJECT_ATTRIBUTES a;WDFDEVICE device;DEVICE* d;WDF_IO_QUEUE_CONFIG q;WDFQUEUE queue;NTSTATUS status;WDF_FILEOBJECT_CONFIG files;WDF_PNPPOWER_EVENT_CALLBACKS power;KIRQL old;UNREFERENCED_PARAMETER(driver);
 InitTrace(1,root,STATUS_SUCCESS);
 if(root){DECLARE_CONST_UNICODE_STRING(sddl,L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");WdfDeviceInitSetDeviceType(init,UT_DEVICE_TYPE);WdfDeviceInitSetExclusive(init,TRUE);WdfDeviceInitSetCharacteristics(init,FILE_DEVICE_SECURE_OPEN|FILE_AUTOGENERATED_DEVICE_NAME,FALSE);status=WdfDeviceInitAssignSDDLString(init,&sddl);InitTrace(2,root,status);if(!NT_SUCCESS(status))return status;WDF_FILEOBJECT_CONFIG_INIT(&files,CreateFile,CloseFile,CleanupFile);WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&a,FILE_CONTEXT);a.ExecutionLevel=WdfExecutionLevelPassive;a.SynchronizationScope=WdfSynchronizationScopeNone;WdfDeviceInitSetFileObjectConfig(init,&files,&a);}
 else{WdfFdoInitSetFilter(init);WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power);power.EvtDeviceD0Entry=D0Entry;power.EvtDeviceD0Exit=D0Exit;WdfDeviceInitSetPnpPowerEventCallbacks(init,&power);}
 WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&a,DEVICE);a.EvtCleanupCallback=CleanupDevice;a.SynchronizationScope=WdfSynchronizationScopeNone;a.ExecutionLevel=root?WdfExecutionLevelPassive:WdfExecutionLevelDispatch;status=WdfDeviceCreate(&init,&a,&device);InitTrace(3,root,status);if(!NT_SUCCESS(status))return status;d=DeviceContext(device);d->control=root;
 if(root){DECLARE_CONST_UNICODE_STRING(link,L"\\DosDevices\\Global\\TitanUSBTrace");KeAcquireSpinLock(&Lock,&old);if(Control){KeReleaseSpinLock(&Lock,old);return STATUS_OBJECT_NAME_COLLISION;}Control=device;KeReleaseSpinLock(&Lock,old);status=WdfDeviceCreateSymbolicLink(device,&link);InitTrace(4,root,status);if(!NT_SUCCESS(status))return status;}
 else{ULONG needed=0;DEVPROPTYPE type=0;d->info.id=(ULONGLONG)InterlockedIncrement64(&NextId);(VOID)IoGetDevicePropertyData(WdfDeviceWdmGetPhysicalDevice(device),&DEVPKEY_Device_InstanceId,LOCALE_NEUTRAL,0,sizeof(d->info.instance),d->info.instance,&needed,&type);if(type!=DEVPROP_TYPE_STRING||needed>sizeof(d->info.instance))d->info.instance[0]=0;d->info.instance[RTL_NUMBER_OF(d->info.instance)-1]=0;(VOID)WdfDeviceQueryProperty(device,DevicePropertyDeviceDescription,sizeof(d->info.description),d->info.description,&needed);d->info.description[RTL_NUMBER_OF(d->info.description)-1]=0;KeAcquireSpinLock(&Lock,&old);if(ModuleCount<UT_MAX_MODULES){InsertTailList(&Devices,&d->link);++ModuleCount;d->listed=TRUE;}KeReleaseSpinLock(&Lock,old);}
 WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&q,root?WdfIoQueueDispatchSequential:WdfIoQueueDispatchParallel);q.PowerManaged=WdfFalse;if(root)q.EvtIoDeviceControl=ControlIo;else q.EvtIoInternalDeviceControl=InternalIo;WDF_OBJECT_ATTRIBUTES_INIT(&a);a.ExecutionLevel=root?WdfExecutionLevelPassive:WdfExecutionLevelDispatch;status=WdfIoQueueCreate(device,&q,&a,&queue);InitTrace(5,root,status);return status;
}
VOID DriverUnload(WDFDRIVER driver){UNREFERENCED_PARAMETER(driver);if(InitProvider){EtwUnregister(InitProvider);InitProvider=0;}}
NTSTATUS DriverEntry(PDRIVER_OBJECT object,PUNICODE_STRING path){WDF_DRIVER_CONFIG c;WDF_OBJECT_ATTRIBUTES a;LARGE_INTEGER f;NTSTATUS status;KeInitializeSpinLock(&Lock);InitializeListHead(&Devices);KeQueryPerformanceCounter(&f);Frequency=f.QuadPart;if(!NT_SUCCESS(EtwRegister(&InitGuid,NULL,NULL,&InitProvider)))InitProvider=0;WDF_DRIVER_CONFIG_INIT(&c,AddDevice);c.EvtDriverUnload=DriverUnload;WDF_OBJECT_ATTRIBUTES_INIT(&a);status=WdfDriverCreate(object,path,&a,&c,WDF_NO_HANDLE);if(!NT_SUCCESS(status)&&InitProvider){EtwUnregister(InitProvider);InitProvider=0;}return status;}
