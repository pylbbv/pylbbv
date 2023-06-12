/* Definitions for bytecode */

#ifndef Py_LIMITED_API
#ifndef Py_CODE_H
#define Py_CODE_H
#ifdef __cplusplus
extern "C" {
#endif

/* Each instruction in a code object is a fixed-width value,
 * currently 2 bytes: 1-byte opcode + 1-byte oparg.  The EXTENDED_ARG
 * opcode allows for larger values but the current limit is 3 uses
 * of EXTENDED_ARG (see Python/compile.c), for a maximum
 * 32-bit value.  This aligns with the note in Python/compile.c
 * (compiler_addop_i_line) indicating that the max oparg value is
 * 2**32 - 1, rather than INT_MAX.
 */

typedef union {
    uint16_t cache;
    struct {
        uint8_t code;
        uint8_t arg;
    } op;
} _Py_CODEUNIT;


/* These macros only remain defined for compatibility. */
#define _Py_OPCODE(word) ((word).op.code)
#define _Py_OPARG(word) ((word).op.arg)

static inline _Py_CODEUNIT
_py_make_codeunit(uint8_t opcode, uint8_t oparg)
{
    // No designated initialisers because of C++ compat
    _Py_CODEUNIT word;
    word.op.code = opcode;
    word.op.arg = oparg;
    return word;
}

static inline void
_py_set_opcode(_Py_CODEUNIT *word, uint8_t opcode)
{
    word->op.code = opcode;
}

#define _Py_MAKE_CODEUNIT(opcode, oparg) _py_make_codeunit((opcode), (oparg))
#define _Py_SET_OPCODE(word, opcode) _py_set_opcode(&(word), (opcode))


typedef struct {
    PyObject *_co_code;
    PyObject *_co_varnames;
    PyObject *_co_cellvars;
    PyObject *_co_freevars;
} _PyCoCached;

// TYPENODE is a tagged pointer that uses the last 2 LSB as the tag
#define _Py_TYPENODE_t uintptr_t

// TYPENODE Tags
typedef enum _Py_TypeNodeTags {
    // Node is unused
    TYPE_NULL = 0,
    // TYPE_ROOT_POSITIVE can point to a PyTypeObject or be a NULL
    TYPE_ROOT_POSITIVE = 1,
    // TYPE_REF points to a TYPE_ROOT or a TYPE_REF
    TYPE_REF  = 2,
    // TYPE_ROOT_NEGATIVE is a bitmask of negative types
    TYPE_ROOT_NEGATIVE = 3
} _Py_TypeNodeTags;

// @TODO define bitmask
#define _Py_NEGATIVE_BITMASK_LEN 4

#define _Py_TYPENODE_GET_TAG(typenode) ((typenode) & (0b11))
#define _Py_TYPENODE_CLEAR_TAG(typenode) ((typenode) & (~(uintptr_t)(0b11)))

#define _Py_TYPENODE_MAKE_ROOT_POSITIVE(ptr) (_Py_TYPENODE_CLEAR_TAG(ptr) | TYPE_ROOT_POSITIVE)
#define _Py_TYPENODE_MAKE_ROOT_NEGATIVE(ptr) (_Py_TYPENODE_CLEAR_TAG(ptr) | TYPE_ROOT_NEGATIVE)
#define _Py_TYPENODE_MAKE_REF(ptr) (_Py_TYPENODE_CLEAR_TAG(ptr) | TYPE_REF)

#define _Py_TYPENODE_POSITIVE_NULLROOT _Py_TYPENODE_MAKE_ROOT_POSITIVE(0)
#define _Py_TYPENODE_NEGATIVE_NULLROOT _Py_TYPENODE_MAKE_ROOT_NEGATIVE((1 << _Py_NEGATIVE_BITMASK_LEN) - 1) << 2)

#define _Py_TYPENODE_IS_ROOT(typenode) ((typenode) & 1)
#define _Py_TYPENODE_IS_REF(typenode) (_Py_TYPENODE_GET_TAG(typenode) == TYPE_REF)
#define _Py_TYPENODE_IS_POSITIVE_NULL(typenode) (typenode == _Py_TYPENODE_POSITIVE_NULLROOT)
#define _Py_TYPENODE_IS_NEGATIVE_NULL(typenode) (typenode == _Py_TYPENODE_NEGATIVE_NULLROOT)

// Tier 2 types meta interpreter
typedef struct _PyTier2TypeContext {
    // points into type_stack, points to one element after the stack
    _Py_TYPENODE_t *type_stack_ptr;
    int type_locals_len;
    int type_stack_len;
    _Py_TYPENODE_t *type_stack;
    _Py_TYPENODE_t *type_locals;
} _PyTier2TypeContext;

// Tier 2 interpreter information
typedef struct _PyTier2BBMetadata {
    // Index into _PyTier2Info->bb_data
    int id;
    _PyTier2TypeContext *type_context;
    _Py_CODEUNIT *tier2_start;
    // Note, this is the first tier 1 instruction to execute AFTER the BB ends.
    _Py_CODEUNIT *tier1_end;
} _PyTier2BBMetadata;

// Bump allocator for basic blocks (overallocated)
typedef struct _PyTier2BBSpace  {
    // (in bytes)
    Py_ssize_t max_capacity;
    // How much space has been consumed in bbs. (in bytes)
    Py_ssize_t water_level;
    // There's extra memory at the end of this.
    _Py_CODEUNIT u_code[1];
} _PyTier2BBSpace;

typedef struct _PyTier2BBStartTypeContextTriplet {
    int id;
    _Py_CODEUNIT *tier1_start;
    // This is a strong reference. So during cleanup we need to free this.
    _PyTier2TypeContext *start_type_context;
} _PyTier2BBStartTypeContextTriplet;

// Tier 2 info stored in the code object. Lazily allocated.
typedef struct _PyTier2Info {
    /* the tier 2 basic block to execute (if any) */
    _PyTier2BBMetadata *_entry_bb;
    _PyTier2BBSpace *_bb_space;
    // Keeps track of offset of jump targets (in number of codeunits)
    // from co_code_adaptive.
    int backward_jump_count;
    int *backward_jump_offsets;
    // Each backward jump offset will have a corresponding array of _PyTier2BBMetadata *
    // This allows us to find a suitable BB on a backward jump.
    // So backward jump offset [1, 2, 3 ,4]
    // will have [[BB_ID1, BB_ID2], [BB_ID3,], [], []]
    // etc.
    _PyTier2BBStartTypeContextTriplet **backward_jump_target_bb_pairs;
    // Max len of bb_data
    int bb_data_len;
    // Current index to write into in bb_data. Incremented after each write.
    // This also assigns the BB ID.
    int bb_data_curr;
    _PyTier2BBMetadata **bb_data;

    // @TODO:
    //   Potentially optimise _PyTier2TypeContext by allocating the stacksize
    //   to the size needed for the snapshot, and the type propagation is performed
    //   on type_metainterpreter_stack_scratch which is allocated only once per
    //   code object.
    // PyTypeObject** type_metainterpreter_stack_scratch;
} _PyTier2Info;

// To avoid repeating ourselves in deepfreeze.py, all PyCodeObject members are
// defined in this macro:
#define _PyCode_DEF(SIZE) {                                                    \
    PyObject_VAR_HEAD                                                          \
                                                                               \
    /* Note only the following fields are used in hash and/or comparisons      \
     *                                                                         \
     * - co_name                                                               \
     * - co_argcount                                                           \
     * - co_posonlyargcount                                                    \
     * - co_kwonlyargcount                                                     \
     * - co_nlocals                                                            \
     * - co_stacksize                                                          \
     * - co_flags                                                              \
     * - co_firstlineno                                                        \
     * - co_consts                                                             \
     * - co_names                                                              \
     * - co_localsplusnames                                                    \
     * This is done to preserve the name and line number for tracebacks        \
     * and debuggers; otherwise, constant de-duplication would collapse        \
     * identical functions/lambdas defined on different lines.                 \
     */                                                                        \
                                                                               \
    /* These fields are set with provided values on new code objects. */       \
                                                                               \
    /* The hottest fields (in the eval loop) are grouped here at the top. */   \
    PyObject *co_consts;           /* list (constants used) */                 \
    PyObject *co_names;            /* list of strings (names used) */          \
    PyObject *co_exceptiontable;   /* Byte string encoding exception handling  \
                                      table */                                 \
    int co_flags;                  /* CO_..., see below */                     \
    short _co_linearray_entry_size;  /* Size of each entry in _co_linearray */ \
                                                                               \
    /* The rest are not so impactful on performance. */                        \
    int co_argcount;              /* #arguments, except *args */               \
    int co_posonlyargcount;       /* #positional only arguments */             \
    int co_kwonlyargcount;        /* #keyword only arguments */                \
    int co_stacksize;             /* #entries needed for evaluation stack */   \
    int co_firstlineno;           /* first source line number */               \
                                                                               \
    /* redundant values (derived from co_localsplusnames and                   \
       co_localspluskinds) */                                                  \
    int co_nlocalsplus;           /* number of local + cell + free variables */ \
    int co_framesize;             /* Size of frame in words */                 \
    int co_nlocals;               /* number of local variables */              \
    int co_ncellvars;             /* total number of cell variables */         \
    int co_nfreevars;             /* number of free variables */               \
    uint32_t co_version;          /* version number */                         \
                                                                               \
    PyObject *co_localsplusnames; /* tuple mapping offsets to names */         \
    PyObject *co_localspluskinds; /* Bytes mapping to local kinds (one byte    \
                                     per variable) */                          \
    PyObject *co_filename;        /* unicode (where it was loaded from) */     \
    PyObject *co_name;            /* unicode (name, for reference) */          \
    PyObject *co_qualname;        /* unicode (qualname, for reference) */      \
    PyObject *co_linetable;       /* bytes object that holds location info */  \
    PyObject *co_weakreflist;     /* to support weakrefs to code objects */    \
    _PyCoCached *_co_cached;      /* cached co_* attributes */                 \
    int _co_firsttraceable;       /* index of first traceable instruction */   \
    char *_co_linearray;          /* array of line offsets */                  \
    int _tier2_warmup;            /* warmup counter for tier 2 */              \
    _PyTier2Info *_tier2_info;        /* info required for tier 2, lazily alloc */ \
    /* Scratch space for extra data relating to the code object.               \
       Type is a void* to keep the format private in codeobject.c to force     \
       people to go through the proper APIs. */                                \
    void *co_extra;                                                            \
    char co_code_adaptive[(SIZE)];                                             \
}

/* Bytecode object */
struct PyCodeObject _PyCode_DEF(1);

/* Masks for co_flags above */
#define CO_OPTIMIZED    0x0001
#define CO_NEWLOCALS    0x0002
#define CO_VARARGS      0x0004
#define CO_VARKEYWORDS  0x0008
#define CO_NESTED       0x0010
#define CO_GENERATOR    0x0020

/* The CO_COROUTINE flag is set for coroutine functions (defined with
   ``async def`` keywords) */
#define CO_COROUTINE            0x0080
#define CO_ITERABLE_COROUTINE   0x0100
#define CO_ASYNC_GENERATOR      0x0200

/* bpo-39562: These constant values are changed in Python 3.9
   to prevent collision with compiler flags. CO_FUTURE_ and PyCF_
   constants must be kept unique. PyCF_ constants can use bits from
   0x0100 to 0x10000. CO_FUTURE_ constants use bits starting at 0x20000. */
#define CO_FUTURE_DIVISION      0x20000
#define CO_FUTURE_ABSOLUTE_IMPORT 0x40000 /* do absolute imports by default */
#define CO_FUTURE_WITH_STATEMENT  0x80000
#define CO_FUTURE_PRINT_FUNCTION  0x100000
#define CO_FUTURE_UNICODE_LITERALS 0x200000

#define CO_FUTURE_BARRY_AS_BDFL  0x400000
#define CO_FUTURE_GENERATOR_STOP  0x800000
#define CO_FUTURE_ANNOTATIONS    0x1000000

/* This should be defined if a future statement modifies the syntax.
   For example, when a keyword is added.
*/
#define PY_PARSER_REQUIRES_FUTURE_KEYWORD

#define CO_MAXBLOCKS 20 /* Max static block nesting within a function */

PyAPI_DATA(PyTypeObject) PyCode_Type;

#define PyCode_Check(op) Py_IS_TYPE((op), &PyCode_Type)

static inline Py_ssize_t PyCode_GetNumFree(PyCodeObject *op) {
    assert(PyCode_Check(op));
    return op->co_nfreevars;
}

static inline int PyCode_GetFirstFree(PyCodeObject *op) {
    assert(PyCode_Check(op));
    return op->co_nlocalsplus - op->co_nfreevars;
}

#define _PyCode_CODE(CO) _Py_RVALUE((_Py_CODEUNIT *)(CO)->co_code_adaptive)
#define _PyCode_NBYTES(CO) (Py_SIZE(CO) * (Py_ssize_t)sizeof(_Py_CODEUNIT))

/* Unstable public interface */
PyAPI_FUNC(PyCodeObject *) PyUnstable_Code_New(
        int, int, int, int, int, PyObject *, PyObject *,
        PyObject *, PyObject *, PyObject *, PyObject *,
        PyObject *, PyObject *, PyObject *, int, PyObject *,
        PyObject *);

PyAPI_FUNC(PyCodeObject *) PyUnstable_Code_NewWithPosOnlyArgs(
        int, int, int, int, int, int, PyObject *, PyObject *,
        PyObject *, PyObject *, PyObject *, PyObject *,
        PyObject *, PyObject *, PyObject *, int, PyObject *,
        PyObject *);
        /* same as struct above */
// Old names -- remove when this API changes:
_Py_DEPRECATED_EXTERNALLY(3.12) static inline PyCodeObject *
PyCode_New(
        int a, int b, int c, int d, int e, PyObject *f, PyObject *g,
        PyObject *h, PyObject *i, PyObject *j, PyObject *k,
        PyObject *l, PyObject *m, PyObject *n, int o, PyObject *p,
        PyObject *q)
{
    return PyUnstable_Code_New(
        a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q);
}
_Py_DEPRECATED_EXTERNALLY(3.12) static inline PyCodeObject *
PyCode_NewWithPosOnlyArgs(
        int a, int poac, int b, int c, int d, int e, PyObject *f, PyObject *g,
        PyObject *h, PyObject *i, PyObject *j, PyObject *k,
        PyObject *l, PyObject *m, PyObject *n, int o, PyObject *p,
        PyObject *q)
{
    return PyUnstable_Code_NewWithPosOnlyArgs(
        a, poac, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q);
}

/* Creates a new empty code object with the specified source location. */
PyAPI_FUNC(PyCodeObject *)
PyCode_NewEmpty(const char *filename, const char *funcname, int firstlineno);

/* Return the line number associated with the specified bytecode index
   in this code object.  If you just need the line number of a frame,
   use PyFrame_GetLineNumber() instead. */
PyAPI_FUNC(int) PyCode_Addr2Line(PyCodeObject *, int);

PyAPI_FUNC(int) PyCode_Addr2Location(PyCodeObject *, int, int *, int *, int *, int *);

#define PY_FOREACH_CODE_EVENT(V) \
    V(CREATE)                 \
    V(DESTROY)

typedef enum {
    #define PY_DEF_EVENT(op) PY_CODE_EVENT_##op,
    PY_FOREACH_CODE_EVENT(PY_DEF_EVENT)
    #undef PY_DEF_EVENT
} PyCodeEvent;


/*
 * A callback that is invoked for different events in a code object's lifecycle.
 *
 * The callback is invoked with a borrowed reference to co, after it is
 * created and before it is destroyed.
 *
 * If the callback sets an exception, it must return -1. Otherwise
 * it should return 0.
 */
typedef int (*PyCode_WatchCallback)(
  PyCodeEvent event,
  PyCodeObject* co);

/*
 * Register a per-interpreter callback that will be invoked for code object
 * lifecycle events.
 *
 * Returns a handle that may be passed to PyCode_ClearWatcher on success,
 * or -1 and sets an error if no more handles are available.
 */
PyAPI_FUNC(int) PyCode_AddWatcher(PyCode_WatchCallback callback);

/*
 * Clear the watcher associated with the watcher_id handle.
 *
 * Returns 0 on success or -1 if no watcher exists for the provided id.
 */
PyAPI_FUNC(int) PyCode_ClearWatcher(int watcher_id);

/* for internal use only */
struct _opaque {
    int computed_line;
    const uint8_t *lo_next;
    const uint8_t *limit;
};

typedef struct _line_offsets {
    int ar_start;
    int ar_end;
    int ar_line;
    struct _opaque opaque;
} PyCodeAddressRange;

/* Update *bounds to describe the first and one-past-the-last instructions in the
   same line as lasti.  Return the number of that line.
*/
PyAPI_FUNC(int) _PyCode_CheckLineNumber(int lasti, PyCodeAddressRange *bounds);

/* Create a comparable key used to compare constants taking in account the
 * object type. It is used to make sure types are not coerced (e.g., float and
 * complex) _and_ to distinguish 0.0 from -0.0 e.g. on IEEE platforms
 *
 * Return (type(obj), obj, ...): a tuple with variable size (at least 2 items)
 * depending on the type and the value. The type is the first item to not
 * compare bytes and str which can raise a BytesWarning exception. */
PyAPI_FUNC(PyObject*) _PyCode_ConstantKey(PyObject *obj);

PyAPI_FUNC(PyObject*) PyCode_Optimize(PyObject *code, PyObject* consts,
                                      PyObject *names, PyObject *lnotab);

PyAPI_FUNC(int) PyUnstable_Code_GetExtra(
    PyObject *code, Py_ssize_t index, void **extra);
PyAPI_FUNC(int) PyUnstable_Code_SetExtra(
    PyObject *code, Py_ssize_t index, void *extra);
// Old names -- remove when this API changes:
_Py_DEPRECATED_EXTERNALLY(3.12) static inline int
_PyCode_GetExtra(PyObject *code, Py_ssize_t index, void **extra)
{
    return PyUnstable_Code_GetExtra(code, index, extra);
}
_Py_DEPRECATED_EXTERNALLY(3.12) static inline int
_PyCode_SetExtra(PyObject *code, Py_ssize_t index, void *extra)
{
    return PyUnstable_Code_SetExtra(code, index, extra);
}

/* Equivalent to getattr(code, 'co_code') in Python.
   Returns a strong reference to a bytes object. */
PyAPI_FUNC(PyObject *) PyCode_GetCode(PyCodeObject *code);
/* Equivalent to getattr(code, 'co_varnames') in Python. */
PyAPI_FUNC(PyObject *) PyCode_GetVarnames(PyCodeObject *code);
/* Equivalent to getattr(code, 'co_cellvars') in Python. */
PyAPI_FUNC(PyObject *) PyCode_GetCellvars(PyCodeObject *code);
/* Equivalent to getattr(code, 'co_freevars') in Python. */
PyAPI_FUNC(PyObject *) PyCode_GetFreevars(PyCodeObject *code);

typedef enum _PyCodeLocationInfoKind {
    /* short forms are 0 to 9 */
    PY_CODE_LOCATION_INFO_SHORT0 = 0,
    /* one lineforms are 10 to 12 */
    PY_CODE_LOCATION_INFO_ONE_LINE0 = 10,
    PY_CODE_LOCATION_INFO_ONE_LINE1 = 11,
    PY_CODE_LOCATION_INFO_ONE_LINE2 = 12,

    PY_CODE_LOCATION_INFO_NO_COLUMNS = 13,
    PY_CODE_LOCATION_INFO_LONG = 14,
    PY_CODE_LOCATION_INFO_NONE = 15
} _PyCodeLocationInfoKind;

#ifdef __cplusplus
}
#endif
#endif  // !Py_CODE_H
#endif  // !Py_LIMITED_API
