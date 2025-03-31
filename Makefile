.PHONY: stmm
stmm:
	scp npu_api.h root@169.254.66.88:~/matmul_test/
	scp npu_api.c root@169.254.66.88:~/matmul_test/
	scp stmm_test.c root@169.254.66.88:~/matmul_test/
	
