#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/stat.h>

static char *portName = "/dev/ttyACM0";
static unsigned char buffer[4096];
static unsigned char commandA = 0xA0; // start comms
static unsigned char commandB = 0x0A;
static unsigned char getChip = 0xFD;
static unsigned char unk1 = 0x50;
static unsigned char unk2 = 0x05;
static unsigned char getHwSwVer = 0xFC;
static unsigned char read32 = 0xD1;
static unsigned char write32 = 0xD4;
static unsigned char getBLVer = 0xFE;
static unsigned char sendDA = 0xD7;
static unsigned char runProgram = 0xD5;
static unsigned char nsJump = 0xA8; // possibly legacy jump

static int fd = 0;

// returns FD
int getProgrammer(const char* progLocation, struct stat *statStruct)
{
    int progFD = open(progLocation, O_RDONLY);

    fstat(progFD, statStruct);

    return progFD;
}

// most likely firmware limit on weird bulk size
void sendBulkData(int programmerFD, unsigned int fileSizes,  size_t maxBulksSize)
{
    unsigned int lastPacketSize = fileSizes % maxBulksSize;
    unsigned int numOfFullSizePackets = (fileSizes - lastPacketSize) / maxBulksSize;

    for (int i = 0; i < numOfFullSizePackets; i++)
    {
        if (read(programmerFD, buffer, maxBulksSize) > 0)
        {
            if(write(fd, buffer, maxBulksSize) <= 0)
            {
                printf("Error: %s\n", strerror(errno));
                break;
            }
        }
        else
        {
            printf("Error: %s\n", strerror(errno));
            break;
        }

        usleep(15000); // give SoC time to process
    }

    read(programmerFD, buffer, lastPacketSize);
    write(fd, buffer, lastPacketSize);

    usleep(15000);

    close(programmerFD);
}

// ARM needs data in big endian
void littleToBig(void* buffer, size_t size)
{
    if (size == 4)
    {
        unsigned int* num = (unsigned int*)buffer;

        *num = ((*num >> 24 &0xFF) | (*num << 24 & 0xFF000000) | (*num >> 8 & 0xFF00) | (*num << 8 & 0xFF0000));
    }

}

void printHelper(unsigned char* buffer, size_t size)
{
    for (int i = 0; i < size; i++)
    {
        printf("%.2X ", buffer[i]);
    }

    printf("\n");
}

void rprint(size_t size)
{
    usleep(1000);
    int nread = read(fd, buffer, size);
    printf("Read bytes: %i\n", nread);

    if (nread > 0)
        printHelper(buffer, nread);
}

void wprint(unsigned char* data, size_t size)
{
    int nwrite = write(fd, data, size);
    printf("Write bytes: %i\n", nwrite);

    if (nwrite > 0)
        printHelper(data, nwrite);

    usleep(1000); // give some time for the device to process
}

int handlePrimarySetup()
{
    // continue with handshake
    wprint(&commandB, 1);
    rprint(1);

    // 50
    wprint(&unk1, 1);
    rprint(1); // AF

    // 05
    wprint(&unk2, 1);
    rprint(1); // FA

    // get chip ver
    wprint(&getChip, 1);
    rprint(1); // FD repeat
    rprint(2); // chip ver 03 35, got 03 03, no battery?
    rprint(2); // 00 00

    // FC
    wprint(&getHwSwVer, 1);
    rprint(1); // FC
    rprint(2); // 8A 00
    rprint(2); // CA 00
    rprint(2); // 00 00
    rprint(2); // 00 00

    // D1
    unsigned int someAddress = 0x10206044;
    unsigned int someValue = 0x1;
    wprint(&read32, 1);
    rprint(1); // D1
    littleToBig(&someAddress, 4);
    wprint((unsigned char*)&someAddress, 4);
    rprint(4); // 10 20 60 44
    littleToBig(&someValue, 4);
    wprint((unsigned char*)&someValue, 4);
    rprint(4); // 00 00 00 01
    rprint(2); // 00 00
    rprint(4); // 28 90 00 10
    rprint(2); // 00 00

    // D4
    unsigned int someAddress2 = 0x10007000;
    unsigned int someValue2 = 0x1;
    unsigned int someValue3 = 0x22000000;
    wprint(&write32, 1);
    rprint(1); // D4
    littleToBig(&someAddress2, 4);
    wprint((unsigned char*)&someAddress2, 4);
    rprint(4); // 10 00 70 00
    littleToBig(&someValue2, 4);
    wprint((unsigned char*)&someValue2, 4);
    rprint(4); // 00 00 00 01
    rprint(2); // 00 00
    littleToBig((unsigned char*)&someValue3, 4);
    wprint((unsigned char*)&someValue3, 4);
    rprint(4); // 22 00 00 00
    rprint(2); // 00 00, possibility of 00 01


    // FE
    unsigned char someValue4 = 0xFF;
    wprint(&getBLVer, 1);
    rprint(1); // FE
    wprint(&someValue4, 1);
    rprint(1); // 05
    wprint(&someValue4, 1); // again?
    rprint(1); // FE, why 05 previous read

    // get programmer file size
    struct stat fileSystemStats;
    int programmerFD = getProgrammer("./MT6735P.bin", &fileSystemStats);

    if (programmerFD <= 0)
    {
        printf("Error: %s\n", strerror(errno));
        return -1;
    }

    // D7
    unsigned int controlProgramAddress = 0x200000;
    unsigned int programFullSize = (unsigned int)fileSystemStats.st_size; // 0x15358
    unsigned int signatureLength = 0x100;
    wprint(&sendDA, 1);
    rprint(1); // D7
    littleToBig((unsigned char*)&controlProgramAddress, 4);
    wprint((unsigned char*)&controlProgramAddress, 4);
    rprint(4); // 00 20 00 00
    littleToBig((unsigned char*)&programFullSize, 4);
    wprint((unsigned char*)&programFullSize, 4);
    rprint(4); // 00 01 53 58
    littleToBig((unsigned char*)&signatureLength, 4); // get rid of signature field
    wprint((unsigned char*)&signatureLength, 4);
    rprint(4); // 00 00 01 00
    rprint(2); // 00 00

    // SoC expecting DA transfer until size is matched
    sendBulkData(programmerFD, (unsigned int)fileSystemStats.st_size, 0x3F0);
    rprint(2); // BE 8E CRC16 checksum?
    rprint(2); // 00 00


    // D5
    wprint(&runProgram, 1);
    rprint(1); // D5
    rprint(5); // READY, uploaded program should be executing


    return 0;
}

int main()
{
    printf("Trying to access BROM/Preloader\n");

    struct termios newtio, oldtio;
    bzero(&newtio, sizeof(struct termios));
    newtio.c_cflag = B115200 | CS8 | CREAD | CRTSCTS;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    newtio.c_cc[VMIN] = 0;
    newtio.c_cc[VTIME] = 1;

    for(int i = 0; i < 150; i++)
    {
        fd = open(portName, O_RDWR | O_NONBLOCK | O_NOCTTY, 0);

        if (fd > 0)
            break;

        usleep(100000);
        //printf("Trying to access %s again. \n", portName);
    }

    if (fd > 0)
    {
        tcgetattr(fd, &oldtio);
        tcsetattr(fd, TCSANOW, &newtio);
        tcflush(fd, TCIFLUSH);

        int nread = 0;

        for (int i = 0; i < 10; i++)
        {
            nread = read(fd, buffer, 5); // 1 byte reads go haywire

            if (nread == 5)
            {
                printf("Got: %s\n", buffer);
                write(fd, &commandA, 1);
            }

            else if (nread == 1)
            {
                printf("Got command: %X\n", buffer[0]);

                if (buffer[0] == 0x5F) // initial handhake done
                {
                    printf("Progressing further...\n");
                    handlePrimarySetup();
                    break;
                }
                else
                {
                    printf("Command in wrong order: %X\n", buffer[0]);
                }
            }
            else
                printf("Size messed up: %i\n", nread);



            usleep(10000);
        }

    }

    if (fd == -1)
    {
        printf("Error: %s\n", strerror(errno));
        return -1;
    }


    close(fd);
    return 0;
}

