PZ3
=======

Introduction
-------------
PZ3 is a parallel SMT solver from the paper "Parallelizing SMT Solving: Lazy Decomposition + Conciliation". It is based on Z3 project(http://z3.codeplex.com) by Microsoft Research.


Requirement
-------------
OS: Linux distributions

CPU: multi-core CPU with more than 4 cores

Memory: 8GB or higher


Buiding & Configuration
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

Note: it is not recommended to specify the number of cores larger than the number of physical cores because overall performance may degrade significantly.
