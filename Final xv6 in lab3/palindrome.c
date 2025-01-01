#include "types.h"
#include "user.h"

void create_palindrome1(int num)
{
    int perv_val;
    asm volatile(
        "movl %%ebx, %0;"
        "movl %1, %%ebx;"
        : "=r"(perv_val)
        : "r"(num));
    create_palindrome();
    asm volatile("movl %0, %%ebx" : : "r"(perv_val));
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf(1, "Didn't enter the number\n");
        exit();
    }
    int num = atoi(argv[1]);
    create_palindrome1(num);
    exit();
}