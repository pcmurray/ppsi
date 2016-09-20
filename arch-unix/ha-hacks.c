#include <stdint.h>
#include <stdlib.h>
//#include <sys/types.h>
#include <libwr/shmem.h>

struct wrs_shm_head *ppsi_head;

/* dummy function, no shmem locks (no even shmem) are implemented here */
void wrs_shm_write(void *headptr, int flags)
{
        return;
}
