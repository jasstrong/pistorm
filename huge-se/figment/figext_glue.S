/* figext_glue.S -- born-32 figment extension: _HeapDispatch / _FigmentDispatch ($A0A4).
 *
 * born-32's system heap is a figment heap (zone heapType = 2), so the System 7.5 Process
 * Manager takes its figment path and calls _HeapDispatch selectors $10-$1F
 * (InitProcessMgrHeap, ProcessMgrMaxBlock, RegisterDisposeProc, ...).  born-32 figment
 * never provided $A0A4, so those calls reached the System's classic SE HeapDispatch
 * (18 selectors, no range check): selector $1C jumped into the middle of an unrelated
 * routine -> _EgretDispatch -> "unimplemented trap".
 *
 * This is Apple's HEAPDISPATCH (FigmentSources/MemMgrGlue.a) with the a_* interfaces for
 * the selectors born-32 figment implements ($10-$1F; ProcessMgrHeap.c + figment's MemMgr.c).
 * Selectors $0-$F (QuickTime compatibility, trash notifications, the "SystemSafe"
 * allocators) are not implemented and return paramErr.  Linked on its own at ROM+$74000
 * ($40874000) against figment.elf's symbols, because figment's ROM slot is full.
 */
	.text
	.even

/* In:  D0.W = selector, A0/A1/D1 = selector-specific (D1 = trap word)
 * Out: selector-specific (D0 and/or A0) */
	.globl	b32_HeapDispatch
b32_HeapDispatch:
	/* Apple's patch header (BRA.S *+8; JMP xxx.L).  The System's SetTrapAddress (7.5.3:
	 * $39902) walks a chain of these and stores a later patch into the LAST header's JMP
	 * operand instead of the trap table.  Without it, the System's patch load replaced this
	 * dispatcher with its classic SE HeapDispatch (18 selectors, no figment support).  The
	 * operand is in ROM, so that store is dropped (or lands harmlessly on a writable ROM
	 * image) -- the BRA.S always skips the JMP; nothing executes it. */
	.word	0x6006				/* bra.s *+8 */
	.word	0x4EF9				/* jmp notImpl.l */
	.long	notImpl
	cmp.w	#(Table_end - Table) / 2,%d0	/* unsigned: also rejects negative selectors */
	bcc.s	notImpl
	move.l	%a2,-(%sp)
	lea	Table(%pc),%a2
	add.w	(%a2,%d0.w*2),%a2
	jsr	(%a2)
	move.l	(%sp)+,%a2
	rts

notImpl:
	moveq	#-50,%d0			/* paramErr */
	rts

Table:
	.word	q_HLock - Table				/* $0 HLockQ */
	.word	q_HUnlock - Table			/* $1 HUnlockQ */
	.word	q_HPurge - Table			/* $2 HPurgeQ */
	.word	q_HNoPurge - Table			/* $3 HNoPurgeQ */
	.word	q_HGetState - Table			/* $4 HGetStateQ */
	.word	q_HSetState - Table			/* $5 HSetStateQ (flags in A1) */
	.rept	7					/* $6-$C: trash notifications, TrashZone,  */
	.word	q_NoErr - Table				/*   TrashMemoryArea, TrashProcess */
	.endr
	.word	q_NewHandleSystemSafe - Table		/* $D */
	.word	q_EnoughSystemHeapSlop - Table		/* $E */
	.word	q_NewPtrSystemSafe - Table		/* $F */
	.word	h_Sel10 - Table				/* $10 InitProcessMgrHeap | classic SetTrashList */
	.word	h_Sel11 - Table				/* $11 ShrinkSysHeap | classic GetTrashList */
	.word	a_LowerProcessMgrZoneBottom - Table	/* $12 */
	.word	a_ShrinkProcessMgrZone - Table		/* $13 */
	.word	a_ExtendSystemZone - Table		/* $14 */
	.word	a_ReclaimSpaceIfSysHeapLow - Table	/* $15 */
	.word	a_StraddleBlockSize - Table		/* $16 */
	.word	a_ProcessMgrMaxBlock - Table		/* $17 */
	.word	a_GetProcessMgrLoFreeBytes - Table	/* $18 */
	.word	a_GetSystemHiFreeBytes - Table		/* $19 */
	.word	a_NewHandleHi - Table			/* $1A */
	.word	a_GetNextBlock - Table			/* $1B */
	.word	a_RegisterDisposeProc - Table		/* $1C */
	.word	a_DisposeZone - Table			/* $1D */
	.word	a_SetZoneCompatibilityFlags - Table	/* $1E */
	.word	a_GetZoneCompatibilityFlags - Table	/* $1F */
Table_end:

/* ---- $0-$F: the classic-System meanings, done through figment ----
 * System 7.5.3's own classic SE versions poke classic 24-bit block-header bits
 * (bset #7,-11(ptr) for HLockQ) and walk the heap assuming the 52-byte classic zone
 * header, which would corrupt figment heaps -- so route through the real traps.
 * Trash notifications ($6-$C) are accepted but not tracked yet (Apple's figment
 * QTMemory.c is the fuller implementation). */
q_HLock:
	.short	0xA029				/* _HLock      A0 = handle */
	rts
q_HUnlock:
	.short	0xA02A				/* _HUnlock */
	rts
q_HPurge:
	.short	0xA049				/* _HPurge */
	rts
q_HNoPurge:
	.short	0xA04A				/* _HNoPurge */
	rts
q_HGetState:
	.short	0xA069				/* _HGetState  -> D0 = flags */
	rts
q_HSetState:
	move.l	%a1,%d0				/* flags arrive in A1 (Apple: MOVEA.W D0,A1) */
	.short	0xA06A				/* _HSetState */
	rts
q_NoErr:
	moveq	#0,%d0
	rts
q_NewHandleSystemSafe:				/* A0 = size -> A0 = handle */
	move.l	%a0,%d0
	.short	0xA522				/* _NewHandleSys */
	rts
q_EnoughSystemHeapSlop:
	moveq	#1,%d0				/* true */
	rts
q_NewPtrSystemSafe:				/* A0 = size -> A0 = ptr */
	move.l	%a0,%d0
	.short	0xA51E				/* _NewPtrSys */
	rts

/* ---- $10/$11: one selector, two meanings on born-32 ----
 * figment:      $10 InitProcessMgrHeap(A0 = old PM zone)   $11 ShrinkSysHeap(A0 = size) -> D0
 * classic 7.5.3 SE HeapDispatch:  $10 SetTrashList(A0)     $11 GetTrashList -> A0
 *                                 (the list lives at ExpandMem+$230)
 * Both run here: the Process Manager takes its figment path (SysZone is a figment heap)
 * and passes ApplZone to $10 / calls $11 as $A0A4; the SE patch set passes a fresh handle
 * to $10 and calls $11 as $A1A4 so it gets A0 back. */
h_Sel10:
	cmpa.l	(0x2AA).w,%a0			/* ApplZone -> Process Manager */
	beq	a_InitProcessMgrHeap
	movea.l	(0x2B6).w,%a1			/* ExpandMem */
	move.l	%a0,0x230(%a1)			/* classic SetTrashList */
	moveq	#0,%d0
	rts
h_Sel11:
	btst	#8,%d1				/* trap word $A1A4: caller wants A0 */
	beq	a_ShrinkSysHeap
	movea.l	(0x2B6).w,%a0
	movea.l	0x230(%a0),%a0			/* classic GetTrashList */
	moveq	#0,%d0
	rts

/* ---- a_* interfaces (Apple's MemMgrGlue.a): registers -> C arguments ---- */
a_InitProcessMgrHeap:				/* A0 = old ProcessMgr zone -> D0 = new zone */
	move.l	%a0,-(%sp)
	jsr	c_InitProcessMgrHeap
	addq.l	#4,%sp
	rts
a_ShrinkSysHeap:				/* A0 = amount -> D0 = amount shrunk */
	move.l	%a0,-(%sp)
	jsr	c_ShrinkSysHeap
	addq.l	#4,%sp
	rts
a_LowerProcessMgrZoneBottom:			/* A0 = amount, A1 = ProcessMgr heap */
	move.l	%a1,-(%sp)
	move.l	%a0,-(%sp)
	jsr	c_LowerProcessMgrZoneBottom
	addq.l	#8,%sp
	rts
a_ShrinkProcessMgrZone:				/* A0 = size needed, A1 = old ProcessMgr heap */
	move.l	%a1,-(%sp)
	move.l	%a0,-(%sp)
	jsr	c_ShrinkProcessMgrZone
	addq.l	#8,%sp
	rts
a_ExtendSystemZone:				/* A0 = size needed */
	move.l	%a0,-(%sp)
	jsr	c_ExtendSystemZone
	addq.l	#4,%sp
	rts
a_ReclaimSpaceIfSysHeapLow:			/* A0 = Handle*, A1 = ProcessMgr heap */
	move.l	%a1,-(%sp)
	move.l	%a0,-(%sp)
	jsr	c_ReclaimSpaceIfSysHeapLow
	addq.l	#8,%sp
	rts
a_StraddleBlockSize:				/* A0 -> 5-long parameter block */
	move.l	(%a0),-(%sp)
	move.l	4(%a0),-(%sp)
	move.l	8(%a0),-(%sp)
	move.l	12(%a0),-(%sp)
	move.l	16(%a0),-(%sp)
	jsr	c_StraddleBlockSize
	lea	20(%sp),%sp
	rts
a_ProcessMgrMaxBlock:				/* A0 = ProcessMgr heap */
	move.l	%a0,-(%sp)
	jsr	c_ProcessMgrMaxBlock
	addq.l	#4,%sp
	rts
a_GetProcessMgrLoFreeBytes:			/* A0 = handle of interest, A1 = Boolean* */
	move.l	%a1,-(%sp)
	move.l	%a0,-(%sp)
	jsr	c_GetProcessMgrLoFreeBytes
	addq.l	#8,%sp
	rts
a_GetSystemHiFreeBytes:
	jmp	c_GetSystemHiFreeBytes
a_NewHandleHi:					/* A0 = size, D1 = trap word -> A0 = handle */
	subq.l	#4,%sp
	move.l	%a0,-(%sp)
	move.l	%d1,-(%sp)
	pea	8(%sp)
	jsr	c_NewHandleHi
	lea	12(%sp),%sp
	move.l	(%sp)+,%a0
	rts
a_GetNextBlock:					/* A0 = current block, A1 = zone */
	move.l	%a1,-(%sp)
	move.l	%a0,-(%sp)
	jsr	c_GetNextBlockInHeap
	addq.l	#8,%sp
	rts
a_RegisterDisposeProc:				/* A0 = proc, A1 = zone */
	move.l	%a1,-(%sp)
	move.l	%a0,-(%sp)
	jsr	c_RegisterDisposeProc
	addq.l	#8,%sp
	rts
a_DisposeZone:					/* A0 = zone */
	move.l	%a0,-(%sp)
	jsr	c_DisposeZone
	addq.l	#4,%sp
	rts
a_SetZoneCompatibilityFlags:			/* A0 = zone, A1 = flags */
	move.l	%a1,-(%sp)
	move.l	%a0,-(%sp)
	jsr	c_SetZoneCompatibilityFlags
	addq.l	#8,%sp
	rts
a_GetZoneCompatibilityFlags:			/* A0 = zone */
	move.l	%a0,-(%sp)
	jsr	c_GetZoneCompatibilityFlags
	addq.l	#4,%sp
	rts

/* ---- GESTALT: Pascal-convention glue for ProcessMgrHeap.c's Gestalt() ----
 * pascal OSErr Gestalt(OSType selector, long *response)
 * Stack on entry: 0 = return, 4 = response ptr, 8 = selector, 12 = OSErr result. */
	.globl	GESTALT
GESTALT:
	movea.l	4(%sp),%a1
	move.l	8(%sp),%d0
	.short	0xA1AD				/* _Gestalt: D0 = selector -> A0 = response, D0 = err */
	move.l	%a0,(%a1)
	move.w	%d0,12(%sp)
	movea.l	(%sp)+,%a0
	addq.l	#8,%sp
	jmp	(%a0)
