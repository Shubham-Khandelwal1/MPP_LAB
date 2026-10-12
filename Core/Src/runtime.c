/* The Cortex-M startup owns reset, data, BSS and constructor initialization.
   These hooks satisfy newlib's init-array support with -nostartfiles, without
   linking the generic CRT startup or its unused OS/file syscall stubs. */
void _init(void) {}
void _fini(void) {}
