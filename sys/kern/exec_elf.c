/*	$OpenBSD: exec_elf.c,v 1.11 1996/06/15 07:30:05 etheisen Exp $	*/
/*	$NetBSD: exec_elf.c,v 1.6 1996/02/09 18:59:18 christos Exp $	*/

/*
 * Copyright (c) 1994 Christos Zoulas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/exec.h>
#include <sys/exec_elf.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>

#if defined(COMPAT_LINUX) || defined(COMPAT_SVR4)	/*XXX should be */
#undef EXEC_ELF						/*XXX defined in */
#define EXEC_ELF					/*XXX machine/exec.h */
#endif							/*XXX instead ? */

#if defined(NATIVE_EXEC_ELF) || defined(EXEC_ELF)

#include <sys/mman.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_map.h>

#include <machine/cpu.h>
#include <machine/reg.h>
#include <machine/exec.h>

#ifdef COMPAT_LINUX
#include <compat/linux/linux_exec.h>
#endif

#ifdef COMPAT_SVR4
#include <compat/svr4/svr4_exec.h>
#endif

int (*elf_probe_funcs[]) __P((struct proc *, struct exec_package *,
			      char *, u_long *)) = {
#ifdef COMPAT_SVR4
	svr4_elf_probe,
#endif
#ifdef COMPAT_LINUX
	linux_elf_probe
#endif
};

int elf_check_header __P((Elf32_Ehdr *, int));
int elf_load_file __P((struct proc *, char *, struct exec_vmcmd_set *,
		       u_long *, struct elf_args *, u_long *));

static int elf_read_from __P((struct proc *, struct vnode *, u_long,
	caddr_t, int));
static void elf_load_psection __P((struct exec_vmcmd_set *,
	struct vnode *, Elf32_Phdr *, u_long *, u_long *, int *));

#define ELF_ALIGN(a, b) ((a) & ~((b) - 1))

/*
 * This is the basic elf emul. elf_probe_funcs may change to other emuls.
 */

extern char sigcode[], esigcode[];
#ifdef SYSCALL_DEBUG
extern char *syscallnames[];
#endif

struct emul emul_elf = {
	"native",
	NULL,
	sendsig,
	SYS_syscall,
	SYS_MAXSYSCALL,
	sysent,
#ifdef SYSCALL_DEBUG
	syscallnames,
#else
	NULL,
#endif
	sizeof(AuxInfo) * ELF_AUX_ENTRIES,
	elf_copyargs,
	setregs,
	sigcode,
	esigcode,
};


/*
 * Copy arguments onto the stack in the normal way, but add some
 * extra information in case of dynamic binding.
 */
void *
elf_copyargs(pack, arginfo, stack, argp)
	struct exec_package *pack;
	struct ps_strings *arginfo;
	void *stack;
	void *argp;
{
	size_t len;
	AuxInfo ai[ELF_AUX_ENTRIES], *a;
	struct elf_args *ap;

	stack = copyargs(pack, arginfo, stack, argp);
	if (!stack)
		return NULL;

	/*
	 * Push extra arguments on the stack needed by dynamically
	 * linked binaries
	 */
	if ((ap = (struct elf_args *) pack->ep_emul_arg)) {
		a = ai;

		a->au_id = AUX_phdr;
		a->au_v = ap->arg_phaddr;
		a++;

		a->au_id = AUX_phent;
		a->au_v = ap->arg_phentsize;
		a++;

		a->au_id = AUX_phnum;
		a->au_v = ap->arg_phnum;
		a++;

		a->au_id = AUX_pagesz;
		a->au_v = NBPG;
		a++;

		a->au_id = AUX_base;
		a->au_v = ap->arg_interp;
		a++;

		a->au_id = AUX_flags;
		a->au_v = 0;
		a++;

		a->au_id = AUX_entry;
		a->au_v = ap->arg_entry;
		a++;

		a->au_id = AUX_null;
		a->au_v = 0;
		a++;

		free((char *) ap, M_TEMP);
		len = ELF_AUX_ENTRIES * sizeof (AuxInfo);
		if (copyout(ai, stack, len))
			return NULL;
		stack += len;
	}
	return stack;
}

/*
 * elf_check_header():
 *
 * Check header for validity; return 0 for ok, ENOEXEC if error
 */
int
elf_check_header(ehdr, type)
	Elf32_Ehdr *ehdr;
	int type;
{
        /*
	 * We need to check magic, class size, endianess,
	 * and version before we look at the rest of the
	 * Elf32_Ehdr structure.  These few elements are
	 * represented in a machine independant fashion.
	 */
	if (!IS_ELF(*ehdr) ||
	    ehdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    ehdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    ehdr->e_ident[EI_VERSION] != ELF_TARG_VER)
                return ENOEXEC;
        
        /* Now check the machine dependant header */
	if (ehdr->e_machine != ELF_TARG_MACH ||
	    ehdr->e_version != ELF_TARG_VER)
                return ENOEXEC;

        /* Check the type */
	if (ehdr->e_type != type)
		return ENOEXEC;

	return 0;
}

/*
 * elf_load_psection():
 * 
 * Load a psection at the appropriate address
 */
static void
elf_load_psection(vcset, vp, ph, addr, size, prot)
	struct exec_vmcmd_set *vcset;
	struct vnode *vp;
	Elf32_Phdr *ph;
	u_long *addr;
	u_long *size;
	int *prot;
{
	u_long uaddr, msize, psize, rm, rf;
	long diff, offset;

	/*
	 * If the user specified an address, then we load there.
	 */
	if (*addr != ELF32_NO_ADDR) {
		if (ph->p_align > 1) {
			*addr = ELF_ALIGN(*addr + ph->p_align, ph->p_align);
			uaddr = ELF_ALIGN(ph->p_vaddr, ph->p_align);
		} else
			uaddr = ph->p_vaddr;
		diff = ph->p_vaddr - uaddr;
	} else {
		*addr = uaddr = ph->p_vaddr;
		if (ph->p_align > 1)
			*addr = ELF_ALIGN(uaddr, ph->p_align);
		diff = uaddr - *addr;
	}

	*prot |= (ph->p_flags & PF_R) ? VM_PROT_READ : 0;
	*prot |= (ph->p_flags & PF_W) ? VM_PROT_WRITE : 0;
	*prot |= (ph->p_flags & PF_X) ? VM_PROT_EXECUTE : 0;

	offset = ph->p_offset - diff;
	*size = ph->p_filesz + diff;
	msize = ph->p_memsz + diff;
	psize = round_page(*size);

	/*
	 * Because the pagedvn pager can't handle zero fill of the last
	 * data page if it's not page aligned we map the las page readvn.
	 */
	if(ph->p_flags & PF_W) {
		psize = trunc_page(*size);
		NEW_VMCMD(vcset, vmcmd_map_pagedvn, psize, *addr, vp, offset, *prot);
		if(psize != *size) {
			NEW_VMCMD(vcset, vmcmd_map_readvn, *size - psize, *addr
+ psize, vp, offset + psize, *prot);
		}
	}
	else {
		 NEW_VMCMD(vcset, vmcmd_map_pagedvn, psize, *addr, vp, offset, *prot);
	}

	/*
	 * Check if we need to extend the size of the segment
	 */
	rm = round_page(*addr + msize);
	rf = round_page(*addr + *size);

	if (rm != rf) {
		NEW_VMCMD(vcset, vmcmd_map_zero, rm - rf, rf, NULLVP, 0, *prot);
		*size = msize;
	}
}

/*
 * elf_read_from():
 *
 *	Read from vnode into buffer at offset.
 */
static int
elf_read_from(p, vp, off, buf, size)
	struct vnode *vp;
	u_long off;
	struct proc *p;
	caddr_t buf;
	int size;
{
	int error;
	int resid;

	if ((error = vn_rdwr(UIO_READ, vp, buf, size,
			     off, UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred,
			     &resid, p)) != 0)
		return error;
	/*
	 * See if we got all of it
	 */
	if (resid != 0)
		return ENOEXEC;
	return 0;
}

/*
 * elf_load_file():
 *
 * Load a file (interpreter/library) pointed to by path
 * [stolen from coff_load_shlib()]. Made slightly generic
 * so it might be used externally.
 */
int
elf_load_file(p, path, vcset, entry, ap, last)
	struct proc *p;
	char *path;
	struct exec_vmcmd_set *vcset;
	u_long *entry;
	struct elf_args	*ap;
	u_long *last;
{
	int error, i;
	struct nameidata nd;
	Elf32_Ehdr eh;
	Elf32_Phdr *ph = NULL;
	u_long phsize;
	char *bp = NULL;
	u_long addr = *last;

	bp  = path;
	/*
	 * 1. open file
	 * 2. read filehdr
	 * 3. map text, data, and bss out of it using VM_*
	 */
	NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, path, p);
	if ((error = namei(&nd)) != 0) {
		return error;
	}
	if ((error = elf_read_from(p, nd.ni_vp, 0, (caddr_t) &eh,
				    sizeof(eh))) != 0)
		goto bad;

	if ((error = elf_check_header(&eh, ET_DYN)) != 0)
		goto bad;

	phsize = eh.e_phnum * sizeof(Elf32_Phdr);
	ph = (Elf32_Phdr *) malloc(phsize, M_TEMP, M_WAITOK);

	if ((error = elf_read_from(p, nd.ni_vp, eh.e_phoff,
				    (caddr_t) ph, phsize)) != 0)
		goto bad;

	/*
	 * Load all the necessary sections
	 */
	for (i = 0; i < eh.e_phnum; i++) {
		u_long size = 0;
		int prot = 0;

		switch (ph[i].p_type) {
		case PT_LOAD:
			elf_load_psection(vcset, nd.ni_vp, &ph[i], &addr,
						&size, &prot);
			/* If entry is within this section it must be text */
			if (eh.e_entry >= ph[i].p_vaddr &&
			    eh.e_entry < (ph[i].p_vaddr + size)) {
				*entry = addr + eh.e_entry - ph[i].p_vaddr;
				ap->arg_interp = addr;
			}
			addr += size;
			break;

		case PT_DYNAMIC:
		case PT_PHDR:
		case PT_NOTE:
			break;

		default:
			break;
		}
	}

bad:
	if (ph != NULL)
		free((char *) ph, M_TEMP);

	*last = addr;
	vrele(nd.ni_vp);
	return error;
}

/*
 * exec_elf_makecmds(): Prepare an Elf binary's exec package
 *
 * First, set of the various offsets/lengths in the exec package.
 *
 * Then, mark the text image busy (so it can be demand paged) or error
 * out if this is not possible.  Finally, set up vmcmds for the
 * text, data, bss, and stack segments.
 *
 */
int
exec_elf_makecmds(p, epp)
	struct proc *p;
	struct exec_package *epp;
{
	Elf32_Ehdr *eh = epp->ep_hdr;
	Elf32_Phdr *ph, *pp;
	Elf32_Addr phdr = 0;
	int error, i, n, nload;
	char interp[MAXPATHLEN];
	u_long pos = 0, phsize;

	if (epp->ep_hdrvalid < sizeof(Elf32_Ehdr))
		return ENOEXEC;

	if (elf_check_header(eh, ET_EXEC))
		return ENOEXEC;

	/*
	 * check if vnode is in open for writing, because we want to
	 * demand-page out of it.  if it is, don't do it, for various
	 * reasons
	 */
	if (epp->ep_vp->v_writecount != 0) {
#ifdef DIAGNOSTIC
		if (epp->ep_vp->v_flag & VTEXT)
			panic("exec: a VTEXT vnode has writecount != 0\n");
#endif
		return ETXTBSY;
	}
	/*
	 * Allocate space to hold all the program headers, and read them
	 * from the file
	 */
	phsize = eh->e_phnum * sizeof(Elf32_Phdr);
	ph = (Elf32_Phdr *) malloc(phsize, M_TEMP, M_WAITOK);

	if ((error = elf_read_from(p, epp->ep_vp, eh->e_phoff,
				    (caddr_t) ph, phsize)) != 0)
		goto bad;

	epp->ep_tsize = ELF32_NO_ADDR;
	epp->ep_dsize = ELF32_NO_ADDR;

	interp[0] = '\0';

	for (i = 0; i < eh->e_phnum; i++) {
		pp = &ph[i];
		if (pp->p_type == PT_INTERP) {
			if (pp->p_filesz >= sizeof(interp))
				goto bad;
			if ((error = elf_read_from(p, epp->ep_vp, pp->p_offset,
			    (caddr_t) interp, pp->p_filesz)) != 0)
				goto bad;
			break;
		}
	}

	/*
	 * OK, we want a slightly different twist of the
	 * standard emulation package for "real" elf.
	 */
	epp->ep_emul = &emul_elf;
	pos = ELF32_NO_ADDR;

	/*
	 * On the same architecture, we may be emulating different systems.
	 * See which one will accept this executable. This currently only
	 * applies to Linux and SVR4 on the i386.
	 *
	 * Probe functions would normally see if the interpreter (if any)
	 * exists. Emulation packages may possibly replace the interpreter in
	 * interp[] with a changed path (/emul/xxx/<path>), and also
	 * set the ep_emul field in the exec package structure.
	 */
	if ((n = sizeof elf_probe_funcs / sizeof elf_probe_funcs[0])) {
		error = ENOEXEC;
		for (i = 0; i < n && error; i++)
			error = elf_probe_funcs[i](p, epp, interp, &pos);

		if (error)
			goto bad;
	}

	/*
	 * Load all the necessary sections
	 */
	for (i = nload = 0; i < eh->e_phnum; i++) {
		u_long  addr = ELF32_NO_ADDR, size = 0;
		int prot = 0;

		pp = &ph[i];

		switch (ph[i].p_type) {
		case PT_LOAD:
			/*
			 * XXX
			 * Can handle only 2 sections: text and data
			 */
			if (nload++ == 2)
				goto bad;
			elf_load_psection(&epp->ep_vmcmds, epp->ep_vp,
				&ph[i], &addr, &size, &prot);
			/*
			 * Decide whether it's text or data by looking
			 * at the entry point.
			 */
			if (eh->e_entry >= addr && eh->e_entry < (addr + size)){
				epp->ep_taddr = addr;
				epp->ep_tsize = size;
			} else {
				epp->ep_daddr = addr;
				epp->ep_dsize = size;
			}
			break;

		case PT_SHLIB:
			error = ENOEXEC;
			goto bad;

		case PT_INTERP:
			/* Already did this one */
		case PT_DYNAMIC:
		case PT_NOTE:
			break;

		case PT_PHDR:
			/* Note address of program headers (in text segment) */
			phdr = pp->p_vaddr;
		break;

		default:
			/*
			 * Not fatal, we don't need to understand everything
			 * :-)
			 */
			break;
		}
	}

#if !defined(mips)
	/*
	 * If no position to load the interpreter was set by a probe
	 * function, pick the same address that a non-fixed mmap(0, ..)
	 * would (i.e. something safely out of the way).
	 */
	if (pos == ELF32_NO_ADDR)
		pos = round_page(epp->ep_daddr + MAXDSIZ);
#endif

	/*
	 * Check if we found a dynamically linked binary and arrange to load
	 * it's interpreter
	 */
	if (interp[0]) {
		struct elf_args *ap;

		ap = (struct elf_args *) malloc(sizeof(struct elf_args),
						 M_TEMP, M_WAITOK);
		if ((error = elf_load_file(p, interp, &epp->ep_vmcmds,
				&epp->ep_entry, ap, &pos)) != 0) {
			free((char *) ap, M_TEMP);
			goto bad;
		}
		pos += phsize;
		ap->arg_phaddr = phdr;

		ap->arg_phentsize = eh->e_phentsize;
		ap->arg_phnum = eh->e_phnum;
		ap->arg_entry = eh->e_entry;

		epp->ep_emul_arg = ap;
	} else
		epp->ep_entry = eh->e_entry;

#ifdef ELF_MAP_PAGE_ZERO
	/* Dell SVR4 maps page zero, yeuch! */
	NEW_VMCMD(&epp->ep_vmcmds, vmcmd_map_readvn, NBPG, 0, epp->ep_vp, 0,
	    VM_PROT_READ);
#endif

	free((char *) ph, M_TEMP);
	epp->ep_vp->v_flag |= VTEXT;
	return exec_aout_setup_stack(p, epp);

bad:
	free((char *) ph, M_TEMP);
	kill_vmcmds(&epp->ep_vmcmds);
	return ENOEXEC;
}
#endif /* NATIVE_EXEC_ELF || EXEC_ELF */
