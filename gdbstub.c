// SPDX-License-Identifier: MIT
#define _GNU_SOURCE  // for pthread_setname_np from <pthread.h> on glibc
//
// GDB Remote Serial Protocol stub for the PiStorm emulator.  See gdbstub.h.
//
// Threading model
// ---------------
// Two threads interact:
//   CPU thread    - runs m68k core, calls gdbstub_instr_hook(pc) before each
//                   instruction.  Blocks in gdb_cpu_wait() when stopped.
//   listener/rsp  - accepts gdb client, reads packets, handles register and
//                   memory peeks while CPU is stopped, wakes CPU on c/s.
//
// Synchronization: a single pthread mutex + condvar.  The CPU state is a
// simple enum (RUNNING/STOPPED).  Memory and register accesses from the RSP
// thread only happen while state == STOPPED, so no reader-writer races with
// Musashi internals.  (Other threads - IPL, VNC, keyboard - continue to run,
// which is acceptable for the PTCH/boot debugging use case; this stub is for
// inspecting CPU execution, not freezing the whole virtual machine.)

#include "gdbstub.h"

#include "m68k.h"
#include "config_file/config_file.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

extern struct emulator_config *cfg;

/* ---- tuning ---- */
#define GDB_MAX_BPS       64
#define GDB_PKT_BUFSZ     16384
#define GDB_REG_BYTES     72    /* 18 regs × 4 bytes: d0-d7 a0-a7 ps pc */

/* ---- state ---- */
enum { CPU_RUNNING = 0, CPU_STOPPED = 1 };

static pthread_mutex_t gdb_mu  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  gdb_cv  = PTHREAD_COND_INITIALIZER;

static atomic_int gdb_attached    = 0;   /* client connected */
static atomic_int gdb_break_pending = 0; /* request CPU stop asap */
static int        gdb_single_step = 0;   /* stop after one instruction */
static int        gdb_cpu_state   = CPU_RUNNING;
static int        gdb_stop_signal = 5;   /* SIGTRAP */
static int        gdb_listen_fd   = -1;
static int        gdb_client_fd   = -1;
static int        gdb_no_ack      = 0;   /* QStartNoAckMode */

static uint32_t   gdb_bp[GDB_MAX_BPS];
static int        gdb_bp_count = 0;

/* Watchpoints: types map to gdb Z packet codes.
 *   GDB_WP_WRITE  = 2  -> Z2 write watchpoint
 *   GDB_WP_READ   = 3  -> Z3 read  watchpoint
 *   GDB_WP_ACCESS = 4  -> Z4 access watchpoint (read or write)
 * Stored as tuples (addr, len, type).  Matched on any byte overlap with
 * the memory access range. */
#define GDB_MAX_WPS 32
#define GDB_WP_WRITE  2
#define GDB_WP_READ   3
#define GDB_WP_ACCESS 4
static struct { uint32_t addr; uint16_t len; uint8_t type; } gdb_wp[GDB_MAX_WPS];
int gdbstub_wp_count = 0;   /* non-static: referenced from inline in header */

/* Last watchpoint hit, consumed by the next instruction hook when it stops. */
static atomic_uint gdb_wp_hit_addr = 0;
static atomic_int  gdb_wp_hit_kind = 0;   /* 0=none, else GDB_WP_* */

/* ---- utility ---- */
static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static char hexchar(int v) { return "0123456789abcdef"[v & 0xF]; }

static int hex_to_bytes(const char *s, int nchars, uint8_t *out) {
    int nbytes = nchars / 2;
    for (int i = 0; i < nbytes; i++) {
        int hi = hexval(s[i*2]), lo = hexval(s[i*2+1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (hi << 4) | lo;
    }
    return nbytes;
}
static void bytes_to_hex(const uint8_t *in, int nbytes, char *out) {
    for (int i = 0; i < nbytes; i++) {
        out[i*2]   = hexchar(in[i] >> 4);
        out[i*2+1] = hexchar(in[i] & 0xF);
    }
}

/* ---- socket I/O ---- */
static int sock_write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf; size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, p + off, len - off, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

/* Send an RSP packet: framed as $<payload>#<csum-hex>.  Retries on NAK
 * unless QStartNoAckMode is active. */
static int gdb_send(const char *payload) {
    size_t len = strlen(payload);
    char *pkt = malloc(len + 8);
    if (!pkt) return -1;
    uint8_t csum = 0;
    for (size_t i = 0; i < len; i++) csum += (uint8_t)payload[i];
    int plen = snprintf(pkt, len + 8, "$%s#%c%c",
                        payload, hexchar(csum >> 4), hexchar(csum & 0xF));
    (void)plen;

    for (;;) {
        if (sock_write_all(gdb_client_fd, pkt, strlen(pkt)) < 0) {
            free(pkt);
            return -1;
        }
        if (gdb_no_ack) break;
        char c;
        ssize_t n = recv(gdb_client_fd, &c, 1, 0);
        if (n <= 0) { free(pkt); return -1; }
        if (c == '+') break;
        /* c == '-': resend */
    }
    free(pkt);
    return 0;
}

/* Read one packet payload into buf (null-terminated).  Returns payload
 * length, or -1 on disconnect / interrupt.  Handles Ctrl-C (0x03) out of
 * band: sets break_pending and loops for the next real packet. */
static int gdb_recv(char *buf, size_t cap) {
    size_t n = 0;
    int state = 0; /* 0=await $, 1=payload, 2=csum-hi, 3=csum-lo */
    uint8_t csum = 0;
    int hi = 0;
    for (;;) {
        uint8_t c;
        ssize_t r = recv(gdb_client_fd, &c, 1, 0);
        if (r <= 0) return -1;
        if (state == 0) {
            if (c == 0x03) {
                atomic_store(&gdb_break_pending, 1);
                continue;
            }
            if (c == '$') { state = 1; n = 0; csum = 0; }
            continue;
        }
        if (state == 1) {
            if (c == '#') { state = 2; continue; }
            if (n + 1 < cap) buf[n++] = (char)c;
            csum += c;
            continue;
        }
        if (state == 2) { hi = hexval(c); state = 3; continue; }
        if (state == 3) {
            int lo = hexval(c);
            uint8_t got = (uint8_t)((hi << 4) | lo);
            if (!gdb_no_ack) {
                char ack = (got == csum) ? '+' : '-';
                send(gdb_client_fd, &ack, 1, MSG_NOSIGNAL);
            }
            if (got != csum) { state = 0; continue; }
            buf[n] = 0;
            return (int)n;
        }
    }
}

/* ---- memory accessor (safe while CPU is stopped) ----
 * Walks cfg->map_data[] directly without the 24-bit mask used by the
 * existing direct_get_ptr() helper in emulator.c, so we can reach the
 * $40xxxxxx huge-se address space. */
static uint8_t *gdb_map_ptr(uint32_t addr) {
    if (!cfg) return NULL;
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
        unsigned t = cfg->map_type[i];
        if (t == MAPTYPE_NONE || !cfg->map_data[i]) continue;
        if (t != MAPTYPE_RAM && t != MAPTYPE_RAM_WTC &&
            t != MAPTYPE_RAM_NOALLOC && t != MAPTYPE_ROM) continue;
        if (addr >= cfg->map_offset[i] && addr < cfg->map_high[i])
            return cfg->map_data[i] + (addr - cfg->map_offset[i]);
    }
    return NULL;
}

/* ---- register block ---- */
/* GDB m68k layout for our target.xml: d0..d7 a0..a7 ps pc (18 × 32 bits). */
static void gdb_get_regs(uint8_t *out) {
    memset(out, 0, GDB_REG_BYTES);
    uint32_t v;
    for (int i = 0; i < 8; i++) {
        v = m68k_get_reg(NULL, M68K_REG_D0 + i);
        out[i*4+0] = v >> 24; out[i*4+1] = v >> 16;
        out[i*4+2] = v >> 8;  out[i*4+3] = v;
    }
    for (int i = 0; i < 8; i++) {
        v = m68k_get_reg(NULL, M68K_REG_A0 + i);
        int o = (8 + i) * 4;
        out[o+0] = v >> 24; out[o+1] = v >> 16;
        out[o+2] = v >> 8;  out[o+3] = v;
    }
    v = m68k_get_reg(NULL, M68K_REG_SR);
    out[16*4+0] = v >> 24; out[16*4+1] = v >> 16;
    out[16*4+2] = v >> 8;  out[16*4+3] = v;
    v = m68k_get_reg(NULL, M68K_REG_PC);
    out[17*4+0] = v >> 24; out[17*4+1] = v >> 16;
    out[17*4+2] = v >> 8;  out[17*4+3] = v;
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}
static void gdb_set_regs(const uint8_t *in) {
    for (int i = 0; i < 8; i++)
        m68k_set_reg(NULL, M68K_REG_D0 + i, be32(in + i*4));
    for (int i = 0; i < 8; i++)
        m68k_set_reg(NULL, M68K_REG_A0 + i, be32(in + (8+i)*4));
    m68k_set_reg(NULL, M68K_REG_SR, be32(in + 16*4));
    m68k_set_reg(NULL, M68K_REG_PC, be32(in + 17*4));
}

/* ---- target description ---- */
static const char gdb_target_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
    "<target version=\"1.0\">\n"
    "  <architecture>m68k</architecture>\n"
    "  <feature name=\"org.gnu.gdb.m68k.core\">\n"
    "    <reg name=\"d0\" bitsize=\"32\"/>\n"
    "    <reg name=\"d1\" bitsize=\"32\"/>\n"
    "    <reg name=\"d2\" bitsize=\"32\"/>\n"
    "    <reg name=\"d3\" bitsize=\"32\"/>\n"
    "    <reg name=\"d4\" bitsize=\"32\"/>\n"
    "    <reg name=\"d5\" bitsize=\"32\"/>\n"
    "    <reg name=\"d6\" bitsize=\"32\"/>\n"
    "    <reg name=\"d7\" bitsize=\"32\"/>\n"
    "    <reg name=\"a0\" bitsize=\"32\" type=\"data_ptr\"/>\n"
    "    <reg name=\"a1\" bitsize=\"32\" type=\"data_ptr\"/>\n"
    "    <reg name=\"a2\" bitsize=\"32\" type=\"data_ptr\"/>\n"
    "    <reg name=\"a3\" bitsize=\"32\" type=\"data_ptr\"/>\n"
    "    <reg name=\"a4\" bitsize=\"32\" type=\"data_ptr\"/>\n"
    "    <reg name=\"a5\" bitsize=\"32\" type=\"data_ptr\"/>\n"
    "    <reg name=\"fp\" bitsize=\"32\" type=\"data_ptr\"/>\n"
    "    <reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>\n"
    "    <reg name=\"ps\" bitsize=\"32\"/>\n"
    "    <reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>\n"
    "  </feature>\n"
    "</target>\n";

/* ---- packet handlers ---- */
static void gdb_send_stop(void) {
    char buf[64];
    int kind = atomic_exchange(&gdb_wp_hit_kind, 0);
    if (kind != 0) {
        uint32_t addr = atomic_load(&gdb_wp_hit_addr);
        const char *tag = (kind == GDB_WP_WRITE) ? "watch"
                        : (kind == GDB_WP_READ)  ? "rwatch"
                        :                          "awatch";
        snprintf(buf, sizeof(buf), "T%02x%s:%x;", gdb_stop_signal & 0xFF, tag, addr);
    } else {
        snprintf(buf, sizeof(buf), "S%02x", gdb_stop_signal & 0xFF);
    }
    gdb_send(buf);
}

static int strstart(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void handle_q(const char *pkt) {
    if (strstart(pkt, "qSupported")) {
        char reply[256];
        snprintf(reply, sizeof(reply),
                 "PacketSize=%x;qXfer:features:read+;QStartNoAckMode+",
                 GDB_PKT_BUFSZ - 64);
        gdb_send(reply);
        return;
    }
    if (strstart(pkt, "qXfer:features:read:target.xml:")) {
        unsigned off, len;
        if (sscanf(pkt + strlen("qXfer:features:read:target.xml:"),
                   "%x,%x", &off, &len) != 2) { gdb_send("E01"); return; }
        size_t total = sizeof(gdb_target_xml) - 1;
        if (off >= total) { gdb_send("l"); return; }
        size_t avail = total - off;
        if (len > avail) len = avail;
        if (len > GDB_PKT_BUFSZ - 16) len = GDB_PKT_BUFSZ - 16;
        char *r = malloc(len + 2);
        r[0] = (off + len < total) ? 'm' : 'l';
        memcpy(r + 1, gdb_target_xml + off, len);
        r[len + 1] = 0;
        /* NOTE: binary-safe would require escaping; target.xml is plain ASCII. */
        gdb_send(r);
        free(r);
        return;
    }
    if (strstart(pkt, "qAttached")) { gdb_send("1"); return; }
    if (strstart(pkt, "qC"))        { gdb_send("QC0"); return; }
    if (strstart(pkt, "qfThreadInfo")) { gdb_send("m0"); return; }
    if (strstart(pkt, "qsThreadInfo")) { gdb_send("l");  return; }
    if (strstart(pkt, "qOffsets"))  { gdb_send(""); return; }
    if (strstart(pkt, "qSymbol"))   { gdb_send("OK"); return; }
    if (strstart(pkt, "qTStatus"))  { gdb_send(""); return; }
    gdb_send("");
}

static void handle_Q(const char *pkt) {
    if (strstart(pkt, "QStartNoAckMode")) {
        gdb_send("OK");
        gdb_no_ack = 1;
        return;
    }
    gdb_send("");
}

static void handle_v(const char *pkt) {
    if (strstart(pkt, "vMustReplyEmpty")) { gdb_send(""); return; }
    if (strstart(pkt, "vCont?"))          { gdb_send("vCont;c;s"); return; }
    if (strstart(pkt, "vCont;c")) {
        gdb_single_step = 0;
        pthread_mutex_lock(&gdb_mu);
        gdb_cpu_state = CPU_RUNNING;
        pthread_cond_broadcast(&gdb_cv);
        pthread_mutex_unlock(&gdb_mu);
        /* no immediate reply; reply when we next stop */
        return;
    }
    if (strstart(pkt, "vCont;s")) {
        gdb_single_step = 1;
        pthread_mutex_lock(&gdb_mu);
        gdb_cpu_state = CPU_RUNNING;
        pthread_cond_broadcast(&gdb_cv);
        pthread_mutex_unlock(&gdb_mu);
        return;
    }
    gdb_send("");
}

static int bp_find(uint32_t addr) {
    for (int i = 0; i < gdb_bp_count; i++)
        if (gdb_bp[i] == addr) return i;
    return -1;
}
static int wp_find(uint32_t addr, unsigned len, unsigned type) {
    for (int i = 0; i < gdbstub_wp_count; i++)
        if (gdb_wp[i].addr == addr && gdb_wp[i].len == len &&
            gdb_wp[i].type == type) return i;
    return -1;
}
static void handle_bp(const char *pkt, int set) {
    /* Format: Z<type>,addr,kind / z<type>,addr,kind
     *   type 0/1 = software/hardware breakpoint (PC match)
     *   type 2   = write watchpoint       (len = kind in bytes)
     *   type 3   = read  watchpoint
     *   type 4   = access watchpoint */
    unsigned type; unsigned addr; unsigned kind;
    if (sscanf(pkt + 1, "%u,%x,%x", &type, &addr, &kind) < 2) {
        gdb_send("E01"); return;
    }
    if (type <= 1) {
        if (set) {
            if (bp_find(addr) >= 0) { gdb_send("OK"); return; }
            if (gdb_bp_count >= GDB_MAX_BPS) { gdb_send("E02"); return; }
            gdb_bp[gdb_bp_count++] = addr;
        } else {
            int idx = bp_find(addr);
            if (idx >= 0) gdb_bp[idx] = gdb_bp[--gdb_bp_count];
        }
        gdb_send("OK");
        return;
    }
    if (type >= 2 && type <= 4) {
        if (kind == 0 || kind > 256) { gdb_send("E03"); return; }
        if (set) {
            if (wp_find(addr, kind, type) >= 0) { gdb_send("OK"); return; }
            if (gdbstub_wp_count >= GDB_MAX_WPS) { gdb_send("E04"); return; }
            gdb_wp[gdbstub_wp_count].addr = addr;
            gdb_wp[gdbstub_wp_count].len  = (uint16_t)kind;
            gdb_wp[gdbstub_wp_count].type = (uint8_t)type;
            /* Publish count last so the read-side hot path sees a fully
             * initialized entry. */
            __atomic_store_n(&gdbstub_wp_count, gdbstub_wp_count + 1,
                             __ATOMIC_RELEASE);
        } else {
            int idx = wp_find(addr, kind, type);
            if (idx >= 0) {
                gdb_wp[idx] = gdb_wp[gdbstub_wp_count - 1];
                __atomic_store_n(&gdbstub_wp_count, gdbstub_wp_count - 1,
                                 __ATOMIC_RELEASE);
            }
        }
        gdb_send("OK");
        return;
    }
    gdb_send("");   /* unknown type */
}

static void handle_m(const char *pkt) {
    unsigned addr, len;
    if (sscanf(pkt + 1, "%x,%x", &addr, &len) != 2) { gdb_send("E01"); return; }
    if (len > (GDB_PKT_BUFSZ - 16) / 2) len = (GDB_PKT_BUFSZ - 16) / 2;
    char *r = malloc(len * 2 + 1);
    for (unsigned i = 0; i < len; i++) {
        uint8_t *p = gdb_map_ptr(addr + i);
        uint8_t v = p ? *p : 0;
        r[i*2]   = hexchar(v >> 4);
        r[i*2+1] = hexchar(v & 0xF);
    }
    r[len * 2] = 0;
    gdb_send(r);
    free(r);
}
static void handle_M(const char *pkt) {
    unsigned addr, len;
    const char *colon = strchr(pkt, ':');
    if (!colon || sscanf(pkt + 1, "%x,%x", &addr, &len) != 2) {
        gdb_send("E01"); return;
    }
    const char *hex = colon + 1;
    for (unsigned i = 0; i < len; i++) {
        int hi = hexval(hex[i*2]), lo = hexval(hex[i*2+1]);
        if (hi < 0 || lo < 0) { gdb_send("E02"); return; }
        uint8_t *p = gdb_map_ptr(addr + i);
        if (p) *p = (uint8_t)((hi << 4) | lo);
        /* addresses outside our maps are silently dropped */
    }
    gdb_send("OK");
}

/* ---- main per-packet dispatch ---- */
static int gdb_handle_packet(char *pkt) {
    switch (pkt[0]) {
        case '?':
            gdb_send_stop();
            return 0;
        case 'g': {
            uint8_t regs[GDB_REG_BYTES];
            char hex[GDB_REG_BYTES * 2 + 1];
            gdb_get_regs(regs);
            bytes_to_hex(regs, GDB_REG_BYTES, hex);
            hex[GDB_REG_BYTES * 2] = 0;
            gdb_send(hex);
            return 0;
        }
        case 'G': {
            uint8_t regs[GDB_REG_BYTES];
            if (hex_to_bytes(pkt + 1, GDB_REG_BYTES * 2, regs) != GDB_REG_BYTES) {
                gdb_send("E01"); return 0;
            }
            gdb_set_regs(regs);
            gdb_send("OK");
            return 0;
        }
        case 'p': {
            unsigned n;
            if (sscanf(pkt + 1, "%x", &n) != 1 || n > 17) { gdb_send("E01"); return 0; }
            uint32_t v;
            if (n < 8)       v = m68k_get_reg(NULL, M68K_REG_D0 + n);
            else if (n < 16) v = m68k_get_reg(NULL, M68K_REG_A0 + (n - 8));
            else if (n == 16) v = m68k_get_reg(NULL, M68K_REG_SR);
            else              v = m68k_get_reg(NULL, M68K_REG_PC);
            char hex[9];
            uint8_t b[4] = { v >> 24, v >> 16, v >> 8, v };
            bytes_to_hex(b, 4, hex); hex[8] = 0;
            gdb_send(hex);
            return 0;
        }
        case 'P': {
            unsigned n; char *eq = strchr(pkt, '=');
            if (!eq || sscanf(pkt + 1, "%x", &n) != 1) { gdb_send("E01"); return 0; }
            uint8_t b[4];
            if (hex_to_bytes(eq + 1, 8, b) != 4) { gdb_send("E02"); return 0; }
            uint32_t v = be32(b);
            if (n < 8)        m68k_set_reg(NULL, M68K_REG_D0 + n, v);
            else if (n < 16)  m68k_set_reg(NULL, M68K_REG_A0 + (n - 8), v);
            else if (n == 16) m68k_set_reg(NULL, M68K_REG_SR, v);
            else if (n == 17) m68k_set_reg(NULL, M68K_REG_PC, v);
            else { gdb_send("E03"); return 0; }
            gdb_send("OK");
            return 0;
        }
        case 'm': handle_m(pkt); return 0;
        case 'M': handle_M(pkt); return 0;
        case 'Z': handle_bp(pkt, 1); return 0;
        case 'z': handle_bp(pkt, 0); return 0;
        case 'c': {
            /* optional resume addr */
            unsigned addr;
            if (pkt[1] && sscanf(pkt + 1, "%x", &addr) == 1)
                m68k_set_reg(NULL, M68K_REG_PC, addr);
            gdb_single_step = 0;
            pthread_mutex_lock(&gdb_mu);
            gdb_cpu_state = CPU_RUNNING;
            pthread_cond_broadcast(&gdb_cv);
            pthread_mutex_unlock(&gdb_mu);
            return 1; /* leave packet loop; CPU resumes */
        }
        case 's': {
            unsigned addr;
            if (pkt[1] && sscanf(pkt + 1, "%x", &addr) == 1)
                m68k_set_reg(NULL, M68K_REG_PC, addr);
            gdb_single_step = 1;
            pthread_mutex_lock(&gdb_mu);
            gdb_cpu_state = CPU_RUNNING;
            pthread_cond_broadcast(&gdb_cv);
            pthread_mutex_unlock(&gdb_mu);
            return 1;
        }
        case 'H': gdb_send("OK"); return 0;
        case 'q': handle_q(pkt); return 0;
        case 'Q': handle_Q(pkt); return 0;
        case 'v': handle_v(pkt); return 0;
        case 'D': gdb_send("OK"); return -1;
        case 'k': return -1;
        default:  gdb_send(""); return 0;
    }
}

/* ---- the RSP service loop (listener thread) ---- */
static void gdb_service_client(void) {
    char *pkt = malloc(GDB_PKT_BUFSZ);
    if (!pkt) return;

    gdb_no_ack = 0;
    atomic_store(&gdb_attached, 1);
    /* Force an initial stop on attach so the user can set breakpoints. */
    atomic_store(&gdb_break_pending, 1);

    for (;;) {
        /* Wait for CPU to reach a stop before handling packets that need
         * consistent state.  The '?' reply and register reads only make
         * sense once stopped.  But we also need to be able to accept a
         * Ctrl-C from the client while the CPU is running, which happens
         * inside gdb_recv() via the 0x03 handling. */

        /* Wait for CPU stop (or a packet if already stopped). */
        pthread_mutex_lock(&gdb_mu);
        while (gdb_cpu_state == CPU_RUNNING) {
            /* Use a timed wait so we can poll for Ctrl-C without blocking. */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 50 * 1000 * 1000; /* 50 ms */
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&gdb_cv, &gdb_mu, &ts);
            pthread_mutex_unlock(&gdb_mu);

            /* Non-blocking poll for Ctrl-C / disconnect. */
            int flags = fcntl(gdb_client_fd, F_GETFL, 0);
            fcntl(gdb_client_fd, F_SETFL, flags | O_NONBLOCK);
            char c;
            ssize_t r = recv(gdb_client_fd, &c, 1, 0);
            fcntl(gdb_client_fd, F_SETFL, flags);
            if (r == 0) goto disconnect;
            if (r > 0 && c == 0x03) {
                atomic_store(&gdb_break_pending, 1);
            }

            pthread_mutex_lock(&gdb_mu);
        }
        pthread_mutex_unlock(&gdb_mu);

        /* CPU is stopped: send stop reply, then service packets until c/s/D/k. */
        gdb_send_stop();

        for (;;) {
            int n = gdb_recv(pkt, GDB_PKT_BUFSZ);
            if (n < 0) goto disconnect;
            int rc = gdb_handle_packet(pkt);
            if (rc > 0) break;     /* resume */
            if (rc < 0) goto disconnect;
        }
    }

disconnect:
    free(pkt);
    atomic_store(&gdb_attached, 0);
    atomic_store(&gdb_break_pending, 0);
    atomic_store(&gdb_wp_hit_kind, 0);
    gdb_single_step = 0;
    gdb_bp_count = 0;
    __atomic_store_n(&gdbstub_wp_count, 0, __ATOMIC_RELEASE);
    pthread_mutex_lock(&gdb_mu);
    gdb_cpu_state = CPU_RUNNING;
    pthread_cond_broadcast(&gdb_cv);
    pthread_mutex_unlock(&gdb_mu);
    close(gdb_client_fd);
    gdb_client_fd = -1;
    printf("[GDB] client disconnected\n");
}

static void *gdb_listener_thread(void *arg) {
    (void)arg;
    /* Ignore SIGPIPE in this thread (we use MSG_NOSIGNAL but be safe). */
    signal(SIGPIPE, SIG_IGN);

    for (;;) {
        struct sockaddr_in peer;
        socklen_t slen = sizeof(peer);
        int cfd = accept(gdb_listen_fd, (struct sockaddr *)&peer, &slen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("[GDB] accept");
            sleep(1);
            continue;
        }
        int one = 1;
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        gdb_client_fd = cfd;
        printf("[GDB] client connected from %s:%u\n",
               inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));

        gdb_service_client();
    }
    return NULL;
}

/* ---- CPU-side hook ---- */
static void gdb_cpu_wait(void) {
    /* Called with state already set to STOPPED; broadcast, then wait. */
    pthread_mutex_lock(&gdb_mu);
    gdb_cpu_state = CPU_STOPPED;
    pthread_cond_broadcast(&gdb_cv);
    while (gdb_cpu_state == CPU_STOPPED)
        pthread_cond_wait(&gdb_cv, &gdb_mu);
    pthread_mutex_unlock(&gdb_mu);
}

void gdbstub_instr_hook(unsigned int pc) {
    /* Hot path: skip if no gdb client and no breakpoints set. */
    if (!atomic_load_explicit(&gdb_attached, memory_order_relaxed) &&
        gdb_bp_count == 0) return;

    int stop = 0;

    if (atomic_load_explicit(&gdb_break_pending, memory_order_relaxed)) {
        atomic_store(&gdb_break_pending, 0);
        stop = 1;
    }
    if (gdb_single_step) {
        gdb_single_step = 0;
        stop = 1;
    }
    if (gdb_bp_count) {
        for (int i = 0; i < gdb_bp_count; i++) {
            if (gdb_bp[i] == pc) { stop = 1; break; }
        }
    }
    if (stop) {
        gdb_stop_signal = 5; /* SIGTRAP */
        gdb_cpu_wait();
    }
}

/* ---- watchpoint hot path (called from emulator.c memory hooks) ----
 * The slow-path helper only runs when at least one watchpoint is registered
 * (the inline wrapper in gdbstub.h filters out the common no-watchpoint case).
 * Scans the watch list; on match records (addr, kind) and sets the break
 * flag.  The actual stop happens on the next gdbstub_instr_hook() call, so
 * the reported PC is one instruction past the triggering access (imprecise
 * watchpoint semantics — standard gdb behavior on non-hardware targets). */
void gdbstub_watch_check_slow(unsigned int addr, int len, int is_write) {
    int n = __atomic_load_n(&gdbstub_wp_count, __ATOMIC_ACQUIRE);
    unsigned end = addr + (unsigned)len;   /* exclusive upper bound */
    for (int i = 0; i < n; i++) {
        unsigned wstart = gdb_wp[i].addr;
        unsigned wend   = wstart + gdb_wp[i].len;
        if (end <= wstart || addr >= wend) continue;   /* no overlap */
        int t = gdb_wp[i].type;
        if (is_write) {
            if (t != GDB_WP_WRITE && t != GDB_WP_ACCESS) continue;
        } else {
            if (t != GDB_WP_READ  && t != GDB_WP_ACCESS) continue;
        }
        /* Hit: record and request stop at next instruction boundary. */
        atomic_store(&gdb_wp_hit_addr, addr);
        atomic_store(&gdb_wp_hit_kind, t);
        atomic_store(&gdb_break_pending, 1);
        return;
    }
}

/* ---- init / shutdown ---- */
int gdbstub_init(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("[GDB] socket"); return -1; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port        = htons(port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("[GDB] bind"); close(fd); return -1;
    }
    if (listen(fd, 1) < 0) {
        perror("[GDB] listen"); close(fd); return -1;
    }
    gdb_listen_fd = fd;

    /* Install instruction hook.  Harmless if never armed - the hook
     * returns immediately when no client is attached. */
    m68k_set_instr_hook_callback(gdbstub_instr_hook);

    pthread_t tid;
    if (pthread_create(&tid, NULL, gdb_listener_thread, NULL) != 0) {
        perror("[GDB] pthread_create");
        close(fd);
        return -1;
    }
    pthread_setname_np(tid, "pistorm: gdb");
    pthread_detach(tid);

    printf("[GDB] stub listening on :%d\n", port);
    return 0;
}

void gdbstub_shutdown(void) {
    if (gdb_client_fd >= 0) close(gdb_client_fd);
    if (gdb_listen_fd >= 0) close(gdb_listen_fd);
}
