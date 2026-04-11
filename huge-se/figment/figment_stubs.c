/* figment_stubs.c — stub functions and libc replacements for Figment ROM build */

/* memset — needed by ClearBytes and other code. Inline for ROM, no libc. */
void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

/* ClearBytes — replaces MPW inline asm that does JSR ([$1E04]).
 * The vector at $1E04 is not initialized for Figment. */
void ClearBytes(void *dst, unsigned long len) {
    memset(dst, 0, len);
}

/* memmove — needed by BlockMoveData */
void *memmove(void *dst, const void *src, unsigned long n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}
