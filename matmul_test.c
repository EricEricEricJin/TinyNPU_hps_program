#include <error.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#define IO_BASE (0xC0000000)
#define IO_SPAN (0x08)
#define OUTPUT_OFFSET (0x00)
#define INPUT_OFFSET (0x04)

#define W_DATA_BASE (0x20000000)
#define W_DATA_SPAN ((32 * 6) * (704 + 1))

#define P (704)
#define N (176)
#define M (166)

enum {
    CMD_INPUT_X         = 0x0,
    CMD_START_FETCH     = 0x1,
    CMD_START_MM        = 0x2,
    CMD_OUTPUT_Y        = 0x3,
};

// load weight
int load_weight(const char *fp, void *buf, size_t n_bytes)
{
    FILE *fd;
    fd = fopen(fp, "rb");
    if (fd == NULL)
    {
        perror("Error opening data file.");
        return -1;
    }

    int ret = fread(buf, sizeof(int8_t), n_bytes, fd);
    fclose(fd);
    return ret;
}

// map_mem
void *map_mem(int fd, off_t base, size_t span)
{
    void *ret = mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
    if (ret == MAP_FAILED)
    {
        perror("Error mapping memory.");
        return NULL;
    }
    return ret;
}

#define H2F_SEND   do { *((uint32_t*)input_map) = *((uint32_t*)h2f_line); } while (0)

int main(int argc, char **argv)
{
    int8_t W_buf[N * P];
    int8_t X_buf[M * N];

    uint8_t h2f_line[4];
    uint8_t f2h_line[4];

    void *io_map    = NULL;
    void *input_map = NULL;
    void *w_data_map = NULL;
    void *output_map = NULL;

    int ret;

    // open mem dev
    int mem_fd = 0;
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0)
    {
        perror("Couldn't open /dev/mem\n");
        return -1;
    }

    // map mem
    w_data_map = map_mem(mem_fd, W_DATA_BASE, W_DATA_SPAN);
    io_map = map_mem(mem_fd, IO_BASE, IO_SPAN);
    input_map = io_map + INPUT_OFFSET;
    output_map = io_map + OUTPUT_OFFSET;

    if ( !w_data_map || !io_map )
        return -1;

    // read weight data from file
    if ((load_weight("w_641.bin", W_buf, sizeof(W_buf) / sizeof(int8_t)) != sizeof(W_buf) / sizeof(int8_t)) ||
        (load_weight("x_786.bin", X_buf, sizeof(X_buf) / sizeof(int8_t)) != sizeof(X_buf) / sizeof(int8_t)))
        return -1;

    // put the weight into shared mem
    void *w_dst = w_data_map;
    void *w_src = W_buf;
    for (int i = 0; i < P; i++)
    {
        memcpy(w_dst, w_src, N);
        w_dst += 32 * 6;
        w_src += N;
    }
    
    // put misc
    __fp16 scale = 0.01335;
    int8_t zero = 25;
    int8_t z_X = -11;
    int8_t z_W = 0;

    memcpy(w_dst, &scale, 2);
    w_dst += 2;
    memcpy(w_dst, &zero, 1);
    w_dst += 1;
    memcpy(w_dst, &z_W, 1);
    w_dst += 1;
    memcpy(w_dst, &z_X, 1);
    w_dst += 1;

    printf("%2x\n", *((uint16_t*)&scale));

    printf("Weight data loaded.\n");

    // write the input signal X
    for (int i = 0; i < N / 2; i++)
    {
        h2f_line[3] = CMD_INPUT_X;    // cmd_type
        h2f_line[2] = i & 0xff;       // index
        memcpy(h2f_line, &X_buf[i * 2], 2); // 2 numbers
    
        H2F_SEND;
        usleep(100);
    }
    printf("X input loaded.\n");

    // Start fetch W data
    h2f_line[3] = CMD_START_FETCH;
    H2F_SEND;
    printf("Fetch start.\n");
    usleep(10000);

    // Start computing matmul
    h2f_line[3] = CMD_START_MM;
    H2F_SEND;
    printf("Calc start.\n");
    usleep(10000);
    
    // read output
    int8_t y[704];
    uint16_t y_idx;

    h2f_line[3] = CMD_OUTPUT_Y;
    for (int i = 0; i < 704 / 2; i++)
    {
        memcpy(h2f_line, &i, 2);
        H2F_SEND;
        usleep(100);
        *((uint32_t*)f2h_line) = *((uint32_t*)output_map);
        memcpy(&y_idx, f2h_line + 2, 2);
        memcpy(y + 2 * i, f2h_line, 2);
        if (y_idx != i)
        {
            perror("Index not match!");
            return -1;
        }
    }        
    printf("Output copied.\n");

    // print output
    for (int i = 0; i < 704; i++)
    {
        printf("%d,", y[i]);
    }
    printf("\n");
    
    // unmap all
    if (munmap(IO_BASE, IO_SPAN) < 0 ||
        munmap(W_DATA_BASE, W_DATA_SPAN) < 0 )
    {
        close(mem_fd);
        return -1;
    }

    close(mem_fd);
    printf("Unmapped.\n");

    return 0;
}
