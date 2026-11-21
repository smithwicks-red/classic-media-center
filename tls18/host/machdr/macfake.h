/* macfake.h -- HOST-ONLY stand-in for the Universal Interfaces 3.4.1
   declarations used by the macTLS glue, https_client.c and HTTPSGet.c.
   Types/field names follow UI 3.4.1; constant VALUES are only what the
   host fake needs and are not guaranteed to equal Apple's. */
#ifndef MACFAKE_H
#define MACFAKE_H
#include <stddef.h>
#include <stdint.h>
#define pascal
#ifndef TARGET_API_MAC_CARBON
#define TARGET_API_MAC_CARBON 0
#endif
typedef unsigned char  UInt8;   typedef signed char  SInt8;
typedef unsigned short UInt16;  typedef short        SInt16;
typedef uint32_t       UInt32;  typedef int32_t      SInt32;
typedef SInt16 OSErr; typedef SInt32 OSStatus; typedef unsigned char Boolean;
typedef char *Ptr; typedef Ptr *Handle; typedef long Size;
typedef UInt32 FourCharCode; typedef FourCharCode OSType; typedef FourCharCode ResType;
typedef unsigned char Str255[256], Str63[64], Str31[32], Str32[33];
typedef unsigned char *StringPtr; typedef const unsigned char *ConstStr255Param, *ConstStr63Param;
typedef struct { short v, h; } Point;
typedef struct { short top, left, bottom, right; } Rect;
typedef struct { UInt32 hi, lo; } UnsignedWide;
typedef long (*ProcPtr)(void);
typedef SInt16 ScriptCode;
#ifndef true
#define true 1
#define false 0
#endif
enum { noErr = 0, fnfErr = -43, eofErr = -39, memFullErr = -108, paramErr = -50, userCanceledErr = -128 };
/* Memory.h */
Ptr  NewPtr(Size n); Ptr NewPtrClear(Size n); void DisposePtr(Ptr p); OSErr MemError(void);
void MaxApplZone(void); void BlockMoveData(const void *s, void *d, Size n);
/* Events.h / Timer.h / OSUtils.h / DateTimeUtils.h */
UInt32 TickCount(void); void GetMouse(Point *p); void Microseconds(UnsignedWide *u);
void GetDateTime(UInt32 *secs);
typedef struct { SInt32 latitude, longitude; union { SInt8 dlsDelta; SInt32 gmtDelta; } u; } MachineLocation;
void ReadLocation(MachineLocation *loc);
typedef struct { UInt16 what; UInt32 message; UInt32 when; Point where; UInt16 modifiers; } EventRecord;
typedef void *RgnHandle;
enum { nullEvent = 0, mouseDown = 1, keyDown = 3, updateEvt = 6, activateEvt = 8, osEvt = 15, kHighLevelEvent = 23, everyEvent = 0xFFFF };
Boolean WaitNextEvent(UInt16 mask, EventRecord *ev, UInt32 sleep, RgnHandle rgn);
void FlushEvents(UInt16 mask, UInt16 stop);
/* Files.h / Folders.h / Script.h */
typedef struct { short vRefNum; long parID; Str63 name; } FSSpec;
typedef struct { short ioVRefNum; long ioWDDirID; } WDPBRec;
enum { fsCurPerm = 0, fsRdPerm = 1, fsWrPerm = 2, fsRdWrPerm = 3, fsFromStart = 1, fsFromLEOF = 2 };
enum { smRoman = 0, smSystemScript = -1 };
enum { kOnSystemDisk = -32768, kDontCreateFolder = 0, kCreateFolder = 1 };
#define kPreferencesFolderType 0x70726566UL /* 'pref' */
#define kDesktopFolderType     0x6465736BUL /* 'desk' */
#define kTemporaryFolderType   0x74656d70UL /* 'temp' */
OSErr FindFolder(short vRefNum, OSType folderType, Boolean create, short *foundVRefNum, long *foundDirID);
OSErr FSMakeFSSpec(short vRefNum, long dirID, ConstStr255Param name, FSSpec *spec);
OSErr FSpCreate(const FSSpec *spec, OSType creator, OSType type, ScriptCode script);
OSErr FSpDelete(const FSSpec *spec);
OSErr FSpOpenDF(const FSSpec *spec, SInt8 perm, short *refNum);
OSErr FSRead(short ref, long *count, void *buf);
OSErr FSWrite(short ref, long *count, const void *buf);
OSErr FSClose(short ref);
OSErr SetEOF(short ref, long eof); OSErr GetEOF(short ref, long *eof);
OSErr SetFPos(short ref, short mode, long off); OSErr GetFPos(short ref, long *pos);
OSErr FlushVol(ConstStr63Param name, short vRefNum);
OSErr PBHSetVolSync(WDPBRec *pb);
/* (b19) catalog / volume info / rename / folders -- field names as in
   Universal Interfaces 3.4.1, only the fields Media Center uses. */
enum { dupFNErr = -48, dirNFErr = -120, dskFulErr = -34, ioDirMask = 0x10 };
typedef struct { StringPtr ioNamePtr; short ioVRefNum; short ioFDirIndex; SInt8 ioFlAttrib; long ioDrDirID; } DirInfo;
typedef struct { StringPtr ioNamePtr; short ioVRefNum; short ioFDirIndex; SInt8 ioFlAttrib; long ioDirID; long ioFlLgLen; } HFileInfo;
typedef union { HFileInfo hFileInfo; DirInfo dirInfo; } CInfoPBRec;
typedef CInfoPBRec *CInfoPBPtr;
OSErr PBGetCatInfoSync(CInfoPBPtr pb);
typedef struct { StringPtr ioNamePtr; short ioVRefNum; short ioVolIndex; long ioVAlBlkSiz; unsigned short ioVFrBlk; } HVolumeParam;
typedef union { HVolumeParam volumeParam; } HParamBlockRec;
typedef HParamBlockRec *HParmBlkPtr;
OSErr PBHGetVInfoSync(HParmBlkPtr pb);
OSErr FSpDirCreate(const FSSpec *spec, ScriptCode script, long *createdDirID);
OSErr FSpRename(const FSSpec *spec, ConstStr255Param newName);
/* Processes.h */
typedef struct { UInt32 highLongOfPSN, lowLongOfPSN; } ProcessSerialNumber;
typedef struct { UInt32 processInfoLength; StringPtr processName; ProcessSerialNumber processNumber;
  UInt32 processType; OSType processSignature; UInt32 processMode; Ptr processLocation; UInt32 processSize;
  UInt32 processFreeMem; ProcessSerialNumber processLauncher; UInt32 processLaunchDate; UInt32 processActiveTime;
  FSSpec *processAppSpec; } ProcessInfoRec;
OSErr GetCurrentProcess(ProcessSerialNumber *psn);
OSErr GetProcessInformation(const ProcessSerialNumber *psn, ProcessInfoRec *info);
/* AppleEvents.h */
typedef struct { OSType descriptorType; Handle dataHandle; } AEDesc;
typedef AEDesc AppleEvent;
typedef OSErr (*AEEventHandlerProcPtr)(const AppleEvent *e, AppleEvent *r, long refcon);
typedef AEEventHandlerProcPtr AEEventHandlerUPP;
#define NewAEEventHandlerUPP(p) ((AEEventHandlerUPP)(p))
#define kCoreEventClass     0x61657674UL
#define kAEOpenApplication  0x6f617070UL
#define kAEOpenDocuments    0x6f646f63UL
#define kAEPrintDocuments   0x70646f63UL
#define kAEQuitApplication  0x71756974UL
OSErr AEInstallEventHandler(UInt32 cls, UInt32 id, AEEventHandlerUPP h, long refcon, Boolean sys);
OSErr AEProcessAppleEvent(const EventRecord *ev);
/* Quickdraw/Fonts/Windows/Menus/TextEdit/Dialogs (init only) */
typedef void *GrafPtr;
typedef struct { char privates[76]; long randSeed; struct { Rect bounds; } screenBits; GrafPtr thePort; } QDGlobals;
extern QDGlobals qd;
void InitGraf(void *port); void InitFonts(void); void InitWindows(void); void InitMenus(void);
void TEInit(void); void InitDialogs(void *p); void InitCursor(void);
/* Threads.h */
OSErr YieldToAnyThread(void);
/* Gestalt.h */
OSErr Gestalt(OSType sel, long *resp);
#endif
