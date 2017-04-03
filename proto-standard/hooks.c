#include <ppsi/ppsi.h>

/* proto-standard offers all-null hooks as a default extension */
//__attribute__((weak) ignored by MetroWerks eppc c compiler
//struct pp_ext_hooks  __attribute__((weak)) pp_hooks;  /* does not work */
struct pp_ext_hooks  __declspec(weak) pp_hooks;         /* works */
