#include <osv/kaslr.hh>
#include <osv/version.h> 
#include <string.h>



static unsigned long rotate_xor(unsigned long hash, const void *area,
				size_t size)
{
	size_t i;
	unsigned long *ptr = (unsigned long *)area;
	for (i = 0; i < size / sizeof(hash); i++) {
		/* Rotate by odd number of bits and XOR. */
		hash = (hash << ((sizeof(hash) * 8) - 7)) | (hash >> 7);
		hash ^= ptr[i];
	}
	return hash;
}

/* Attempt to create a simple but unpredictable starting entropy. */
static unsigned long get_boot_seed(void)
{
	unsigned long hash = 0;
    const char * version_str = OSV_VERSION;
	hash = rotate_xor(hash, version_str, strlen(version_str));
	//TODO : add more entropy source such as boot_param or loader_argv
	return hash;
}

unsigned long kaslr_get_random_long(void)
{
#if (defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || \
    defined(__amd64) || defined(__ppc64__) || defined(_WIN64) || \
    defined(__LP64__) || defined(_LP64) || defined(__aarch64__))
	const unsigned long mix_const = 0x5d6008cbf3848dd3UL;
#else
	const unsigned long mix_const = 0x3f39e593UL;
#endif
	unsigned long raw, random = get_boot_seed();

//TODO : support non-x86-64 arch
#ifdef __x86_64__
	if (processor::features().rdrand) {
		if (processor::rdrand(&raw)) {
			random ^= raw;
		}
	}
    //no check for rdtsc feature?? 
	raw = processor::rdtsc();
	random ^= raw;

	/* Circular multiply for better bit diffusion */
	asm("mulq %3"
	    : "=a" (random), "=d" (raw)
	    : "a" (random), "rm" (mix_const)); 
#endif
	random += raw;

	return random;
}