/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <fcntl.h>
#include <sys/stat.h>
#include <osv/file.h>
#include <osv/poll.h>
#include <fs/vfs/vfs.h>
#include <osv/anon_file.hh>
#include <osv/mmu.hh>
#include <osv/pagecache.hh>
#include <osv/debug.hh>
#include <osv/prio.hh>
#include <cstring>

static void* zero_page;

void  __attribute__((constructor(init_prio::anon_fops))) setup()
{
	zero_page = memory::alloc_page();
    memset(zero_page, 0, mmu::page_size);
}

anon_file::anon_file()
	: file(fflags(O_RDWR), DTYPE_ANONFD), size(0)
{
}

int anon_file::close()
{
	auto fp = this;
	SCOPE_LOCK(fp->f_lock);

	for (auto &it: fp->o2p)
		if (it.second != nullptr)
			memory::free_page(it.second);

	return 0;
}

int anon_file::read(struct uio *uio, int flags)
{
	auto fp = this;
	SCOPE_LOCK(fp->f_lock);

	if ((flags & FOF_OFFSET) == 0)
		uio->uio_offset = fp->f_offset;

	size_t bytes = 0, cursor = uio->uio_offset;

	while (uio->uio_resid > 0 && cursor < fp->size)
	{
        struct iovec *iov = uio->uio_iov;

        size_t copy_len = std::min(iov->iov_len, fp->size - cursor), copied = 0;
		while (copied < copy_len)
		{
			size_t st = cursor;
			size_t en = std::min(round_page(cursor + 1), cursor + copy_len);
			size_t ofs_aligned = cursor & ~PAGE_MASK;
			void *page = fp->o2p[ofs_aligned];

			if (!page)
				page = zero_page;
			
			memcpy((char*)iov->iov_base + copied, (char*)page + (st - ofs_aligned), en - st);
			copied += en - st;
		}

        uio->uio_iov++;
        uio->uio_iovcnt--;
        uio->uio_resid -= copy_len;
        uio->uio_offset += copy_len;
		cursor = uio->uio_offset;
		bytes += copy_len;
    }

	if ((flags & FOF_OFFSET) == 0)
		fp->f_offset += bytes;

	return 0;
}

int anon_file::write(struct uio *uio, int flags)
{
	auto fp = this;
	SCOPE_LOCK(fp->f_lock);

	// "POSIX requires that opening a file with the O_APPEND flag should have no effect on the location at which pwrite() writes data."
	// pwrite, append: from uio_offset
	// pwrite, no append: from uio_offset
	// not pwrite, append: from size
	// not pwrite, not append: from f_offset

	if ((flags & FOF_OFFSET) == 0)
	{
		if ((fp->f_flags & O_APPEND) == 0)
			uio->uio_offset = fp->f_offset;
		else
			uio->uio_offset = fp->size;
	}

	struct iovec *iov = uio->uio_iov;
	size_t cursor = uio->uio_offset;

	while (uio->uio_resid > 0)
	{
		size_t copy_len = iov->iov_len, copied = 0;
		while (copied < copy_len)
		{
			size_t st = cursor;
			size_t en = std::min(round_page(cursor + 1), cursor + copy_len);
			size_t ofs_aligned = cursor & ~PAGE_MASK;
			void*& page = fp->o2p[ofs_aligned];

			if (!page)
			{
				page = memory::alloc_page();
				if (!page)
					throw make_error(ENOMEM);
				memset(page, 0, mmu::page_size);
				fp->size = std::max(fp->size, ofs_aligned + mmu::page_size);
			}
			
			memcpy((char*)page + (st - ofs_aligned), (char*)iov->iov_base + copied, en - st);
			copied += en - st;
		}

		uio->uio_iov++;
		uio->uio_iovcnt--;
		uio->uio_resid -= copy_len;
		uio->uio_offset += copy_len;
		cursor = uio->uio_offset;
	}

	if ((flags & FOF_OFFSET) == 0)
		fp->f_offset = uio->uio_offset;

	return 0;
}

int anon_file::truncate(off_t len)
{
	auto fp = this;
	SCOPE_LOCK(fp->f_lock);

	// anon fd implemented as sparse file, no need to change anything
	fp->size = round_page(len);

	return 0;
}

int anon_file::ioctl(u_long com, void *data)
{
	return EBADF;
}

int anon_file::poll(int events)
{
	return EBADF;
}

int anon_file::stat(struct stat* buf)
{
	auto fp = this;

	// we race anyways, just elide scope lock
	//SCOPE_LOCK(fp->f_lock);

	// How should other fields be set?
	buf->st_size = fp->size;

	return 0;
}

int anon_file::chmod(mode_t mode)
{
	return EBADF;
}

bool anon_file::map_page(uintptr_t offset, mmu::hw_ptep<0> ptep, mmu::pt_element<0> pte, bool write, bool shared)
{
	auto fp = this;
    SCOPE_LOCK(fp->f_lock);

	void** _p_backed_page;
	DROP_LOCK (mmu::vma_list_mutex.for_read()) {
		// This pre-allocates the element with key offset.
		// This may trigger a reallocation, which would block if we have read-lock acquired.
		// Drop lock just for this operation.
		_p_backed_page = &fp->o2p[offset];
	}
	void*& backed_page = *_p_backed_page;

    if (write)  // write
	{
		if (!backed_page)  // backed page nonexistent, create new page
		{
			void *page = memory::alloc_page();
			if (!page)
				throw make_error(ENOMEM);
			memset(page, 0, mmu::page_size);
			if (shared)  // shared, save it as backed page
				backed_page = page;
			return mmu::write_pte(page, ptep, pte);
		}
        else if (!shared)  // CoW: backed page exist & private
		{
            void* page = memory::alloc_page();
			if (!page)
				throw make_error(ENOMEM);
            memcpy(page, backed_page, mmu::page_size);
            return mmu::write_pte(page, ptep, pte);
        }
		// backed page exist & shared, just add backed_page to pt
    }
	else if (!backed_page)  // read, backed page nonexistent - add zero_page to pte & mark CoW
        return mmu::write_pte(zero_page, ptep, mmu::pte_mark_cow(pte, true));
	// else, read & backed page existent. just add backed page to pt

    return mmu::write_pte(backed_page, ptep, mmu::pte_mark_cow(pte, !shared));
}

bool anon_file::put_page(void *addr, uintptr_t offset, mmu::hw_ptep<0> ptep)
{
	auto fp = this;
    SCOPE_LOCK(fp->f_lock);

	void*& backed_page = fp->o2p[offset];

	auto old = clear_pte(ptep);
	void *old_addr = mmu::phys_to_virt(old.addr());

	// remove if private mapping
	return old_addr != backed_page && old_addr != zero_page;
}

std::unique_ptr<mmu::file_vma> anon_file::mmap(addr_range range, unsigned flags, unsigned perm, off_t offset)
{
	auto fp = this;
	SCOPE_LOCK(fp->f_lock);

	uintptr_t len = range.end() - range.start();
	
	if (len == 0 || len + offset < len || len + offset > fp->size)  // zero length, offset < 0 or over anon fd size
		throw make_error(EINVAL);

	return mmu::map_file_mmap(fp, range, flags, perm, offset);
}
