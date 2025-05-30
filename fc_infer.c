#include "npu_api.h"

#define N (166)

int8_t residual_sum(int8_t a, int8_t b, float sa, float sb, int8_t z)
{
    return (int8_t) (sa*a + sb*b + z);
}

uint64_t time_us[N+1];

int main()
{
    // ---------- Memory Addresses ----------
    const int GAMMA_LO_OFFSET = 0;
    const int GAMMA_HI_OFFSET = 176;
    const int BETA_OFFSET = 352;

    const int W0_OFFSET = 0x400;
    const int W1_OFFSET = W0_OFFSET + 176*176+16;
    const int W2_OFFSET = W1_OFFSET + 176*176+16;
    const int W3_OFFSET = W2_OFFSET + 176*176+16;
    const int LUT_SILU_OFFSET = 0x20000;

    const int FC2_W0_OFFSET = 0x30000;
    const int FC2_W1_OFFSET = FC2_W0_OFFSET + 176*176+16;
    const int FC2_W2_OFFSET = FC2_W1_OFFSET + 176*176+16;
    const int FC2_W3_OFFSET = FC2_W2_OFFSET + 176*176+16;

    // ---------- Residual Add quantization ----------
    const int8_t res_za = 11;
    const float res_sa = 26.644136428833008;
    const int8_t res_zb = -20;
    const float res_sb = 156.83506774902344;
    const int8_t res_zout = -22;
    const float res_sout = 158.14305114746094;

    const float converted_sa = res_sa / res_sout;
    const float converted_sb = res_sb / res_sout;
    const int8_t converted_z = res_zout - res_za * converted_sa - res_zb * converted_sb;

    // ---------- Mapped SRC, Mapped DST, final output array ----------
    const int8_t* src = (int8_t*)(SDRAM_XY_BASE);
    const int8_t* dst = (int8_t*)(SDRAM_XY_BASE + 0x10000);
    int8_t final_output[N][176] = {0};

    // ---------- NPU Initialization ----------
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

    // ---------- Load parameters into SDRAM ---------- 
    // Layer Norm
    read_file_to_mem("gamma_lo.bin", (void*)(SDRAM_W_BASE+GAMMA_LO_OFFSET), 176);
    read_file_to_mem("gamma_hi.bin", (void*)(SDRAM_W_BASE+GAMMA_HI_OFFSET), 176);
    read_file_to_mem("beta_scaled.bin", (void*)(SDRAM_W_BASE+BETA_OFFSET), 176);
    // FC1
    read_file_to_mem("stmm_w0.bin", (void*)(SDRAM_W_BASE+W0_OFFSET), 176*176+16);
    read_file_to_mem("stmm_w1.bin", (void*)(SDRAM_W_BASE+W1_OFFSET), 176*176+16);
    read_file_to_mem("stmm_w2.bin", (void*)(SDRAM_W_BASE+W2_OFFSET), 176*176+16);
    read_file_to_mem("stmm_w3.bin", (void*)(SDRAM_W_BASE+W3_OFFSET), 176*176+16);
    // SILU    
    read_file_to_mem("lut_silu_0.bin", (void*)(SDRAM_W_BASE+LUT_SILU_OFFSET), 256);    
    // FC2
    read_file_to_mem("stmm2_w0.bin", (void*)(SDRAM_W_BASE+FC2_W0_OFFSET), 176*176+16);
    read_file_to_mem("stmm2_w1.bin", (void*)(SDRAM_W_BASE+FC2_W1_OFFSET), 176*176+16);
    read_file_to_mem("stmm2_w2.bin", (void*)(SDRAM_W_BASE+FC2_W2_OFFSET), 176*176+16);
    read_file_to_mem("stmm2_w3.bin", (void*)(SDRAM_W_BASE+FC2_W3_OFFSET), 176*176+16);
    // // MUL
    // read_file_to_mem("lut_mul_0.bin", (void*)(SDRAM_W_BASE+LUT_MUL_OFFSET), 256);
    int8_t mul_lut[0xff];
    read_file_to_mem("lut_mul_0.bin", (void*)mul_lut, 256);

    // Load Input into SDRAM
    // Input
    read_file_to_mem("x_768.bin", (void*)src, 176*N);

    printf("W and X loaded.\n");

    // ---------- Load fixed params into NPU ----------
    time_us[0] = get_time_in_microseconds();

    npu_fetch(&npu, LAYERNORM, GAMMA_LO_OFFSET);
    npu_wait(&npu, FETCH_DONE);
            
    npu_fetch(&npu, LUT_0, LUT_SILU_OFFSET);
    npu_wait(&npu, FETCH_DONE);

    npu_fetch(&npu, LUT_1, LUT_SILU_OFFSET);
    npu_wait(&npu, FETCH_DONE);
    
    printf("LN, LUT0, LUT1 Fetch done.\n");
    
    // Load inputs into NPU
    npu_load(&npu, 0, 0, N);
    printf("Input Load done.\n");

    // Load STMM0 into NPU for the first time
    npu_fetch(&npu, STMM_0, W0_OFFSET);
    npu_wait(&npu, FETCH_DONE);
            
    npu_fetch(&npu, STMM_1, W1_OFFSET);
    npu_wait(&npu, FETCH_DONE);

    npu_fetch(&npu, STMM_2, W2_OFFSET);
    npu_wait(&npu, FETCH_DONE);
    
    npu_fetch(&npu, STMM_3, W3_OFFSET);
    npu_wait(&npu, FETCH_DONE);
    printf("first-time STMM0, STMM1, STMM2, STMM3 Fetch done.\n");    

    for (int i = 0; i < N; i++)
    {
        // Layer Norm
        npu_move(&npu, i, 0x210, false, false, 1);
        npu_wait(&npu, MOVE_DONE);
        npu_exec(&npu, LAYERNORM);
        npu_wait(&npu, EU_DONE(LAYERNORM));
        npu_move(&npu, 0x218, 167, false, false, 1);
        npu_wait(&npu, MOVE_DONE);

        // FC1
        npu_move(&npu, 167, 0x200, true, false, 4);
        npu_wait(&npu, MOVE_DONE); 

        npu_wait(&npu, FETCH_DONE);
        
        npu_exec(&npu, STMM_0);
        npu_exec(&npu, STMM_1);
        npu_exec(&npu, STMM_2);
        npu_exec(&npu, STMM_3);
        npu_wait(&npu, EU_DONE(STMM_0));
        npu_wait(&npu, EU_DONE(STMM_1));
        npu_wait(&npu, EU_DONE(STMM_2));
        npu_wait(&npu, EU_DONE(STMM_3));
        
        // Load FC2 weight into StMM
        npu_fetch(&npu, STMM_0, FC2_W0_OFFSET);
        npu_wait(&npu, FETCH_DONE);
        npu_fetch(&npu, STMM_1, FC2_W1_OFFSET);
        npu_wait(&npu, FETCH_DONE);
        npu_fetch(&npu, STMM_2, FC2_W2_OFFSET);
        npu_wait(&npu, FETCH_DONE);
        npu_fetch(&npu, STMM_3, FC2_W3_OFFSET);

        npu_move(&npu, 0x208, 167, false, false, 4);
        npu_wait(&npu, MOVE_DONE);


        // SILU LUT
        for (int j = 0; j < 2; j++)
        {
            npu_move(&npu, 167+j*2, 0x220, false, false, 2);
            npu_wait(&npu, MOVE_DONE);
            
            npu_exec(&npu, LUT_0);
            npu_exec(&npu, LUT_1);
            npu_wait(&npu, EU_DONE(LUT_0));
            npu_wait(&npu, EU_DONE(LUT_1));

            npu_move(&npu, 0x228, 167+j*2, false, false, 2);
            npu_wait(&npu, MOVE_DONE);
        }

        // FC2
        npu_move(&npu, 167, 0x200, false, false, 4);
        npu_wait(&npu, MOVE_DONE);

        npu_wait(&npu, FETCH_DONE);

        npu_exec(&npu, STMM_0);
        npu_exec(&npu, STMM_1);
        npu_exec(&npu, STMM_2);
        npu_exec(&npu, STMM_3);
        npu_wait(&npu, EU_DONE(STMM_0));
        npu_wait(&npu, EU_DONE(STMM_1));
        npu_wait(&npu, EU_DONE(STMM_2));
        npu_wait(&npu, EU_DONE(STMM_3));

        // Load FC1 param
        npu_fetch(&npu, STMM_0, W0_OFFSET);
        npu_wait(&npu, FETCH_DONE);
        npu_fetch(&npu, STMM_1, W1_OFFSET);
        npu_wait(&npu, FETCH_DONE);
        npu_fetch(&npu, STMM_2, W2_OFFSET);
        npu_wait(&npu, FETCH_DONE);
        npu_fetch(&npu, STMM_3, W3_OFFSET);
        
        npu_move(&npu, 0x208, 167, false, false, 4);
        npu_wait(&npu, MOVE_DONE);

        // Store 
        npu_store(&npu, 0x10000, 167, 4);
        npu_wait(&npu, LDST_DONE);

        // Sum MUl Sum
        int8_t tmp;
        for (int k = 0; k < 176; k++)
        {
            tmp = 0;
            for (int j = 0; j < 4; j++)
                tmp += dst[j*176+k];    
            tmp = mul_lut[(uint8_t)tmp];
            tmp = residual_sum(tmp, src[i*176+k], converted_sa, converted_sb, converted_z);
            final_output[i][k] = tmp;
        };
        time_us[i+1] = get_time_in_microseconds();
    }
    // long t1 = get_time_in_microseconds();


    printf("Check results\n");
    for (int i = 0; i < 176; i++)
    {
        printf("%d ", final_output[0][i]);
    }
    write_mem_to_file("fc_infer_output.bin", final_output, 176*N);

    printf("\nCheck done.\n");
    npu_deinit(&npu);
    printf("NPU deinit done.\n");

    // printf("Time: %ld us\n", t1 - t0);
    write_mem_to_file("fc_infer_time.bin", time_us, sizeof(time_us));

    return 0;
}
