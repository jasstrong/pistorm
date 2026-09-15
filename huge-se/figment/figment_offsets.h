/* Auto-generated — do not edit */
#define FIGMENT_TRAP_COUNT 40
static const struct { uint8_t trap_num; uint32_t offset; } figment_trap_table[] = {
    { 0x19, 0x40846DA4 }, /* fig_InitZone */
    { 0x1A, 0x40846DE4 }, /* fig_GetZone */
    { 0x1B, 0x40846DF0 }, /* fig_SetZone */
    { 0x1C, 0x40846EEC }, /* fig_FreeMem */
    { 0x1D, 0x40846EF8 }, /* fig_MaxMem */
    { 0x1E, 0x40846E92 }, /* fig_NewPtr */
    { 0x1F, 0x40846EAC }, /* fig_DisposePtr */
    { 0x20, 0x40846EC6 }, /* fig_SetPtrSize */
    { 0x21, 0x40846EBA }, /* fig_GetPtrSize */
    { 0x22, 0x40846DFE }, /* fig_NewHandle */
    { 0x23, 0x40846E2E }, /* fig_DisposeHandle */
    { 0x24, 0x40846E48 }, /* fig_SetHandleSize */
    { 0x25, 0x40846E3C }, /* fig_GetHandleSize */
    { 0x26, 0x40846E58 }, /* fig_HandleZone */
    { 0x27, 0x40846E82 }, /* fig_ReallocHandle */
    { 0x28, 0x40846E6E }, /* fig_RecoverHandle */
    { 0x29, 0x40846F46 }, /* fig_HLock */
    { 0x2A, 0x40846F54 }, /* fig_HUnlock */
    { 0x2B, 0x40846F38 }, /* fig_EmptyHandle */
    { 0x2C, 0x40847010 }, /* fig_InitApplZone */
    { 0x2D, 0x40846DC2 }, /* fig_SetApplLimit */
    { 0x36, 0x40846DDA }, /* fig_MoreMasters */
    { 0x40, 0x40846F18 }, /* fig_ReserveMem */
    { 0x48, 0x40846ED6 }, /* fig_PtrZone */
    { 0x49, 0x40846F62 }, /* fig_HPurge */
    { 0x4A, 0x40846F70 }, /* fig_HNoPurge */
    { 0x4B, 0x40846F7E }, /* fig_SetGrowZone */
    { 0x4C, 0x40846F0A }, /* fig_CompactMem */
    { 0x4D, 0x40846F28 }, /* fig_PurgeMem */
    { 0x61, 0x40846FA8 }, /* fig_MaxBlock */
    { 0x62, 0x40846FB4 }, /* fig_PurgeSpace */
    { 0x63, 0x40846DD0 }, /* fig_MaxApplZone */
    { 0x64, 0x40846F8C }, /* fig_MoveHHi */
    { 0x65, 0x40846FC6 }, /* fig_StackSpace */
    { 0x66, 0x40846E18 }, /* fig_NewEmptyHandle */
    { 0x67, 0x40846FD8 }, /* fig_HSetRBit */
    { 0x68, 0x40846FE6 }, /* fig_HClrRBit */
    { 0x69, 0x40846FF4 }, /* fig_HGetState */
    { 0x6A, 0x40847000 }, /* fig_HSetState */
    { 0xA4, 0x40874000 }, /* b32_HeapDispatch (figext) */
    { 0, 0 }  /* sentinel */
};
