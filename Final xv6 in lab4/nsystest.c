#include "types.h"
#include "user.h"
#include "fcntl.h"

#define NPROCESS 5

int main(int argc, char *argv[])
{
    unlink("ns.txt");
    int fd = open("ns.txt", O_CREATE | O_WRONLY);
    for (int i = 0; i < NPROCESS; i++)
    {
        if (!fork())
        {
            write(fd, "G#17", 4);
            exit();
        }
        else
            continue;
    }
    for (int i = 0; i < NPROCESS; i++)
        wait();
    write(fd, "\n", 1);
    close(fd);
    nsyscalls();
    exit();
}