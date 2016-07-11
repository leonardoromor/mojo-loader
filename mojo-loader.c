
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>
#include <stdlib.h>

#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>

#include <sys/types.h>
#include <sys/select.h>

#include <unistd.h>

#include <stdbool.h>

#define FILE_SIZE_LIMIT   (1UL << 32)
#define BLOCK_SIZE 1024

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options] binary\n", progname);
  fprintf(stderr, "Options:\n"
          "\t-d, --device <dev>      : Serial port device (e.g. /dev/ttyACM0)\n"
          "\t-v, --verify            : Verify the uploaded binary\n"
          "\t-f, --flash             : Save it on flash\n"
          );
  return 1;
}

int serial_setup(int fd) {
  struct termios config;
  bzero(&config, sizeof(config));

  if (tcgetattr(fd, &config) != 0) {
    fprintf(stderr, "Error %d from tcgetattr.\n", errno);
    return -1;
  }

  if (cfsetispeed(&config,  B115200) < 0 || cfsetospeed(&config,  B115200) < 0) {
    fprintf(stderr, "Error while setting baudrate");
    return -1;
  }

  config.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK |
                      ISTRIP | IXON | IXOFF | IXANY);
  config.c_lflag = 0;
  config.c_cflag &= ~(CSIZE | PARENB);
  config.c_cflag |= CS8;
  config.c_cflag |= (CLOCAL | CREAD);
  config.c_cflag &= ~CRTSCTS;
  config.c_cc[VMIN]  = 1;
  config.c_cc[VTIME] = 5;

  if (tcsetattr(fd, TCSAFLUSH, &config) != 0) {
    fprintf(stderr, "Error %d from tcsetattr.\n", errno);
    return -1;
  }

  return 0;
}

void reset_mojo(int fd) {
  int flag, i;
  flag = TIOCM_DTR;

  ioctl(fd, TIOCMBIS, &flag); // DTR on
  usleep(5e3);
  for (i = 0; i < 5; ++i) {
    ioctl(fd, TIOCMBIC, &flag);// DTR off
    usleep(5e3);
    ioctl(fd, TIOCMBIS, &flag);// DTR on
  }
}

void upload_binary(int fd_serial, int fd_bin, const uint32_t bin_size) {
  int transferred, block;
  float perc;
  char recv, send;
  char buf[BLOCK_SIZE];

  tcflush(fd_serial, TCIOFLUSH);
  send = 'R';
  write(fd_serial, &send, sizeof(char));
  if (read(fd_serial, &recv, sizeof(char)) == 0 || recv != 'R') {
    fprintf(stderr, "Mojo did not respond! Make sure the port is correct.\n"
                    "Exiting...\n");
    exit(1);
  }

  write(fd_serial, &bin_size, sizeof(uint32_t));
  if (read(fd_serial, &recv, sizeof(char)) == 0 || recv != 'O') {
    fprintf(stderr, "Mojo did not aknowledged the file size.\n"
                    "Exiting...\n");
    exit(1);
  }

  //Split in chunks, let's avoid to upload the whole file in RAM.
  transferred = 0;
  printf("%.0f%%\n", 0.0);
  while (transferred != bin_size) {
    block = read(fd_bin, buf, BLOCK_SIZE);
    if (block == write(fd_serial, buf, block)) transferred += block;
    else {
      fprintf(stderr, "Error during writing...");
      exit(1);
    }
    perc = (float) transferred / bin_size;
    printf("%.1f%%\n", perc * 100);
  }

  if (read(fd_serial, &recv, sizeof(char)) == 0 || recv != 'D') {
    fprintf(stderr, "Mojo did not aknowledged the transfer.\n"
                    "Exiting...\n");
    exit(1);
  }
}

int main(int argc, char *argv[]) {
  static const char *filename;
  static const char *interface;
  static int verify, flash;

  static struct option long_options[] = {
    {"device",     required_argument,       NULL,   'd'},
    {"verify",     optional_argument,       &verify,  1},
    {"flash",      optional_argument,       &flash,   1},
    {0, 0, 0, 0}
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "d:vf",
                              long_options, NULL)) != -1) {
    switch (opt) {
      case 0:
        break;
      case 'd':
        interface = strdup(optarg);
        break;
      case 'v':
        verify = 1;
        break;
      case 'f':
        flash = 1;
        break;
      default:
        return usage(argv[0]);
    }
  }

  if (optind < 2) {
    return usage(argv[0]);
  }

  if (!interface) {
    fprintf(stderr, "Error, missing interface path.\n");
    return usage(argv[0]);
  }

  if (optind >= argc) {
    fprintf(stderr, "Error, missing binary file.\n");
    return usage(argv[0]);
  }

  filename = strdup(argv[optind]);

  // Open the mojo interface
  int fd_serial = open(interface, O_RDWR | O_NOCTTY | O_SYNC);

  if (fd_serial < 0) {
    return fprintf(stderr, "Error while opening the device.\n");
  }

  if (serial_setup(fd_serial) < 0) {
    fprintf(stderr, "Error during serial initialization\n");
    exit(1);
  }

  reset_mojo(fd_serial);

  // Open file
  int fd_bin = open(filename, O_RDONLY | O_RSYNC);
  if (fd_bin < 0)
    fprintf(stderr, "Error while opening the binary file %d", errno);

  // Get file size
  const long bin_size = lseek(fd_bin, 0L, SEEK_END);
  // Rewind
  lseek(fd_bin, 0L, SEEK_SET);

  if (bin_size >  FILE_SIZE_LIMIT) {
    fprintf(stderr, "Error the selected file is too big\n");
    exit(1);
  }

  upload_binary(fd_serial, fd_bin, bin_size);

  if (close(fd_bin) < 0)
    fprintf(stderr, "Error on closing the binary file: %d", errno);
  if (close(fd_serial) < 0)
    fprintf(stderr, "Error on closing the serial port: %d", errno);
  return 0;
}
