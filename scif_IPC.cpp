/*
This file holds code for benchmarking latencies and throughput for a prototype of 'n' processes
performing performing IPC writes across cores on the same scif node via the scif RMA/RMM. The 
sending processes write data to the appropriate offset on the registered memory on the receiver 
daemon. Each process measures its transfer latency and throughput. 
*/

using namespace std;
#include <iostream>
#include <omp.h>
#include <stdlib.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include "timer.cpp"
#include <scif.h>
#include <string.h>
#include <cstdio>
#include <sys/wait.h>
#include <sys/mman.h>

#define SRC_REG_OFFSET 0x100000;  //Registered offset of src_buf on the sender
#define STOP_OFFSET 0x81000;  //Registered offset for the stop indicator
#define TGT_REG_OFFSET 0x100000000;  //Registered offset for recv_space on the receiver daemon.
#define RECV_BUF_SIZE_MB 32;  //size of receive buffer for each proc in MB

int proc_set_affinity(int proc_num, pid_t pid);
void proc_get_affinity(int proc_num, pid_t pid, int NUM_PROCS);
void check_errno(int err_no);

int main (int argc, char* argv[])
{
  pid_t pid;
  int ret;
  int pagesize = sysconf(_SC_PAGESIZE);
  char file_path[40] = "/home/prabhashankar/work/";  //default file path
  if (argc < 5) {
    cerr<<"Format ./lauch_benchmark.out <NUM_PROCS> <msg_size> <iterations> <rma_type(m=scif_mmap/a=scif_writeto/v=scif_vwriteto)> <file_path(optional)>\n";
    return 1;
  }
  int NUM_PROCS = atoi(argv[1]);
  size_t msg_size = atoi(argv[2]);
  int iter = atoi(argv[3]);
  char rma_type = argv[4][0];
  if (argc == 6) {
	  strcpy(file_path, argv[5]);
  }
  off_t src_reg_offset = SRC_REG_OFFSET;
  off_t tgt_reg_offset = TGT_REG_OFFSET;
  off_t stop_offset = STOP_OFFSET;
  int recv_buf_size_MB = RECV_BUF_SIZE_MB;
  int recv_buf_size_bytes = recv_buf_size_MB * 1024 * 1024;
  int recv_space_bytes = recv_buf_size_MB * 1024 * 1024 * NUM_PROCS;
  //Making sure the size of registered memory is a multiple of page size..ie 4 KB
  size_t register_len = (msg_size % 4096 == 0? msg_size : ((msg_size / 4096) + 1) * 4096);
  uint16_t self;
  uint16_t nodes[32];
  //fn returns number of SCIF nodes in nw "NUM_NODES", nodes numbers in "nodes" and calling node id in "self"
  int NUM_NODES = scif_get_nodeIDs(nodes, 32, &self);
  int NO_VALS = msg_size / sizeof(double);  //Number of double precision values being written
  //array holding stop indicators corresponding to each sending/receiving process
  //the stop indicators are set by each process once it has completed sending/receving
  int *stop_array=(int*)mmap(NULL, NUM_PROCS*sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  //variable proc_num holds the process number of the forked process
  int proc_num = 0;
  //loop to fork NUM_PROCS number of processes
  for (; proc_num <= NUM_PROCS - 1 ; proc_num++) {
    pid = fork();
    if (pid == 0) {
      break; //each forked child breaks off from the loop
    }
    else if (pid == -1) {
      cerr<<"Fork failed\n";
      return 1;
    }
  }
  int TOT_NUM_PROCS = sysconf(_SC_NPROCESSORS_CONF);
  //fn to set affinity for each process
  if (proc_set_affinity((proc_num * 4 + 1) % TOT_NUM_PROCS, pid)  == -1)  //Setting affinity
    return 1;
  //fn to check affinity set
  proc_get_affinity(proc_num, pid, NUM_PROCS);
  //"proc_num"s > 0 are used as senders
  if (proc_num > 0) {
    int tries;
    scif_portID portid_send,portid_rcv;
    //Assigning scif portid <3000+proc_num> to connecting end point to IPC receiver daemon
    portid_send.port = 3000+proc_num;
    portid_send.node = self;
    //...and port 3000 for listening port on the IPC receiver daemon
    portid_rcv.port = 3000;
    portid_rcv.node = self;
    double begin_time_init = timerval();  //initialization start time of transfer registered
    double *src_buf;  //buffer to hold data to be sent
    ret = posix_memalign((void**) & src_buf, pagesize, NO_VALS*sizeof(double));
    if (ret) {
      cerr <<"Node :"<<self<<" Proc :"<<proc_num<<" could not allocate src_buf\n";
      return 1;
    }
    scif_epd_t ep_send = scif_open(); //create end point to connect to IPC receiver daemon
    if (ep_send == SCIF_OPEN_FAILED) {
      cerr << "Node :"<<self<<" Proc :"<<proc_num<<" scif open failed\n";
      return 1;
    }
    //Binding end point to port
    errno = 0;
    if (scif_bind(ep_send, portid_send.port) < 0) {
        cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" End point bind failed\n";
        check_errno(errno);
        return 1;
    }
    errno = 0;
    tries = 5;  //number of re-attempts
    retry:
    //Sends connection request to listening port on the receiver
    if (scif_connect(ep_send, &portid_rcv) < 0) {
      cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" Sending connection request failed....tries left : "<<tries<<endl;
      check_errno(errno);
      //In case receiver end point has not been set to listen connection is re-attempted
      //after stipulated time
      if (tries-- > 0) {
        usleep(2 * 100000);
        goto retry;
      }
      return 1;
    }
    //Waits for IPC receiver daemon to register memory send status
    int ready = 0;
    while (ready != 1) {
      if (scif_recv(ep_send, &ready, sizeof(int), SCIF_RECV_BLOCK) < 0) {
        cerr <<"Node :"<<self<<" Proc :"<<proc_num<<" Recv error of ready\n";
        return 1;
      }
    }
    //pointer to be mapped to the stop indicator on the local receiver daemon
    int volatile *stop_local;
    //mapping it to registered memory offset of the stop indicator on the local receiver daemon
    if((stop_local = (int*)scif_mmap(0, pagesize, SCIF_PROT_READ | SCIF_PROT_WRITE, 0, ep_send, stop_offset)) == SCIF_MMAP_FAILED) {
      cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" Error during scif_mmap for stop_local :"<<errno<<endl;
      check_errno(errno);
      return 1;
    }
    double *src_mapped_buf;
    //if the rma_type is 'm' then create mapping to registered offset of recv_space on the remote receiver daemon
    if (rma_type == 'm') {
      if((src_mapped_buf = (double*)scif_mmap(0, recv_space_bytes, SCIF_PROT_READ | SCIF_PROT_WRITE, 0, ep_send, tgt_reg_offset)) == SCIF_MMAP_FAILED) {
        cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" Error during scif_mmap :"<<errno<<endl;
        check_errno(errno);
        return 1;
      }
    }
    //if rma_type is 'a' then register src_buffer as required by scif_write
    if (rma_type == 'a') {
      if (SCIF_REGISTER_FAILED == scif_register(ep_send, src_buf, register_len, src_reg_offset, SCIF_PROT_READ | SCIF_PROT_WRITE, SCIF_MAP_FIXED)) {
        cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" scif register failed\n";
        check_errno(errno);
        return 1;
      }
    }
    double end_time_init = timerval(); //Register end of initialization
    //initializing source buffer
    for(int i=0 ; i<NO_VALS ; i++) {
      src_buf[i] = i + proc_num;
    }
    double begin_time_tx = timerval(); //Register start of transfer time
    //if sending process then perform transfer "iter" number of times. 
    switch (rma_type) {
      case 'm':
        for (int i = 1 ; i <= iter ; i++) {
          memcpy(src_mapped_buf + (proc_num - 1) * recv_buf_size_bytes / sizeof(double), src_buf, NO_VALS * sizeof(double));
        }
        break;
      case 'a':
        for (int i = 1 ; i <= iter ; i++) {
          if(scif_writeto(ep_send, src_reg_offset, sizeof(double) * NO_VALS, tgt_reg_offset + (proc_num - 1) * recv_buf_size_bytes, SCIF_RMA_SYNC) < 0) {
            cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" Error while writing :"<<errno<<endl;
            check_errno(errno);
            return 1;
          }
        }
        break;
      case 'v':
        for (int i = 1 ; i <= iter ; i++) {
          if(scif_vwriteto(ep_send, (void*)src_buf, sizeof(double) * NO_VALS, tgt_reg_offset + (proc_num - 1) * recv_buf_size_bytes, SCIF_RMA_SYNC) < 0) {
            cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" Error while writing :"<<errno<<endl;
            check_errno(errno);
            return 1;
          }
        }
        break;
    }
    double end_time_tx = timerval(); //Register end of transfer
    stop_array[proc_num - 1] = 1;  //Setting the stop indicator corresponding to the process
    int stop_sum = 0;
    for (int i = 0 ; i < NUM_PROCS ; i++) {
      stop_sum += stop_array[i];
    }
    //if stop indicators of all procs are set then increment stop indicators in local and remote daemons
    if (stop_sum == NUM_PROCS) {
      (*stop_local)++;
    }
    if (rma_type == 'm') {
      if (scif_munmap(src_mapped_buf, recv_space_bytes) < 0) {
        cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" scif_munmap failed\n";
        return 1;
      }
    }
    if (rma_type == 'a') {
      if (scif_unregister(ep_send, src_reg_offset, register_len) < 0) {
        cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" scif_unregister failed\n";
        check_errno(errno);
        return 1;
      }
    }
    if (scif_munmap((void*)stop_local, pagesize) < 0) {
      cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" scif_munmap failed for stop_local\n";
      return 1;
    }
    //Waits to receive confirmation from local daemon that memory has been unregistered
    //before closing end point
    int rcv_unreg_status = 0;
    while (rcv_unreg_status != 1) {
      if(scif_recv(ep_send, &rcv_unreg_status, sizeof(int), SCIF_RECV_BLOCK)  < 0) {
        cerr<<"Node :"<<self<<" Proc :"<<proc_num<<" Rcv error of IPC rcv_unreg_status\n";
        return 1;
      }
    }
    //Calculating latency in us
    double latency_tx=(end_time_tx - begin_time_tx) * 1000000 / iter;
    double latency_init=(end_time_init - begin_time_init) * 1000000;
    //Calculating throughput in MBps
    double throughput=double(NO_VALS * sizeof(double) * 1000000) / ((latency_tx) * double(1024 * 1024));
    cout<<"Node :"<<self<<" Proc :"<<proc_num<<" Initialization Latency, Transfer Latency, Throughput : "<<latency_init<<" us, "<<latency_tx<<" us, "<<throughput<<" MBps"<<endl;
    scif_close(ep_send);
    if (pid != 0) {
      for (proc_num = 0 ; proc_num <= NUM_PROCS-1 ; proc_num++) {
        wait(NULL);
      }
    }
    free(src_buf);
  }
  //proc_num = 0 is used as receiver daemon
  if (proc_num == 0) {
    double *recv_space;  //pointer to total receiver space for all procs
    int ret = posix_memalign((void**) &recv_space, pagesize, recv_space_bytes);
    if (ret) {
      cerr << "Node :"<<self<<" Receiver : could not allocate recv_space\n";
      return 1;
    }
    int volatile *stop;  //stop indicator
    ret = posix_memalign((void**) &stop, pagesize, sizeof(int));
    if (ret) {
      cerr << "Node :"<<self<<" Receiver : could not allocate STOP\n";
      return 1;
    }
    *stop = 0;
    scif_portID portid_listen_IPC;
    //Assigning port 3000 to listen for local end point connection requests
    portid_listen_IPC.port = 3000;
    portid_listen_IPC.node = self;
    //to hold portids local connecting end points
    scif_portID *portid_send_IPC = new scif_portID[NUM_PROCS];
    //to assign as local end points when local connection requests are accepted
    scif_epd_t *ep_rcv_IPC = new scif_epd_t[NUM_PROCS];
    //create end point to listen for local connection requests
    scif_epd_t ep_listen_IPC = scif_open();
    if (ep_listen_IPC == SCIF_OPEN_FAILED) {
      cerr <<"Node :"<<self<<" Receiver : scif open failed\n";
      return 1;
    }
    errno = 0;
    //Binding end point to port
    if (scif_bind(ep_listen_IPC, portid_listen_IPC.port) < 0) {
      cerr<<"Node :"<<self<<" Receiver : End point bind failed for IPC\n";
      check_errno(errno);
      exit(1);
    }
    //Setting end point on remote node to listen to incoming connections
    if (scif_listen(ep_listen_IPC, NUM_PROCS) < 0) {
      cerr<<"Node :"<<self<<" Receiver : Setting IPC end point to listen failed\n";
      exit(1);
    }
    for (int i = 0 ; i < NUM_PROCS ; i++) {
      //Accept incoming local conection request
      if(scif_accept(ep_listen_IPC, &portid_send_IPC[i], &ep_rcv_IPC[i], SCIF_ACCEPT_SYNC) < 0) {
    	cerr<<"Node :"<<self<<" Receiver : Accepting connection request failed for IPC proc :"<<i+1<<endl;
    	exit(1);
      }
    }
    for (int i = 0; i < NUM_PROCS ; i++) {
    //Register memory on the remote node to be visible to the scif_driver...the size of memory
    //registered has to be a multiple of page size
      if (SCIF_REGISTER_FAILED == scif_register(ep_rcv_IPC[i], recv_space, recv_space_bytes, tgt_reg_offset, SCIF_PROT_READ | SCIF_PROT_WRITE, SCIF_MAP_FIXED)) {
        cerr<<"Node :"<<self<<" Receiver : Error :"<<errno<<endl;
        cerr<<"Node :"<<self<<" Receiver : scif register failed for IPC proc :"<<i+1<<endl;
        check_errno(errno);
        exit(1);
      }
      //Registering stop so that local connected procs be memory mapped to it
      if (SCIF_REGISTER_FAILED == scif_register(ep_rcv_IPC[i], (void*)stop, pagesize,stop_offset, SCIF_PROT_READ | SCIF_PROT_WRITE, SCIF_MAP_FIXED)) {
        cerr<<"Node :"<<self<<" Receiver : Error :"<<errno<<endl;
        cerr<<"Node :"<<self<<" Receiver : scif register STOP failed for IPC proc :"<<i+1<<endl;
        check_errno(errno);
        exit(1);
      }
      int ready=1;
      //Indicate to local connected processes that memory has been registered
      if(scif_send(ep_rcv_IPC[i], &ready, sizeof(int), SCIF_SEND_BLOCK) < 0) {
        cerr <<"Node :"<<self<<" Receiver : Send error of ready for IPC proc :"<<i+1<<endl;
        exit(1);
      }
    }
    //loop for receiving data...only stops when local procs have completed
    while (*stop != 1);
    //Code block to check written data...un-comment block to print
/*
    for (int proc = 1 ; proc <= NUM_PROCS ; proc++) {
      double *recv_buf = recv_space + (proc - 1) * recv_buf_size_bytes / sizeof(double);
      cout<<" Proc "<<proc<<" Received vals :";
      for (int j = 0 ; j < NO_VALS ; j++) {
        cout<<*(recv_buf + j)<<" ";
      }
      cout<<endl;
    }
*/
    for (int i = 0 ; i < NUM_PROCS ; i++) {
      //Once RMA transaction has been completed, memory is unregistered
      scif_unregister(ep_rcv_IPC[i], tgt_reg_offset, recv_space_bytes);
      scif_unregister(ep_rcv_IPC[i], stop_offset, pagesize);
      //Intimating that memory has been unregistered
      int rcv_unreg_status = 1;
      if (scif_send(ep_rcv_IPC[i], &rcv_unreg_status, sizeof(int), SCIF_SEND_BLOCK) < 0) {
        cerr<<"Node :"<<self<<" Receiver : Send error of rcv_unreg_status for IPC proc "<<i+1<<endl;
        exit(1);
      }
      scif_close(ep_rcv_IPC[i]);
    }
    free((void*)stop);
    free(recv_space);
    delete[] ep_rcv_IPC;
    delete[] portid_send_IPC;
    scif_close(ep_listen_IPC);
    return 0;
  }
  munmap(stop_array, sizeof(int) * NUM_PROCS);
  return 0;
}

void check_errno(int err_no) {
  switch (err_no) {
    case EBADF: cerr<<"EBADF"<<endl;break;
    case ECONNRESET: cerr<<"ECONNRESET"<<endl;break;
    case EFAULT: cerr<<"EFAULT"<<endl;break;
    case ENXIO: cerr<<"ENXIO"<<endl;break;
    case EINVAL: cerr<<"EINVAL"<<endl;break;
    case ENODEV: cerr<<"ENODEV"<<endl;break;
    case ENOTCONN: cerr<<"ENOTCONN"<<endl;break;
    case ENOTTY: cerr<<"ENOTTY"<<endl;break;
    case ECONNREFUSED: cerr<<"ECONNREFUSED"<<endl;break;
    case EINTR: cerr<<"EINTR"<<endl;break;
    case EISCONN: cerr<<"EISCONN"<<endl;break;
    case ENOBUFS: cerr<<"ENOBUFS"<<endl;break;
    case ENOSPC: cerr<<"ENOSPC"<<endl;break;
    case EOPNOTSUPP: cerr<<"EOPNOTSUPP"<<endl;break;
    case ENOMEM: cerr<<"ENOMEM"<<endl;break;
    case EACCES: cerr<<"EACCES"<<endl;break;
    case EADDRINUSE: cerr<<"EADDRINUSE"<<endl;break;
    case EAGAIN: cerr<<"EAGAIN"<<endl;break;
    default: cerr<<"errno not resolved\n";
  }
}

int proc_set_affinity(int proc_num, pid_t pid) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(proc_num, &mask);
  errno=0;
  if(sched_setaffinity(pid, sizeof(mask), &mask) == -1) {
    cerr<<"Setting affinity failed for proc :"<<proc_num<<endl;
    switch(errno) {
      case ESRCH: cerr<<"ESRCH\n";break;
      case EFAULT: cerr<<"EFAULT\n";break;
      case EINVAL: cerr<<"EINVAL\n";break;
      default: cerr<<"Errno not resolved\n";
    }
    return -1;
  }
  return 0;
}

void proc_get_affinity(int proc_num, pid_t pid, int NUM_PROCS)
{
  cpu_set_t mycpuid;
  sched_getaffinity(pid, sizeof(mycpuid), &mycpuid);
  int TOT_NUM_PROCS = sysconf(_SC_NPROCESSORS_CONF);
  for (int cpu_num = 1 ; cpu_num <= TOT_NUM_PROCS ; cpu_num++) {
    if (CPU_ISSET(cpu_num, &mycpuid)) {
      cout<<"Affinity set for proc "<<proc_num<<" :"<<cpu_num<<endl;
    }
  }
}
