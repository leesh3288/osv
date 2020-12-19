#include <osv/kaslr.hh>
#include <osv/version.hh>
#include <osv/elf.hh>
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
    unsigned short PHT_entry_size = ((elf::Elf64_Ehdr *)kern_elf_base)->e_phentsize;
    unsigned short PHT_entry_cnt = ((elf::Elf64_Ehdr *)kern_elf_base)->e_phnum;
    size_t PHT_file_offset = ((elf::Elf64_Ehdr *)kern_elf_base)->e_phoff;
    elf::Elf64_Phdr * PHT_table = (elf::Elf64_Phdr *)((intptr_t)kern_elf_base + PHT_file_offset);
    unsigned char type = 0;
    size_t d_tag, DT_init_arraysz = 0, DT_relasz = 0,  DT_pltrelsz = 0;
    unsigned int DT_symbolcnt = 0;
    intptr_t * DT_init_array = 0, DT_rela = 0 , *DT_pltgot = 0, DT_symtab = 0, *DT_hash = 0;//,* DT_jmprel, DT_strtab;

    //PHT
    for(short i = 0; i < PHT_entry_cnt; i++){
        PHT_table->p_vaddr += kaslr_vm_shift;
        type = PHT_table->p_type;
        if (type == 2){
            //DYNAMIC
            intptr_t DT_info = (intptr_t)kern_elf_base + PHT_table->p_offset;
            elf::Elf64_Dyn * DT_info_curr = (elf::Elf64_Dyn *)DT_info;
            size_t DT_entry_size = 0x10;
            while((d_tag = DT_info_curr->d_tag)){
                if (d_tag == elf::DT_INIT_ARRAY){ 
                    // DT_INIT_ARRAY
                    DT_init_array = (intptr_t *)DT_info_curr->d_un.d_ptr;
                    DT_info_curr->d_un.d_ptr += kaslr_vm_shift;
                }
                if (d_tag == elf::DT_INIT_ARRAYSZ){
                    //DT_INIT_ARRAYSZ
                    DT_init_arraysz = (size_t)DT_info_curr->d_un.d_val;
                }
                if (d_tag == elf::DT_PLTRELSZ){
                    DT_pltrelsz = (size_t)DT_info_curr->d_un.d_val;
                }
                if (d_tag == elf::DT_PLTGOT){
                    DT_pltgot = (intptr_t *)DT_info_curr->d_un.d_ptr;
                    DT_info_curr->d_un.d_ptr += kaslr_vm_shift;
                }
                if (d_tag == elf::DT_HASH){
                    DT_hash = (intptr_t *)DT_info_curr->d_un.d_ptr;
                    DT_info_curr->d_un.d_ptr += kaslr_vm_shift;
                }
                if (d_tag == elf::DT_GNU_HASH){
                    // GNU_hash
                    DT_info_curr->d_un.d_ptr += kaslr_vm_shift;
                }
                if (d_tag == elf::DT_STRTAB){
                //    DT_strtab = *(intptr_t *)(DT_info_curr + 0x8);
                    DT_info_curr->d_un.d_ptr += kaslr_vm_shift;
                }
                if (d_tag == elf::DT_SYMTAB){
                    DT_symtab = (intptr_t)DT_info_curr->d_un.d_ptr;
                    DT_info_curr->d_un.d_ptr += kaslr_vm_shift;
                }
                if (d_tag == elf::DT_RELA){
                    DT_rela = (intptr_t)DT_info_curr->d_un.d_ptr;
                    DT_info_curr->d_un.d_ptr += kaslr_vm_shift;
                }
                if (d_tag == elf::DT_RELASZ){
                    DT_relasz = (size_t)DT_info_curr->d_un.d_val;
                }
                //if (d_tag == 0x17){
                //    DT_jmprel = *(intptr_t **)(DT_info_curr + 0x8);
                //    *(intptr_t *)(DT_info_curr + 0x8) += kaslr_vm_shift;
                //}
                DT_info_curr = (elf::Elf64_Dyn *)((intptr_t)DT_info_curr + DT_entry_size);
            }

            // init array patch
            for(size_t j = 0; j < DT_init_arraysz/8; j ++)
                DT_init_array[j] += kaslr_vm_shift;

            // pltgot patch
            for(size_t j = 0; j < DT_pltrelsz/8; j ++)
                DT_pltgot[j] = DT_pltgot[j];

            // symtbl patch
            DT_symbolcnt = *(unsigned int *)(DT_hash + 0x4);
            elf::Elf64_Sym * DT_symtab_curr = (elf::Elf64_Sym *)DT_symtab;
            for(size_t j = 0; j < DT_symbolcnt; j++){
                unsigned char st_info = DT_symtab_curr->st_info;
                if (st_info & 0xf0){
                    // global or weak symbol
                    if ((st_info & 0xf) == 2 || (st_info & 0xf) == 1)
                        // object or function
                        DT_symtab_curr->st_value += kaslr_vm_shift;
                }
                DT_symtab_curr = (elf::Elf64_Sym *)((intptr_t)DT_symtab_curr + 0x18);
            }

            elf::Elf64_Rela * DT_rela_curr = (elf::Elf64_Rela *)DT_rela;
            // rela patch
            while((uintptr_t)DT_rela_curr < (DT_rela + DT_relasz)){
                DT_rela_curr->r_offset += kaslr_vm_shift;
                size_t r_info = DT_rela_curr->r_info;
                if((r_info & 0xff) == 0x25){
                    DT_rela_curr->r_addend += kaslr_vm_shift;
                }
                DT_rela_curr = (elf::Elf64_Rela *)((intptr_t)DT_rela_curr + 0x18);
            } 
        }
        PHT_table = (elf::Elf64_Phdr *)((intptr_t)PHT_table + PHT_entry_size);
    }

    // SHT
    unsigned short SHT_entry_size = ((elf::Elf64_Ehdr *)kern_elf_base)->e_shentsize;
    unsigned short SHT_entry_cnt = ((elf::Elf64_Ehdr *)kern_elf_base)->e_shnum;
    size_t SHT_file_offset = ((elf::Elf64_Ehdr *)kern_elf_base)->e_shoff;
    intptr_t SHT_table = (intptr_t)kern_elf_base + SHT_file_offset;
    unsigned short SH_str_index = ((elf::Elf64_Ehdr *)kern_elf_base)->e_shstrndx;
    if(SH_str_index > 0xff00){
        SH_str_index = ((elf::Elf64_Shdr *)SHT_table)->sh_link;
    }
    elf::Elf64_Shdr * SH_str_table_entry = (elf::Elf64_Shdr *)((SH_str_index * SHT_entry_size) + SHT_table);
    char * SH_str_table = (char *)((intptr_t)kern_elf_base + SH_str_table_entry->sh_offset);
    elf::Elf64_Shdr * SHT_curr = (elf::Elf64_Shdr *)SHT_table;

    //TODO : is max_SHT_entry cnt 0x40?
    uintptr_t addr_info[0x40][2];
    unsigned char addr_range_cnt = 0;
    
    // get address mapping info
    for(size_t i = 0 ; i < SHT_entry_cnt; i++){
        intptr_t addr = SHT_curr->sh_addr;
        if(addr){
            addr_info[addr_range_cnt][0] = SHT_curr->sh_addr;
            addr_info[addr_range_cnt][1] = SHT_curr->sh_addr + SHT_curr->sh_size;
            addr_range_cnt ++;

            SHT_curr->sh_addr += kaslr_vm_shift;
        }
        SHT_curr = (elf::Elf64_Shdr *)((intptr_t)SHT_curr + SHT_entry_size);
    }


    SHT_curr = (elf::Elf64_Shdr *)SHT_table;
    // patch based on section addr info
    for(size_t i = 0 ; i < SHT_entry_cnt; i++){
        if(!strncmp(SH_str_table + SHT_curr->sh_name, ".data.rel", 9)){
            //do data section patch
            intptr_t * section_curr = (intptr_t *)SHT_curr->sh_addr;
            size_t section_size = SHT_curr->sh_size;
            while(section_size > 0){
                uintptr_t ptr = *section_curr;
                //if(ptr < (uintptr_t)kern_elf_base) {
                //    section_size -= sizeof(intptr_t);
                //    section_curr ++;
                //    continue;
                //}

                for(unsigned char j = 0; j < addr_range_cnt; j++){
                    if(ptr >= addr_info[j][0] && ptr <= addr_info[j][1]){
                        *section_curr += kaslr_vm_shift;  
                        break;
                    }
                }

                section_size -= sizeof(intptr_t);
                section_curr ++;
            }
        }
        SHT_curr = (elf::Elf64_Shdr *)((intptr_t)SHT_curr + SHT_entry_size);
    }
}