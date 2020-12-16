#ifdef __x86_64__
#include <arch/x64/processor.hh>
#endif

unsigned long kaslr_get_random_long(void);