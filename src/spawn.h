
#ifndef DEF_SPAWN
#define DEF_SPAWN

#include <unistd.h>
#include <stdbool.h>

/* Represent a spawned process. */
typedef struct _spawn_t {
    /* The PID of the process. */
    pid_t process;
    /* The pipe linked to the process. stdout can be read from pipe[0]. pipe[1]
     * is closed in the parent process, but written to by the spawned process.
     */
    int pipe[2];
} spawn_t;

/* Spawn a program and link its stdout to a pipe.
 * prog[0] must be the name of the program, prog[n] an argument. prog must be
 * terminated by NULL.
 */
spawn_t spawn_create(char* const prog[]);

/* Init a spawn structure so that spawn_ok(spawn_init()) returns false. */
spawn_t spawn_init();

/* Spawn a program and link its stdout to a pipe.
 * The program will be spawned using the default shell or /bin/sh if $SHELL is
 * not set.
 */
spawn_t spawn_create_shell(const char* command);

/* Spawn a process and wait for it to end. */
bool spawn_exec(char* const prog[]);
bool spawn_exec_shell(const char* command);

/* Check if the spawn could be created. */
bool spawn_ok(spawn_t sp);

/* Pause a spawn. */
void spawn_pause(spawn_t sp);

/* Resume a spawn. */
void spawn_resume(spawn_t sp);

/* Indicates if a spawn is paused. */
bool spawn_paused(spawn_t sp);

/* Close a spawned process, discarding all pending data. */
void spawn_close(spawn_t* sp);

/* Wait for a spawned process to end without closing it. */
void spawn_wait(spawn_t sp);

/* Check if a spawned process has ended. Doesn't close it. */
bool spawn_ended(spawn_t sp);

/* Read data from a spawned process. Return the number of bytes read. The read
 * is blocking.
 */
size_t spawn_read(spawn_t sp, char* buffer, size_t bufsize);

/* Check if there is data to read from a spawned process. */
bool spawn_ready(spawn_t sp);

/* Return an FD to watch the spawned process with select for read. */
int spawn_fd(spawn_t sp);

#endif

