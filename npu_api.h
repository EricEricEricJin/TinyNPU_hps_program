#ifndef __NPU_API_H__
#define __NPU_API_H__

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

#include <signal.h>

#define SDRAM_W_BASE 0x20000000
#define SDRAM_W_SPAN 0x10000000

#define SDRAM_XY_BASE 0x30000000
#define SDRAM_XY_SPAN 0x20000

#define PIO_BASE 0xC0000000
#define PIO_SPAN 0x08
#define H2F_OFFSET 0x00
#define F2H_OFFSET 0x04

#if (__x86_64__)
#define SIM
#endif

#ifdef SIM
#define log(...) printf("$display(\"%s\");\n", __VA_ARGS__)
#else
#define log(...)             \
    do                       \
    {                        \
        printf(__VA_ARGS__); \
        printf("\n");        \
    } while (0)
#endif

enum
{
    MOVE_DONE = 1 << 31,
    LDST_DONE = 1 << 30,
    FETCH_DONE = 1 << 28,
    STMM_0_DONE = 1,
    // todo
};

typedef struct npu *npu_t;

struct npu
{

    char *mem_dev;

    void *pio_base;
    size_t pio_span;
    size_t h2f_offset;
    size_t f2h_offset;

    void *sdram_w_base;
    size_t sdram_w_span;

    volatile void *sdram_xy_base;
    size_t sdram_xy_span;

    int mem_fd;

    volatile uint32_t *pio_map;
    volatile uint32_t *pio_map_h2f;
    volatile uint32_t *pio_map_f2h;

    volatile uint32_t *sdram_map_w;
    volatile uint32_t *sdram_map_xy;
};

int npu_init(npu_t npu);

void npu_wait(npu_t npu, uint32_t mask);

void npu_load(npu_t npu, size_t sdram_offset, uint32_t rf_addr, uint32_t line_num);
void npu_store(npu_t npu, size_t sdram_offset, uint32_t rf_addr, uint32_t line_num);

void npu_move(npu_t npu, uint32_t src_addr, uint32_t dst_addr, bool src_freeze, bool dst_freeze, uint32_t line_num);

void npu_fetch(npu_t npu, uint32_t unit, size_t sdram_offset);
void npu_exec(npu_t npu, uint32_t unit);

void npu_deinit(npu_t npu);

int read_file_to_mem(const char *fp, void *buf, size_t n_bytes);

#endif
