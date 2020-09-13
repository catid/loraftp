// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "linux_serial.hpp"

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iostream>
using namespace std;

namespace lora {


//------------------------------------------------------------------------------
// Tools

int BaudrateToBaud(int baudrate)
{
    switch (baudrate)
    {
    case 50: return B50;
    case 75: return B75;
    case 110: return B110;
    case 134: return B134;
    case 150: return B150;
    case 200: return B200;
    case 300: return B300;
    case 600: return B600;
    case 1200: return B1200;
    case 1800: return B1800;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 500000: return B500000;
    case 576000: return B576000;
    case 921600: return B921600;
    case 1000000: return B1000000;
    case 1152000: return B1152000;
    case 1500000: return B1500000;
    case 2000000: return B2000000;
    case 2500000: return B2500000;
    case 3000000: return B3000000;
    case 3500000: return B3500000;
    case 4000000: return B4000000;
    }
    cerr << "Invalid baudrate" << endl;
    return -1;
}


//------------------------------------------------------------------------------
// RawSerialPort

bool RawSerialPort::Initialize(const char* port_file, int baudrate)
{
    Shutdown();

    int baud = BaudrateToBaud(baudrate);
    if (baud < 0) {
        cerr << "invalid baudrate";
        return false;
    }

    fd = open(port_file, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK);
    if (fd < 0) {
        cerr << "Unable to open serial port: " << port_file << endl;
        return -1;
    }

    fcntl(fd, F_SETFL, O_RDWR);

    struct termios options;
    tcgetattr(fd, &options);

    cfmakeraw(&options);
    cfsetispeed(&options, baud);
    cfsetospeed(&options, baud);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    options.c_oflag &= ~(PARENB | PARODD | CMSPAR);
    options.c_oflag &= ~(OPOST | ONLCR | OCRNL);

    options.c_iflag &= ~(INLCR | IGNCR | ICRNL | IGNBRK);
    options.c_iflag &= ~(IUCLC);
    options.c_iflag &= ~(PARMRK);
    options.c_iflag &= ~(INPCK | ISTRIP);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_iflag &= ~(CRTSCTS);

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    const int operation_timeout_msec = 5000; // 5 seconds

    options.c_cc[VMIN]  = 0;
    options.c_cc[VTIME] = operation_timeout_msec / 100;

    Flush();

    tcsetattr(fd, TCSANOW, &options);

    int status = 0;
    ioctl(fd, TIOCMGET, &status);

    status |= TIOCM_DTR;
    status |= TIOCM_RTS;

    ioctl(fd, TIOCMSET, &status);

    usleep(10000); // 10 msec

    return true;
}

void RawSerialPort::Shutdown()
{
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
}

void RawSerialPort::Flush()
{
    tcflush(fd, TCIOFLUSH);
}

bool RawSerialPort::Write(const void* data, int bytes)
{
    int r = write(fd, (const char*)data, bytes);
    if (r != bytes) {
        if (r < 0) {
            cerr << "write failed: errno=" << errno << endl;
        } else {
            cerr << "truncated write: r=" << r << endl;
        }
        return false;
    }

    return true;
}

int RawSerialPort::GetAvailable()
{
    int result = 0;
    int r = ioctl(fd, FIONREAD, &result);
    if (r < 0) {
        cerr << "FIONREAD failed: errno=" << errno << endl;
        return -1;
    }

    return r;
}

int RawSerialPort::Read(void* data, int bytes)
{
    int r = read(fd, (char*)data, bytes);
    if (r < 0) {
        cerr << "read failed: errno=" << errno << endl;
        return -1;
    }

    return r;
}


} // namespace lora
