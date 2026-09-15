/* Auto-generated — do not edit */
#define FIGMENT_TRAP_COUNT 40
static const struct { uint8_t trap_num; uint32_t offset; } figment_trap_table[] = {
    { 0x19, 0x40846DA4 }, /* fig_InitZone */
    { 0x1A, 0x40846DDE }, /* fig_GetZone */
    { 0x1B, 0x40846DEA }, /* fig_SetZone */
    { 0x1C, 0x40846ED0 }, /* fig_FreeMem */
    { 0x1D, 0x40846EDC }, /* fig_MaxMem */
    { 0x1E, 0x40846E7E }, /* fig_NewPtr */
    { 0x1F, 0x40846E96 }, /* fig_DisposePtr */
    { 0x20, 0x40846EAE }, /* fig_SetPtrSize */
    { 0x21, 0x40846EA2 }, /* fig_GetPtrSize */
    { 0x22, 0x40846DF6 }, /* fig_NewHandle */
    { 0x23, 0x40846E22 }, /* fig_DisposeHandle */
    { 0x24, 0x40846E3A }, /* fig_SetHandleSize */
    { 0x25, 0x40846E2E }, /* fig_GetHandleSize */
    { 0x26, 0x40846E48 }, /* fig_HandleZone */
    { 0x27, 0x40846E70 }, /* fig_ReallocHandle */
    { 0x28, 0x40846E5C }, /* fig_RecoverHandle */
    { 0x29, 0x40846F24 }, /* fig_HLock */
    { 0x2A, 0x40846F30 }, /* fig_HUnlock */
    { 0x2B, 0x40846F18 }, /* fig_EmptyHandle */
    { 0x2C, 0x40846FDA }, /* fig_InitApplZone */
    { 0x2D, 0x40846DC2 }, /* fig_SetApplLimit */
    { 0x36, 0x40846DD6 }, /* fig_MoreMasters */
    { 0x40, 0x40846EFC }, /* fig_ReserveMem */
    { 0x48, 0x40846EBC }, /* fig_PtrZone */
    { 0x49, 0x40846F3C }, /* fig_HPurge */
    { 0x4A, 0x40846F48 }, /* fig_HNoPurge */
    { 0x4B, 0x40846F54 }, /* fig_SetGrowZone */
    { 0x4C, 0x40846EEE }, /* fig_CompactMem */
    { 0x4D, 0x40846F0A }, /* fig_PurgeMem */
    { 0x61, 0x40846F78 }, /* fig_MaxBlock */
    { 0x62, 0x40846F84 }, /* fig_PurgeSpace */
    { 0x63, 0x40846DCE }, /* fig_MaxApplZone */
    { 0x64, 0x40846F60 }, /* fig_MoveHHi */
    { 0x65, 0x40846F96 }, /* fig_StackSpace */
    { 0x66, 0x40846E0E }, /* fig_NewEmptyHandle */
    { 0x67, 0x40846FA8 }, /* fig_HSetRBit */
    { 0x68, 0x40846FB4 }, /* fig_HClrRBit */
    { 0x69, 0x40846FC0 }, /* fig_HGetState */
    { 0x6A, 0x40846FCC }, /* fig_HSetState */
    { 0xA4, 0x40874000 }, /* b32_HeapDispatch (figext) */
    { 0, 0 }  /* sentinel */
};
