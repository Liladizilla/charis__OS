/* string.h - CharisOS string and memory utility functions */
#pragma once
#include <kernel/types.h>

/* Memory functions */
void* kmemset(void* dst, u8 val, usize n);
void* kmemcpy(void* dst, const void* src, usize n);
void* kmemmove(void* dst, const void* src, usize n);

/* String functions */
char* kstrcpy(char* dst, const char* src);
char* kstrncpy(char* dst, const char* src, usize n);
usize kstrlen(const char* s);
int kstrcmp(const char* a, const char* b);
int kstrncmp(const char* a, const char* b, usize n);
char* kstrchr(const char* s, char c);
char* kstrrchr(const char* s, char c);
char* kstrstr(const char* haystack, const char* needle);

/* Integer to string conversion */
char* kitoa(s64 val, char* buf, int base);
char* kutoa(u64 val, char* buf, int base);