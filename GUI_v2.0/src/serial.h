#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>

/*
 * Thin cross-platform wrapper around a serial port. Everything above this
 * layer works with an opaque serial_fd_t and never sees termios or the
 * Win32 API.
 */

#ifdef _WIN32
#  include <windows.h>
   typedef HANDLE serial_fd_t;
#  define SERIAL_INVALID INVALID_HANDLE_VALUE
#else
   typedef int serial_fd_t;
#  define SERIAL_INVALID (-1)
#endif

/* Opens `port` ("COM7", "/dev/ttyACM0", ...) at the baud rate given as a
 * decimal string. Returns SERIAL_INVALID if the port could not be opened. */
serial_fd_t serial_open(const char *port, const char *baud_str);

void serial_write(serial_fd_t fd, const void *buf, size_t len);
void serial_close(serial_fd_t fd);
int  serial_is_invalid(serial_fd_t fd);

#endif /* SERIAL_H */
