/* MPW trap macros → GAS inline trap words */

/* Memory Manager (OS traps) */
.macro _InitZone;      .word 0xA019; .endm
.macro _GetZone;       .word 0xA11A; .endm
.macro _SetZone;       .word 0xA01B; .endm
.macro _NewPtr;        .word 0xA11E; .endm
.macro _NewPtrSys;     .word 0xA51E; .endm
.macro _NewPtrSysClear; .word 0xA71E; .endm
.macro _NewHandle;     .word 0xA122; .endm
.macro _NewHandleSys;  .word 0xA522; .endm
.macro _NewHandleSysClear; .word 0xA722; .endm
.macro _NewEmptyHandle; .word 0xA166; .endm
.macro _DisposeHandle; .word 0xA023; .endm
.macro _DisposHandle;  .word 0xA023; .endm
.macro _DisposePtr;    .word 0xA01F; .endm
.macro _ReallocHandle; .word 0xA027; .endm
.macro _SetHandleSize; .word 0xA024; .endm
.macro _GetHandleSize; .word 0xA025; .endm
.macro _HandleZone;    .word 0xA126; .endm
.macro _RecoverHandle; .word 0xA128; .endm
.macro _ResrvMem;      .word 0xA040; .endm
.macro _MaxBlock;      .word 0xA061; .endm
.macro _InitApplZone;  .word 0xA02C; .endm
.macro _BlockMove;     .word 0xA02E; .endm
.macro _Random;        .word 0xA861; .endm

/* File Manager */
.macro _Open;          .word 0xA000; .endm
.macro _Close;         .word 0xA001; .endm
.macro _Read;          .word 0xA002; .endm
.macro _Write;         .word 0xA003; .endm
.macro _Create;        .word 0xA008; .endm
.macro _GetEOF;        .word 0xA011; .endm
.macro _SetEOF;        .word 0xA012; .endm
.macro _FlushFile;     .word 0xA013; .endm
.macro _OpenRF;        .word 0xA00A; .endm

/* Resource Manager (Toolbox traps) */
.macro _GetResource;   .word 0xA9A0; .endm
.macro _GetNamedResource; .word 0xA9A1; .endm
.macro _GetResLoad;    .word 0xA9AB; .endm
.macro _CountResources; .word 0xA99C; .endm
.macro _GetIndResource; .word 0xA99D; .endm
.macro _AddResource;   .word 0xA9AB; .endm
.macro _RmveResource;  .word 0xA9AD; .endm
.macro _UpdateResFile; .word 0xA999; .endm
.macro _WriteResource; .word 0xA9B0; .endm
.macro _ReleaseResource; .word 0xA9A3; .endm
.macro _CloseResFile;  .word 0xA99A; .endm
.macro _ResourceDispatch; .word 0xA822; .endm
.macro _FigmentDispatch; .word 0xA15C; .endm

/* String */
.macro _CmpString;     .word 0xA03C; .endm

/* Misc */
.macro _PrGlue;        .word 0xA8FD; .endm
.macro _PROpenDriver;  .word 0xA8FD; .endm
.macro _TrashZone;     .word 0xA02C; .endm  /* may need different trap */

/* Segment Loader */
.macro _GetResAttrs;   .word 0xA9A6; .endm
.macro _GetResInfo;    .word 0xA9A8; .endm
.macro _HLock;         .word 0xA029; .endm
.macro _HUnlock;       .word 0xA02A; .endm
.macro _HPurge;        .word 0xA049; .endm
.macro _HNoPurge;      .word 0xA04A; .endm
.macro _MoveHHi;       .word 0xA064; .endm
.macro _StripAddress;  .word 0xA055; .endm
.macro _ReadXPRam;     .word 0xA051; .endm
.macro _InitResources; .word 0xA995; .endm
.macro _RsrcZoneInit;  .word 0xA996; .endm
.macro _SetResLoad;    .word 0xA99B; .endm
.macro _SizeRsrc;      .word 0xA9A5; .endm
.macro _DetachResource; .word 0xA992; .endm
.macro _HomeResFile;   .word 0xA9A4; .endm
.macro _UseResFile;    .word 0xA998; .endm
.macro _CurResFile;    .word 0xA994; .endm
.macro _SetResPurge;   .word 0xA993; .endm
.macro _SetResAttrs;   .word 0xA9A7; .endm
.macro _SetResInfo;    .word 0xA9A9; .endm
.macro _ChangedResource; .word 0xA9AA; .endm
.macro _OpenRFPerm;    .word 0xA9C4; .endm
.macro _UniqueID;      .word 0xA9C1; .endm
.macro _GetResFileAttrs; .word 0xA9F6; .endm
.macro _SetResFileAttrs; .word 0xA9F7; .endm
.macro _LoadResource;  .word 0xA9A2; .endm
.macro _ResError;      .word 0xA9AF; .endm
.macro _CreateResFile; .word 0xA9B1; .endm
.macro _Count1Resources; .word 0xA80D; .endm
.macro _Get1Resource;  .word 0xA81F; .endm
.macro _Get1NamedResource; .word 0xA820; .endm
.macro _Get1IndResource; .word 0xA80E; .endm
.macro _Get1IndType;   .word 0xA80F; .endm
.macro _Count1Types;   .word 0xA81C; .endm
.macro _MaxSizeRsrc;   .word 0xA821; .endm
.macro _RGetResource;  .word 0xA80C; .endm
.macro _Unique1ID;     .word 0xA810; .endm
.macro _HOpenResFile;  .word 0xA81A; .endm
.macro _HCreateResFile; .word 0xA81B; .endm
.macro _Gestalt;       .word 0xA1AD; .endm
.macro _SysError;      .word 0xA9C9; .endm

/* Resource dispatch sub-calls */
.macro _GetOverrideMap;  moveq #6,%d0; .word 0xA822; .endm
.macro _IsThisASystemResourceMap; moveq #-1,%d0; .word 0xA822; .endm
.macro _SetScanOverride; moveq #7,%d0; .word 0xA822; .endm
.macro _MakeOverrideMap; moveq #4,%d0; .word 0xA822; .endm
.macro _InsertOverrideMap; moveq #5,%d0; .word 0xA822; .endm
.macro _GetOverrideAttributes; moveq #8,%d0; .word 0xA822; .endm
.macro _SetOverrideAttributes; moveq #9,%d0; .word 0xA822; .endm

/* Handle state */
.macro _HGetState;     .word 0xA069; .endm
.macro _HSetState;     .word 0xA06A; .endm

/* Missing traps */
.macro _SetPtrSize;    .word 0xA020; .endm
.macro _CompactMem;    .word 0xA04C; .endm
.macro _PurgeMem;      .word 0xA04D; .endm
.macro _EmptyHandle;   .word 0xA02B; .endm
.macro _SetApplLimit;  .word 0xA02D; .endm
.macro _MoreMasters;   .word 0xA036; .endm
.macro _MaxApplZone;   .word 0xA063; .endm
.macro _SetGrowZone;   .word 0xA04B; .endm
.macro _FreeMem;       .word 0xA01C; .endm
.macro _ReserveMem;    .word 0xA040; .endm
.macro _MaxMem;        .word 0xA11D; .endm
.macro _PtrZone;       .word 0xA148; .endm
.macro _HClrRBit;      .word 0xA068; .endm
.macro _HSetRBit;      .word 0xA067; .endm
.macro _GetString;     .word 0xA9BA; .endm
.macro _Munger;        .word 0xA9E0; .endm
.macro _HandToHand;    .word 0xA9E1; .endm
.macro _PtrToXHand;    .word 0xA9E2; .endm
.macro _PtrToHand;     .word 0xA9E9; .endm
.macro _FlushEvents;   .word 0xA032; .endm
.macro _RelString;     .word 0xA050; .endm
.macro _PrDrvrOpen;    .word 0xA8FD; .endm
.macro _GetTrapAddress; .word 0xA146; .endm
.macro _SetTrapAddress; .word 0xA047; .endm
.macro _DisposPtr;     .word 0xA01F; .endm
.macro _MFGetPrtTypeStrings; .word 0xA8FD; .endm  /* printing trap */
