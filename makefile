CChost = icc
CCmic = icc -mmic
LFLAGS = -lscif
RECEIVER_HOST = scif_rcv_procs_host.out
RECEIVER_MIC = scif_rcv_procs_mic.out
SENDER = launch_benchmark.out
SCRIPT = scif_multiproc.cpp
HOST = 0
MIC0 = 1
MIC1 = 2
MIC2 = 3
MIC3 = 4
OP_TYPE ?= w

# Update the following default parameter values as desired

# name (not scif_id) of mic to run benchmark
MIC ?= mic0

# number of iterations
ITER ?= 10000

# number of processes to be forked
NUM_PROCS ?= 4

# msg_size to be used when running run_host or run_mic
MSG_SIZE ?= 1024

# RMA_TYPE: m (scif_mmap) / a (scif_writeto) / v (scif_vwriteto)
RMA_TYPE ?= m

# max size to be used when running getvals_host or getvals_mic
MAX_MSG_SIZE ?= 1048576

# comlete path of the folder where files are copied
FILE_PATH ?= /home/prabhashankar/work/

host: receiver_mic sender_host 
mic: receiver_host receiver_mic sender_mic 
sender_host:
	$(CChost) -o $(SENDER) $(SCRIPT) $(LFLAGS)		
receiver_mic:
	$(CCmic) -o $(RECEIVER_MIC) $(SCRIPT) $(LFLAGS)		
sender_mic:
	$(CCmic) -o $(SENDER) $(SCRIPT) $(LFLAGS)
receiver_host:
	$(CChost) -o $(RECEIVER_HOST) $(SCRIPT) $(LFLAGS)
run_host: TARGET_NODE ?= $(MIC0)
run_host: clean host 
	$(FILE_PATH)/launch_benchmark.out $(NUM_PROCS) $(MSG_SIZE) $(TARGET_NODE) $(OP_TYPE) $(ITER) $(RMA_TYPE) $(FILE_PATH)
run_mic: TARGET_NODE ?= $(HOST)
run_mic: clean mic 
	ssh $(MIC) $(FILE_PATH)/launch_benchmark.out $(NUM_PROCS) $(MSG_SIZE) $(TARGET_NODE) $(OP_TYPE) $(ITER) $(RMA_TYPE) $(FILE_PATH)
clean:
	rm -f *o *.out
getvals_host: TARGET_NODE ?= $(MIC0)
getvals_host: clean host
	sh $(FILE_PATH)/get_vals.sh $(NUM_PROCS) $(MAX_MSG_SIZE) $(TARGET_NODE) $(OP_TYPE) $(ITER) $(RMA_TYPE) $(FILE_PATH)
getvals_mic: TARGET_NODE ?= $(HOST)
getvals_mic: clean mic
	ssh $(MIC) sh $(FILE_PATH)/get_vals.sh $(NUM_PROCS) $(MAX_MSG_SIZE) $(TARGET_NODE) $(OP_TYPE) $(ITER) $(RMA_TYPE) $(FILE_PATH)