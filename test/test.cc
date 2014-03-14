#include <ctime>
#include <stdio.h>
#include <cstdlib>

#include <fcntl.h>
#include <unistd.h>

#define LAP_DEVICE_FILE "/dev/lap"

void set_matrix_zero(int *A, int m, int n)
{
    for (int i =0; i < m; ++i)
        for (int j = 0; j < n; ++j)
            A[i*n+j] = 0;
}

void init_matrix(int *A, int m, int n)
{
    for (int i =0; i < m; ++i)
        for (int j = 0; j < n; ++j)
//            A[i*m+j] = rand();
           A[i*n+j] = i+j;
}

void print_matrix(int *A, int m, int n)
{
    for (int i =0; i < m; ++i) {
        for (int j = 0; j < n; ++j)
            printf("%d ", A[i*n+j]);
        printf("\n");
    }
}

typedef struct {
    int m;
    int n;
    int *A;
    int *B;
    int *C;

    int *data_pointer;

    void do_gemm();
} gemm_data;

void gemm_data::do_gemm()
{
    if (m<=0 || n <= 0) {
        printf("Invalid argument! m,n must be larger than 0.\n");
        return;
    }

    size_t data_size = sizeof(int)*2 + sizeof(int) * 3 * m * n;
    data_pointer = (int *) malloc(data_size);
    *data_pointer = m;
    *(data_pointer+1) = n;
    A = data_pointer + 2;
    B = A + m*n;
    C = B + m*n;

    printf("Before initialization!\n");
    init_matrix(A, m, n);
    init_matrix(B, m, n);
    set_matrix_zero(C, m, n);

    printf("Before the gemm execution!\n");
    printf("A=\n");
    print_matrix(A, m, n);
    printf("B=\n");
    print_matrix(B, m, n);
    printf("C=\n");
    print_matrix(C, m, n);

    int fd = open(LAP_DEVICE_FILE, O_RDWR);
    if (fd < 0) {
        printf("Error during open the device file.\n");
        return;
    }

    int s;
    s = write(fd, data_pointer, data_size);
    if (s != data_size) {
        printf("Error during write: %lu expected, %d written.\n", data_size, s);
        return;
    }

    s = read(fd, data_pointer, data_size);
    if (s != data_size) {
        printf("Error during read: %lu expected, %d written.\n", data_size, s);
        return;
    }

#if 0
    asm("push %%rdi;\
         mov %%rdi, %1; \
         xlat; \
         pop %%rdi;"
         : "=m"(data_pointer)
         : "m"(data_pointer));
#endif

    printf("After the gemm execution!\n");
    printf("A=\n");
    print_matrix(A, m, n);
    printf("B=\n");
    print_matrix(B, m, n);
    printf("C=\n");
    print_matrix(C, m, n);

}

int main(int argc, char **argv)
{
    //unsigned long long a = 17;
    //printf("a's addr = %p, a = %llu\n", &a, a);
    //int b[] = {1,2,3,4,5};
    gemm_data data;
    srand(time(NULL));

    if (argc < 3) {
        printf("Usage: test2 M N\n");
        return 0;
    }

    data.m = atoi(argv[1]);
    data.n = atoi(argv[2]);

    printf("m=%d, n=%d\n", data.m, data.n);

    data.do_gemm();

    return 0;
}
