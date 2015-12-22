Download
--------
__Download PZ3 (with benchmark files)__: http://git.io/jYKU

Introduction
-------------
PZ3 is a parallel SMT solver from the paper "Parallelizing SMT Solving: Lazy Decomposition + Conciliation". It is based on Z3 project(http://z3.codeplex.com) by Microsoft Research. Although PZ3 only supports QF_UF theory for now, we plan to extend PZ3 to support other popular theories such as QF_DL, QF_LRA.

Requirements
-------------
OS: Linux distributions

CPU: multi-core CPU with more than 4 cores

Memory: 8GB or higher


Building & Configuration
---------------------------
**1.** Download source code of Z3 from http://z3.codeplex.com/. You can download Z3 4.3.2 from master branch or 4.3.3 from unstable branch. Please ensure that you are not using an older Z3 because old version has relatively poor support on interpolation.

**2.** Build Z3 from source and install it using the following commands:

    python scripts/mk_make.py
    cd build
    make
    sudo make install

**3.** Build PZ3 from source using the following commands:

    make


Usage
------
The usage of PZ3 is

    pz3 [Path of SMTLIB2 file] [Number of cores]

For example, if you want to solve `test.smt2` with 4 cores, you can execute

    pz3 test.smt2 4


Note 
-----
1. it is not recommended to specify `Number of cores` larger than the number of physical cores because overall performance may degrade significantly.
2. when `Number of cores` is specified with 1, we are using sequential Z3 to solve the benchmark file.
3. PZ3 calls `Z3_interpolate_proof` but in some versions of Z3 this API is not available. If you have problem building or running the program, you should add the codes below into `src/api/z3_interp.h`.

```c
void Z3_API Z3_interpolate_proof(Z3_context ctx,
                            Z3_ast proof,
                            int num,
                            Z3_ast *cnsts,
                            unsigned *parents,
                            Z3_params options,
                            Z3_ast *interps,
                            int num_theory,
                            Z3_ast *theory);
```
