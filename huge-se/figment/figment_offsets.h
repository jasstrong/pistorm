/* Auto-generated — do not edit */
#define FIGMENT_TRAP_COUNT 40
static const struct { uint8_t trap_num; uint32_t offset; } figment_trap_table[] = {
    { 0x19, 0x40846DA8 }, /* fig_InitZone */
    { 0x1A, 0x40846DE2 }, /* fig_GetZone */
    { 0x1B, 0x40846DEE }, /* fig_SetZone */
    { 0x1C, 0x40846ED4 }, /* fig_FreeMem */
    { 0x1D, 0x40846EE0 }, /* fig_MaxMem */
    { 0x1E, 0x40846E82 }, /* fig_NewPtr */
    { 0x1F, 0x40846E9A }, /* fig_DisposePtr */
    { 0x20, 0x40846EB2 }, /* fig_SetPtrSize */
    { 0x21, 0x40846EA6 }, /* fig_GetPtrSize */
    { 0x22, 0x40846DFA }, /* fig_NewHandle */
    { 0x23, 0x40846E26 }, /* fig_DisposeHandle */
    { 0x24, 0x40846E3E }, /* fig_SetHandleSize */
    { 0x25, 0x40846E32 }, /* fig_GetHandleSize */
    { 0x26, 0x40846E4C }, /* fig_HandleZone */
    { 0x27, 0x40846E74 }, /* fig_ReallocHandle */
    { 0x28, 0x40846E60 }, /* fig_RecoverHandle */
    { 0x29, 0x40846F28 }, /* fig_HLock */
    { 0x2A, 0x40846F34 }, /* fig_HUnlock */
    { 0x2B, 0x40846F1C }, /* fig_EmptyHandle */
    { 0x2C, 0x40846FDE }, /* fig_InitApplZone */
    { 0x2D, 0x40846DC6 }, /* fig_SetApplLimit */
    { 0x36, 0x40846DDA }, /* fig_MoreMasters */
    { 0x40, 0x40846F00 }, /* fig_ReserveMem */
    { 0x48, 0x40846EC0 }, /* fig_PtrZone */
    { 0x49, 0x40846F40 }, /* fig_HPurge */
    { 0x4A, 0x40846F4C }, /* fig_HNoPurge */
    { 0x4B, 0x40846F58 }, /* fig_SetGrowZone */
    { 0x4C, 0x40846EF2 }, /* fig_CompactMem */
    { 0x4D, 0x40846F0E }, /* fig_PurgeMem */
    { 0x61, 0x40846F7C }, /* fig_MaxBlock */
    { 0x62, 0x40846F88 }, /* fig_PurgeSpace */
    { 0x63, 0x40846DD2 }, /* fig_MaxApplZone */
    { 0x64, 0x40846F64 }, /* fig_MoveHHi */
    { 0x65, 0x40846F9A }, /* fig_StackSpace */
    { 0x66, 0x40846E12 }, /* fig_NewEmptyHandle */
    { 0x67, 0x40846FAC }, /* fig_HSetRBit */
    { 0x68, 0x40846FB8 }, /* fig_HClrRBit */
    { 0x69, 0x40846FC4 }, /* fig_HGetState */
    { 0x6A, 0x40846FD0 }, /* fig_HSetState */
    { 0xA4, 0x40874000 }, /* b32_HeapDispatch (figext) */
    { 0, 0 }  /* sentinel */
};
