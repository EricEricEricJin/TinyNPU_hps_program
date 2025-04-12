.PHONY: npu stmm layernorm lut fc

stmm:
	scp npu_api.h root@169.254.66.88:~/matmul_test/
	scp npu_api.c root@169.254.66.88:~/matmul_test/
	scp stmm_test.c root@169.254.66.88:~/matmul_test/
	
layernorm:
	scp npu_api.h root@169.254.66.88:~/layernorm_test/
	scp npu_api.c root@169.254.66.88:~/layernorm_test/
	scp layernorm_test.c root@169.254.66.88:~/layernorm_test/

lut:
	scp npu_api.h root@169.254.66.88:~/lut_test/
	scp npu_api.c root@169.254.66.88:~/lut_test/
	scp lut_test.c root@169.254.66.88:~/lut_test/

fc:
	scp npu_api.h root@169.254.66.88:~/fc_test/
	scp npu_api.c root@169.254.66.88:~/fc_test/
	scp fc_infer.c root@169.254.66.88:~/fc_test/
	