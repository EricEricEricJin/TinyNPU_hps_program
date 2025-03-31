#include <error.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include <stdarg.h>
#include <assert.h>

#define PIO_BASE (0xC0000000)
#define PIO_SPAN (0x08)
#define H2F_OFFSET (0x00)
#define F2H_OFFSET (0x04)

#define SDRAM_BASE (0x20000000)
#define SDRAM_SPAN    (0x20000)

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

typedef enum
{
    OP_LOAD = 0b00,
    OP_STORE = 0b01,
    OP_MOVE = 0b10,
    OP_FETCH_EXEC = 0b11,
} op_t;

uint32_t _inst_ldst(op_t op, uint32_t sdram_addr, uint32_t rf_addr, uint32_t line_num)
{

    assert(sdram_addr < SDRAM_SPAN);
    assert(rf_addr < 512);
    assert(line_num < 256);

    uint32_t inst = 0;
    inst |= (op << 30);
    inst |= (rf_addr << 21);
    inst |= (sdram_addr << 8);
    inst |= line_num;

    printf("Load/Store inst: 0x%08x\n", inst);

    return inst;
}

uint32_t inst_load(uint32_t sdram_addr, uint32_t rf_addr, uint32_t line_num)
{
    return _inst_ldst(OP_LOAD, sdram_addr, rf_addr, line_num);
}

uint32_t inst_store(uint32_t sdram_addr, uint32_t rf_addr, uint32_t line_num)
{
    return _inst_ldst(OP_STORE, sdram_addr, rf_addr, line_num);
}

uint32_t inst_move(uint32_t src_addr, uint32_t dst_addr, bool src_freeze, bool dst_freeze, uint32_t line_num)
{
    assert(src_addr < 1024);
    assert(dst_addr < 1024);
    assert(line_num < 256);

    uint32_t inst = 0;
    inst |= (OP_MOVE << 30);

    inst |= (src_addr << 20);
    inst |= (dst_addr << 10);
    inst |= (src_freeze << 9);
    inst |= (dst_freeze << 8);
    inst |= line_num;

    printf("Move inst: 0x%08x\n", inst);

    return inst;
}

int main()
{
    
    volatile void *pio_map = NULL;
    volatile void *pio_map_h2f = NULL;
    volatile void *pio_map_f2h = NULL;
    volatile void *sdram_map = NULL;

    int8_t* src_prt, *dst_ptr;

    // open mem dev
    int mem_fd = 0;
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0)
    {
        perror("Couldn't open /dev/mem\n");
        return -1;
    }

    // map mem
    pio_map = map_mem(mem_fd, PIO_BASE, PIO_SPAN);
    sdram_map = map_mem(mem_fd, SDRAM_BASE, SDRAM_SPAN);
    if (!pio_map || !sdram_map)
        return -1;

    pio_map_h2f = pio_map + H2F_OFFSET;
    pio_map_f2h = pio_map + F2H_OFFSET;

    // load data to sdram mapped space
    load_weight("rd_mem.bin", sdram_map, SDRAM_SPAN);
    printf("0x%x bytes loaded to %p.\n", SDRAM_SPAN, sdram_map);
    // printf("First 16 numbers: ");
    // for (int i = 0; i < 16; i++)
    //     printf("%d,", ((int8_t*)sdram_map)[i]);
    // printf("\n");

    // command to load data
    *((uint32_t*)pio_map_h2f) = inst_load(0, 0, 166);
    printf("Load command sent \n");
    
    // wait for load done
    while (1)
    {
        usleep(10);
        uint32_t status = *((uint32_t*)pio_map_f2h);
        if (status & (1 << 30))
            break;
    }
    printf("Load done.\n");

    // before move 
    // printf("Before move: ");
    // result_ptr = (int8_t*)(0x10000 + sdram_map);
    // for (int i = 0; i < 16; i++)
    //     printf("%d,", result_ptr[i]);
    // printf("\n");


    // command to move data
    *((uint32_t*)pio_map_h2f) = inst_move(0, 167, 0, 0, 166);
    printf("Move command sent.\n");

    // wait for move done
    while (1)
    {
        usleep(10);
        uint32_t status = *((uint32_t*)pio_map_f2h);
        if (status & (1 << 31))
            break;
    }
    printf("Move done.\n");

    // usleep(10);
    // uint32_t status = *((uint32_t*)pio_map_f2h);
    // printf("Debug fdbk: 0x%x\n", status);
    // return -1;
    
    // command to store data
    *((uint32_t*)pio_map_h2f) = inst_store(0x1000, 167, 166);

    // wait for store done
    while (1)
    {
        usleep(10);
        uint32_t status = *((uint32_t*)pio_map_f2h);
        if (status & (1 << 30))
            break;
    }
    printf("Store done.\n");
    
    // uint32_t status = *((uint32_t*)pio_map_f2h);
    // printf("Debug fdbk = %x\n", status);
    // sleep(1);
    // exit(1);
    // sleep(1);


    // print to see~
    printf("Checking result: \n");
    src_prt = (int8_t*)sdram_map;
    dst_ptr = (int8_t*)(0x10000 + sdram_map);
    for (int i = 0; i < 166*176; i++)
    {
        if (src_prt[i] != dst_ptr[i])
            printf("Mismatch at %d: %d, %d\n", i, src_prt[i], dst_ptr[i]);
    }
    printf("Check done.\n");

    // unmap mem
    // unmap all
    munmap(PIO_BASE, PIO_SPAN);
    munmap(SDRAM_BASE, SDRAM_SPAN);
    
    close(mem_fd);
    
    printf("Unmapped.\n");
    
    return 0;
}
