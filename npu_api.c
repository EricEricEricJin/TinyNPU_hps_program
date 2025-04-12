#include "npu_api.h"

#define WRITE_INST(inst)              \
    do                                \
    {                                 \
        *(npu->pio_map_h2f) = (inst); \
        usleep(1);                    \
    } while (0)

#define WAIT_DONE(mask)                   \
    do                                    \
    {                                     \
        uint32_t status;                  \
        while (1)                         \
        {                                 \
            usleep(1);                    \
            status = *(npu->pio_map_f2h); \
            if (status & (mask))          \
                break;                    \
        }                                 \
    } while (0)

long get_time_in_microseconds()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

enum op_t
{
    OP_LOAD = 0b00,
    OP_STORE = 0b01,
    OP_MOVE = 0b10,
    OP_FETCH_EXEC = 0b11,
};

enum fun_t
{
    FUN_FETCH = 0b0,
    FUN_EXEC = 0b1,
};

int npu_init(npu_t npu)
{

    // open memory
    npu->mem_fd = open(npu->mem_dev, O_RDWR | O_SYNC);
    if (npu->mem_fd < 0)
    {
        perror("Error opening /dev/mem");
        return -1;
    }

    // map PIO
    npu->pio_map = (uint32_t *)mmap(npu->pio_base, npu->pio_span, PROT_READ | PROT_WRITE, MAP_SHARED, npu->mem_fd, npu->pio_base);
    if ((void *)npu->pio_map == MAP_FAILED)
    {
        perror("Error mapping PIO memory.");
        munmap(npu->pio_map, npu->pio_span);
        close(npu->mem_fd);
        return -1;
    }
    npu->pio_map_h2f = npu->pio_map + npu->h2f_offset;
    npu->pio_map_f2h = npu->pio_map + npu->f2h_offset;

    // map W mem
    npu->sdram_map_w = (uint32_t *)mmap(npu->sdram_w_base, npu->sdram_w_span, PROT_READ | PROT_WRITE, MAP_SHARED, npu->mem_fd, npu->sdram_w_base);
    if ((void *)npu->sdram_map_w == MAP_FAILED)
    {
        perror("Error mapping SDRAM W memory.");
        munmap(npu->sdram_map_w, npu->sdram_w_span);
        close(npu->mem_fd);
        return -1;
    }

    // map input output mem
    npu->sdram_map_xy = (uint32_t *)mmap(npu->sdram_xy_base, npu->sdram_xy_span, PROT_READ | PROT_WRITE, MAP_SHARED, npu->mem_fd, npu->sdram_xy_base);
    if ((void *)npu->sdram_map_xy == MAP_FAILED)
    {
        perror("Error mapping SDRAM XY memory.");
        munmap(npu->sdram_map_xy, npu->sdram_xy_span);
        close(npu->mem_fd);
        return -1;
    }

    printf("xy_map=%p, w_map=%p.\n", npu->sdram_map_xy, npu->sdram_map_w);
    return 0;
}

void npu_wait(npu_t npu, uint32_t mask)
{
    WAIT_DONE(mask);
}

bool npu_check(npu_t npu, uint32_t mask)
{
    uint32_t status = *(npu->pio_map_f2h);
    return status & mask;
}

uint32_t inst_load_store(size_t sdram_span, uint32_t op, size_t sdram_offset, uint32_t rf_addr, uint32_t line_num)
{
    assert(rf_addr < 512);
    assert(line_num < 256);
    assert(sdram_offset < sdram_span && (sdram_offset & 0xf) == 0);
    sdram_offset >>= 4;

    uint32_t inst = 0;
    inst |= (op << 30) | (rf_addr << 21) | (sdram_offset << 8) | line_num;

    return inst;
}

void npu_load(npu_t npu, size_t sdram_offset, uint32_t rf_addr, uint32_t line_num)
{
    uint32_t inst = inst_load_store(npu->sdram_xy_span, OP_LOAD, sdram_offset, rf_addr, line_num);
    WRITE_INST(inst);
}

void npu_store(npu_t npu, size_t sdram_offset, uint32_t rf_addr, uint32_t line_num)
{
    uint32_t inst = inst_load_store(npu->sdram_xy_span, OP_STORE, sdram_offset, rf_addr, line_num);
    WRITE_INST(inst);
}

void npu_move(npu_t npu, uint32_t src_addr, uint32_t dst_addr, bool src_freeze, bool dst_freeze, uint32_t line_num)
{
    assert(src_addr < 0x400);
    assert(dst_addr < 0x400);
    assert(line_num < 0xff);

    uint32_t inst = (OP_MOVE << 30) | (src_addr << 20) | (dst_addr << 10) | (src_freeze << 9) | (dst_freeze << 8) | line_num;
    WRITE_INST(inst);
}

void npu_fetch(npu_t npu, uint32_t unit, size_t sdram_offset)
{
    assert(unit < 32);
    assert(sdram_offset < npu->sdram_w_span && (sdram_offset & 0xf) == 0);
    sdram_offset >>= 4;

    uint32_t inst = (OP_FETCH_EXEC << 30) | (FUN_FETCH << 29) | (unit << 24) | sdram_offset;
    WRITE_INST(inst);
}

void npu_exec(npu_t npu, uint32_t unit)
{
    assert(unit < 32);
    uint32_t inst = (OP_FETCH_EXEC << 30) | (FUN_EXEC << 29) | (unit << 24);
    WRITE_INST(inst);
}

void npu_deinit(npu_t npu)
{
    munmap(npu->pio_map, npu->pio_span);
    munmap(npu->sdram_map_w, npu->sdram_w_span);
    munmap(npu->sdram_map_xy, npu->sdram_xy_span);
    close(npu->mem_fd);
}

int read_file_to_mem(const char *fp, void *buf, size_t n_bytes)
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

int write_mem_to_file(const char *fp, const void *buf, size_t n_bytes)
{
    FILE *fd;
    fd = fopen(fp, "wb");
    if (fd == NULL)
    {
        perror("Error opening data file.");
        return -1;
    }

    int ret = fwrite(buf, sizeof(int8_t), n_bytes, fd);
    fclose(fd);
    return ret;
}