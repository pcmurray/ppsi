/* jx.c - initial linking test for ppsi port to mqx/ppc
 */
#include "mqx_ppc_port.h"
#include "ppsi/assert.h"
#include "ppsi/ppsi.h"
// included by ppsi.h:
//  "arch/arch.h"
//  "ppsi/constants.h"
//      "arch/constants.h
//  "ppsi/diag-macros.h"
//  "ppsi/ieee1588_types.h"
//      "ppsi/pp-instance.h"
//  "ppsi/jiffies.h"
//  "ppsi/lib.h"



// PACKED struct
//#pragma pack (push)           // works
//#pragma pack (1)              // works
//#pragma pack (pop)            // works

//#pragma options align=packed  // works
//...                           // works
//#pragma options align=reset   // works

//__attribute__((packed))       // doesn't work; just ignored


PACK_START
struct foo_ext_hooks
	{
    char a;
	int x;
    char b;
	int y;
	int z;
	};
PACK_END


struct foo_ext_hooks
  WEAK                          // works
  //__attribute__((used))
  //__attribute__((deprecated)) // works
  foo_hooks
    =
	{
    .y = 2,
    .a = 'A',
    .z = 3,
    .x = 1,
	};

int moo (void) __attribute__((warn_unused_result)); // attrib ignored


//int inline moo (void)                             // works
int inline moo (void) __attribute((noinline))     // works
//int inline moo (void) __attribute((never_inline)) // works
    {
    return 42;
    }

//int sys_moo (void) __attribute__((alias("moo")));   // unsupported


//int jx (char *p, char *q)
int main_task (char *p, char *q)
	{
    volatile int i;

	printf ("foo_hooks x=%d, y=%d, z=%d\n",
		foo_hooks.x, foo_hooks.y, foo_hooks.z);
    
    assert((1 != 0), "%s\n", "Hi There, Folks");

    panic ("1 + 1 %d %d\n", 42);

    i = moo();
    //sys_moo();    // alias unsupported

	return (strcmp (p, q));
	}

