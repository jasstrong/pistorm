/* Resource Manager equates — extracted from SuperMario source */
/* Low-memory globals, structure offsets, constants */

/* === Low-memory globals === */
.equ TheZone,       0x0118
.equ HeapEnd,       0x0114
.equ ApplZone,      0x02AA
.equ SysZone,       0x02A6
.equ ROMBase,       0x02AE
.equ ApplLimit,     0x0130
.equ BufPtr,        0x010C
.equ MemErr,        0x0220
.equ ROM85,         0x028E
.equ Lo3Bytes,      0x031A     /* low-mem global: must contain $00FFFFFF */
.equ ExpandMem,     0x02B6
.equ MMFlags,       0x0CB0

/* Resource Manager globals */
.equ TopMapHndl,    0x0A50
.equ SysMapHndl,    0x0A54
.equ SysMap,        0x0A58
.equ CurMap,        0x0A5A
.equ ResReadOnly,   0x0A5C
.equ ResLoad,       0x0A5E
.equ ResErr,        0x0A60
.equ ResErrProc,    0x0AF2
.equ SysResName,    0x0AD8
.equ RomMapInsert,  0x0B9E
.equ TmpResLoad,    0x0B9F
.equ ROMMapHndl,    0x0B5E     /* was 0x0B06 — WRONG! */
.equ CurApRefNum,   0x0900

/* RMGR HandleScan cache at $B80 */
.equ RMGRHiVars,    0x0B80
.equ HSCRType,      0x0B80
.equ HSCHandle,     0x0B84
.equ HSCROffset,    0x0B88
.equ HSCTOffset,    0x0B8C
.equ HSCMapHndl,    0x0B90
.equ HSCEntryCnt,   0x0B94
.equ HSCTypeCnt,    0x0B96
.equ HSCMapRNum,    0x0B98
.equ ResOneDeep,    0x0B9A
.equ GIROneDeep,    0x0B9B
.equ GIRIndex,      0x0B9C
.equ NewResDefEnd,  0x0BA0
.equ kBrianBits,    0x0B20

/* Resource Manager vector table entries */
.equ JRMgrStdEntry, 0x0698
.equ JRMgrStdExit,  0x069C
.equ JNewMap,        0x06A0    /* was 0x07EC */
.equ JCheckLoad,     0x06A4    /* was 0x07F0 */
.equ JSuperLoad,     0x06A8    /* was 0x07E4 */
.equ JCmpFrm,        0x06AC    /* was 0x07E8 */
.equ JSegStack,      0x07E0

/* === Resource file header offsets === */
.equ resDataOffset,  0
.equ resMapOffset,   4
.equ dataSize,       8
.equ mapSize,        12
.equ hdrSize,        16
.equ dirCopy,        16
.equ userData,       128
.equ DataFirstRes,   256

/* === Resource map offsets (from map start) === */
.equ mNext,          16
.equ mRefNum,        20
.equ mAttr,          22
.equ mInMemoryAttr,  23
.equ mTypes,         24
.equ mNames,         26
.equ NullRMSize,     30

/* === Type entry (8 bytes each) === */
.equ tType,          0
.equ tCount,         4
.equ tOffset,        6
.equ tSize,          8
.equ typeShift,      3

/* === Reference entry (12 bytes each) === */
.equ rID,            0
.equ rNameOff,       2
.equ rAttr,          4
.equ rLocn,          4
.equ rHndl,          8
.equ reSize,         12

/* === ROM map constants === */
.equ kROMMapRefNum,  1
.equ ROMRsrcStart,   0x1A
.equ lenWdLen,       4

/* === Resource attribute bits === */
.equ resSysRef,      7
.equ resSysHeap,     6
.equ resPurgeable,   5
.equ resLocked,      4
.equ resProtected,   3
.equ resPreload,     2
.equ resChanged,     1
.equ rcbMask,        0xFD

/* === Map attribute bits === */
.equ mapReadOnly,    7
.equ mapCompact,     6
.equ mapChanged,     5
.equ mCCMask,        0x60
.equ mChMask,        0x20
.equ mCoMask,        0x40

/* === Override attribute bits === */
.equ decompressionPasswordBit,       0
.equ overrideNextMapBit,             1
.equ dontCountOrIndexDuplicatesBit,  2
.equ twoDeepBit,                     3
.equ preventFileFromBeingClosedBit,  4
.equ mapForceSysHeap,                0

/* === Map insert constants === */
.equ MapTrue,        0xFFFF
.equ MapFalse,       0xFF00

/* === Error codes === */
.equ resNotFound,    -192
.equ resFNotFound,   -193
.equ addResFailed,   -194
.equ addRefFailed,   -195
.equ rmvResFailed,   -196
.equ rmvRefFailed,   -197
.equ resAttrErr,     -198
.equ mapReadErr,     -199
.equ CantDecompress, -186
.equ badExtResource, -185
.equ resProblem,     -204
.equ noErr,          0
.equ eofErr,         -39
.equ fnfErr,         -43
.equ permErr,        -54
.equ memFullErr,     -108
.equ nilHandleErr,   -109

/* === IO parameter block offsets === */
.equ ioCompletion,   12
.equ ioResult,       16
.equ ioNamePtr,      18
.equ ioVRefNum,      22
.equ ioRefNum,       24
.equ ioPermssn,      27
.equ ioPosMode,      44
.equ ioPosOffset,    46
.equ ioReqCount,     36
.equ ioActCount,     40
.equ ioBuffer,       32
.equ ioMisc,         28
.equ ioHQElSize,     52

/* === Stack frame === */
.equ ioStkFrame,     -52

/* === Memory Manager flags === */
.equ MMStartMode,    6

/* === ExpandMem record offsets (approximate) === */
.equ emScanOverrideMaps,    0x4C
.equ emLastMapOverridden,   0x4E
.equ emOverrideMapRefNum,   0x50
.equ emResourceCache,       0x52

/* Case aliases for MPW mixed-case usage */
.equ RHndl, rHndl
.equ MRefNum, mRefNum
.equ MNext, mNext
.equ MAttr, mAttr
.equ MTypes, mTypes
.equ MNames, mNames
.equ TType, tType
.equ TCount, tCount
.equ TOffset, tOffset
.equ TSize, tSize
.equ RLocn, rLocn
.equ RAttr, rAttr
.equ ReSize, reSize
.equ RID, rID
.equ RNameOff, rNameOff
.equ MInMemoryAttr, mInMemoryAttr
.equ RomRsrcStart, ROMRsrcStart
.equ TypeShift, typeShift
.equ HdrSize, hdrSize
.equ LenWdLen, lenWdLen
.equ IoStkFrame, ioStkFrame
.equ IoCompletion, ioCompletion
.equ IoResult, ioResult
.equ IoNamePtr, ioNamePtr
.equ IoVRefNum, ioVRefNum
.equ IoRefNum, ioRefNum
.equ IoPermssn, ioPermssn
.equ IoPosMode, ioPosMode
.equ IoPosOffset, ioPosOffset
.equ IoReqCount, ioReqCount
.equ IoActCount, ioActCount
.equ IoBuffer, ioBuffer
.equ IoMisc, ioMisc

/* Preload key info offsets */
.equ preKey,        0x20B7A185
.equ PreKey,        0x20B7A185
.equ preDataBuff,   0x00
.equ PreDataBuff,   0x00
.equ preVersion,    0x04
.equ PreVersion,    0x04
.equ preLock,       0x08
.equ PreLock,       0x08
.equ preUnLock,     0x0C
.equ PreUnLock,     0x0C
.equ preExtra,      0x10
.equ PreExtra,      0x10
.equ preEnd,        0x14
.equ PreEnd,        0x14
.equ preSize,       0x14
.equ PreSize,       0x14

/* ROM combo structure table field offsets (from SuperMario source):
 *   [0-3] offset to first entry, [4] MaxComInd, [5] ComBFSz,
 *   [6-7] Version, [8-9] MemHeadSz  */
.equ MaxComInd,     4
.equ ComBFSz,       5
.equ MemHeadSz,     8
.equ UnivInfoPtr,   0x0300
.equ DefaultRSRCs,  0x30

/* Cache constants */
.equ kCacheFlags,       0
.equ kCachedMap,        2
.equ kResourceType,     4
.equ kResourceCount,    8
.equ kCacheHeaderSize,  12
.equ kMapHandleEntry,   0
.equ kDataOffsetEntry,  4
.equ kResourceIDEntry,  6
.equ kCacheEntrySize,   8
.equ kCacheIsOneDeepBit, 0
.equ kCacheHasROMResourcesBit, 1
.equ kCacheHasOverridesBit, 2
.equ kInitialEntryCount, 30
.equ kDeanBit,          1

/* I/O permissions */
.equ fsRdPerm,      1
.equ fsWrPerm,      2
.equ fsRdWrPerm,    3

/* Misc */
.equ ROZ,           7
.equ true,          1
.equ false,         0
.equ nil,           0

/* Stack frame ioBlock offsets (ResourceMgrStack RECORD) */
.equ ioBlock,        ioStkFrame    /* = -52, the IO parameter block on stack */
.equ ioFileType,     26            /* ioMisc in PB */
.equ IOFileType,     26
.equ ioDirID,        48            /* ioDirID in HFS PB */

/* Resource header buffer (on stack) */
.equ ResHeaderBuffer, -64          /* approximate */
.equ ResSize,         0
.equ resCompressed,   0

/* Extended resource record */
/* ExtResRecord.extendedAttributes — approximate offset */

/* ExpandMem record — more offsets */
.equ emResourceCache,  0x52
.equ emLastResLoad,    0x56

/* HSCache is at a fixed address, preload fields are offsets FROM HSCache */
/* Already defined above */

/* VarTable record (decompression) */
.equ VarTableRec_VarRecSize, 8
/* Resource header on-stack buffer */
.equ ResHeader,      0
.equ ResHeaderSize,  16

/* More misc equates */
.equ flags,          5         /* DCE flags byte offset */
.equ RESize,         reSize   /* alias */
.equ PreUnlock, 0x0C
.equ Preunlock, 0x0C
