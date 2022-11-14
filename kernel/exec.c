#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *pgdir, uint64 addr, struct inode *ip, uint offset, uint sz);


static void
vmprint_handler(pagetable_t pgtbl, int level)
{
  if (level > 3) {
    return;
  } 
  for(int i = 0; i < 512; i++){
    pte_t pte = pgtbl[i];
    if((pte & PTE_V) /*&& (pte & (PTE_R|PTE_W|PTE_X)) == 0*/){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      for (int k = 0; k < level; ++k) {
        printf("||");
        if (k != level - 1) {
          printf(" ");
        }
      }
      printf("%d: pte %p pa %p\n", i, pte, child);
      vmprint_handler((pagetable_t)child, level + 1);
    // } else if(pte & PTE_V){
    //   panic("freewalk: leaf");
    }
  }
}

void 
vmprint(pagetable_t pgtbl) 
{
  printf("page table %p\n", pgtbl);
  vmprint_handler(pgtbl, 1);
}



int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG+1], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // // unmap all old user pages in kernel page table
  // uint64 npage = PGROUNDUP(p->sz) / PGSIZE;
  // uvmunmap(p->kernel_pagetable, 0, npage, 0);

  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
    // if((sz1 = uvmalloc_new(pagetable, sz, ph.vaddr + ph.memsz, myproc()->kernel_pagetable)) == 0)
      goto bad;
    sz = sz1;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;


  // Allocate two pages at the next page boundary.
  // Use the second as the user stack.
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
  // if((sz1 = uvmalloc_new(pagetable, sz, sz + 2*PGSIZE, myproc()->kernel_pagetable)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz-2*PGSIZE);
  sp = sz;
  stackbase = sp - PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    // printf("exec.c1: argc %d, sp %x, stackbase %x\n", argc, sp, stackbase);
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // unmap all old user pages in kernel page table
  uint64 npage = PGROUNDUP(oldsz) / PGSIZE;
  uvmunmap(p->kernel_pagetable, 0, npage, 0);
  // map new user pages in kernel page table
  if (map_uvm_in_kpgtbl(pagetable, 0, sz, myproc()->kernel_pagetable) == 0) {
    panic("map user pages fail");
  }

  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  if (p->pid == 1) {
    vmprint(pagetable);
  }

  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  if(pagetable) {
    proc_freepagetable(pagetable, sz);
  }
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  if((va % PGSIZE) != 0)
    panic("loadseg: va must be page aligned");

  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
