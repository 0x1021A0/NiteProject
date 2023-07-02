#define va_list __builtin_va_list
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

void printf(const char* fmt, ...);
void vprintf(const char* fmt, va_list args);
char* itoa(int d, char* buf, int base);
char* utoa(unsigned long u, char* buf, int base);