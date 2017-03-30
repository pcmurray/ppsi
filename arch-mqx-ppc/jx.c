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



struct foo_ext_hooks
  __attribute__((packed))
	{
	int x;
	int y;
    char a;
	int z;
	};


struct foo_ext_hooks
  WEAK
  __attribute__((packed))
  __attribute__((used))
  //__attribute__((deprecated))     // works just fine
  foo_hooks =
	{
	1,
	2,
    'b',
    3
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

