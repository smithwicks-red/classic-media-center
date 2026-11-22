/* mc06_syntax.h -- HOST ONLY (0.6, from mc05_syntax.h): just enough extra Toolbox types and
   prototypes, on top of host/machdr, to run gcc -fsyntax-only over the
   Mac-only MediaCenter.c (+ H264Stream.c). Nothing here is linked. */
#ifndef MC06_SYNTAX_H
#define MC06_SYNTAX_H
typedef struct { short rowBytes; Rect bounds; } BitMap; typedef struct GrafPortStub { short device; BitMap portBits; RgnHandle visRgn; } *WindowPtr, *CGrafPtr, *GWorldPtr; typedef WindowPtr DialogPtr; typedef unsigned long ByteCount;
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
/* ---- (0.6) the skin: types and prototypes of every Toolbox call the
   0.6 code adds (QDOffscreen, QuickDraw text and colour, TextEdit,
   Menus, Fonts, Resources, Code Fragment Manager, Dialogs) ---- */
typedef struct { unsigned short red, green, blue; } RGBColor;
typedef struct { Ptr baseAddr; short rowBytes; Rect bounds; short pmVersion, packType;
                 long packSize; long hRes, vRes; short pixelType, pixelSize; } PixMap, *PixMapPtr, **PixMapHandle;
typedef struct GDevice { short gdRefNum, gdID, gdType; void *gdITable; short gdResPref;
                 void *gdSearchProc, *gdCompProc; short gdFlags; PixMapHandle gdPMap; } GDevice, *GDPtr, **GDHandle;
typedef struct TERec { Rect destRect, viewRect; short selStart, selEnd, active, caretState, teLength;
                 Handle hText; void *inPort; } TERec, *TEPtr, **TEHandle;
typedef struct MenuInfo { short menuID; } **MenuHandle;
typedef unsigned long GWorldFlags; typedef long CFragConnectionID; typedef unsigned char CFragSymbolClass;
typedef unsigned long CFragArchitecture; typedef unsigned long CFragLoadOptions; typedef short TruncCode;
typedef void *TEClickLoopUPP; typedef void *ModalFilterUPP; typedef SInt16 DialogItemIndexZeroBased;
typedef pascal Boolean (*TEClickLoopProcPtr)(TEPtr pTE);
typedef pascal Boolean (*ModalFilterProcPtr)(DialogPtr d, EventRecord *ev, DialogItemIndex *item);
enum { useTempMem = 4, srcCopy = 0, srcOr = 1, bold = 1, applFont = 1, shiftKey = 0x200,
       truncEnd = 0, truncMiddle = 0x4000, plainDBox = 2, dBoxProc = 1, inMenuBar = 1, inDesk = 0,
       kPowerPCCFragArch = 'pwpc', kFindCFrag = 0, kReferenceCFrag = 1,
       errAEEventNotHandled = -1708, typeLongInteger = 'long' };
typedef OSErr QDErr;
QDErr NewGWorld(GWorldPtr *gw, short depth, const Rect *r, void *ctab, GDHandle gd, GWorldFlags f);
void DisposeGWorld(GWorldPtr gw); PixMapHandle GetGWorldPixMap(GWorldPtr gw);
Boolean LockPixels(PixMapHandle pm); Ptr GetPixBaseAddr(PixMapHandle pm);
void GetGWorld(CGrafPtr *port, GDHandle *gdh); void SetGWorld(CGrafPtr port, GDHandle gdh);
GDHandle GetMainDevice(void); GDHandle GetGDevice(void);
void CopyBits(const BitMap *src, const BitMap *dst, const Rect *sr, const Rect *dr, short mode, RgnHandle mask);
void RGBForeColor(const RGBColor *c); void RGBBackColor(const RGBColor *c);
void ClipRect(const Rect *r); void SetRect(Rect *r, short l, short t, short rr, short b);
Boolean SectRect(const Rect *a, const Rect *b, Rect *o); Boolean PtInRect(Point p, const Rect *r);
void TextFont(short f); void TextSize(short s); void TextFace(short f); void TextMode(short m);
short TextWidth(const void *t, short first, short n); void MoveTo(short h, short v);
void DrawText(const void *t, short first, short n);
short TruncText(short width, Ptr text, short *len, TruncCode where);
void GetFNum(ConstStr255Param name, short *id);
void SetResLoad(Boolean load); Handle GetNamedResource(ResType t, ConstStr255Param name);
short HomeResFile(Handle h); short CurResFile(void);
OSErr GetSharedLibrary(ConstStr63Param lib, CFragArchitecture a, CFragLoadOptions o,
                       CFragConnectionID *c, Ptr *mainAddr, Str255 errName);
OSErr FindSymbol(CFragConnectionID c, ConstStr255Param name, Ptr *sym, CFragSymbolClass *cls);
TEHandle TENew(const Rect *dest, const Rect *view); void TEDispose(TEHandle te);
void TEAutoView(Boolean f, TEHandle te); void TESetClickLoop(TEClickLoopUPP u, TEHandle te);
TEClickLoopUPP NewTEClickLoopUPP(TEClickLoopProcPtr p);
void TEKey(short c, TEHandle te); void TEClick(Point p, Boolean ext, TEHandle te);
void TEIdle(TEHandle te); void TEActivate(TEHandle te); void TEDeactivate(TEHandle te);
void TEUpdate(const void *r, TEHandle te); void TESetSelect(long a, long b, TEHandle te);
void TECut(TEHandle te); void TECopy(TEHandle te); void TEPaste(TEHandle te); void TEDelete(TEHandle te);
OSErr TEFromScrap(void); OSErr TEToScrap(void); long TEGetScrapLength(void); long ZeroScrap(void);
MenuHandle NewMenu(short id, ConstStr255Param title); void AppendMenu(MenuHandle m, ConstStr255Param s);
void AppendResMenu(MenuHandle m, ResType t); void InsertMenu(MenuHandle m, short before);
void DrawMenuBar(void); long MenuSelect(Point p); long MenuKey(short ch); void HiliteMenu(short id);
void EnableItem(MenuHandle m, short i); void DisableItem(MenuHandle m, short i);
void GetMenuItemText(MenuHandle m, short i, Str255 s); short OpenDeskAcc(ConstStr255Param n);
Boolean SystemEdit(short cmd); void SystemClick(const EventRecord *e, WindowPtr w);
WindowPtr NewCWindow(void *st, const Rect *b, ConstStr255Param t, Boolean vis, short proc,
                     WindowPtr behind, Boolean goAway, long refCon);
void DisposeWindow(WindowPtr w); void ShowWindow(WindowPtr w); void SelectWindow(WindowPtr w);
WindowPtr FrontWindow(void); short FindWindow(Point p, WindowPtr *w);
void DragWindow(WindowPtr w, Point p, const Rect *b); void SetPort(void *p); void GlobalToLocal(Point *p);
void BeginUpdate(WindowPtr w); void EndUpdate(WindowPtr w); Boolean StillDown(void);
void Delay(long n, unsigned long *t); UInt32 GetDblTime(void); void SysBeep(short d);
DialogPtr NewDialog(void *st, const Rect *b, ConstStr255Param t, Boolean vis, short proc,
                    WindowPtr behind, Boolean goAway, long refCon, Handle items);
RgnHandle NewRgn(void); void DisposeRgn(RgnHandle r); void GetClip(RgnHandle r); void SetClip(RgnHandle r);
Boolean IsDialogEvent(const EventRecord *e); Boolean DialogSelect(const EventRecord *e, DialogPtr *d, short *item);
OSErr SetDialogDefaultItem(DialogPtr d, short i); void ModalDialog(ModalFilterUPP f, DialogItemIndex *i);
ModalFilterUPP NewModalFilterUPP(ModalFilterProcPtr p); void DisposeModalFilterUPP(ModalFilterUPP u);
void DisposeDialog(DialogPtr d); Handle NewHandleClear(Size n); void HLock(Handle h); void HUnlock(Handle h);
OSErr AEGetParamPtr(const AppleEvent *e, AEKeyword k, DescType t, DescType *at, void *p, Size max, Size *sz);
OSErr FSpCreate(const FSSpec *s, OSType creator, OSType type, ScriptCode sc);
OSErr FSpOpenDF(const FSSpec *s, SInt8 perm, short *ref); OSErr SetEOF(short ref, long n);
OSErr FSWrite(short ref, long *n, const void *p); OSErr FSClose(short ref); OSErr FlushVol(ConstStr63Param v, short vref);
void InitCursor(void);
#endif
