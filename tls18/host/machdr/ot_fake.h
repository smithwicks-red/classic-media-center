/* ot_fake.h -- HOST-ONLY subset of OpenTransport.h / OpenTptInternet.h */
#ifndef OT_FAKE_H
#define OT_FAKE_H
#include "macfake.h"
typedef SInt32 OTResult; typedef UInt32 OTEventCode; typedef UInt32 OTByteCount;
typedef UInt32 OTFlags; typedef UInt32 OTOpenFlags; typedef UInt16 OTAddressType; typedef SInt32 OTSequence;
typedef struct OTFakeEP *EndpointRef; typedef EndpointRef ProviderRef;
typedef struct OTConfiguration *OTConfigurationRef;
typedef struct OpaqueOTClientContextPtr *OTClientContextPtr;
typedef struct { OTByteCount maxlen; OTByteCount len; UInt8 *buf; } TNetbuf;
typedef struct { TNetbuf addr; TNetbuf opt; TNetbuf udata; OTSequence sequence; } TCall;
typedef struct { TNetbuf addr; UInt32 qlen; } TBind;
typedef struct { TNetbuf opt; OTFlags flags; } TOptMgmt;
typedef struct { OTByteCount len; UInt32 level; UInt32 name; UInt32 status; UInt32 value[1]; } TOption;
typedef struct { SInt32 addr, options, tsdu, etsdu, connect, discon; UInt32 servtype, flags; } TEndpointInfo;
typedef struct { OTAddressType fAddressType; char fName[256]; } DNSAddress;
typedef UInt16 InetPort; typedef UInt32 InetHost;
typedef struct { OTAddressType fAddressType; InetPort fPort; InetHost fHost; UInt8 fUnused[8]; } InetAddress;
typedef void (*OTNotifyProcPtr)(void *contextPtr, OTEventCode code, OTResult result, void *cookie);
typedef OTNotifyProcPtr OTNotifyUPP;
#define NewOTNotifyUPP(p) ((OTNotifyUPP)(p))
#define kTCPName "tcp"
enum { AF_DNS = 42, AF_INET_OT = 2 };
enum { T_LISTEN = 0x0001, T_CONNECT = 0x0002, T_DATA = 0x0004, T_EXDATA = 0x0008, T_DISCONNECT = 0x0010,
       T_ERROR = 0x0020, T_UDERR = 0x0040, T_ORDREL = 0x0080, T_GODATA = 0x0100, T_GOEXDATA = 0x0200 };
#define T_BINDCOMPLETE    0x20000001UL
#define T_PASSCON         0x1000
#define T_ACCEPTCOMPLETE  0x20000003UL
#define T_DISCONNECTCOMPLETE 0x20000005UL
#define T_OPTMGMTCOMPLETE 0x20000006UL
#define T_OPENCOMPLETE    0x20000007UL
#define kOTSyncIdleEvent  0x40000003UL
enum { T_UNINIT = 0, T_UNBND, T_IDLE, T_OUTCON, T_INCON, T_DATAXFER, T_OUTREL, T_INREL };
enum { kOTNoError = 0, kOTBadAddressErr = -3150, kOTBadNameErr = -3153, kOTOutStateErr = -3155,
       kOTLookErr = -3158, kOTFlowErr = -3161, kOTNoDataErr = -3162, kOTStateChangeErr = -3167 };
enum { T_NEGOTIATE = 0x0004, T_SUCCESS = 0x0020, INET_TCP = 6, TCP_NODELAY = 1, T_MORE = 0x0001 };
OSStatus InitOpenTransport(void); void CloseOpenTransport(void);
OTConfigurationRef OTCreateConfiguration(const char *path);
EndpointRef OTOpenEndpoint(OTConfigurationRef cfg, OTOpenFlags f, TEndpointInfo *info, OSStatus *err);
OSStatus OTAsyncOpenEndpoint(OTConfigurationRef cfg, OTOpenFlags f, TEndpointInfo *info, OTNotifyUPP n, void *ctx);
OSStatus OTBind(EndpointRef ep, TBind *req, TBind *ret);
OTByteCount OTInitDNSAddress(DNSAddress *a, char *str);
OSStatus OTConnect(EndpointRef ep, TCall *snd, TCall *rcv);
OSStatus OTRcvConnect(EndpointRef ep, TCall *call);
OTResult OTSnd(EndpointRef ep, void *buf, OTByteCount n, OTFlags flags);
OTResult OTRcv(EndpointRef ep, void *buf, OTByteCount n, OTFlags *flags);
OTResult OTLook(EndpointRef ep);
OTResult OTGetEndpointState(EndpointRef ep);
OSStatus OTSndOrderlyDisconnect(EndpointRef ep); OSStatus OTRcvOrderlyDisconnect(EndpointRef ep);
OSStatus OTSndDisconnect(EndpointRef ep, TCall *call); OSStatus OTRcvDisconnect(EndpointRef ep, void *discon);
OSStatus OTCloseProvider(ProviderRef ep);
OSStatus OTSetAsynchronous(ProviderRef ep); OSStatus OTSetSynchronous(ProviderRef ep);
OSStatus OTSetNonBlocking(ProviderRef ep); OSStatus OTSetBlocking(ProviderRef ep);
OSStatus OTUseSyncIdleEvents(ProviderRef ep, Boolean b);
OSStatus OTOptionManagement(EndpointRef ep, TOptMgmt *req, TOptMgmt *ret);
void OTMemzero(void *p, OTByteCount n);
/* (J5) passive side: listener + acceptor endpoints (fake_mac.c, POSIX) */
typedef UInt32 OTQLen; typedef SInt32 OTReason;
typedef struct { TNetbuf udata; OTReason reason; OTSequence sequence; } TDiscon;
#define kOTInvalidEndpointRef ((EndpointRef)0)
enum { kOTAddressBusyErr = -3172 };
void OTInitInetAddress(InetAddress *a, InetPort port, InetHost host);
OSStatus OTListen(EndpointRef ep, TCall *call);
OSStatus OTAccept(EndpointRef listener, EndpointRef resRef, TCall *call);
OSStatus OTUnbind(EndpointRef ep);
/* host-only test hooks: g_fake_ot_bind_any = 1 lets a 0.0.0.0 bind
   through (default: every listener binds 127.0.0.1 only) */
extern int g_fake_ot_bind_any;
/* host-only driver hook: deliver pending "interrupt-time" notifier events */
void FakeOT_Poll(void);
/* (J5b) notifier-mode listener/acceptors (QTSniff Q2 calls) */
typedef struct { UInt32 hi, lo; } OTTimeStamp;
void OTGetTimeStamp(OTTimeStamp *t);
UInt32 OTElapsedMilliseconds(OTTimeStamp *t);
Boolean OTEnterNotifier(ProviderRef ep);
void OTLeaveNotifier(ProviderRef ep);
/* host test hooks */
void FakeOT_Starve(double secs);   /* run only "deferred tasks" for secs */
extern unsigned long g_fake_ot_enter_depth, g_fake_ot_starve_notifies;
#endif
