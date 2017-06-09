#include "rktio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void do_check_valid(rktio_t *rktio, int ok, int where)
{
  if (!ok) {
    printf("error at %d: %d@%d = %s\n",
           where,
           rktio_get_last_error(rktio),
           rktio_get_last_error_kind(rktio),
           rktio_get_error_string(rktio,
                                  rktio_get_last_error_kind(rktio),
                                  rktio_get_last_error(rktio)));
  }
}

static void do_check_expected_error(rktio_t *rktio, int err, int where)
{
  if (!err) {
    printf("error expected at %d\n",
           where);
  }
}

static void do_check_expected_racket_error(rktio_t *rktio, int err, int what, int where)
{
  if (!err) {
    printf("error expected at %d\n",
           where);
  } else if ((what != rktio_get_last_error(rktio))
             || (RKTIO_ERROR_KIND_RACKET != rktio_get_last_error_kind(rktio))) {
    printf("wrong error at %d: %d@%d = %s\n",
           where,
           rktio_get_last_error(rktio),
           rktio_get_last_error_kind(rktio),
           rktio_get_error_string(rktio,
                                  rktio_get_last_error_kind(rktio),
                                  rktio_get_last_error(rktio)));
  }
}

#define check_valid(e) do_check_valid(rktio, e, __LINE__)
#define check_expected_error(e) do_check_expected_error(rktio, e, __LINE__)
#define check_expected_racket_error(e, what) do_check_expected_racket_error(rktio, e, what, __LINE__)

static void check_hello_content(rktio_t *rktio, char *fn)
{
  rktio_fd_t *fd;
  intptr_t amt;
  char buffer[256], *s;
  
  fd = rktio_open(rktio, fn, RKTIO_OPEN_READ);
  check_valid(!!fd);
  check_valid(rktio_poll_read_ready(rktio, fd) != RKTIO_POLL_ERROR);
  amt = rktio_read(rktio, fd, buffer, sizeof(buffer));
  check_valid(amt == 5);
  check_valid(!strncmp(buffer, "hello", 5));
  amt = rktio_read(rktio, fd, buffer, sizeof(buffer));
  check_valid(amt == RKTIO_READ_EOF);
  check_valid(rktio_close(rktio, fd));
}

int main()
{
  rktio_t *rktio;
  rktio_size_t *sz;
  rktio_fd_t *fd, *fd2;
  intptr_t amt, i, saw_file;
  int perms;
  char buffer[256], *s, *pwd;
  rktio_directory_list_t *ls;
  rktio_file_copy_t *cp;
  rktio_timestamp_t *ts1, *ts1a;

  rktio = rktio_init();

  /* Basic file I/O */

  fd = rktio_open(rktio, "test1", RKTIO_OPEN_WRITE | RKTIO_OPEN_CAN_EXIST);
  check_valid(!!fd);
  check_valid(rktio_poll_write_ready(rktio, fd) != RKTIO_POLL_ERROR);
  amt = rktio_write(rktio, fd, "hello", 5);
  check_valid(amt == 5);
  check_valid(rktio_close(rktio, fd));

  check_valid(rktio_file_exists(rktio, "test1"));
  check_valid(!rktio_directory_exists(rktio, "test1"));
  check_valid(rktio_is_regular_file(rktio, "test1"));

  s = rktio_get_current_directory(rktio);
  check_valid(!!s);
  check_valid(rktio_directory_exists(rktio, s));
  check_valid(!rktio_file_exists(rktio, s));
  check_valid(!rktio_is_regular_file(rktio, s));
  check_expected_racket_error(!rktio_open(rktio, s, RKTIO_OPEN_WRITE | RKTIO_OPEN_CAN_EXIST),
                              RKTIO_ERROR_IS_A_DIRECTORY);
  pwd = s;

  sz = rktio_file_size(rktio, "test1");
  check_valid(!!sz);
  check_valid(sz->lo == 5);
  check_valid(sz->hi == 0);
  free(sz);

  fd = rktio_open(rktio, "test2", RKTIO_OPEN_WRITE | RKTIO_OPEN_MUST_EXIST);
  check_expected_error(!fd);

  fd = rktio_open(rktio, "test1", RKTIO_OPEN_WRITE);
  check_expected_racket_error(!fd, RKTIO_ERROR_EXISTS);

  check_hello_content(rktio, "test1");

  /* Copying, renaming, and deleting files */

  if (rktio_file_exists(rktio, "test1a"))
    check_valid(rktio_delete_file(rktio, "test1a", 1));
  if (rktio_file_exists(rktio, "test1b"))
    check_valid(rktio_delete_file(rktio, "test1b", 1));

  ts1 = rktio_get_file_modify_seconds(rktio, "test1a");
  check_expected_error(!ts1);
  perms = rktio_get_file_or_directory_permissions(rktio, "test1a", 1);
  check_expected_error(perms == -1);
  check_expected_error(!rktio_set_file_or_directory_permissions(rktio, "test1a", 511));

  ts1 = rktio_get_file_modify_seconds(rktio, "test1");
  perms = rktio_get_file_or_directory_permissions(rktio, "test1", 0);
  check_valid(perms != -1);
  check_valid(perms & (RKTIO_PERMISSION_READ << 6));
  check_valid(perms & (RKTIO_PERMISSION_WRITE << 6));
  perms = rktio_get_file_or_directory_permissions(rktio, "test1", 1);
  check_valid(perms != -1);
  check_valid(perms & (RKTIO_PERMISSION_READ << 6));
  check_valid(perms & (RKTIO_PERMISSION_WRITE << 6));
  check_valid(rktio_set_file_or_directory_permissions(rktio, "test1", perms & (0x7 << 6)));
  check_valid((perms & (0x7 << 6)) == rktio_get_file_or_directory_permissions(rktio, "test1", 1));
  rktio_set_file_or_directory_permissions(rktio, "test1", perms);

  cp = rktio_copy_file_start(rktio, "test1a", "test1", 0);
  check_valid(!!cp);
  while (!rktio_copy_file_is_done(rktio, cp)) {
    check_valid(rktio_copy_file_step(rktio, cp));
  }
  rktio_copy_file_stop(rktio, cp);
  check_hello_content(rktio, "test1a");

  ts1a = rktio_get_file_modify_seconds(rktio, "test1a");
  check_valid(*ts1a >= *ts1);

  rktio_set_file_modify_seconds(rktio, "test1a", *ts1 - 10);
  free(ts1a);
  ts1a = rktio_get_file_modify_seconds(rktio, "test1a");
  check_valid(*ts1a == (*ts1 - 10));
  
  free(ts1);
  free(ts1a);

  check_valid(rktio_file_exists(rktio, "test1a"));
  cp = rktio_copy_file_start(rktio, "test1a", "test1", 0);
  check_expected_racket_error(!cp, RKTIO_ERROR_EXISTS);

  cp = rktio_copy_file_start(rktio, "test1a", "test1", 1);
  check_valid(!!cp);
  rktio_copy_file_stop(rktio, cp);

  check_valid(rktio_rename_file(rktio, "test1b", "test1a", 0));
  check_valid(rktio_file_exists(rktio, "test1b"));
  check_expected_racket_error(!rktio_rename_file(rktio, "test1b", "test1", 0),
                              RKTIO_ERROR_EXISTS);
  check_valid(rktio_file_exists(rktio, "test1"));
  check_valid(rktio_file_exists(rktio, "test1b"));
  check_valid(!rktio_file_exists(rktio, "test1a"));
  
  check_valid(rktio_delete_file(rktio, "test1b", 0));
  check_valid(!rktio_file_exists(rktio, "test1b"));

  /* Listing directory content */

  ls = rktio_directory_list_start(rktio, pwd, 0);
  check_valid(!!ls);
  saw_file = 0;
  while (1) {
    s = rktio_directory_list_step(rktio, ls);
    check_valid(!!s);
    if (!*s) break;
    if (!strcmp(s, "test1"))
      saw_file = 1;
    check_valid(strcmp(s, "test1b"));
  }
  check_valid(saw_file);

  /* Pipes and non-blocking operations */
  
  fd = rktio_open(rktio, "demo_fifo", RKTIO_OPEN_READ);
  check_valid(!!fd);
  check_valid(!rktio_poll_read_ready(rktio, fd));
  fd2 = rktio_open(rktio, "demo_fifo", RKTIO_OPEN_WRITE | RKTIO_OPEN_CAN_EXIST);
  check_valid(!!fd2);
  check_valid(!rktio_poll_read_ready(rktio, fd));

  amt = rktio_write(rktio, fd2, "hello", 5);
  check_valid(amt == 5);
  check_valid(rktio_poll_read_ready(rktio, fd));
  amt = rktio_read(rktio, fd, buffer, sizeof(buffer));
  check_valid(amt == 5);
  check_valid(!strncmp(buffer, "hello", 5));
  check_valid(!rktio_poll_read_ready(rktio, fd));

  check_valid(rktio_close(rktio, fd2));
  amt = rktio_read(rktio, fd, buffer, sizeof(buffer));
  check_valid(amt == RKTIO_READ_EOF);
  check_valid(rktio_close(rktio, fd));

  fd2 = rktio_open(rktio, "demo_fifo", RKTIO_OPEN_WRITE | RKTIO_OPEN_CAN_EXIST);
  check_valid(!!fd2);
  /* should eventually block: */
  for (i = 0; i < 100000; i++) {
    amt = rktio_write(rktio, fd2, "hello", 5);
    check_valid(amt != RKTIO_WRITE_ERROR);
    if (!amt)
      break;
  }
  check_valid(i < 100000);
  
  fd = rktio_open(rktio, "demo_fifo", RKTIO_OPEN_READ);
  check_valid(!!fd);
  /* should eventually block: */
  for (i = 0; i < 100000; i++) {
    amt = rktio_read(rktio, fd2, buffer, sizeof(buffer));
    check_valid(amt != RKTIO_READ_ERROR);
    check_valid(amt != RKTIO_READ_EOF);
    if (!amt)
      break;
  }
  check_valid(i < 100000);
  
  check_valid(rktio_close(rktio, fd));
  check_valid(rktio_close(rktio, fd2));
  
  return 0;
}