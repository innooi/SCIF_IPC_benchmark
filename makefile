CChost = icc
CCmic = icc -mmic
LFLAGS = -lscif
RECEIVER = scif_rcv_procs.out
SENDER = launch_benchmark.out
SCRIPT = scif_multiproc.cpp
HOST = 0
MIC0 = 1
ITER ?= 10000
NUM_PROCS ?= 4
MSG_SIZE ?= 1024
OP_TYPE ?= w
RMA_TYPE ?= m
MAX_MSG_SIZE ?= 1048576
FILE_PATH ?= /home/prabhashankar/work/
host: receiver_mic sender_host 
mic: receiver_host sender_mic 
sender_host:
	$(CChost) -o $(SENDER) $(SCRIPT) $(LFLAGS)		
receiver_mic:
	$(CCmic) -o $(RECEIVER) $(SCRIPT) $(LFLAGS)		
sender_mic:
	$(CCmic) -o $(SENDER) $(SCRIPT) $(LFLAGS)
receiver_host:
	$(CChost) -o $(RECEIVER) $(SCRIPT) $(LFLAGS)
run_host: TARGET_NODE = $(MIC0)
run_host: clean host 
	$(FILE_PATH)/launch_benchmark.out $(NUM_PROCS) $(MSG_SIZE) $(TARGET_NODE) $(OP_TYPE) $(ITER) $(RMA_TYPE) $(FILE_PATH)
run_mic: TARGET_NODE = $(HOST)
run_mic: clean mic 
	 ssh mic0 $(FILE_PATH)/launch_benchmark.out $(NUM_PROCS) $(MSG_SIZE) $(TARGET_NODE) $(OP_TYPE) $(ITER) $(RMA_TYPE) $(FILE_PATH)
clean:
	rm -f *o *.out
getvals_host: TARGET_NODE = $(MIC0)
getvals_host: clean host
	sh $(FILE_PATH)/get_vals.sh $(NUM_PROCS) $(MAX_MSG_SIZE) $(TARGET_NODE) $(OP_TYPE) $(ITER) $(RMA_TYPE) $(FILE_PATH)
getvals_mic: TARGET_NODE = $(HOST)
getvals_mic: clean mic
	ssh mic0 sh $(FILE_PATH)/get_vals.sh $(NUM_PROCS) $(MAX_MSG_SIZE) $(TARGET_NODE) $(OP_TYPE) $(ITER) $(RMA_TYPE) $(FILE_PATH)