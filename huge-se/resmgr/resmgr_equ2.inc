/* Additional equates for Resource Manager — case variants and missing symbols */

/* Case variants of low-memory globals */
.equ theZone,       TheZone
.equ rombase,       ROMBase
.equ ROMBASE,       ROMBase
.equ RomBase,       ROMBase
.equ bufPtr,        BufPtr
.equ curMap,        CurMap
.equ resload,       ResLoad
.equ RomMapHndl,    ROMMapHndl

/* Resource fork header offset aliases */
.equ DataSize,      dataSize
.equ MapSize,       mapSize
.equ ResMapOffset,  resMapOffset
.equ ResDataOffset, resDataOffset

/* Resource attribute bit aliases (bit numbers for BTST) */
.equ ResSysRef,     resSysRef
.equ ResSysHeap,    resSysHeap
.equ ResPreLoad,    resPreload
.equ ResPurgeable,  resPurgeable
.equ ResLocked,     resLocked
.equ ResProtected,  resProtected
.equ ResChanged,    resChanged
.equ RCBMask,       rcbMask
.equ resExtended,   0           /* bit 0: extended resource flag */

/* Map attribute bit aliases */
.equ MapReadOnly,   mapReadOnly
.equ MapCompact,    mapCompact
.equ MapChanged,    mapChanged
.equ MCCMask,       mCCMask

/* Error code aliases */
.equ ResNotFound,   resNotFound
.equ ResFNotFound,  resFNotFound
.equ AddResFailed,  addResFailed
.equ AddRefFailed,  addRefFailed
.equ RmvResFailed,  rmvResFailed
.equ RmvRefFailed,  rmvRefFailed
.equ OpWrErr,       -61
.equ DupFNErr,      -48
.equ EOFErr,        eofErr
.equ EOFERR,        eofErr
.equ FNFErr,        fnfErr
.equ MemFullErr,    memFullErr

/* Map field aliases */
.equ MTyps,         mTypes
.equ MNams,         mNames
.equ TypeCount,     0       /* first word of type list = count-1 */

/* Resource map override stack frame offsets (from A6) */
.equ realCurMap,           -2
.equ overrideCurMap,       -4
.equ OverrideSysMapHndl,   -8
.equ OverrideSysMap,       -12
.equ doOverrides,          -14

/* IO parameter block field aliases */
.equ IOFileName,    ioNamePtr
.equ IORefNum,      ioRefNum
.equ IOVRefNum,     ioVRefNum
.equ IOOwnBuf,      ioBuffer
.equ IOLEOF,        46      /* logical EOF offset in PB */
.equ ioQElSize,     ioHQElSize
.equ dirID,         48
.equ ioDirID,       48
.equ fsCurPerm,     0

/* Hardware/system equates */
.equ HWCfgFlags,    0x0B22
.equ hwCbFPU,       0       /* bit: has FPU */
.equ CPUFlag,       0x012F
.equ cpu68040,      4
.equ Ticks,         0x016A
.equ BootDrive,     0x0210
.equ MinStack,      0x0114  /* or use a reasonable default */
.equ MinFree,       0       /* minimum free memory */
.equ IntlSpec,      0x0BA0
.equ LastFOND,      0x0BC2
.equ LastSPExtra,   0x0BC6
.equ RMgrPerm,      0x0BA4
.equ fondCache,     0       /* font cache pointer — stub */

/* Extended resource record offsets */
.equ signature,                         0
.equ headerVersion,                     4
.equ actualSize,                        6
.equ decompressID,                      10
.equ extendedAttributes,                12
.equ ExtResRecord_signature,            0
.equ ExtResRecord_extendedAttributes,   12
.equ ExtResRecord_actualSize,           6
.equ ExtResRecord_ResHeader_actualSize, 6
.equ ExtResRecord_ResHeaderSize,        16
.equ CurHeaderVersion,                  1
.equ greggyHeaderVersion,               2
.equ resCompressed,                     0  /* bit in extendedAttributes */

/* Decompression equates */
.equ DeCompressDefType,   0x64636D70  /* 'dcmp' */
.equ decompressSlop,      0
.equ varTableRatio,       0
.equ defprocID,           0
.equ cTableID,            0
.equ DefProcHandle,       0
.equ overRun,             0

/* Misc constants */
.equ OneOne,         0x0A02     /* low-mem global containing $00010001, was literal */
.equ MinusOne,       -1
.equ Resource, 5

/* External function stubs — these won't be called in our minimal RM */
.equ RobustnessData, 0
.equ bkLim,          0     /* old MM zone header offset */
.equ purgeProc,      0x28  /* old MM zone header offset for purge proc */
.equ localSize,      ioStkFrame   /* -52: I/O parameter block + locals */
.equ result,         -2
.equ currentMap,     -4
.equ cachedMap,      -8
.equ bitsToTest,     -10
.equ paramSize,      12

/* ExpandMem fields */
.equ emProcessMgrExists,  0x5A
.equ emSplineKey,         0x60

/* Override mask aliases */
.equ kAllOverrideAttributesMask,     30
.equ kAllButOverrideAttributesMask,  225
.equ kCacheHasOverridesMask,         4
.equ kCacheHasROMResourcesMask,      2
.equ kCacheIsOneDeepMask,            1
.equ kFakeResourceOverrideMapRefNum, 3
.equ kTwoDeepMask,                   8

/* Vector table aliases */
.equ jCheckLoad,    JCheckLoad
.equ jNewMap,       JNewMap
.equ jSuperLoad,    JSuperLoad
.equ jCmpFrm,      JCmpFrm
.equ jCacheFlush,   0x07F4

/* ExtendedResource struct field access (used as Record.field) */
.equ compressedData, 16
.equ extendedData,   16

/* IO */
.equ vRefNum,       22

/* Font-related */
.equ IOFileName,    ioNamePtr

/* Missing absolute address equates */
.equ HSCache,         0x0B80
.equ ROMMapInsert,    RomMapInsert

/* ExtResRecord field access — dots converted to underscores */
/* These are used as ExtResRecord.field — already converted by mpw2gas */
/* But the linker sees them as external symbols. Define as offsets. */
.equ ExtResRecord_signature, 0
.equ ExtResRecord_ResHeaderSize, 16
.equ ExtResRecord_extendedAttributes, 12
.equ ExtResRecord_actualSize, 6
.equ ExtResRecord_ResHeader_actualSize, 6
.equ ExtendedResource_extendedData, 16
.equ ExtendedResource_compressedData, 16
