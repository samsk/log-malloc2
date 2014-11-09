log-malloc2
===========

*log-malloc2* is **pre-loadable** library tracking all memory allocations of a program. It
produces simple text trace output, that makes it easy to find leaks and also identify their origin.

#Features

- logging to file descriptor 1022 (if opened)
- call stack **backtrace** (using GNU backtrace() if available)
- **requested memory tracking** (byte-exact)
- allocated memory tracking (byte-exact - using malloc_usable_size())
- process memory status tracking (from /proc/self/statm)
- call counting
- thread safe
- optional **C API** for runtime memory usage checking


#Usage

`log-malloc -o /tmp/program.log command args`

OR

`LD_PRELOAD=./liblog-malloc2.so command args ... 1022>/tmp/program.log`

#Helper scripts

- ```backtrace2line```
 - Script to automatically convert backtrace in files and line numbers.
 - Can also deal with [ASLR](https://en.wikipedia.org/wiki/Address_space_layout_randomization) randomized addresses.

- ```log-malloc-findleak```
 - Script to discover possible program memory leaks from trace file

- ```log-malloc-trackusage```
 - Script to track program memory usage over time.



# C API

- ``` size_t log_malloc_get_usage(void)```
 - Get actual program memory usage in bytes

- ```void log_malloc_trace_enable(void)```
 - Enable trace messages to be printed to trace fd.

- ```void log_malloc_trace_disable(void)```
 - Disable trace messages.

- ```int log_malloc_trace_printf(const char *fmt, ...)```
 - Printf smth. to trace fd (message size is limited to 1024 bytes).

- ```LOG_MALLOC_SAVE(name, trace)``` [MACRO]
 - Creates savepoint with given _name_ that stores actual memory usage.
 - If _trace_ is true, message will be logged to trace fd.

- ```LOG_MALLOC_UPDATE(name, trace)``` [MACRO]
 - Updates actual memory usage in savepoint with given _name_.
 - If _trace_ is true, trace message will be logged to trace fd.

- ```LOG_MALLOC_COMPARE(name, trace)``` [MACRO]
 - Compares actual memory usage with the saved one under given _name_.
 - If _trace_ is true, trace message will be logged to trace fd.
 - Call returns memory usage difference (size_t).

- ```LOG_MALLOC_ASSERT(name, iter)``` [MACRO]
 - ASSERT with fail, if actual memory usage differs from the one saved in savepoint.
 - _iter_ can specify that assertion should be checked first after given number of LOG_MALLOC_SAVE() iterations.

- ```LOG_MALLOC_NDEBUG``` [MACRO]
 - If defined, above macros will generate no code.

# Author

- ***Samuel Behan***
 - **contact**: samuel*(dot)*behan*(at)*dob*(dot)*sk
 - **homepage**: [http://dob.sk](http://dob.sk?u=github)
 - **projects**: [http://devel.dob.sk](http://devel.dob.sk?u=github)

# Licensing

* [LGPLv3](https://www.gnu.org/licenses/lgpl.html) for C code (library)
* [GPLv3](https://www.gnu.org/licenses/gpl.html) for Perl code (scripts)

# See Also

* More/Detailed info in project [README](README)
* Project home: [http://devel.dob.sk/log-malloc2](http://devel.dob.sk/log-malloc2?u=github)

#Examples

###Example Trace Output

    $ log-malloc -o - ./examples/leak-01

    *** log-malloc trace-fd = 1022 ***

    + malloc 53 0x7f9bff564080 [85:160]!
    + calloc 1182 0x7f9bff5640e0 [1267:1384] (1182 1)!
    + malloc 53 0x7f9bff5645b0 [1320:1472]!
    + malloc 56 0x7f9bff564610 [1376:1560]!
    + calloc 360 0x7f9bff564670 [1736:1952] (15 24)!
    + calloc 32 0x7f9bff564030 [32:72] (1 32) #3183 168 131 1 0 84 0
    /lib64/libdl.so.2(+0x1960)[0x7f9bfcfa8960]
    /lib64/libdl.so.2(dlsym+0x5a)[0x7f9bfcfa843a]
    /home/sam/devel/log-malloc2/scripts/../.libs/liblog-malloc2.so(+0x12e3)[0x7f9bfd7882e3]
    /lib64/ld-linux-x86-64.so.2(+0xfa0b)[0x7f9bfd99ba0b]
    /lib64/ld-linux-x86-64.so.2(+0xfb1c)[0x7f9bfd99bb1c]
    /lib64/ld-linux-x86-64.so.2(+0x140a)[0x7f9bfd98d40a]
    /lib64/libdl.so.2(+0x13a0)[0x7f9bfcfa83a0]
    # PID 23451
    # EXE /home/sam/devel/log-malloc2/examples/leak-01
    # CWD /home/sam/devel/log-malloc2
    + INIT [1736:1952] malloc=3 calloc=3 realloc=0 memalign=0/0 valloc=0 free=0
    + malloc 2000 0x7f9bff564800 [3736:3992] #3183 168 131 1 0 84 0
    ./examples/leak-01(main+0x32)[0x7f9bfdbb1af2]
    /lib64/libc.so.6(__libc_start_main+0x11b)[0x7f9bfd3f01cb]
    ./examples/leak-01(+0x9a9)[0x7f9bfdbb19a9]
    [0x0]
    + malloc 100 0x7f9bff565000 [3836:4128] #3183 168 131 1 0 84 0
    ./examples/leak-01(main+0x40)[0x7f9bfdbb1b00]
    /lib64/libc.so.6(__libc_start_main+0x11b)[0x7f9bfd3f01cb]
    ./examples/leak-01(+0x9a9)[0x7f9bfdbb19a9]
    [0x0]
    + free -100 0x7f9bff565000 [3736:3992] #3183 168 131 1 0 84 0
    + FINI [3736:3992] malloc=5 calloc=3 realloc=0 memalign=0/0 valloc=0 free=1
    # FILE /proc/self/maps
    7f9bfcd90000-7f9bfcda6000 r-xp 00000000 fe:02 911050                     /usr/lib64/gcc/x86_64-pc-linux-gnu/4.8.3/libgcc_s.so.1
    7f9bfcda6000-7f9bfcfa5000 ---p 00016000 fe:02 911050                     /usr/lib64/gcc/x86_64-pc-linux-gnu/4.8.3/libgcc_s.so.1
    7f9bfcfa5000-7f9bfcfa6000 r--p 00015000 fe:02 911050                     /usr/lib64/gcc/x86_64-pc-linux-gnu/4.8.3/libgcc_s.so.1
    7f9bfcfa6000-7f9bfcfa7000 rw-p 00016000 fe:02 911050                     /usr/lib64/gcc/x86_64-pc-linux-gnu/4.8.3/libgcc_s.so.1
    7f9bfcfa7000-7f9bfcfaa000 r-xp 00000000 fe:02 819448                     /lib64/libdl-2.19.so
    7f9bfcfaa000-7f9bfd1a9000 ---p 00003000 fe:02 819448                     /lib64/libdl-2.19.so
    7f9bfd1a9000-7f9bfd1aa000 r--p 00002000 fe:02 819448                     /lib64/libdl-2.19.so
    7f9bfd1aa000-7f9bfd1ab000 rw-p 00003000 fe:02 819448                     /lib64/libdl-2.19.so
    7f9bfd1ab000-7f9bfd1c5000 r-xp 00000000 fe:02 818913                     /lib64/libpthread-2.19.so
    7f9bfd1c5000-7f9bfd3c5000 ---p 0001a000 fe:02 818913                     /lib64/libpthread-2.19.so
    7f9bfd3c5000-7f9bfd3c6000 r--p 0001a000 fe:02 818913                     /lib64/libpthread-2.19.so
    7f9bfd3c6000-7f9bfd3c7000 rw-p 0001b000 fe:02 818913                     /lib64/libpthread-2.19.so
    7f9bfd3c7000-7f9bfd3cb000 rw-p 00000000 00:00 0
    7f9bfd3cb000-7f9bfd57d000 r-xp 00000000 fe:02 819451                     /lib64/libc-2.19.so
    7f9bfd57d000-7f9bfd77d000 ---p 001b2000 fe:02 819451                     /lib64/libc-2.19.so
    7f9bfd77d000-7f9bfd781000 r--p 001b2000 fe:02 819451                     /lib64/libc-2.19.so
    7f9bfd781000-7f9bfd783000 rw-p 001b6000 fe:02 819451                     /lib64/libc-2.19.so
    7f9bfd783000-7f9bfd787000 rw-p 00000000 00:00 0
    7f9bfd787000-7f9bfd78b000 r-xp 00000000 fe:02 14619                      /home/sam/devel/log-malloc2/.libs/liblog-malloc2.so.1.0.0
    7f9bfd78b000-7f9bfd98a000 ---p 00004000 fe:02 14619                      /home/sam/devel/log-malloc2/.libs/liblog-malloc2.so.1.0.0
    7f9bfd98a000-7f9bfd98b000 r--p 00003000 fe:02 14619                      /home/sam/devel/log-malloc2/.libs/liblog-malloc2.so.1.0.0
    7f9bfd98b000-7f9bfd98c000 rw-p 00004000 fe:02 14619                      /home/sam/devel/log-malloc2/.libs/liblog-malloc2.so.1.0.0
    7f9bfd98c000-7f9bfd9ae000 r-xp 00000000 fe:02 818908                     /lib64/ld-2.19.so
    7f9bfdb99000-7f9bfdb9d000 rw-p 00000000 00:00 0
    7f9bfdbad000-7f9bfdbae000 rw-p 00000000 00:00 0
    7f9bfdbae000-7f9bfdbaf000 r--p 00022000 fe:02 818908                     /lib64/ld-2.19.so
    7f9bfdbaf000-7f9bfdbb0000 rw-p 00023000 fe:02 818908                     /lib64/ld-2.19.so
    7f9bfdbb0000-7f9bfdbb1000 rw-p 00000000 00:00 0
    7f9bfdbb1000-7f9bfdbb2000 r-xp 00000000 fe:02 15340                      /home/sam/devel/log-malloc2/examples/leak-01
    7f9bfddb1000-7f9bfddb2000 r--p 00000000 fe:02 15340                      /home/sam/devel/log-malloc2/examples/leak-01
    7f9bfddb2000-7f9bfddb3000 rw-p 00001000 fe:02 15340                      /home/sam/devel/log-malloc2/examples/leak-01
    7f9bff564000-7f9bff585000 rw-p 00000000 00:00 0                          [heap]
    7fffb5471000-7fffb5493000 rw-p 00000000 00:00 0                          [stack]
    7fffb54cf000-7fffb54d1000 r-xp 00000000 00:00 0                          [vdso]
    ffffffffff600000-ffffffffff601000 r-xp 00000000 00:00 0                  [vsyscall]


###Example leak

    $ ./scripts/log-malloc-findleak.pl /tmp/lm2.trace
    SUSPECTED 1 LEAKS:
     0x7f4688a04000 leaked 100 bytes (0.10 KiB) allocated by malloc (line: 1)
           FUNCTION             FILE                      SYMBOL
           main                 examples/leak-02.c:10     ./examples/leak-02(main+0x40)[0x7f4687a6fb50]
                                                          /lib64/libc.so.6(__libc_start_main+0x11b)[0x7f46872ae1cb]
           _start               ??:?                      ./examples/leak-02(+0x9f9)[0x7f4687a6f9f9]
                                                          [0x0]


