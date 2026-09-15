/* figment_glue.S — converted from MPW MemMgrGlue.a */
/* D0 result codes: GCC returns a 16-bit OSErr in D0.W only and leaves the high word
 * undefined, but Mac code relies on the Memory Manager returning a full 32-bit D0 (the
 * classic MM ends with MOVEQ).  System 7.5 does `move.w rowBytes,d0; _NewHandle` right
 * after an MM call, so a stale high word ($00F4...) turned a 10-byte request into ~16MB
 * and memFullErr (After Dark toasters and Speedometer's gauge drew nothing).  Every glue
 * whose C routine returns OSErr therefore sign-extends D0 before returning. */
    .text
    .even

/* -----------------------------------------------------------------------
 * fig_InitZone — Interface for InitZone
 *
 * In:  A0 = pointer to InitZone parameter block
 *        (A0)   = zone start
 *        4(A0)  = zone limit
 *        8(A0)  = master pointer count (word)
 *        $A(A0) = grow zone proc pointer
 * Out: D0 = result code
 */
    .globl fig_InitZone
fig_InitZone:
    move.l  (%a0),-(%sp)            /* put zone start on the stack */
    move.l  4(%a0),-(%sp)           /* put zone limit on the stack */
    sub.l   %d0,%d0                 /* make MP count a 32-bit value */
    move.w  8(%a0),%d0              /*   for C calling conventions */
    move.l  %d0,-(%sp)              /* put MP count on the stack */
    move.l  0xA(%a0),-(%sp)         /* put grow zone ptr on the stack */
    jsr     c_InitZone
    lea     16(%sp),%sp             /* clean up 16 bytes off the stack */
    rts

/* -----------------------------------------------------------------------
 * fig_SetApplLimit — Interface for SetApplLimit
 *
 * In:  A0 = zone limit
 * Out: D0 = result code
 */
    .globl fig_SetApplLimit
fig_SetApplLimit:
    move.l  %a0,-(%sp)              /* put parameter on stack */
    jsr     c_SetApplLimit
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_MaxApplZone — Interface for MaxApplZone
 *
 * In:  none
 * Out: D0 = result code
 */
    .globl fig_MaxApplZone
fig_MaxApplZone:
    jsr     c_MaxApplZone
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    rts

/* -----------------------------------------------------------------------
 * fig_MoreMasters — Interface for MoreMasters
 *
 * In:  none
 * Out: D0 = result code
 */
    .globl fig_MoreMasters
fig_MoreMasters:
    jsr     c_MoreMasters
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    rts

/* -----------------------------------------------------------------------
 * fig_GetZone — Interface for GetZone
 *
 * In:  none
 * Out: D0 = result code, A0 = the zone
 */
    .globl fig_GetZone
fig_GetZone:
    jsr     c_GetZone
    move.l  %d0,%a0                 /* move result into A0 */
    sub.l   %d0,%d0                 /* result = noErr */
    rts

/* -----------------------------------------------------------------------
 * fig_SetZone — Interface for SetZone
 *
 * In:  A0 = the zone
 * Out: D0 = result code
 */
    .globl fig_SetZone
fig_SetZone:
    move.l  %a0,-(%sp)              /* put new zone ptr on the stack */
    jsr     c_SetZone
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_NewHandle — Interface for NewHandle
 *
 * In:  D0 = size, D1 = trap word
 * Out: D0 = result code, A0 = the handle
 */
    .globl fig_NewHandle
fig_NewHandle:
    subq.l  #4,%sp                  /* make space for handle */
    move.l  %d0,-(%sp)              /* put size on the stack */
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    pea     8(%sp)                  /* push pointer to handle on stack */
    jsr     c_NewHandle
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    lea     12(%sp),%sp             /* cleanup 12 bytes from the stack */
    move.l  (%sp)+,%a0              /* put handle in A0 */
    rts

/* -----------------------------------------------------------------------
 * fig_NewEmptyHandle — Interface for NewEmptyHandle
 *
 * In:  D1 = trap word
 * Out: D0 = result code, A0 = the handle
 */
    .globl fig_NewEmptyHandle
fig_NewEmptyHandle:
    subq.l  #4,%sp                  /* make space for handle */
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    pea     4(%sp)                  /* push pointer to handle on stack */
    jsr     c_NewEmptyHandle
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #8,%sp                  /* cleanup 8 bytes from the stack */
    move.l  (%sp)+,%a0              /* put handle in A0 */
    rts

/* -----------------------------------------------------------------------
 * fig_DisposeHandle — Interface for DisposeHandle
 *
 * In:  A0 = handle to dispose
 * Out: D0 = result code
 */
    .globl fig_DisposeHandle
fig_DisposeHandle:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_DisposeHandle
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_GetHandleSize — Interface for GetHandleSize
 *
 * In:  A0 = handle
 * Out: D0 = size (>=0) or error (<0)
 */
    .globl fig_GetHandleSize
fig_GetHandleSize:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_GetHandleSize
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_SetHandleSize — Interface for SetHandleSize
 *
 * In:  D0 = requested size, A0 = handle
 * Out: D0 = result code
 */
    .globl fig_SetHandleSize
fig_SetHandleSize:
    move.l  %d0,-(%sp)              /* put size on stack */
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_SetHandleSize
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #8,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_HandleZone — Interface for HandleZone
 *
 * In:  A0 = handle
 * Out: D0 = result code, A0 = the zone
 */
    .globl fig_HandleZone
fig_HandleZone:
    subq.l  #4,%sp                  /* make space for the zone */
    move.l  %a0,-(%sp)              /* put handle on the stack */
    pea     4(%sp)                  /* push pointer to the zone on stack */
    jsr     c_HandleZone
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #8,%sp                  /* cleanup 8 bytes from the stack */
    move.l  (%sp)+,%a0              /* put zone in A0 */
    rts

/* -----------------------------------------------------------------------
 * fig_RecoverHandle — Interface for RecoverHandle
 *
 * In:  A0 = pointer, D1 = trap word
 * Out: A0 = the handle (D0 is unchanged)
 */
    .globl fig_RecoverHandle
fig_RecoverHandle:
    move.l  %d0,-(%sp)              /* save D0 */
    move.l  %a0,-(%sp)              /* put client ptr on the stack */
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    jsr     c_RecoverHandle
    addq.l  #8,%sp                  /* cleanup 8 bytes from the stack */
    move.l  %d0,%a0                 /* move the handle to A0 */
    move.l  (%sp)+,%d0              /* restore D0 */
    rts

/* -----------------------------------------------------------------------
 * fig_ReallocHandle — Interface for ReallocHandle
 *
 * In:  D0 = size, A0 = handle
 * Out: D0 = result code
 */
    .globl fig_ReallocHandle
fig_ReallocHandle:
    move.l  %d0,-(%sp)              /* put size on stack */
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_ReallocHandle
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #8,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_NewPtr — Interface for NewPtr
 *
 * In:  D0 = size, D1 = trap word
 * Out: D0 = result code, A0 = thePtr
 */
    .globl fig_NewPtr
fig_NewPtr:
    subq.l  #4,%sp                  /* make space for thePtr */
    move.l  %d0,-(%sp)              /* put size on the stack */
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    pea     8(%sp)                  /* push pointer to thePtr on stack */
    jsr     c_NewPtr
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    lea     12(%sp),%sp             /* cleanup 12 bytes from the stack */
    move.l  (%sp)+,%a0              /* put thePtr in A0 */
    rts

/* -----------------------------------------------------------------------
 * fig_DisposePtr — Interface for DisposePtr
 *
 * In:  A0 = ptr to dispose
 * Out: D0 = result code
 */
    .globl fig_DisposePtr
fig_DisposePtr:
    move.l  %a0,-(%sp)              /* put ptr on stack */
    jsr     c_DisposePtr
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_GetPtrSize — Interface for GetPtrSize
 *
 * In:  A0 = pointer
 * Out: D0 = size (>=0) or error (<0)
 */
    .globl fig_GetPtrSize
fig_GetPtrSize:
    move.l  %a0,-(%sp)              /* put pointer on stack */
    jsr     c_GetPtrSize
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_SetPtrSize — Interface for SetPtrSize
 *
 * In:  D0 = requested size, A0 = pointer
 * Out: D0 = result code
 */
    .globl fig_SetPtrSize
fig_SetPtrSize:
    move.l  %d0,-(%sp)              /* put size on stack */
    move.l  %a0,-(%sp)              /* put pointer on stack */
    jsr     c_SetPtrSize
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #8,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_PtrZone — Interface for PtrZone
 *
 * In:  A0 = pointer
 * Out: D0 = result code, A0 = the zone
 */
    .globl fig_PtrZone
fig_PtrZone:
    subq.l  #4,%sp                  /* make space for the zone */
    move.l  %a0,-(%sp)              /* put ptr on the stack */
    pea     4(%sp)                  /* push pointer to the zone on stack */
    jsr     c_PtrZone
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #8,%sp                  /* cleanup 8 bytes from the stack */
    move.l  (%sp)+,%a0              /* put zone in A0, cleanup stack */
    rts

/* -----------------------------------------------------------------------
 * fig_FreeMem — Interface for FreeMem
 *
 * In:  D1 = trap word
 * Out: D0 = function result (long)
 */
    .globl fig_FreeMem
fig_FreeMem:
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    jsr     c_FreeMem
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_MaxMem — Interface for MaxMem
 *
 * In:  D1 = trap word
 * Out: D0 = function result (long), A0 = grow size (long)
 */
    .globl fig_MaxMem
fig_MaxMem:
    subq.l  #4,%sp                  /* make space for grow size */
    pea     (%sp)                   /* push pointer to grow size on stack */
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    jsr     c_MaxMem
    addq.l  #8,%sp                  /* cleanup 8 bytes from the stack */
    move.l  (%sp)+,%a0              /* put grow size in A0, adjust stack */
    rts

/* -----------------------------------------------------------------------
 * fig_CompactMem — Interface for CompactMem
 *
 * In:  D0 = size needed, D1 = trap word
 * Out: D0 = function result (long)
 */
    .globl fig_CompactMem
fig_CompactMem:
    move.l  %d0,-(%sp)              /* put sizeNeeded on stack */
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    jsr     c_CompactMem
    addq.l  #8,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_ReserveMem — Interface for ReserveMem
 *
 * In:  D0 = size needed, D1 = trap word
 * Out: D0 = function result (long)
 */
    .globl fig_ReserveMem
fig_ReserveMem:
    move.l  %d0,-(%sp)              /* put sizeNeeded on stack */
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    jsr     c_ReserveMem
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #8,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_PurgeMem — Interface for PurgeMem
 *
 * In:  D0 = size needed, D1 = trap word
 * Out: D0 = function result (long)
 */
    .globl fig_PurgeMem
fig_PurgeMem:
    move.l  %d0,-(%sp)              /* put sizeNeeded on stack */
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    jsr     c_PurgeMem
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #8,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_EmptyHandle — Interface for EmptyHandle
 *
 * In:  A0 = handle to empty
 * Out: D0 = result code
 */
    .globl fig_EmptyHandle
fig_EmptyHandle:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_EmptyHandle
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_HLock — Interface for HLock
 *
 * In:  A0 = handle to lock
 * Out: D0 = result code
 */
    .globl fig_HLock
fig_HLock:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_HLock
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_HUnlock — Interface for HUnlock
 *
 * In:  A0 = handle to unlock
 * Out: D0 = result code
 */
    .globl fig_HUnlock
fig_HUnlock:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_HUnlock
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_HPurge — Interface for HPurge
 *
 * In:  A0 = handle to purge
 * Out: D0 = result code
 */
    .globl fig_HPurge
fig_HPurge:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_HPurge
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_HNoPurge — Interface for HNoPurge
 *
 * In:  A0 = handle
 * Out: D0 = result code
 */
    .globl fig_HNoPurge
fig_HNoPurge:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_HNoPurge
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_SetGrowZone — Interface for SetGrowZone
 *
 * In:  A0 = pointer to grow zone proc
 * Out: D0 = result code
 */
    .globl fig_SetGrowZone
fig_SetGrowZone:
    move.l  %a0,-(%sp)              /* put grow zone proc on stack */
    jsr     c_SetGrowZone
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_MoveHHi — Interface for MoveHHi
 *
 * In:  A0 = handle to move
 * Out: D0 = result code
 */
    .globl fig_MoveHHi
fig_MoveHHi:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_MoveHHi
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_MoveHLow — Interface for MoveHLow
 *
 * In:  A0 = handle to move
 * Out: D0 = result code
 */
    .globl fig_MoveHLow
fig_MoveHLow:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_MoveHLow
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_MaxBlock — Interface for MaxBlock
 *
 * In:  D1 = trap word
 * Out: D0 = function result (long)
 */
    .globl fig_MaxBlock
fig_MaxBlock:
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    jsr     c_MaxBlock
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_PurgeSpace — Interface for PurgeSpace
 *
 * In:  D1 = trap word
 * Out: D0 = total (long), A0 = contig (long)
 */
    .globl fig_PurgeSpace
fig_PurgeSpace:
    subq.l  #4,%sp                  /* make space for contig parameter */
    pea     (%sp)                   /* push pointer to contig on the stack */
    move.l  %d1,-(%sp)              /* put trap word on the stack */
    jsr     c_PurgeSpace
    addq.l  #8,%sp                  /* pop parameters off stack */
    move.l  (%sp)+,%a0              /* put contig in A0 */
    rts

/* -----------------------------------------------------------------------
 * fig_StackSpace — Interface for StackSpace
 *
 * In:  none
 * Out: D0 = function result (long)
 *
 * Note: implemented inline as an optimization (called often, very small)
 */
    .globl fig_StackSpace
fig_StackSpace:
    moveq   #30,%d0                 /* fudge factor */
    add.l   %sp,%d0
    andi.w  #0xFFFC,%d0             /* round down to multiple of 4 */
    sub.l   0xBAE,%d0               /* HiHeapMark ($0BAE) */
    clr.w   0x220                   /* MemErr ($0220) = noErr */
    rts

/* -----------------------------------------------------------------------
 * fig_HSetRBit — Interface for HSetRBit
 *
 * In:  A0 = handle to modify
 * Out: D0 = result code
 */
    .globl fig_HSetRBit
fig_HSetRBit:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_HSetRBit
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_HClrRBit — Interface for HClrRBit
 *
 * In:  A0 = handle to modify
 * Out: D0 = result code
 */
    .globl fig_HClrRBit
fig_HClrRBit:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_HClrRBit
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_HGetState — Interface for HGetState
 *
 * In:  A0 = handle
 * Out: D0 = handle state (byte)
 */
    .globl fig_HGetState
fig_HGetState:
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_HGetState
    addq.l  #4,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_HSetState — Interface for HSetState
 *
 * In:  D0 = new flags (byte, passed as long), A0 = handle
 * Out: D0 = result code
 */
    .globl fig_HSetState
fig_HSetState:
    move.l  %d0,-(%sp)              /* put flags on stack (longword, not byte) */
    move.l  %a0,-(%sp)              /* put handle on stack */
    jsr     c_HSetState
    ext.l   %d0                     /* OSErr is 16 bits in GCC's ABI; callers need a 32-bit D0 */
    addq.l  #8,%sp
    rts

/* -----------------------------------------------------------------------
 * fig_InitApplZone — Interface for InitApplZone
 *
 * Creates application zone from end of system zone to ApplLimit,
 * using Figment's _InitZone so the zone has Figment-format headers.
 * The old ROM's _InitApplZone creates old-format zones that Figment
 * can't operate on.
 *
 * In:  (nothing)
 * Out: D0 = result code
 */
    .globl fig_InitApplZone
fig_InitApplZone:
    /* Build InitZone parameter block on stack (14 bytes):
     *   +0: startPtr (4) = end of system zone
     *   +4: limitPtr (4) = ApplLimit
     *   +8: cMoreMasters (2) = 4
     *  +10: growZoneProc (4) = nil
     */
    lea     -14(%sp),%sp
    movea.l %sp,%a0

    /* startPtr = system zone's oldBackLimit + trailer (approx zone limit)
     * Figment sets HeapEnd ($0114) to the trailer block address.
     * The trailer is 16 bytes, so app zone starts at HeapEnd+16.
     * But safer: use (SysZone->oldBackLimit) which the old MM set to zone end. */
    movea.l (0x2A6).w,%a1          /* A1 = SysZone */
    move.l  (%a1),(%a0)            /* startPtr = SysZone->oldBackLimit */

    /* limitPtr = ApplLimit ($0130) */
    move.l  (0x130).w,4(%a0)

    /* cMoreMasters = 4 */
    move.w  #4,8(%a0)

    /* growZoneProc = nil */
    clr.l   10(%a0)

    /* Call Figment's InitZone — A0 already points to param block */
    bsr     fig_InitZone

    lea     14(%sp),%sp

    /* Set ApplZone = TheZone (InitZone sets TheZone to the new zone) */
    move.l  (0x118).w,(0x2AA).w

    /* Reinstall the packages, as the stock SE ROM's InitApplZone does ($40A410) and as
     * Apple's Figment post-init did (MemMgrBoot.a Fig_VIAZPOSTINIT, not built here).
     * Without it the package table (AppPacks, $0AB8) keeps the ROM's $FFFFFFFF low-memory
     * fill, so the first _PackN call dereferences a -1 handle and jumps into RAM. */
    .short  0xA9E6                 /* _InitAllPacks */
    moveq   #0,%d0
    move.w  %d0,(0x220).w          /* MemErr = noErr (InitAllPacks may trash it) */
    rts

/* -----------------------------------------------------------------------
 * b32_FindHeap — FindHeap for the patchedIn build (no heap tree).
 *
 * Mirrors the classic MM's MMRHPrologue (MemoryMgrInternal.a, "patch from Mac+"):
 * when TheZone is SysZone or ApplZone, use ApplZone if the address lies above
 * ApplZone, else SysZone; otherwise TheZone.  Plain patchedIn FindHeap returned
 * TheZone, so RecoverHandle(p) on a system-heap block while TheZone == ApplZone
 * gave ApplZone + relHandle (Assistant Toolbox's "LaterLaser INIT" then copied
 * 0 bytes and jumped into an empty block -> error type 10).  Kept at the end of
 * the last object so no other figment routine moves.
 *
 * In:  4(SP) = address     Out: D0 = heap
 */
    .globl b32_FindHeap
b32_FindHeap:
    move.l  (0x118).w,%d0           /* TheZone */
    cmp.l   (0x2A6).w,%d0           /* SysZone? */
    beq.s   1f
    cmp.l   (0x2AA).w,%d0           /* ApplZone? */
    bne.s   2f
1:  move.l  (0x2A6).w,%d0           /* presume SysZone */
    move.l  4(%sp),%d1              /* address */
    cmp.l   (0x2AA).w,%d1
    bls.s   2f                      /* address <= ApplZone -> SysZone */
    move.l  (0x2AA).w,%d0           /* above ApplZone -> ApplZone */
2:  rts
