#include <osv/kaslr.hh>
#include <osv/version.hh>
#include <string.h>

extern size_t kaslr_vm_shift;
extern size_t elf_base;

extern "C" {
    static unsigned long get_boot_seed(void);
    void relocate_kernel(void * kern_elf_base);
}

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
    const char * version_str = osv::version().c_str();
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

void relocate_kernel(void * kern_elf_base)
{   
    unsigned short PHT_entry_size = *(unsigned short *)((char *)kern_elf_base + 0x36);
    unsigned short PHT_entry_cnt = *(unsigned short *)((char *)kern_elf_base + 0x38);
    size_t PHT_file_offset = *(size_t *)((char *)kern_elf_base + 0x20);
    intptr_t PHT_table = (intptr_t)kern_elf_base + PHT_file_offset;
    unsigned char type = 0;
    size_t d_tag, DT_init_arraysz = 0, DT_relasz = 0,  DT_pltrelsz = 0;
    unsigned int DT_symbolcnt = 0;
    intptr_t * DT_init_array = 0, DT_rela = 0 , *DT_pltgot = 0, DT_symtab = 0, *DT_hash = 0;//,* DT_jmprel, DT_strtab;

    for(short i = 0; i < PHT_entry_cnt; i++){
        *(intptr_t *)(PHT_table + 0x10) += kaslr_vm_shift;
        type = *(unsigned char *)PHT_table;
        if (type == 2){
            //DYNAMIC
            intptr_t DT_info = (intptr_t)kern_elf_base + *(size_t *)(PHT_table + 0x8);
            intptr_t DT_info_curr = DT_info;
            size_t DT_entry_size = 0x10;
            while((d_tag = *(size_t *)DT_info_curr)){
                if (d_tag == 0x19){ 
                    // DT_INIT_ARRAY
                    DT_init_array = *(intptr_t **)(DT_info_curr + 0x8);
                    *(intptr_t *)(DT_info_curr + 0x8) += kaslr_vm_shift;
                }
                if (d_tag == 0x1B){
                    //DT_INIT_ARRAYSZ
                    DT_init_arraysz = *(size_t *)(DT_info_curr + 0x8);
                }
                if (d_tag == 2){
                    DT_pltrelsz = *(size_t *)(DT_info_curr + 0x8);
                }
                if (d_tag == 3){
                    DT_pltgot = *(intptr_t **)(DT_info_curr + 0x8);
                    *(intptr_t *)(DT_info_curr + 0x8) += kaslr_vm_shift;
                }
                if (d_tag == 4){
                    DT_hash = *(intptr_t **)(DT_info_curr + 0x8);
                    *(intptr_t *)(DT_info_curr + 0x8) += kaslr_vm_shift;
                }
                if (d_tag == 0x6FFFFEF5){
                    // GNU_hash
                    *(intptr_t *)(DT_info_curr + 0x8) += kaslr_vm_shift;
                }
                if (d_tag == 5){
                //    DT_strtab = *(intptr_t *)(DT_info_curr + 0x8);
                    *(intptr_t *)(DT_info_curr + 0x8) += kaslr_vm_shift;
                }
                if (d_tag == 6){
                    DT_symtab = *(intptr_t *)(DT_info_curr + 0x8);
                    *(intptr_t *)(DT_info_curr + 0x8) += kaslr_vm_shift;
                }
                if (d_tag == 7){
                    DT_rela = *(intptr_t *)(DT_info_curr + 0x8);
                    *(intptr_t *)(DT_info_curr + 0x8) += kaslr_vm_shift;
                }
                if (d_tag == 8){
                    DT_relasz = *(size_t *)(DT_info_curr + 0x8);
                }
                //if (d_tag == 0x17){
                //    DT_jmprel = *(intptr_t **)(DT_info_curr + 0x8);
                //    *(intptr_t *)(DT_info_curr + 0x8) += kaslr_vm_shift;
                //}
                DT_info_curr += DT_entry_size;
            }

            // init array patch
            for(size_t j = 0; j < DT_init_arraysz/8; j ++)
                DT_init_array[j] += kaslr_vm_shift;

            // pltgot patch
            for(size_t j = 0; j < DT_pltrelsz/8; j ++)
                DT_pltgot[j] = DT_pltgot[j];

            // symtbl patch
            DT_symbolcnt = *(unsigned int *)(DT_hash + 0x4);
            for(size_t j = 0; j < DT_symbolcnt; j++){
                intptr_t symbol_ptr = (intptr_t)(DT_symtab + j * 0x18);
                unsigned char st_info = *(char *)(symbol_ptr + 4);
                if (st_info & 0xf0){
                    // global or weak symbol
                    if ((st_info & 0xf) == 2 || (st_info & 0xf) == 1)
                        // object or function
                        *(intptr_t *)(symbol_ptr + 0x8) += kaslr_vm_shift;
                }
            }

            intptr_t DT_rela_curr = DT_rela;
            // rela patch
            while((uintptr_t)DT_rela_curr < (DT_rela + DT_relasz)){
                *(size_t *)DT_rela_curr += kaslr_vm_shift;
                size_t r_info = *(size_t *)(DT_rela_curr + 0x8);
                if((r_info & 0xff) == 0x25){
                    *(intptr_t *)(DT_rela_curr + 0x10) += kaslr_vm_shift;
                }
                DT_rela_curr += 0x18;
            } 
        }
        PHT_table += PHT_entry_size;
    }
}