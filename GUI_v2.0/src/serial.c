#include "serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

static DWORD baud_from_string(const char *s) {
    return (DWORD)atoi(s); /* Windows accepts the raw integer directly */
}

serial_fd_t serial_open(const char *port, const char *baud_str) {
    /* Windows requires the "\.\COMn" prefix for ports above COM9, and
     * accepts it for the lower ones too, so always use it. */
    char win_port[64];
    if (strncmp(port, "\\\\.\\", 4) != 0)
        snprintf(win_port, sizeof(win_port), "\\\\.\\%s", port);
    else
        snprintf(win_port, sizeof(win_port), "%s", port);

    HANDLE hPort = CreateFileA(
        win_port,
        GENERIC_READ | GENERIC_WRITE,
        0,       /* no sharing */
        NULL,    /* default security */
        OPEN_EXISTING,
        0,       /* non-overlapped I/O */
        NULL);
    if (hPort == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(hPort, &dcb)) { CloseHandle(hPort); return INVALID_HANDLE_VALUE; }

    dcb.BaudRate    = baud_from_string(baud_str);
    dcb.ByteSize    = 8;
    dcb.StopBits    = ONESTOPBIT;
    dcb.Parity      = NOPARITY;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (!SetCommState(hPort, &dcb)) { CloseHandle(hPort); return INVALID_HANDLE_VALUE; }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout         = MAXDWORD; /* 50 */
    timeouts.ReadTotalTimeoutConstant    = 0;        /* 500 */
    timeouts.ReadTotalTimeoutMultiplier  = 0;        /* 10 */
    timeouts.WriteTotalTimeoutConstant   = 500;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hPort, &timeouts);

    return hPort;
}

void serial_write(serial_fd_t fd, const void *buf, size_t len) {
    DWORD written;
    WriteFile(fd, buf, (DWORD)len, &written, NULL);
}

void serial_close(serial_fd_t fd) {
    CloseHandle(fd);
}

int serial_is_invalid(serial_fd_t fd) {
    return fd == INVALID_HANDLE_VALUE;
}

#else /* POSIX */

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

static speed_t baud_from_string(const char *s) {
    int b = atoi(s);
    switch (b) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:     return B115200;
    }
}

serial_fd_t serial_open(const char *port, const char *baud_str) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) return -1;

    speed_t baud = baud_from_string(baud_str);

    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

void serial_write(serial_fd_t fd, const void *buf, size_t len) {
    ssize_t n = write(fd, buf, len);
    (void)n; /* best-effort: the GUI reports failures at connect time */
}

void serial_close(serial_fd_t fd) {
    close(fd);
}

int serial_is_invalid(serial_fd_t fd) {
    return fd < 0;
}

#endif /* _WIN32 */
