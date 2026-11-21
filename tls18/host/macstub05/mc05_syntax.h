/* mc05_syntax.h -- HOST ONLY (J5): just enough extra Toolbox types and
   prototypes, on top of host/machdr, to run gcc -fsyntax-only over the
   Mac-only MediaCenter.c (+ H264Stream.c). Nothing here is linked. */
#ifndef MC05_SYNTAX_H
#define MC05_SYNTAX_H
typedef struct GrafPortStub { RgnHandle visRgn; } *WindowPtr; typedef WindowPtr DialogPtr; typedef unsigned long ByteCount;
typedef AEDesc AEAddressDesc; typedef AEDesc AEDescList; typedef AEDesc AERecord;
typedef Handle AliasHandle; typedef void *ControlHandle; typedef void **CursHandle;
typedef struct { short x; } Cursor; typedef void *ComponentInstance; typedef void *Component;
typedef struct { OSType componentType, componentSubType, componentManufacturer; UInt32 componentFlags, componentFlagsMask; } ComponentDescription;
typedef struct ListRec { Rect rView; char selFlags; } ListRec, *ListPtr, **ListHandle; typedef Rect ListBounds; typedef Point Cell;
typedef short DialogItemIndex; typedef SInt32 AEReturnID; typedef SInt32 AETransactionID; typedef SInt32 AESendMode; typedef SInt16 AESendPriority;
typedef void *AEIdleUPP; typedef void *AEFilterUPP; typedef OSType DescType; typedef OSType AEKeyword;
enum { updateMask = 0x40, typeAlias = 'alis', typeApplSignature = 'sign', typeType = 'type', typeEnumerated = 'enum',
       typeChar = 'TEXT', typeNull = 'null', keyDirectObject = '----', kAENoReply = 1, kAENeverInteract = 0x30,
       kAENormalPriority = 0, kAEDefaultTimeout = -1, kAutoGenerateReturnID = -1, kAnyTransactionID = 0,
       watchCursor = 4, teFlushDefault = 0, inDrag = 4, inContent = 3, inSysWindow = 2, activeFlag = 1,
       suspendResumeMessage = 1, resumeFlag = 1,
       autoKey = 5, charCodeMask = 0xFF, cmdKey = 0x100,
       mDownMask = 2, mUpMask = 4, keyDownMask = 8, keyUpMask = 16, autoKeyMask = 32 };
CursHandle GetCursor(short id); void SetCursor(const Cursor *c);
OSErr AECreateDesc(DescType t, const void *p, Size n, AEDesc *d);
OSErr AECreateAppleEvent(OSType c, OSType i, const AEAddressDesc *t, AEReturnID r, AETransactionID x, AppleEvent *e);
OSErr AECreateList(const void *p, Size n, Boolean isRec, AEDescList *l);
OSErr AEPutKeyPtr(AERecord *r, AEKeyword k, DescType t, const void *p, Size n);
OSErr AEPutKeyDesc(AERecord *r, AEKeyword k, const AEDesc *d);
OSErr AECoerceDesc(const AEDesc *d, DescType t, AEDesc *r);
OSErr AEPutParamDesc(AppleEvent *e, AEKeyword k, const AEDesc *d);
OSErr AEPutParamPtr(AppleEvent *e, AEKeyword k, DescType t, const void *p, Size n);
OSErr AESend(const AppleEvent *e, AppleEvent *r, AESendMode m, AESendPriority p, SInt32 t, AEIdleUPP i, AEFilterUPP f);
OSErr AEDisposeDesc(AEDesc *d);
enum { btnCtrl = 0, chkCtrl = 1, ctrlItem = 4, editText = 16, statText = 8, userItem = 0, itemDisable = 128,
       kExtensionFolderType = 'extn', kSystemFolderType = 'macs', lOnlyOne = -128, movableDBoxProc = 5 };
#endif
