/*
Copyright (c) 2015 - 2021 Advanced Micro Devices, Inc. All rights reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/* HIT_START
 * BUILD: %t %s ../test_common.cpp LINK_OPTIONS -lrt
 * TEST: %t
 * HIT_END
 */

#include "test_common.h"
#include "MultiProcess.h"

void multi_process(int num_process, bool debug_process) {

#ifdef __unix__

  float *A_h, *B_h, *C_h;
  float *A_d, *B_d, *C_d;
  hipEvent_t start, stop;
  size_t Nbytes = N * sizeof(float);

  MultiProcess<hipIpcEventHandle_t>* mProcess = new MultiProcess<hipIpcEventHandle_t>(num_process);
  mProcess->CreateShmem();
  pid_t pid = mProcess->SpawnProcess(debug_process);

  // Parent Process
  if (pid != 0) {

    unsigned blocks = (N + threadsPerBlock - 1) / threadsPerBlock;
    if (blocks > 1024) blocks = 1024;
    if (blocks == 0) blocks = 1;

    printf("N=%zu (A+B+C= %6.1f MB total) blocks=%u threadsPerBlock=%u iterations=%d\n", N,
           ((double)3 * N * sizeof(float)) / 1024 / 1024, blocks, threadsPerBlock, iterations);
    printf("iterations=%d\n", iterations);

    HipTest::initArrays(&A_d, &B_d, &C_d, &A_h, &B_h, &C_h, N);

    // NULL stream check:
    HIPCHECK(hipEventCreateWithFlags(&start, hipEventDisableTiming|hipEventInterprocess));
    HIPCHECK(hipEventCreateWithFlags(&stop, hipEventDisableTiming|hipEventInterprocess));


    HIPCHECK(hipMemcpy(A_d, A_h, Nbytes, hipMemcpyHostToDevice));
    HIPCHECK(hipMemcpy(B_d, B_h, Nbytes, hipMemcpyHostToDevice));


    for (int i = 0; i < iterations; i++) {
      //--- START TIMED REGION
      long long hostStart = HipTest::get_time();
      // Record the start event
      HIPCHECK(hipEventRecord(start, NULL));

      hipLaunchKernelGGL(HipTest::vectorADD, dim3(blocks), dim3(threadsPerBlock), 0, 0,
                        static_cast<const float*>(A_d), static_cast<const float*>(B_d), C_d, N);


      HIPCHECK(hipEventRecord(stop, NULL));
      HIPCHECK(hipEventSynchronize(stop));
      HIPCHECK(hipEventQuery(stop));
      long long hostStop = HipTest::get_time();
      //--- STOP TIMED REGION

      float eventMs = 1.0f;
      // should fail
      HIPASSERT(hipSuccess != hipEventElapsedTime(&eventMs, start, stop));
      float hostMs = HipTest::elapsed_time(hostStart, hostStop);

      printf("host_time (gettimeofday)          =%6.3fms\n", hostMs);
      printf("kernel_time (hipEventElapsedTime) =%6.3fms\n", eventMs);
      printf("\n");

    }

    hipIpcEventHandle_t ipc_handle;
    HIPCHECK(hipIpcGetEventHandle(&ipc_handle, start));

    mProcess->WriteHandleToShmem(ipc_handle);
    mProcess->WaitTillAllChildReads();

  } else {
    hipEvent_t ipc_event;
    hipIpcEventHandle_t ipc_handle;
    mProcess->ReadHandleFromShmem(ipc_handle);
    HIPCHECK(hipIpcOpenEventHandle(&ipc_event, ipc_handle));

    HIPCHECK(hipEventSynchronize(ipc_event));
    HIPCHECK(hipEventDestroy(ipc_event));
    mProcess->NotifyParentDone();
  }

  if (pid != 0) {
    HIPCHECK(hipMemcpy(C_h, C_d, Nbytes, hipMemcpyDeviceToHost));
    printf("check:\n");
    HipTest::checkVectorADD(A_h, B_h, C_h, N, true);

    HIPCHECK(hipEventDestroy(start));
    HIPCHECK(hipEventDestroy(stop));
    delete mProcess;
  }

#endif /* __unix__ */

}

int main(int argc, char* argv[]) {
  HipTest::parseStandardArguments(argc, argv, true);
  multi_process((N < 64) ? N : 64, debug_test);
  passed();
}
