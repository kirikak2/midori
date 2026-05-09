/*
 * PicoRuby UI - VM selection wrapper
 */

#include "../include/ui.h"

#if defined(PICORB_VM_MRUBY)
  #include "mruby/ui.c"
#elif defined(PICORB_VM_MRUBYC)
  #include "mrubyc/ui.c"
#endif
