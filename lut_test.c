#include "npu_api.h"

int main()
{
    struct npu npu;

    npu.mem_dev = "/dev/mem";

    npu.pio_base = PIO_BASE;
    npu.pio_span = PIO_SPAN;
    npu.h2f_offset = H2F_OFFSET;
    npu.f2h_offset = F2H_OFFSET;
    npu.sdram_w_base = SDRAM_W_BASE;
    npu.sdram_w_span = SDRAM_W_SPAN;
    npu.sdram_xy_base = SDRAM_XY_BASE;
    npu.sdram_xy_span = SDRAM_XY_SPAN;

    printf("NPU init start.\n");
    if (npu_init(&npu) != 0)
    {
        printf("NPU init failed.\n");
        return -1;
    }
    printf("NPU init done.\n");

    read_file_to_mem("lut_silu_0.bin", (void*)SDRAM_W_BASE, 256);
    read_file_to_mem("x_789.bin", (void*)SDRAM_XY_BASE, 704);

    long t0 = get_time_in_microseconds();

    npu_fetch(&npu, 8, 0);
    npu_wait(&npu, FETCH_DONE);
    printf("Fetch done. ");

    npu_load(&npu, 0, 0, 4);
    npu_wait(&npu, LDST_DONE);
    printf("Load done. ");

    for (int i = 0; i < 4; i++)
    {
        npu_move(&npu, i, 0x220, false, false, 1);
        npu_wait(&npu, MOVE_DONE);
        printf("Move done. ");
    
        npu_exec(&npu, 8);
        npu_wait(&npu, 1 << 8);
        printf("Exec done. ");

        npu_move(&npu, 0x228, 4+i, false, false, 1);
        npu_wait(&npu, MOVE_DONE);
        printf("Move done. ");
    }


    npu_store(&npu, 0x10000, 4, 4);
    npu_wait(&npu, LDST_DONE);
    long t1 = get_time_in_microseconds();
    printf("Store done. ");
    
    printf("Time: %ld us\n", t1 - t0);
    // Print the result
    printf("Checking result...\n");
    int8_t *result = (int8_t *)((uint8_t *)npu.sdram_xy_base) + 0x10000;
    for (int i = 0; i < 704 / 16; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            printf("%d ", result[i * 16 + j]);
        }
        printf("\n");
    }
    printf("\n");
    write_mem_to_file("lut_output.bin", result, 704);
    printf("Result check done.\n");
    npu_deinit(&npu);
    printf("NPU deinit done.\n");
    return 0;
}