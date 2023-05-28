# pyLBBVAndPatch 

A Lazy Basic Block Versioning + Copy and Patch JIT Interpreter for CPython.

Python is a widely-used programming language. CPython is its reference implementation. Due to Python’s dynamic type semantics, CPython is generally unable to execute Python programs as fast as it potentially could with static type semantics.

Last semester, while taking CS4215, we made progress on a technique for removing type checks and other overheads associated with dynamic languages known as [Lazy Basic Block Versioning (LBBV)](https://arxiv.org/abs/1411.0352). This work will be referred to as PyLBBV. More details can be found in our [technical report](https://github.com/pylbbv/pylbbv/blob/pylbbv/report/CPython_Tier_2_LBBV_Report_For_Repo.pdf).

This orbital, we intend to refine PyLBBV. These include but are not limited to:
- General refactoring
- Bug fixing
- Better unboxing + support unboxing of other types
- More type specialised code

Furthermore, we intend to further PyLBBV by implementing a [Copy and Patch JIT](https://arxiv.org/abs/2011.13127) on top of the type specialisation PyLBBV provides. The culmination of these efforts will allow further optimisations to be implemented. We hope that this effort can allow Python to be as fast as a statically typed language. PyLBBV's type propagator has been [submitted](https://github.com/faster-cpython/ideas/issues/564) for consideration to the Faster CPython team at Microsoft with positive response. Our work here will be made publically available so that it will benefit CPython and its users, and we plan to collaborate closely with the developers of CPython in the course of this project.

Due to Python being a huge language, pyLBBVAndPatch intends to support and optimise a subset of Python. Specifically pyLBBVAndPatch focuses on integer and float arithmetic. We believe this scope is sufficient as an exploration of the LBBV + Copy and Patch JIT approach for CPython.

# Project Plan

- Fix bugs and refactor hot-patches in PyLBBV
- Implement interprocedural type propagation
- Implement typed object versioning
- Implement unboxing of integers, floats and other primitive types
- Implement Copy and Patch JIT (Just In Time) Compilation

## Immediate Goals

Refer to [the issues page](https://github.com/pylbbv/pylbbv/issues).

# Changelog

* Refactor: Typeprop codegen by @JuliaPoo in https://github.com/pylbbv/pylbbv/pull/1
    * Refactored type propagation codegen to more explicitly align with the formalism in our [technical report (Appendix)](https://github.com/pylbbv/pylbbv/blob/pylbbv/report/CPython_Tier_2_LBBV_Report_For_Repo.pdf) and remove duplicated logic
    * Fixed bug with possible reference cycle in the type propagation `TYPE_SET` operation
* feat: granular type checks by @Fidget-Spinner in https://github.com/pylbbv/pylbbv/pull/2
    * Split `BINARY_CHECK_X` into simply `CHECK_X`
        * @KENJIN: Write rationale and issues
* Perf: Improved typeprop by switching overwrite -> set by @JuliaPoo in https://github.com/pylbbv/pylbbv/pull/6
    * Stricter type propagation reduces type information loss

# Build instructions

You should follow the official CPython build instructions for your platform.
https://devguide.python.org/getting-started/setup-building/

We have one major difference - you must have a pre-existing Python installation.
Preferrably Python 3.9 or higher. On MacOS/Unix systems, that Python installation
*must* be located at `python3`.

The main reason for this limitation is that Python is used to bootstrap the compilation
of Python. However, since our interpreter is unable to run a large part of the Python
language, our interpreter cannot be used as a bootstrap Python.

During the build process, errors may be printed, and the build process may error. However,
the final Python executable should still be generated.

# Where are files located?

The majority of the changes and functionality are in `Python/tier2.c` where Doxygen documentation
is written alongside the code, and in `Tools/cases_generator/` which contains the DSL implementation.

# Running tests

We've written simple tests of the main functionalities.
Unfortunately we did not have time to write comprehensive tests, and it doesn't seem worth it eitherways given the experimental nature of this project.

After building, run `python tier2_test.py` in the repository's root folder.

# Debugging output

In `tier2.c`, two flags can be set to print debug messages:
```c
// Prints codegen debug messages
#define BB_DEBUG 0

// Prints typeprop debug messages
#define TYPEPROP_DEBUG 0
```