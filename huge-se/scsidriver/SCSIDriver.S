/* SCSIDriver.S — ROM-resident SCSI disk driver for Huge SE (".SCSIHD")
 *
 * Replaces the on-disk Apple_Driver partition code entirely.  patch-rom.py
 * embeds this image in the ROM mirror half; a shim patched over SCSIBoot's
 * driver-load SRead copies it into the _NewPtr,SYS buffer where the loaded
 * disk driver would have gone.  Everything downstream (checksum bypass
 * aside) treats it exactly like a freshly-loaded disk driver:
 *
 *   - ROM CallDriver does `jsr (a3)` at image+0 with:
 *       A0 = ptr to the Apple_HFS partition map entry (512-byte PM block)
 *       D5 = SCSI ID in low 3 bits (control-mask bits above, per <LW2>)
 *     The install entry builds a DCE, claims unit 32+ID, AddDrives the
 *     HFS partition, and returns D0=0.
 *   - The Device Manager then dispatches Open/Prime/Control/Status through
 *     the DRVR header offsets like any driver since 1984.
 *
 * All I/O via _SCSIDispatch traps (SCSIGet/Select/Cmd/Read/Complete) —
 * this driver contains NO hardware addresses; the (already-remapped) ROM
 * SCSI Manager owns the 5380.  That is the entire point: the one piece of
 * disk-loaded code that pokes SCSI hardware is gone from the disk.
 *
 * Position-independent: all internal refs PC-relative.
 */

    .text
    .even

/* ---- Low-memory globals ---- */
    .equ UTableBase,   0x11C    /* ptr to unit table (array of DCE handles) */
    .equ UnitNtryCnt,  0x1D2    /* word: number of unit table entries */
    .equ DrvQHdr,      0x308    /* drive queue header: qFlags.w qHead.l qTail.l */
    .equ JIODone,      0x8FC    /* jump vector: Device Manager IODone */
    .equ DskErr,       0x142    /* word: last disk error for FS */

/* ---- IOParam offsets ---- */
    .equ ioTrap,       6
    .equ ioResult,     16
    .equ ioBuffer,     0x20
    .equ ioReqCount,   0x24
    .equ ioActCount,   0x28
    .equ ioPosMode,    0x2C
    .equ ioPosOffset,  0x2E
    .equ NoQueueBit,   9        /* in ioTrap: immediate (no-queue) request */

/* ---- DCE offsets ---- */
    .equ dCtlDriver,   0
    .equ dCtlFlags,    4
    .equ dCtlQHdr,     6
    .equ dCtlPosition, 0x10
    .equ dCtlStorage,  0x14
    .equ dCtlRefNum,   0x18

/* ---- Drive queue element (standard part) ---- */
    .equ dqQLink,      0
    .equ dqQType,      4
    .equ dqDrive,      6
    .equ dqRefNum,     8
    .equ dqFSID,       10
    .equ dqDrvSz,      12
    .equ dqDrvSz2,     14
    .equ DQESize,      16

/* Our private per-drive block, kept in front of the DQE and pointed to by
 * dCtlStorage.  (dCtlStorage is documented as a Handle, but only this
 * driver ever reads it; the HD SC driver plays the same trick.) */
    .equ pvScsiID,     0        /* word: SCSI target ID */
    .equ pvPartStart,  2        /* long: partition physical start block */
    .equ pvSize,       8        /* padded */

/* ---- Partition map entry offsets (Apple new-style 'PM') ---- */
    .equ pmPyPartStart, 8
    .equ pmPartBlkCnt,  12

/* ---- _SCSIDispatch selectors (IM IV) ---- */
    .equ scsiGet,      1
    .equ scsiSelect,   2
    .equ scsiCmd,      3
    .equ scsiComplete, 4
    .equ scsiRead,     5
    .equ scsiWrite,    6

/* ---- TIB opcodes ---- */
    .equ scInc,        1
    .equ scStop,       7

/* ======================================================================
 * Install entry — image+0, reached via ROM CallDriver `jsr (a3)`.
 * In:  A0 = Apple_HFS partition map entry, D5 = SCSI ID (low 3 bits)
 * Out: D0 = 0
 * ====================================================================== */
    .globl drv_install
drv_install:
    movem.l %d1-%d7/%a0-%a6,-(%sp)

    move.w  %d5,%d3
    and.w   #7,%d3              /* d3 = SCSI ID */

    /* unit = 32+ID; refnum = -(unit+1) = -33-ID */
    move.w  %d3,%d6
    add.w   #32,%d6             /* d6 = unit number */
    cmp.w   UnitNtryCnt.w,%d6   /* beyond unit table? */
    bcc     install_fail
    move.w  %d6,%d7
    addq.w  #1,%d7
    neg.w   %d7                 /* d7 = refnum */

    /* Already installed for this unit? bail quietly (success). */
    movea.l UTableBase.w,%a4
    move.w  %d6,%d0
    lsl.w   #2,%d0
    tst.l   (%a4,%d0.w)
    bne     install_done

    /* remember PM entry fields before A0 gets clobbered */
    move.l  pmPyPartStart(%a0),%d4   /* d4 = partition start block */
    move.l  pmPartBlkCnt(%a0),%d5    /* d5 = partition block count */

    /* ---- DCE: _NewHandle ,SYS ,CLEAR ---- */
    moveq   #0x30,%d0           /* DCE is $28 bytes; pad to $30 */
    .short  0xA722              /* _NewHandle ,Sys,Clear */
    bne     install_fail
    movea.l %a0,%a4             /* a4 = DCE handle */
    movea.l (%a4),%a3           /* a3 = DCE ptr (clear, won't move: sys+locked-in-place use) */

    lea     drvr_header(%pc),%a1
    move.l  %a1,dCtlDriver(%a3)
    move.w  #0x0F00,dCtlFlags(%a3)   /* read+write+ctl+status enable; dRAMBased CLEAR (pointer) */
    move.w  %d7,dCtlRefNum(%a3)

    /* ---- private block + DQE: _NewPtr ,SYS ,CLEAR ---- */
    moveq   #pvSize+DQESize,%d0
    .short  0xA51E+0x200        /* _NewPtr ,Sys,Clear = A71E */
    bne     install_fail
    /* a0 = private block; DQE at a0+pvSize */
    move.w  %d3,pvScsiID(%a0)
    move.l  %d4,pvPartStart(%a0)
    move.l  %a0,dCtlStorage(%a3)

    lea     pvSize(%a0),%a2     /* a2 = DQE */
    move.w  #1,dqQType(%a2)     /* long drive-size format */
    move.w  %d7,dqRefNum(%a2)
    move.w  %d5,dqDrvSz(%a2)         /* size low word */
    move.l  %d5,%d0
    swap    %d0
    move.w  %d0,dqDrvSz2(%a2)        /* size high word */

    /* drive number: 1 + max(existing dQDrive), floor 4 */
    moveq   #4,%d2
    movea.l DrvQHdr+2.w,%a1     /* qHead */
drive_scan:
    move.l  %a1,%d0
    beq     drive_scanned
    cmp.w   dqDrive(%a1),%d2
    bhi     drive_next
    move.w  dqDrive(%a1),%d2
    addq.w  #1,%d2
drive_next:
    cmpa.l  DrvQHdr+6.w,%a1     /* at qTail? */
    beq     drive_scanned
    movea.l dqQLink(%a1),%a1
    bra     drive_scan
drive_scanned:
    move.w  %d2,dqDrive(%a2)

    /* claim unit table slot */
    movea.l UTableBase.w,%a1
    move.w  %d6,%d0
    lsl.w   #2,%d0
    move.l  %a4,(%a1,%d0.w)

    /* _AddDrive: D0 = drive<<16 | refnum(lo), A0 = DQE */
    move.w  %d2,%d0
    swap    %d0
    move.w  %d7,%d0
    movea.l %a2,%a0
    .short  0xA04E              /* _AddDrive */

install_done:
    movem.l (%sp)+,%d1-%d7/%a0-%a6
    moveq   #0,%d0
    rts
install_fail:
    movem.l (%sp)+,%d1-%d7/%a0-%a6
    moveq   #-1,%d0
    rts

/* ======================================================================
 * DRVR header — Device Manager entry offsets are relative to this point.
 * ====================================================================== */
    .even
drvr_header:
    .word   0x0F00                       /* drvrFlags */
    .word   0, 0, 0                      /* delay, emask, menu */
    .word   drv_open-drvr_header
    .word   drv_prime-drvr_header
    .word   drv_control-drvr_header
    .word   drv_status-drvr_header
    .word   drv_close-drvr_header
    .byte   7
    .ascii  ".SCSIHD"
    .even

/* ----------------------------------------------------------------------
 * Open / Close — nothing to do; install already built the world.
 * ---------------------------------------------------------------------- */
drv_open:
    moveq   #0,%d0
    rts
drv_close:
    moveq   #0,%d0
    rts

/* ----------------------------------------------------------------------
 * Prime — A0 = IOParam, A1 = DCE.  Read (ioTrap.b==2) / Write (==3).
 * Byte offset = ioPosOffset, must be block-aligned (boot I/O always is).
 *
 * Frame layout (link %a6): all locals at fixed A6-relative offsets so the
 * pascal-style _SCSIDispatch pushes never need moving-SP arithmetic.
 * ---------------------------------------------------------------------- */
    .equ fCmd,    -10           /* 10-byte CDB */
    .equ fTIB,    -30           /* 2 × 10-byte SCSIInstr {op.w,p1.l,p2.l} */
    .equ fStat,   -32           /* word: completion status */
    .equ fMsg,    -34           /* word: completion message */
    .equ fPB,     -38           /* long: saved IOParam ptr */
    .equ fDCE,    -42           /* long: saved DCE ptr */
    .equ fFrame,  42

drv_prime:
    link    %a6,#-fFrame
    movem.l %d3-%d5/%a2-%a3,-(%sp)
    move.l  %a0,fPB(%a6)
    move.l  %a1,fDCE(%a6)
    movea.l %a0,%a3             /* a3 = PB */
    movea.l dCtlStorage(%a1),%a2 /* a2 = private {ID, partStart} */

    /* block = partStart + ioPosOffset>>9 ; nblks = ioReqCount>>9 */
    move.l  ioPosOffset(%a3),%d4
    moveq   #9,%d0
    lsr.l   %d0,%d4
    add.l   pvPartStart(%a2),%d4     /* d4 = absolute block */
    move.l  ioReqCount(%a3),%d5
    lsr.l   %d0,%d5                  /* d5 = block count */
    beq     prime_ok                 /* zero-length: trivially done */

    /* ---- SCSIGet: arbitrate for the bus ---- */
    subq.w  #2,%sp
    move.w  #scsiGet,-(%sp)
    .short  0xA815              /* _SCSIDispatch */
    move.w  (%sp)+,%d0
    bne     prime_io_err

    /* ---- SCSISelect(ID) ---- */
    subq.w  #2,%sp
    move.w  pvScsiID(%a2),-(%sp)
    move.w  #scsiSelect,-(%sp)
    .short  0xA815
    move.w  (%sp)+,%d0
    bne     prime_io_err

    /* ---- build 10-byte READ(28)/WRITE(2A) CDB at fCmd ---- */
    lea     fCmd(%a6),%a0
    moveq   #0x28,%d0           /* READ(10) */
    move.b  ioTrap+1(%a3),%d1
    and.w   #3,%d1
    cmp.w   #3,%d1              /* aWrCmd? */
    bne     prime_cmd_set
    moveq   #0x2A,%d0           /* WRITE(10) */
prime_cmd_set:
    move.b  %d0,(%a0)
    clr.b   1(%a0)
    move.l  %d4,2(%a0)          /* big-endian LBA */
    clr.b   6(%a0)
    move.w  %d5,7(%a0)          /* transfer length in blocks */
    clr.b   9(%a0)

    /* ---- SCSICmd(ptr, 10) ---- */
    subq.w  #2,%sp
    pea     fCmd(%a6)
    move.w  #10,-(%sp)
    move.w  #scsiCmd,-(%sp)
    .short  0xA815
    move.w  (%sp)+,%d0
    bne     prime_io_err

    /* ---- TIB at fTIB: {scInc, buf, bytecount}, {scStop, 0, 0}
     * SCSIInstr is 10 bytes: opcode.w, param1.l, param2.l — no padding. */
    lea     fTIB(%a6),%a0
    move.w  #scInc,(%a0)+
    move.l  ioBuffer(%a3),(%a0)+
    move.l  ioReqCount(%a3),(%a0)+
    move.w  #scStop,(%a0)+
    clr.l   (%a0)+
    clr.l   (%a0)+

    /* ---- SCSIRead/SCSIWrite(TIB) ----
     * Re-derive r/w from ioTrap: d1 is trap-scratch, the SCSICmd
     * dispatch above may have clobbered it. */
    subq.w  #2,%sp
    pea     fTIB(%a6)
    move.b  ioTrap+1(%a3),%d1
    and.w   #3,%d1
    move.w  #scsiRead,%d0
    cmp.w   #3,%d1
    bne     prime_xfer_sel
    move.w  #scsiWrite,%d0
prime_xfer_sel:
    move.w  %d0,-(%sp)
    .short  0xA815
    move.w  (%sp)+,%d0
    bne     prime_io_err

    /* ---- SCSIComplete(&stat, &msg, timeout=600 ticks) ---- */
    subq.w  #2,%sp
    pea     fStat(%a6)
    pea     fMsg(%a6)
    move.l  #600,-(%sp)
    move.w  #scsiComplete,-(%sp)
    .short  0xA815
    move.w  (%sp)+,%d0
    bne     prime_io_err
    tst.w   fStat(%a6)          /* SCSI status: 0 = GOOD */
    bne     prime_io_err

prime_ok:
    move.l  ioReqCount(%a3),ioActCount(%a3)
    moveq   #0,%d0
    bra     prime_done
prime_io_err:
    moveq   #-36,%d0            /* ioErr */
prime_done:
    movem.l (%sp)+,%d3-%d5/%a2-%a3
    movea.l fPB(%a6),%a0        /* a0 = PB again */
    movea.l fDCE(%a6),%a1       /* a1 = DCE for JIODone */
    unlk    %a6
    /* EDiskDone idiom: immediate request → plain rts with result in d0;
     * queued request → return through the JIODone vector. */
    btst    #NoQueueBit-8,ioTrap(%a0)   /* bit 9 of word = bit 1 of high byte */
    bne     prime_immed
    move.l  JIODone.w,-(%sp)
prime_immed:
    ext.l   %d0
    beq     prime_clean
    move.w  %d0,DskErr.w
prime_clean:
    rts

/* ---- CntrlParam offsets ---- */
    .equ csCode,   0x1A
    .equ csParam,  0x1C

/* ----------------------------------------------------------------------
 * Control — KillIO answers with a plain rts (mandatory); everything else
 * is accepted as a no-op and completed through the IODone tail so queued
 * requests don't stall the Device Manager.
 * ---------------------------------------------------------------------- */
drv_control:
    moveq   #0,%d0
    cmpi.w  #1,csCode(%a0)      /* killCode: plain return, never IODone */
    beq     ctl_plain_rts
    bra     io_done_tail
ctl_plain_rts:
    rts

/* ----------------------------------------------------------------------
 * Status — csCode 8 (drive status): fixed disk, installed, writable.
 * Unknown csCodes get statusErr.
 * ---------------------------------------------------------------------- */
drv_status:
    cmpi.w  #8,csCode(%a0)
    bne     status_unknown
    /* DrvSts: track.w wProt.b inPlace.b installed.b sides.b
     * (a1 = DCE must survive for JIODone — don't touch it) */
    clr.w   csParam+0(%a0)      /* track */
    clr.b   csParam+2(%a0)      /* not write-protected */
    move.b  #8,csParam+3(%a0)   /* disk in place: 8 = fixed, non-ejectable */
    move.b  #1,csParam+4(%a0)   /* installed */
    clr.b   csParam+5(%a0)      /* sides n/a */
    moveq   #0,%d0
    bra     io_done_tail
status_unknown:
    moveq   #-18,%d0            /* statusErr */
    /* fall through */

/* ----------------------------------------------------------------------
 * Shared completion: immediate → rts; queued → through JIODone.
 * In: A0 = PB, A1 = DCE (as delivered by the Device Manager), D0 = result.
 * ---------------------------------------------------------------------- */
io_done_tail:
    btst    #NoQueueBit-8,ioTrap(%a0)
    bne     iodt_immed
    move.l  JIODone.w,-(%sp)
iodt_immed:
    ext.l   %d0
    beq     iodt_clean
    move.w  %d0,DskErr.w
iodt_clean:
    rts
