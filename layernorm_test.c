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

    read_file_to_mem("gamma_lo.bin", (void*)SDRAM_W_BASE, 176);
    read_file_to_mem("gamma_hi.bin", (void*)(SDRAM_W_BASE+176), 176);
    read_file_to_mem("beta_scaled.bin", (void*)(SDRAM_W_BASE+352), 176);
    read_file_to_mem("x_768.bin", (void*)SDRAM_XY_BASE, 176*166);
    printf("W and X loaded. ");
    // for (int i = 0; i < 16; i++)
    // {
    //     printf("%x\t%x\t%x\n", 
    //         ((uint8_t*)npu.sdram_map_w)[i], 
    //         ((uint8_t*)npu.sdram_map_w+176)[i], 
    //         ((uint8_t*)npu.sdram_map_w+176*2)[i]);
    // }
    long t0 = get_time_in_microseconds();

    npu_fetch(&npu, 4, 0);
    npu_wait(&npu, FETCH_DONE);
    printf("Fetch done. ");

    npu_load(&npu, 0, 0, 166);
    npu_wait(&npu, LDST_DONE);
    printf("Load done. ");


    npu_move(&npu, 0, 0x210, false, false, 1);
    npu_wait(&npu, MOVE_DONE);
    printf("Move done. ");

    npu_exec(&npu, 4);
    npu_wait(&npu, 1 << 4);
    printf("Exec done. ");

    npu_move(&npu, 0x218, 167, false, false, 1);
    npu_wait(&npu, MOVE_DONE);
    printf("Move done. ");

    npu_store(&npu, 0x11000, 167, 1);
    npu_wait(&npu, LDST_DONE);
    printf("Store done. ");

    long t1 = get_time_in_microseconds();

    printf("Checking result: \n");
    int8_t* dst_ptr = (int8_t*)(npu.sdram_xy_base) + 0x11000;
    for (int i = 0; i < 176; i++)
    {
        printf("%d ", dst_ptr[i]);
    }
    printf("\nCheck done.\n");
    printf("Time taken: %ld us\n", t1 - t0);
    npu_deinit(&npu);
    return 0;
}
