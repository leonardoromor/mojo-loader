
// Reference:
// https://github.com/embmicro/mojo-loader/blob/basic-loader/src/com/embeddedmicro/mojo/MojoLoader.java

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


// This has been copied from the serial initialization from the mojo java
// uploader
// #define BAUDRATE B115200
// #define DATABITS 8
// #define STOPBITS 1
// #define PARITY 0 // None

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options] binary\n", progname);
  fprintf(stderr, "Options:\n"
          "\t-d, --device <dev>      : Serial port device (e.g. /dev/ttyACM0)\n"
          "\t-v, --verify            : Verify the uploaded binary\n"
          "\t-f, --flash             : Save it on flash\n"
          );
  return 1;
}

// Inspired by:
// http://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c
// flags: https://en.wikibooks.org/wiki/Serial_Programming/termios
int serial_setup(int fd) {
  struct termios config;
  bzero(&config, sizeof(config));

  if (tcgetattr(fd, &config) != 0) {
    fprintf(stderr, "Error %d from tcgetattr.\n", errno);
    return 1;
  }

  if(cfsetispeed(&config,  B115200) < 0 || cfsetospeed(&config,  B115200) < 0) {
    fprintf(stderr, "Error while setting baudrate");
  }

  config.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK |
                      ISTRIP | IXON);
  config.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
  config.c_cflag &= ~(CSIZE | PARENB);
  config.c_cflag |= CS8;

  if (tcsetattr(fd, TCSAFLUSH, &config) != 0)
  {
    fprintf(stderr, "Error %d from tcsetattr.\n", errno);
    return -1;
  }

  return 0;
}

void reset_mojo(int fd) {
  int flag, i;
  flag = TIOCM_DTR;
  ioctl(fd, TIOCMBIS, &flag); // DTR on
  usleep(1e3);
  for (i = 0; i < 5; ++i) {
    ioctl(fd, TIOCMBIC, &flag);// DTR off
    usleep(1e3);
    ioctl(fd, TIOCMBIS, &flag);// DTR on
  }
}


void upload_binary(int fd, const char *filename, char flash, char verify) {
  char recv;
  int status;
  FILE *fbin = fopen(filename, "rb");
  fseek(fbin, 0L, SEEK_END);
  const long fsize = ftell(fbin);
  rewind(fbin);

  //Let's load the whole file in memory
  char *bin = malloc(sizeof(char) * fsize);
  if (fread(bin, 1, fsize, fbin) != fsize) {
    fprintf(stderr, "Error wile uploading file on memory");
  }
  fclose(fbin);

  char send = 'R';
  write(fd, &send, sizeof(char));
  status = read(fd, &recv, sizeof(char));
  if (status == 0 ||  recv != 'R') {
    fprintf(stderr, "Mojo not answering correctly:\n"
                    "expecting: 'R', received: %d bytes: '%c')."
                    "Exiting...\n", status, recv);
    exit(1);
  }

  const uint32_t buf = fsize;
  write(fd, &buf, sizeof(uint32_t));
  status = read(fd, &recv, sizeof(char));
  if (status == 0 ||  recv != 'O') {
    fprintf(stderr, "Mojo not answering correctly:\n"
                    "expecting: '0', received: %d bytes: '%c')."
                    "Exiting...\n", status, recv);
    exit(1);
  }

  free(bin);
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

  filename = argv[optind];

  printf("filename:    %s\n"
         "serial_port: %s\n"
         "verify:      %s\n"
         "flash:       %s\n",
          filename, interface, verify ? "True" : "False", flash ?
          "True" : "False");

  // Open the interface
  int fd = open(interface, O_RDWR | O_NOCTTY | O_SYNC);

  if (fd < 0) {
    return fprintf(stderr, "Error opening the device.\n");
  }

  if (serial_setup(fd) != 0) {
    fprintf(stderr, "Error while serial initialization");
    exit(1);
  }

  reset_mojo(fd);
  tcflush(fd, TCIOFLUSH);

  upload_binary(fd, filename, 0, 0);

  close(fd);
  return 0;
}
