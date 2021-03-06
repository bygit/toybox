/* dmesg.c - display/control kernel ring buffer.
 *
 * Copyright 2006, 2007 Rob Landley <rob@landley.net>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/dmesg.html

// We care that FLAG_c is 1, so keep c at the end.
USE_DMESG(NEWTOY(dmesg, "w(follow)Ctrs#<1n#c[!tr]", TOYFLAG_BIN))

config DMESG
  bool "dmesg"
  default y
  help
    usage: dmesg [-Cc] [-r|-t] [-n LEVEL] [-s SIZE] [-w]

    Print or control the kernel ring buffer.

    -C	Clear the ring buffer
    -c	Clear the ring buffer after printing
    -n	Set kernel logging LEVEL (1-9)
    -r	Raw output (with <level markers>)
    -s	Show the last SIZE many bytes
    -t	Don't print kernel's timestamps
    -w	Keep waiting for more output (aka --follow)
*/

#define FOR_dmesg
#include "toys.h"
#include <sys/klog.h>

GLOBALS(
  long level;
  long size;

  int color;
)

// Use klogctl for reading if we're on a pre-3.5 kernel.
static void legacy_mode() {
  char *data, *to, *from;
  int size;

  // Figure out how much data we need, and fetch it.
  size = TT.size;
  if (!size && 1>(size = klogctl(10, 0, 0))) perror_exit("klogctl");;
  data = to = from = xmalloc(size+1);
  size = klogctl(3 + (toys.optflags & FLAG_c), data, size);
  if (size < 0) perror_exit("klogctl");
  data[size] = 0;

  // Filter out level markers and optionally time markers
  if (!(toys.optflags & FLAG_r)) while ((from - data) < size) {
    if (from == data || from[-1] == '\n') {
      char *to;

      if (*from == '<' && (to = strchr(from, '>'))) from = ++to;
      if ((toys.optflags&FLAG_t) && *from == '[' && (to = strchr(from, ']')))
        from = to+1+(to[1]==' ');
    }
    *(to++) = *(from++);
  } else to = data+size;

  // Write result. The odds of somebody requesting a buffer of size 3 and
  // getting "<1>" are remote, but don't segfault if they do.
  if (to != data) {
    xwrite(1, data, to-data);
    if (to[-1] != '\n') xputc('\n');
  }
  if (CFG_TOYBOX_FREE) free(data);
}

static void color(int c) {
  if (TT.color) printf("\033[%dm", c);
}

void dmesg_main(void)
{
  // For -n just tell kernel to which messages to keep.
  if (toys.optflags & FLAG_n) {
    if (klogctl(8, NULL, TT.level)) perror_exit("klogctl");
    return;
  }

  // For -C just tell kernel to throw everything out.
  if (toys.optflags & FLAG_C) {
    if (klogctl(5, NULL, 0)) perror_exit("klogctl");
    return;
  }

  TT.color = isatty(1);

  // http://lxr.free-electrons.com/source/Documentation/ABI/testing/dev-kmsg

  // Each read returns one message. By default, we block when there are no
  // more messages (--follow); O_NONBLOCK is needed for for usual behavior.
  int fd = xopen("/dev/kmsg", O_RDONLY | ((toys.optflags&FLAG_w)?0:O_NONBLOCK));
  while (1) {
    char msg[8192]; // CONSOLE_EXT_LOG_MAX.
    unsigned long long time_us;
    int facpri, subsystem, pos;
    char *p, *text;
    ssize_t len;

    // kmsg fails with EPIPE if we try to read while the buffer moves under
    // us; the next read will succeed and return the next available entry.
    do {
      len = read(fd, msg, sizeof(msg));
    } while (len == -1 && errno == EPIPE);
    // All reads from kmsg fail if you're on a pre-3.5 kernel.
    if (len == -1 && errno == EINVAL) {
      close(fd);
      return legacy_mode();
    }
    if (len <= 0) break;

    msg[len] = 0;

    if (sscanf(msg, "%u,%*u,%llu,%*[^;];%n", &facpri, &time_us, &pos) != 2)
      continue;

    // Drop extras after end of message text.
    text = msg + pos;
    if ((p = strchr(text, '\n'))) *p = 0;

    // Is there a subsystem? (The ": " is just a convention.)
    p = strstr(text, ": ");
    subsystem = p ? (p - text) : 0;

    // "Raw" is a lie for /dev/kmsg. In practice, it just means we show the
    // syslog facility/priority at the start of each line.
    if (toys.optflags&FLAG_r) printf("<%d>", facpri);

    if (!(toys.optflags&FLAG_t)) {
      color(32);
      printf("[%5lld.%06lld] ", time_us/1000000, time_us%1000000);
      color(0);
    }

    // Errors (or worse) are shown in red, subsystems are shown in yellow.
    if (subsystem) {
      color(33);
      printf("%.*s", subsystem, text);
      text += subsystem;
      color(0);
    }
    if (!((facpri&7) <= 3)) puts(text);
    else {
      color(31);
      printf("%s", text);
      color(0);
      xputc('\n');
    }
  }
  close(fd);
}
