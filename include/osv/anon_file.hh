/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ANON_FILE_HH_
#define ANON_FILE_HH_

#include <osv/file.h>
#include <unordered_map>

class anon_file final : public file {
public:
    explicit anon_file();
    virtual int read(struct uio *uio, int flags) override;
    virtual int write(struct uio *uio, int flags) override;
    virtual int truncate(off_t len) override;
    virtual int close() override;
    virtual int ioctl(u_long com, void *data) override;
    virtual int poll(int events) override;
    virtual int stat(struct stat* buf) override;
    virtual int chmod(mode_t mode) override;

    virtual bool map_page(uintptr_t offset, mmu::hw_ptep<0> ptep, mmu::pt_element<0> pte, bool write, bool shared);
    virtual bool put_page(void *addr, uintptr_t offset, mmu::hw_ptep<0> ptep);
    virtual std::unique_ptr<mmu::file_vma> mmap(addr_range range, unsigned flags, unsigned perm, off_t offset) override;

    size_t size;
    // offset: f_offset

    // sparse backing pages map, offset -> data page ptr
    std::unordered_map<uintptr_t, void*> o2p;
};

#endif /* ANON_FILE_HH_ */
