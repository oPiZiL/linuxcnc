#include <stdio.h>
#include <signal.h>		/* signal(), SIGINT */
#include <unistd.h>		/* sleep() */
#include "rtapi.h"		/* user-level API to RT Linux */
#include "common.h"		/* shmem structure */

static int module;
static int key = SHMEM_KEY;
static int shmem_id;
static SHMEM_STRUCT *shmem_struct;

static int done = 0;
static void quit(int sig)
{
  done = 1;
}

int main()
{
  int retval;

  module = rtapi_init("SHMEM_USR");
  if (module < 1) {
    rtapi_print_msg(RTAPI_MSG_ERR,
		    "shmemusr main: rtapi_init returned %d\n", module);
    return -1;
  }


  /* allocate the shared memory structure */
  shmem_id = rtapi_shmem_new(key, module, sizeof(SHMEM_STRUCT));
  if (shmem_id < 0) {
    rtapi_print_msg(RTAPI_MSG_ERR,
		    "shmemusr main: rtapi_shmem_new returned %d\n", shmem_id);
    rtapi_exit(module);
    return -1;
  }
  retval = rtapi_shmem_getptr(shmem_id, (void **) &shmem_struct);
  if (retval != RTAPI_SUCCESS) {
    rtapi_print_msg(RTAPI_MSG_ERR,
		    "shmemusr main: rtapi_shmem_getptr returned %d\n",
		    retval);
    rtapi_exit(module);
    return -1;
  }

  signal(SIGINT, quit);
  while (!done) {
    rtapi_print("%lu\n", shmem_struct->heartbeat);
    sleep(1);
  }

  retval = rtapi_shmem_delete(shmem_id, module);
  if (retval != RTAPI_SUCCESS) {
    rtapi_print_msg(RTAPI_MSG_ERR,
		    "shmemusr main: rtapi_free_shmem returned %d\n", retval);
    rtapi_exit(module);
    return -1;
  }

  return rtapi_exit(module);
}
