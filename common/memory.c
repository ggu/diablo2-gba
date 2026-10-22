#include "gba.h"
void *memset(void *dst,int value,size_t n) { u8 *d=dst; while(n--) *d++=(u8)value; return dst; }
void *memcpy(void *dst,const void *src,size_t n) { u8*d=dst; const u8*s=src; while(n--) *d++=*s++; return dst; }
void *memmove(void *dst,const void *src,size_t n) { u8*d=dst; const u8*s=src; if(d<s) {while(n--)*d++=*s++;}else{d+=n;s+=n;while(n--)*--d=*--s;}return dst; }
int memcmp(const void *a,const void *b,size_t n) {const u8*x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
size_t strlen(const char*s) {size_t n=0;while(s[n])n++;return n;}
void __aeabi_memcpy(void*d,const void*s,size_t n){memcpy(d,s,n);}
void __aeabi_memcpy4(void*d,const void*s,size_t n){memcpy(d,s,n);}
void __aeabi_memcpy8(void*d,const void*s,size_t n){memcpy(d,s,n);}
void __aeabi_memset(void*d,size_t n,int v){memset(d,v,n);}
void __aeabi_memclr(void*d,size_t n){memset(d,0,n);}
void __aeabi_memclr4(void*d,size_t n){memset(d,0,n);}
