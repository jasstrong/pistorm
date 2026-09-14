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

/* Resource map override stack frame offsets (from A6).
 * These MUST live BELOW the ioBlock (ioStkFrame = -52), per the SuperMario ResourceMgrStack
 * RECORD: a6Link(0), ioBlock(ds.b 52), realCurMap(-54), overrideCurMap(-56),
 * OverrideSysMapHndl(-60), OverrideSysMap(-64), doOverrides(-66).  born-32 had them at
 * -2..-14, INSIDE the ioBlock -> StdEntry's ioBlock writes clobbered OverrideSysMapHndl to
 * garbage ($00010001), which StdExit's SwapRomMap wrote into TopMapHndl (map-chain hang). */
.equ realCurMap,           -54
.equ overrideCurMap,       -56
.equ OverrideSysMapHndl,   -60
.equ OverrideSysMap,       -64
.equ doOverrides,          -66

/* IO parameter block field aliases */
.equ IOFileName,    ioNamePtr
.equ IORefNum,      ioRefNum
.equ IOVRefNum,     ioVRefNum
.equ IOOwnBuf,      ioMisc      /* Apple SysEqu ioOwnBuf EQU $1C (Open's private-buffer ptr) -- NOT ioBuffer ($20): with $20 the
                                   * RM's 'clr.l IOOwnBuf(a0)' left offset 28 as stack garbage and HFS Open ($40805BB6) cleared 10
                                   * bytes at that garbage address + kept it as the FCB's buffer ptr */
.equ IOLEOF,        28      /* logical EOF offset in PB = ioMisc (SE ROM File Mgr _GetEOF returns EOF here, NOT ioPosOffset 46) */
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

/* Extended (compressed) resource header -- offsets relative to the header start.
 * Verified against Apple's compiled DecompressorPatch (System 7.5.3, installed on the SE):
 * sig.L headerLength.W headerVersion.B extendedAttributes.B actualSize.L, then
 * DonnBits (v8): varTableRatio.B overRun.B decompressID.W cTableID.W  (18-byte header)
 * GreggyBits (v9): defprocID.W decompressSlop.W
 * The previous values here were placeholders (all 0 / guessed), so CheckLoad never
 * recognised a compressed resource and loaded the raw packed bytes. */
.equ signature,             0
.equ headerLength,          4
.equ headerVersion,         6
.equ extendedAttributes,    7
.equ actualSize,            8
.equ extendedData,          12     /* data offset: extended, not compressed */
.equ varTableRatio,         12     /* DonnBits */
.equ overRun,               13     /* DonnBits */
.equ decompressID,          14     /* DonnBits */
.equ cTableID,              16     /* DonnBits */
.equ defprocID,             12     /* GreggyBits */
.equ decompressSlop,        14     /* GreggyBits */
.equ compressedData,        18     /* data offset: compressed */
.equ CurHeaderVersion,      8      /* DonnBits */
.equ greggyHeaderVersion,   9      /* GreggyBits */
.equ resCompressed,         0      /* bit in extendedAttributes */

/* DecompressionLocals RECORD {a6Link},DECR: ResourceTemplate buffer (ResSize.L + header),
 * DonnBits flag, pad, cached decompressor handle.  The shared localSize (-66) covers it. */
.equ ResTemplateSize,     22
.equ DoDonnDecompress,    -23
.equ DefProcHandle,       -28

/* Decompression equates */
.equ DeCompressDefType,   0x64636D70  /* 'dcmp' */

/* Misc constants */
.equ OneOne,         0x0A02     /* low-mem global containing $00010001, was literal */
.equ MinusOne,       -1
.equ Resource, 5

/* External function stubs — these won't be called in our minimal RM */
.equ RobustnessData, 0xA89F6572  /* _Unimplemented + 'er': compressed-resource signature */
.equ bkLim,          0     /* old MM zone header offset */
.equ purgeProc,      0x28  /* old MM zone header offset for purge proc */
.equ localSize,      -66   /* ioBlock (52) + override locals (14); was -52 which overlapped them */
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
.equ jCacheFlush,   0x06F4     /* Apple's jCacheFlush (was 0x07F4, an RM vector slot) */


/* IO */
.equ vRefNum,       22

/* Font-related */
.equ IOFileName,    ioNamePtr

/* Missing absolute address equates */
.equ HSCache,         0x0B80
.equ ROMMapInsert,    RomMapInsert

