#include "Python.h"
#include "pycore_abstract.h"
#include "pycore_ceval.h"
#include "pycore_jit.h"
#include "pycore_opcode.h"

#include "ceval_macros.h"
#include "jit_stencils.h"

// XXX: Pre-populate symbol addresses once.

#ifdef MS_WINDOWS
    #include <psapi.h>
    #include <windows.h>
    FARPROC
    dlsym(LPCSTR symbol)
    {
        DWORD cbNeeded;
        HMODULE hMods[1024];
        HANDLE hProcess = GetCurrentProcess();
        if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
            for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
                FARPROC value = GetProcAddress(hMods[i], symbol);
                if (value) {
                    return value;
                }
            }
        }
        return NULL;
    }
    #define DLSYM(SYMBOL) \
        dlsym((SYMBOL))
    #define MAP_FAILED NULL
    #define MMAP(SIZE) \
        VirtualAlloc(NULL, (SIZE), MEM_COMMIT, PAGE_EXECUTE_READWRITE)
    #define MUNMAP(MEMORY, SIZE) \
        VirtualFree((MEMORY), 0, MEM_RELEASE)
#else
    #include <sys/mman.h>
    #define DLSYM(SYMBOL) \
        dlsym(RTLD_DEFAULT, (SYMBOL))
    #define MMAP(SIZE)                                         \
        mmap(NULL, (SIZE), PROT_READ | PROT_WRITE | PROT_EXEC, \
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    #define MUNMAP(MEMORY, SIZE) \
        munmap((MEMORY), (SIZE))
#endif

static int stencils_loaded = 0;

static int
preload_stencil(const Stencil *loading)
{
    for (size_t i = 0; i < loading->nloads; i++) {
        const SymbolLoad *load = &loading->loads[i];
        uintptr_t *addr = (uintptr_t *)(loading->bytes + load->offset);
        uintptr_t value = (uintptr_t)DLSYM(load->symbol);
        if (value == 0) {
            printf("XXX: Failed to preload symbol %s!\n", load->symbol);
            return -1;
        }
        *addr = value + load->addend + load->pc * (uintptr_t)addr;
    }
    return 0;
}

static unsigned char *
alloc(size_t nbytes)
{
    nbytes += sizeof(size_t);
    unsigned char *memory = MMAP(nbytes);
    if (memory == MAP_FAILED) {
        return NULL;
    }
    assert(memory);
    *(size_t *)memory = nbytes;
    return memory + sizeof(size_t);
}


void
_PyJIT_Free(_PyJITFunction trace)
{
    unsigned char *memory = (unsigned char *)trace - sizeof(size_t);
    size_t nbytes = *(size_t *)memory;
    MUNMAP(memory, nbytes);
}


static void
copy_and_patch(unsigned char *memory, const Stencil *stencil, uintptr_t patches[])
{
    memcpy(memory, stencil->bytes, stencil->nbytes);
    for (size_t i = 0; i < stencil->nholes; i++) {
        const Hole *hole = &stencil->holes[i];
        uintptr_t *addr = (uintptr_t *)(memory + hole->offset);
        // XXX: This can't handle 32-bit relocations...
        // XXX: Use += to allow multiple relocations for one offset.
        // XXX: Get rid of pc, and replace it with HOLE_base + addend.
        *addr = patches[hole->kind] + hole->addend + hole->pc * (uintptr_t)addr;
    }
}

// The world's smallest compiler?
// Make sure to call _PyJIT_Free on the memory when you're done with it!
_PyJITFunction
_PyJIT_CompileTrace(int size, _Py_CODEUNIT **trace, int *jump_target_trace_offsets, int n_jump_targets)
{
    assert(size > 0);
    assert(n_jump_targets > 0);
    if (!stencils_loaded) {
        stencils_loaded = 1;
        for (size_t i = 0; i < Py_ARRAY_LENGTH(stencils); i++) {
            if (preload_stencil(&stencils[i])) {
                stencils_loaded = -1;
                break;
            }
        }
    }
    if (stencils_loaded < 0) {
        printf("XXX: JIT disabled!\n");
        return NULL;
    }
    // First, loop over everything once to find the total compiled size:
    // size_t nbytes = trampoline_stencil.nbytes;
    size_t nbytes = 0;
    for (int i = 0; i < size; i++) {
        _Py_CODEUNIT *instruction = trace[i];
        const Stencil *stencil = &stencils[instruction->op.code];
        if (stencil->nbytes == 0) {
            return NULL;
        }
        nbytes += stencil->nbytes;
    };
    unsigned char *memory = alloc(nbytes);
    if (memory == NULL) {
        return NULL;
    }
    unsigned char *head = memory;
    uintptr_t patches[] = GET_PATCHES();
    uintptr_t patches_entrypoint[] = GET_PATCHES();
    //// First, the trampoline:
    //const Stencil *stencil = &trampoline_stencil;
    //patches[HOLE_base] = (uintptr_t)head;
    //patches[HOLE_continue] = (uintptr_t)head + stencil->nbytes;
    //copy_and_patch(head, stencil, patches);
    //head += stencil->nbytes;
    // Then, all of the stencils:
    int seen_jump_targets = 0;
    // Allocate all the entry point (trampoline) stencils,
    unsigned char *entry_points = alloc(trampoline_stencil.nbytes * n_jump_targets);
    unsigned char *first_entry_point = entry_points;
    for (int i = 0; i < size; i++) {
        // For each jump target, create an entry trampoline.
        if (i == jump_target_trace_offsets[seen_jump_targets]) {
            seen_jump_targets++;
            const Stencil *trampoline = &trampoline_stencil;
            patches_entrypoint[HOLE_base] = (uintptr_t)entry_points;
            patches_entrypoint[HOLE_continue] = (uintptr_t)head;
            copy_and_patch(entry_points, trampoline, patches_entrypoint);
            entry_points += trampoline->nbytes;
        }
        _Py_CODEUNIT *instruction = trace[i];
        const Stencil *stencil = &stencils[instruction->op.code];
        patches[HOLE_base] = (uintptr_t)head;
        patches[HOLE_continue] = (i != size - 1)
                               ? (uintptr_t)head + stencil->nbytes
                               : (uintptr_t)memory + trampoline_stencil.nbytes;
        patches[HOLE_next_instr] = (uintptr_t)instruction;
        patches[HOLE_oparg_plus_one] = instruction->op.arg + 1;
        copy_and_patch(head, stencil, patches);
        head += stencil->nbytes;
    };
    // Wow, done already?
    assert(memory + nbytes == head);
    assert(seen_jump_targets == n_jump_targets);
    assert(first_entry_point + trampoline_stencil.nbytes * n_jump_targets == entry_points);
#ifdef Py_DEBUG
    _PyJITFunction temp = (_PyJITFunction)first_entry_point;
    assert(temp);
#endif
    return first_entry_point;
}
