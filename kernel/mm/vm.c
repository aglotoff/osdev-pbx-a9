#include <assert.h>
#include <errno.h>
#include <string.h>

#include <armv7.h>
#include <drivers/console.h>
#include <fs/fs.h>
#include <types.h>
#include <mm/kobject.h>
#include <mm/page.h>

#include <mm/vm.h>

static l2_desc_t *vm_walk_trtab(l1_desc_t *, uintptr_t, int);
static void   vm_static_map(l1_desc_t *, uintptr_t, uint32_t, size_t, int);

static struct KObjectPool *vm_pool;
// static struct KObjectPool *vm_area_pool;

/*
 * ----------------------------------------------------------------------------
 * Translation Table Initializaion
 * ----------------------------------------------------------------------------
 */

// Master kernel translation table.
static l1_desc_t *kern_trtab;

void
vm_init(void)
{
  extern uint8_t _start[];

  struct Page *page;

  // Allocate the master translation table
  if ((page = page_alloc_block(2, PAGE_ALLOC_ZERO)) == NULL)
    panic("out of memory");

  kern_trtab = (l1_desc_t *) page2kva(page);

  // Map all physical memory at KERNEL_BASE
  // Permissions: kernel RW, user NONE
  vm_static_map(kern_trtab, KERNEL_BASE, 0, PHYS_TOP, VM_READ | VM_WRITE);

  // Map all devices 
  vm_static_map(kern_trtab, KERNEL_BASE + PHYS_TOP, PHYS_TOP,
              VECTORS_BASE - KERNEL_BASE - PHYS_TOP,
              VM_READ | VM_WRITE | VM_NOCACHE);

  // Map exception vectors at VECTORS_BASE
  // Permissions: kernel R, user NONE
  vm_static_map(kern_trtab, VECTORS_BASE, (uint32_t) _start, PAGE_SIZE, VM_READ);

  vm_init_percpu();

  vm_pool = kobject_pool_create("vm_pool", sizeof(struct VM), 0);
}

void
vm_init_percpu(void)
{
  // Switch from the minimal entry translation table to the full translation
  // table.
  cp15_ttbr0_set(PADDR(kern_trtab));
  cp15_ttbr1_set(PADDR(kern_trtab));

  // Size of the TTBR0 translation table is 8KB.
  cp15_ttbcr_set(1);

  cp15_tlbiall();
}

/*
 * ----------------------------------------------------------------------------
 * Low-Level Operations
 * ----------------------------------------------------------------------------
 */

// VM proptection flags to ARM MMU AP bits
static int prot_to_ap[] = {
  [VM_READ]                      = AP_PRIV_RO, 
  [VM_WRITE]                     = AP_PRIV_RW, 
  [VM_READ | VM_WRITE]           = AP_PRIV_RW, 
  [VM_USER | VM_READ]            = AP_USER_RO, 
  [VM_USER | VM_WRITE]           = AP_BOTH_RW, 
  [VM_USER | VM_READ | VM_WRITE] = AP_BOTH_RW, 
};

static inline void
vm_L2_DESC_set_flags(l2_desc_t *pte, int flags)
{
  *(pte + (L2_NR_ENTRIES * 2)) = flags;
}

static inline void
vm_L2_DESC_set(l2_desc_t *pte, physaddr_t pa, int prot)
{
  int flags;

  flags = L2_DESC_AP(prot_to_ap[prot & 7]);
  if ((prot & VM_USER) && !(prot & VM_EXEC))
    flags |= L2_DESC_SM_XN;
  if (!(prot & VM_NOCACHE))
    flags |= (L2_DESC_B | L2_DESC_C);

  *pte = pa | flags | L2_DESC_TYPE_SM;
  vm_L2_DESC_set_flags(pte, prot);
}

static inline void
vm_L2_DESC_clear(l2_desc_t *pte)
{
  *pte = 0;
  vm_L2_DESC_set_flags(pte, 0);
}

static inline void
vm_L1_DESC_set_pgtab(l1_desc_t *tte, physaddr_t pa)
{
  *tte = pa | L1_DESC_TYPE_TABLE;
}

static inline void
vm_L1_DESC_set_sect(l1_desc_t *tte, physaddr_t pa, int prot)
{
  int flags;

  flags = L1_DESC_SECT_AP(prot_to_ap[prot & 7]);
  if ((prot & VM_USER) && !(prot & VM_EXEC))
    flags |= L1_DESC_SECT_XN;
  if (!(prot & VM_NOCACHE))
    flags |= (L1_DESC_SECT_B | L1_DESC_SECT_C);

  *tte = pa | flags | L1_DESC_TYPE_SECT;
}

//
// Returns the pointer to the page table entry in the translation table trtab
// that corresponds to the virtual address va. If alloc is not zero, allocate
// any required page tables.
//
static l2_desc_t *
vm_walk_trtab(l1_desc_t *trtab, uintptr_t va, int alloc)
{
  l1_desc_t *tte;
  l2_desc_t *pgtab;

  tte = &trtab[L1_IDX(va)];
  if ((*tte & L1_DESC_TYPE_MASK) == L1_DESC_TYPE_FAULT) {
    struct Page *page;

    if (!alloc || (page = page_alloc_one(PAGE_ALLOC_ZERO)) == NULL)
      return NULL;
    
    page->ref_count++;

    // We could fit four page tables into one physical page. But because we
    // also need to associate some metadata with each entry, we store only two
    // page tables in the bottom of the allocated physical page.
    vm_L1_DESC_set_pgtab(&trtab[(L1_IDX(va) & ~1) + 0], page2pa(page));
    vm_L1_DESC_set_pgtab(&trtab[(L1_IDX(va) & ~1) + 1], page2pa(page) + L2_TABLE_SIZE);
  } else if ((*tte & L1_DESC_TYPE_MASK) != L1_DESC_TYPE_TABLE) {
    panic("not a page table");
  }

  pgtab = KADDR(L1_DESC_TABLE_BASE(*tte));
  return &pgtab[L2_IDX(va)];
}

// 
// Map [va, va + n) of virtual address space to physical [pa, pa + n) in
// the translation page trtab.
//
// This function is only intended to set up static mappings in the kernel's
// portion of address space.
//
static void
vm_static_map(l1_desc_t *trtab, uintptr_t va, uint32_t pa, size_t n, int prot)
{ 
  assert(va % PAGE_SIZE == 0);
  assert(pa % PAGE_SIZE == 0);
  assert(n  % PAGE_SIZE == 0);

  while (n != 0) {
    // Whenever possible, map entire 1MB sections to reduce the memory
    // management overhead.
    if ((va % L1_SECTION_SIZE == 0) &&
        (pa % L1_SECTION_SIZE == 0) &&
        (n  % L1_SECTION_SIZE == 0)) {
      l1_desc_t *tte;

      tte = &trtab[L1_IDX(va)];
      if (*tte)
        panic("remap");

      vm_L1_DESC_set_sect(tte, pa, prot);

      va += L1_SECTION_SIZE;
      pa += L1_SECTION_SIZE;
      n  -= L1_SECTION_SIZE;
    } else {
      l2_desc_t *pte;

      if ((pte = vm_walk_trtab(trtab, va, 1)) == NULL)
        panic("out of memory");
      if (*pte)
        panic("remap %p", *pte);

      vm_L2_DESC_set(pte, pa, prot);

      va += PAGE_SIZE;
      pa += PAGE_SIZE;
      n  -= PAGE_SIZE;
    }
  }
}

/*
 * ----------------------------------------------------------------------------
 * Translation Table Switch
 * ----------------------------------------------------------------------------
 */

/**
 * Load the master kernel translation table.
 */
void
vm_switch_kernel(void)
{
  cp15_ttbr0_set(PADDR(kern_trtab));
  cp15_tlbiall();
}

/**
 * Load the user process translation table.
 * 
 * @param trtab Pointer to the translation table to be loaded.
 */
void
vm_switch_user(struct VM *vm)
{
  cp15_ttbr0_set(PADDR(vm->trtab));
  cp15_tlbiall();
}

/*
 * ----------------------------------------------------------------------------
 * Page Frame Mapping Operations
 * ----------------------------------------------------------------------------
 */

struct Page *
vm_lookup_page(l1_desc_t *trtab, const void *va, l2_desc_t **L2_DESC_store)
{
  l2_desc_t *pte;

  if ((uintptr_t) va >= KERNEL_BASE)
    panic("bad va: %p", va);

  pte = vm_walk_trtab(trtab, (uintptr_t ) va, 0);

  if (L2_DESC_store)
    *L2_DESC_store = pte;

  if ((pte == NULL) || ((*pte & L2_DESC_TYPE_SM) != L2_DESC_TYPE_SM))
    return NULL;

  return pa2page(L2_DESC_SM_BASE(*pte));
}

int
vm_insert_page(l1_desc_t *trtab, struct Page *page, void *va, unsigned perm)
{
  l2_desc_t *pte;

  if ((uintptr_t) va >= KERNEL_BASE)
    panic("bad va: %p", va);

  if ((pte = vm_walk_trtab(trtab, (uintptr_t ) va, 1)) == NULL)
    return -ENOMEM;

  // Incrementing the reference count before calling vm_remove_page() allows
  // us to elegantly handle the situation when the same page is re-inserted at
  // the same virtual address, but with different permissions.
  page->ref_count++;

  // If present, remove the previous mapping.
  vm_remove_page(trtab, va);

  vm_L2_DESC_set(pte, page2pa(page), perm);

  return 0;
}

void
vm_remove_page(l1_desc_t *trtab, void *va)
{
  struct Page *page;
  l2_desc_t *pte;

  if ((uintptr_t) va >= KERNEL_BASE)
    panic("bad va: %p", va);

  if ((page = vm_lookup_page(trtab, va, &pte)) == NULL)
    return;

  if (--page->ref_count == 0)
    page_free_one(page);

  vm_L2_DESC_clear(pte);

  cp15_tlbimva((uintptr_t) va);
}

/*
 * ----------------------------------------------------------------------------
 * Managing Regions of Memory
 * ----------------------------------------------------------------------------
 */

int
vm_user_alloc(struct VM *vm, void *va, size_t n, int prot)
{
  struct Page *page;
  uint8_t *a, *start, *end;
  int r;

  start = ROUND_DOWN((uint8_t *) va, PAGE_SIZE);
  end   = ROUND_UP((uint8_t *) va + n, PAGE_SIZE);

  if ((start > end) || (end > (uint8_t *) KERNEL_BASE))
    panic("invalid range [%p,%p)", start, end);

  for (a = start; a < end; a += PAGE_SIZE) {
    if ((page = page_alloc_one(PAGE_ALLOC_ZERO)) == NULL) {
      vm_user_dealloc(vm, start, a - start);
      return -ENOMEM;
    }

    if ((r = (vm_insert_page(vm->trtab, page, a, prot)) != 0)) {
      page_free_one(page);
      vm_user_dealloc(vm, start, a - start);
      return r;
    }
  }

  return 0;
}

void
vm_user_dealloc(struct VM *vm, void *va, size_t n)
{
  uint8_t *a, *end;
  struct Page *page;
  l2_desc_t *pte;

  a   = ROUND_DOWN((uint8_t *) va, PAGE_SIZE);
  end = ROUND_UP((uint8_t *) va + n, PAGE_SIZE);

  if ((a > end) || (end > (uint8_t *) KERNEL_BASE))
    panic("invalid range [%p,%p)", a, end);

  while (a < end) {
    page = vm_lookup_page(vm->trtab, a, &pte);

    if (pte == NULL) {
      // Skip the rest of the page table 
      a = ROUND_DOWN(a + PAGE_SIZE * L2_NR_ENTRIES, PAGE_SIZE * L2_NR_ENTRIES);
      continue;
    }

    if (page != NULL)
      vm_remove_page(vm->trtab, a);

    a += PAGE_SIZE;
  }
}

/*
 * ----------------------------------------------------------------------------
 * Copying Data Between Address Spaces
 * ----------------------------------------------------------------------------
 */

int
vm_user_copy_out(struct VM *vm, void *dst_va, const void *src_va, size_t n)
{
  uint8_t *src = (uint8_t *) src_va;
  uint8_t *dst = (uint8_t *) dst_va;

  while (n != 0) {
    struct Page *page;
    uint8_t *kva;
    size_t offset, ncopy;

    if ((page = vm_lookup_page(vm->trtab, dst, NULL)) == NULL)
      return -EFAULT;
    
    kva    = (uint8_t *) page2kva(page);
    offset = (uintptr_t) dst % PAGE_SIZE;
    ncopy  = MIN(PAGE_SIZE - offset, n);

    memmove(kva + offset, src, ncopy);

    src += ncopy;
    dst += ncopy;
    n   -= ncopy;
  }

  return 0;
}

int
vm_user_copy_in(struct VM *vm, void *dst_va, const void *src_va, size_t n)
{
  uint8_t *dst = (uint8_t *) dst_va;
  uint8_t *src = (uint8_t *) src_va;

  while (n != 0) {
    struct Page *page;
    uint8_t *kva;
    size_t offset, ncopy;

    if ((page = vm_lookup_page(vm->trtab, src, NULL)) == NULL)
      return -EFAULT;

    kva    = (uint8_t *) page2kva(page);
    offset = (uintptr_t) dst % PAGE_SIZE;
    ncopy  = MIN(PAGE_SIZE - offset, n);

    memmove(dst, kva + offset, ncopy);

    src += ncopy;
    dst += ncopy;
    n   -= ncopy;
  }

  return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Check User Memory Permissions
 * ----------------------------------------------------------------------------
 */

int
vm_user_check_buf(struct VM *vm, const void *va, size_t n, unsigned perm)
{
  struct Page *page, *new_page;
  const char *p, *end;
  l2_desc_t *pte;

  p   = ROUND_DOWN((const char *) va, PAGE_SIZE);
  end = ROUND_UP((const char *) va + n, PAGE_SIZE);

  if ((p >= (char *) KERNEL_BASE) || (end > (char *) KERNEL_BASE))
    return -EFAULT;

  while (p != end) {
    unsigned curr_perm;

    if ((page = vm_lookup_page(vm->trtab, (void *) p, &pte)) == NULL)
      return -EFAULT;

    curr_perm = vm_L2_DESC_get_flags(pte);

    if ((perm & VM_WRITE) && (curr_perm & VM_COW)) {
      curr_perm &= ~VM_COW;
      curr_perm |= VM_WRITE;

      if ((curr_perm & perm) != perm)
        return -EFAULT;

      if ((new_page = page_alloc_one(0)) == NULL)
        return -EFAULT;

      memcpy(page2kva(new_page), page2kva(page), PAGE_SIZE);

      if ((vm_insert_page(vm->trtab, new_page, (void *) p, curr_perm)) != 0)
        return -EFAULT;
    } else {
      if ((curr_perm & perm) != perm)
        return -EFAULT;
    }

    p += PAGE_SIZE;
  }

  return 0;
}

int
vm_user_check_str(struct VM *vm, const char *s, unsigned perm)
{
  struct Page *page;
  const char *p;
  unsigned off;
  l2_desc_t *pte;

  assert(KERNEL_BASE % PAGE_SIZE == 0);

  while (s < (char *) KERNEL_BASE) {
    page = vm_lookup_page(vm->trtab, s, &pte);
    if ((page == NULL) || ((*(pte + (L2_NR_ENTRIES * 2)) & perm) != perm))
      return -EFAULT;

    p = (const char *) page2kva(page);
    for (off = (uintptr_t) s % PAGE_SIZE; off < PAGE_SIZE; off++)
      if (p[off] == '\0')
        return 0;
    s += off;
  }

  return -EFAULT;
}

/*
 * ----------------------------------------------------------------------------
 * Loading Binaries
 * ----------------------------------------------------------------------------
 */

int
vm_user_load(struct VM *vm, void *va, struct Inode *ip, size_t n, off_t off)
{
  struct Page *page;
  uint8_t *dst, *kva;
  int ncopy, offset;
  int r;

  dst = (uint8_t *) va;

  while (n != 0) {
    page = vm_lookup_page(vm->trtab, dst, NULL);
    if (page == NULL)
      return -EFAULT;

    kva = (uint8_t *) page2kva(page);

    offset = (uintptr_t) dst % PAGE_SIZE;
    ncopy  = MIN(PAGE_SIZE - offset, n);

    if ((r = fs_inode_read(ip, kva + offset, ncopy, &off)) != ncopy)
      return r;

    dst += ncopy;
    n   -= ncopy;
  }

  return 0;
}

struct VM   *
vm_create(void)
{
  struct VM *vm;
  struct Page *trtab_page;

  if ((vm = (struct VM *) kobject_alloc(vm_pool)) == NULL)
    return NULL;

  if ((trtab_page = page_alloc_block(1, PAGE_ALLOC_ZERO)) == NULL) {
    kobject_free(vm_pool, vm);
    return NULL;
  }

  trtab_page->ref_count++;

  vm->trtab = page2kva(trtab_page);
  list_init(&vm->areas);

  return vm;
}

void
vm_destroy(struct VM *vm)
{
  struct Page *page;
  unsigned i;

  vm_user_dealloc(vm, (void *) 0, KERNEL_BASE);

  for (i = 0; i < L1_IDX(KERNEL_BASE); i += 2) {
    if (!vm->trtab[i])
      continue;

    page = pa2page(L2_DESC_SM_BASE(vm->trtab[i]));
    if (--page->ref_count == 0)
      page_free_one(page);
  }

  page = kva2page(vm->trtab);
  if (--page->ref_count == 0)
      page_free_one(page);

  kobject_free(vm_pool, vm);
}

struct VM   *
vm_clone(struct VM *vm)
{
  struct VM *new_vm;
  struct Page *src_page, *dst_page;
  uint8_t *va;
  l2_desc_t *pte;

  if ((new_vm = vm_create()) == NULL)
    return NULL;

  va = (uint8_t *) 0;

  while (va < (uint8_t *) KERNEL_BASE) {
    src_page = vm_lookup_page(vm->trtab, va, &pte);
    if (pte == NULL) {
      // Skip the rest of the page table 
      va = ROUND_DOWN(va + PAGE_SIZE * L2_NR_ENTRIES, PAGE_SIZE * L2_NR_ENTRIES);
      continue;
    }

    if (src_page != NULL) {
      unsigned perm;

      perm = vm_L2_DESC_get_flags(pte);

      if ((perm & VM_WRITE) || (perm & VM_COW)) {
        perm &= ~VM_WRITE;
        perm |= VM_COW;

        if (vm_insert_page(vm->trtab, src_page, va, perm) < 0)
          panic("Cannot change page permissions");

        if (vm_insert_page(new_vm->trtab, src_page, va, perm) < 0) {
          vm_destroy(new_vm);
          return NULL;
        }
      } else {
        if ((dst_page = page_alloc_one(0)) == NULL) {
          vm_destroy(new_vm);
          return NULL;
        }

        if (vm_insert_page(new_vm->trtab, dst_page, va, perm) < 0) {
          page_free_one(dst_page);
          vm_destroy(new_vm);
          return NULL;
        }

        memcpy(page2kva(dst_page), page2kva(src_page), PAGE_SIZE);
      }
    }

    va += PAGE_SIZE;
  }

  return new_vm;
}