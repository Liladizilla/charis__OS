/* string.c - CharisOS string and memory utility functions */
#include <kernel/string.h>

void* kmemset(void* dst, u8 val, usize n) {
    if (dst == NULL) return NULL;
    u8* d = (u8*)dst;
    for (usize i = 0; i < n; i++) {
        d[i] = val;
    }
    return dst;
}

void* kmemcpy(void* dst, const void* src, usize n) {
    if (dst == NULL || src == NULL) return NULL;
    if (n == 0 || dst == src) return dst;

    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;

    if (d < s) {
        for (usize i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (usize i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

void* kmemmove(void* dst, const void* src, usize n) {
    if (dst == NULL || src == NULL) return NULL;
    if (n == 0 || dst == src) return dst;

    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;

    if (d < s) {
        for (usize i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (usize i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

char* kstrcpy(char* dst, const char* src) {
    if (dst == NULL) return NULL;
    if (src == NULL) {
        dst[0] = '\0';
        return dst;
    }

    char* out = dst;
    while ((*out++ = *src++) != '\0') {
        ;
    }
    return dst;
}

char* kstrncpy(char* dst, const char* src, usize n) {
    if (dst == NULL) return NULL;
    char* out = dst;
    usize i = 0;

    if (src != NULL) {
        while (i < n && src[i] != '\0') {
            out[i] = src[i];
            i++;
        }
    }

    while (i < n) {
        out[i++] = '\0';
    }
    return dst;
}

usize kstrlen(const char* s) {
    if (s == NULL) return 0;
    usize len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

int kstrcmp(const char* a, const char* b) {
    if (a == b) return 0;
    if (a == NULL) return -1;
    if (b == NULL) return 1;

    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *(const u8*)a - *(const u8*)b;
}

int kstrncmp(const char* a, const char* b, usize n) {
    if (n == 0) return 0;
    if (a == b) return 0;
    if (a == NULL) return -1;
    if (b == NULL) return 1;

    usize i = 0;
    while (i < n && a[i] && b[i] && a[i] == b[i]) {
        i++;
    }
    if (i == n) return 0;
    return *(const u8*)(a + i) - *(const u8*)(b + i);
}

char* kstrchr(const char* s, char c) {
    if (s == NULL) return NULL;
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return NULL;
}

char* kstrrchr(const char* s, char c) {
    if (s == NULL) return NULL;
    const char* last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return (char*)last;
}

char* kstrstr(const char* haystack, const char* needle) {
    if (haystack == NULL || needle == NULL) return NULL;
    if (*needle == '\0') return (char*)haystack;

    usize needle_len = kstrlen(needle);
    if (needle_len == 0) return (char*)haystack;

    for (usize i = 0; haystack[i] != '\0'; i++) {
        if (haystack[i] != needle[0]) continue;
        usize j;
        for (j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) break;
        }
        if (j == needle_len) return (char*)&haystack[i];
    }
    return NULL;
}

static char* kutoa_internal(u64 value, char* buf, int base, bool uppercase) {
    if (buf == NULL) return NULL;
    if (base < 2 || base > 36) {
        buf[0] = '\0';
        return buf;
    }

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    char tmp[65];
    int idx = 0;
    while (value != 0) {
        int digit = value % base;
        if (digit < 10) {
            tmp[idx++] = '0' + digit;
        } else {
            tmp[idx++] = (uppercase ? 'A' : 'a') + (digit - 10);
        }
        value /= base;
    }

    int pos = 0;
    while (idx > 0) {
        buf[pos++] = tmp[--idx];
    }
    buf[pos] = '\0';
    return buf;
}

char* kitoa(s64 val, char* buf, int base) {
    if (buf == NULL) return NULL;
    if (base < 2 || base > 36) {
        buf[0] = '\0';
        return buf;
    }

    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    bool negative = false;
    u64 value;
    if (base == 10 && val < 0) {
        negative = true;
        value = -(u64)val;
    } else {
        value = (u64)val;
    }

    char* out = buf;
    if (negative) {
        *out++ = '-';
    }
    kutoa_internal(value, out, base, false);
    return buf;
}

char* kutoa(u64 val, char* buf, int base) {
    if (buf == NULL) return NULL;
    if (base < 2 || base > 36) {
        if (buf != NULL) *buf = '\0';
        return buf;
    }
    
    // Handle special case of 0
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }
    
    char tmp[65]; // enough for 64-bit binary
    int tmp_pos = 0;
    
    while (val > 0) {
        int rem = val % base;
        tmp[tmp_pos++] = (rem < 10) ? ('0' + rem) : ('A' + rem - 10);
        val /= base;
    }
    
    int pos = 0;
    for (int i = tmp_pos - 1; i >= 0; i--) {
        buf[pos++] = tmp[i];
    }
    buf[pos] = '\0';

    return buf;
}

u64 katoi(const char* s, int base) {
    if (!s || base < 2 || base > 36) return 0;

    u64 result = 0;
    while (*s) {
        char c = *s;
        u64 val;
        if (c >= '0' && c <= '9') {
            val = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            val = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'Z') {
            val = c - 'A' + 10;
        } else {
            break;
        }
        if (val >= (u64)base) break;
        result = result * base + val;
        s++;
    }
    return result;
}