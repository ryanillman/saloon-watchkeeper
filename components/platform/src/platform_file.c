#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "platform.h"

bool platform_replace_file(const char *tmp, const char *main_, const char *bak) {
  struct stat st;
  bool have_main = stat(main_, &st) == 0;
  if (have_main) {
    // FAT will not rename over an existing file, so clear the old backup first. This only happens while main exists,
    // so a backup is never removed when it is the only good copy left.
    remove(bak);
    if (rename(main_, bak) != 0) {
      fprintf(stderr, "file: rename %s -> %s: %s\n", main_, bak, strerror(errno));
      return false;   // main is untouched
    }
  }
  if (rename(tmp, main_) != 0) {
    fprintf(stderr, "file: rename %s -> %s: %s\n", tmp, main_, strerror(errno));
    if (have_main && rename(bak, main_) != 0)   // put the previous file back so a loader still finds it
      fprintf(stderr, "file: could not restore %s from %s: %s\n", main_, bak, strerror(errno));
    return false;
  }
  return true;
}
