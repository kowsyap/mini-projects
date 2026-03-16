/******************************************************************************
 * VecAdd AXI DMA application
 *
 * Starts from the Xilinx simple interrupt DMA example, but drives the custom
 * vecadd AXI-Stream IP instead of a loopback path.
 ******************************************************************************/

#include "vecadd_dma_app.h"

#include "xparameters.h"
#include "xil_cache.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include "xscugic.h"

#define DMA_DEV_ID 0
#define INTC_DEVICE_ID 0
#define TX_INTR_ID XPAR_FABRIC_AXI_DMA_0_INTR
#define RX_INTR_ID XPAR_FABRIC_AXI_DMA_0_INTR_1


#ifdef XPAR_PS7_DDR_0_BASEADDRESS
#define DDR_BASE_ADDR XPAR_PS7_DDR_0_BASEADDRESS
#elif defined (XPAR_PSU_DDR_0_BASEADDR)
#define DDR_BASE_ADDR XPAR_PSU_DDR_0_S_AXI_BASEADDR
#endif

#ifndef DDR_BASE_ADDR
#warning CHECK FOR THE VALID DDR ADDRESS IN XPARAMETERS.H. DEFAULTING TO 0x01000000
#define MEM_BASE_ADDR 0x01000000
#else
#define MEM_BASE_ADDR (DDR_BASE_ADDR + 0x01000000)
#endif

#define TX_BUFFER_BASE (MEM_BASE_ADDR + 0x00100000)
#define RX_BUFFER_BASE (MEM_BASE_ADDR + 0x00300000)
#define RESET_TIMEOUT_COUNTER 10000U
#define NUMBER_OF_EVENTS 1

static XAxiDma AxiDma;
static XScuGic Intc;

static volatile u32 TxDone;
static volatile u32 RxDone;
static volatile u32 Error;

static void TxIntrHandler(void *Callback);
static void RxIntrHandler(void *Callback);
static int DmaInit(XAxiDma *AxiDmaPtr);
static int WaitForCompletion(void);
static int RunTestCase(XAxiDma *AxiDmaPtr, u32 test_index, const VecAddTestCase *test_case);
static void printInputArray(const u32 *data, u32 length);
static void printU64(u64 value);
static int SetupIntrSystem(XScuGic *IntcInstancePtr, XAxiDma *AxiDmaPtr, u16 TxIntrId, u16 RxIntrId);
static void DisableIntrSystem(XScuGic *IntcInstancePtr, u16 TxIntrId, u16 RxIntrId);

u64 calcReferenceSum(const u32 *data, u32 length)
{
    u32 i;
    u64 sum = 0;

    for (i = 0; i < length; ++i) {
        sum += data[i];
    }

    return sum;
}

int runVectorSum(XAxiDma *AxiDmaPtr, const u32 *data, u32 length, u64 *sum_out)
{
    u32 i;
    u32 *TxBufferPtr = (u32 *)TX_BUFFER_BASE;
    u32 *RxBufferPtr = (u32 *)RX_BUFFER_BASE;
    int Status;

    if ((data == NULL) || (sum_out == NULL) || (length == 0U) || (length > VECADD_MAX_INPUT_WORDS)) {
        return XST_INVALID_PARAM;
    }

    for (i = 0; i < length; ++i) {
        TxBufferPtr[i] = data[i];
    }
    RxBufferPtr[0] = 0U;
    RxBufferPtr[1] = 0U;

    Xil_DCacheFlushRange((UINTPTR)TxBufferPtr, length * sizeof(u32));
    Xil_DCacheFlushRange((UINTPTR)RxBufferPtr, VECADD_OUTPUT_BYTES);

    TxDone = 0U;
    RxDone = 0U;
    Error = 0U;

    Status = XAxiDma_SimpleTransfer(AxiDmaPtr, (UINTPTR)RxBufferPtr,
                                    VECADD_OUTPUT_BYTES, XAXIDMA_DEVICE_TO_DMA);
    if (Status != XST_SUCCESS) {
        xil_printf("1 TxDone=%u RxDone=%u Error=%u\r\n", TxDone, RxDone, Error);
        return Status;
    }

    Status = XAxiDma_SimpleTransfer(AxiDmaPtr, (UINTPTR)TxBufferPtr,
                                    length * sizeof(u32), XAXIDMA_DMA_TO_DEVICE);
    if (Status != XST_SUCCESS) {
        xil_printf("2 TxDone=%u RxDone=%u Error=%u\r\n", TxDone, RxDone, Error);
        return Status;
    }

    int timeout = VECADD_WAIT_LIMIT;
    while (XAxiDma_Busy(AxiDmaPtr, XAXIDMA_DMA_TO_DEVICE)) {
        if (--timeout == 0) {
            xil_printf("Timeout on TX channel\n");
            return XST_FAILURE;
        }
    }

    timeout = VECADD_WAIT_LIMIT;
    while (XAxiDma_Busy(AxiDmaPtr, XAXIDMA_DEVICE_TO_DMA)) {
        if (--timeout == 0) {
            xil_printf("Timeout on RX channel\n");
            return XST_FAILURE;
        }
    }

        // my interrupts are not working, it didnt work with timer ip too, i dont know why but I enabled interrupts in ps and connected both of them from dma to irq_f2p via concat

        // Status = Xil_WaitForEventSet(VECADD_WAIT_LIMIT, NUMBER_OF_EVENTS, &Error);
		// if (Status == XST_SUCCESS) {
		// 	if (!TxDone) {
		// 		xil_printf("Transmit error %d\r\n", Status);
		// 		return XST_FAILURE;
		// 	} else if (Status == XST_SUCCESS && !RxDone) {
		// 		xil_printf("Receive error %d\r\n", Status);
		// 		return XST_FAILURE;
		// 	}
		// }

		// Status = Xil_WaitForEventSet(VECADD_WAIT_LIMIT, NUMBER_OF_EVENTS, &TxDone);
		// if (Status != XST_SUCCESS) {
		// 	xil_printf("Transmit failed %d\r\n", Status);
		// 	return XST_FAILURE;

		// }

		// Status = Xil_WaitForEventSet(VECADD_WAIT_LIMIT, NUMBER_OF_EVENTS, &RxDone);
		// if (Status != XST_SUCCESS) {
		// 	xil_printf("Receive failed %d\r\n", Status);
		// 	return XST_FAILURE;
		// }


    Xil_DCacheInvalidateRange((UINTPTR)RxBufferPtr, VECADD_OUTPUT_BYTES);
    *sum_out = (((u64)RxBufferPtr[1]) << 32) | ((u64)RxBufferPtr[0]);

    return XST_SUCCESS;
}

int main(void)
{
    int Status;
    u32 i;
    const VecAddTestCase test_cases[] = {
        {vecadd_test0, sizeof(vecadd_test0) / sizeof(vecadd_test0[0])},
        {vecadd_test1, sizeof(vecadd_test1) / sizeof(vecadd_test1[0])},
        {vecadd_test2, sizeof(vecadd_test2) / sizeof(vecadd_test2[0])},
        {vecadd_test3, sizeof(vecadd_test3) / sizeof(vecadd_test3[0])}
    };

    xil_printf("--- Entering main() ---\r\n");

    Status = DmaInit(&AxiDma);
    if (Status != XST_SUCCESS) {
        xil_printf("DMA initialization failed: %d\r\n", Status);
        return XST_FAILURE;
    }

    for (i = 0; i < VECADD_TEST_CASE_COUNT; ++i) {
        Status = RunTestCase(&AxiDma, i, &test_cases[i]);
        if (Status != XST_SUCCESS) {
            DisableIntrSystem(&Intc, TX_INTR_ID, RX_INTR_ID);
            xil_printf("--- Exiting main() ---\r\n");
            return XST_FAILURE;
        }
    }

    DisableIntrSystem(&Intc, TX_INTR_ID, RX_INTR_ID);

    xil_printf("Successfully ran AXI DMA interrupt Example\r\n");
    xil_printf("--- Exiting main() ---\r\n");

    return XST_SUCCESS;
}

static int DmaInit(XAxiDma *AxiDmaPtr)
{
  int Status;
  XAxiDma_Config *Config;

  Config = XAxiDma_LookupConfig(DMA_DEV_ID);
  if (Config == NULL) {
    return XST_FAILURE;
  }

  Status = XAxiDma_CfgInitialize(AxiDmaPtr, Config);
  if (Status != XST_SUCCESS) {
    return Status;
  }

  if (XAxiDma_HasSg(AxiDmaPtr)) {
    xil_printf("AXI DMA is configured for scatter-gather mode\r\n");
    return XST_FAILURE;
  }

  Status = SetupIntrSystem(&Intc, AxiDmaPtr, TX_INTR_ID, RX_INTR_ID);
  if (Status != XST_SUCCESS) {
    return Status;
  }

  XAxiDma_IntrDisable(AxiDmaPtr, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
  XAxiDma_IntrDisable(AxiDmaPtr, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
  XAxiDma_IntrEnable(AxiDmaPtr, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
  XAxiDma_IntrEnable(AxiDmaPtr, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);

  return XST_SUCCESS;
}

static int WaitForCompletion(void)
{
  u32 timeout = VECADD_WAIT_LIMIT;

  while (timeout > 0U) {
    if (Error != 0U) {
      return XST_FAILURE;
    }
    if ((TxDone != 0U) && (RxDone != 0U)) {
      return XST_SUCCESS;
    }
    --timeout;
  }

  return XST_FAILURE;
}

static int RunTestCase(XAxiDma *AxiDmaPtr, u32 test_index, const VecAddTestCase *test_case)
{
    int Status;
    u64 hw_sum;
    u64 sw_sum;

    xil_printf("Starting test %u\r\n", (unsigned long)test_index);
    xil_printf("Input array:\r\n");
    printInputArray(test_case->data, test_case->length);

    sw_sum = calcReferenceSum(test_case->data, test_case->length);
    Status = runVectorSum(AxiDmaPtr, test_case->data, test_case->length, &hw_sum);
    if (Status != XST_SUCCESS) {
        xil_printf("Transfer failed for test %u\r\n", (unsigned long)test_index);
        return Status;
    }

    xil_printf("Hardware Sum: ");
    printU64(hw_sum);
    xil_printf("\r\n");

    xil_printf("Expected Sum: ");
    printU64(sw_sum);
    xil_printf("\r\n");

    if (hw_sum != sw_sum) {
        xil_printf("Test Failed!\r\n");
        return XST_FAILURE;
    }

    xil_printf("Test Passed!\r\n");
    xil_printf("Finished test %u\r\n", (unsigned long)test_index);

    return XST_SUCCESS;
}


static void printInputArray(const u32 *data, u32 length)
{
    u32 i;
    xil_printf("length: %u\n", length);
    for (i = 0; i < length; ++i) {
        xil_printf("%u, ",data[i]);
    }
    xil_printf("\r\n");
}

static void printU64(u64 value)
{
    char digits[21];
    int pos = 0;
    int i;

    if (value == 0U) {
        xil_printf("0");
        return;
    }

    while (value > 0U) {
        digits[pos] = (char)('0' + (value % 10U));
        value /= 10U;
        ++pos;
    }

    for (i = pos - 1; i >= 0; --i) {
        xil_printf("%c", digits[i]);
    }
}

static void TxIntrHandler(void *Callback)
{
  u32 IrqStatus;
  u32 TimeOut;
  XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

  IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DMA_TO_DEVICE);
  XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DMA_TO_DEVICE);

  if ((IrqStatus & XAXIDMA_IRQ_ALL_MASK) == 0U) {
    return;
  }

  if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK) != 0U) {
    Error = 1U;
    XAxiDma_Reset(AxiDmaInst);

    TimeOut = RESET_TIMEOUT_COUNTER;
    while (TimeOut > 0U) {
      if (XAxiDma_ResetIsDone(AxiDmaInst)) {
        break;
      }
      --TimeOut;
    }
    return;
  }

  if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK) != 0U) {
    TxDone = 1U;
  }
}

static void RxIntrHandler(void *Callback)
{
  u32 IrqStatus;
  u32 TimeOut;
  XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

  IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DEVICE_TO_DMA);
  XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DEVICE_TO_DMA);

  if ((IrqStatus & XAXIDMA_IRQ_ALL_MASK) == 0U) {
    return;
  }

  if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK) != 0U) {
    Error = 1U;
    XAxiDma_Reset(AxiDmaInst);

    TimeOut = RESET_TIMEOUT_COUNTER;
    while (TimeOut > 0U) {
      if (XAxiDma_ResetIsDone(AxiDmaInst)) {
        break;
      }
      --TimeOut;
    }
    return;
  }

  if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK) != 0U) {
    RxDone = 1U;
  }
}

static int SetupIntrSystem(XScuGic *IntcInstancePtr, XAxiDma *AxiDmaPtr, u16 TxIntrId, u16 RxIntrId)
{
  int Status;
  XScuGic_Config *IntcConfig;

  IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
  if (IntcConfig == NULL) {
    return XST_FAILURE;
  }

  Status = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig, IntcConfig->CpuBaseAddress);
  if (Status != XST_SUCCESS) {
    return XST_FAILURE;
  }

  XScuGic_SetPriorityTriggerType(IntcInstancePtr, TxIntrId, 0xA0, 0x3);
  XScuGic_SetPriorityTriggerType(IntcInstancePtr, RxIntrId, 0xA0, 0x3);

  Status = XScuGic_Connect(IntcInstancePtr, TxIntrId, (Xil_InterruptHandler)TxIntrHandler, AxiDmaPtr);
  if (Status != XST_SUCCESS) {
    return XST_FAILURE;
  }

  Status = XScuGic_Connect(IntcInstancePtr, RxIntrId, (Xil_InterruptHandler)RxIntrHandler, AxiDmaPtr);
  if (Status != XST_SUCCESS) {
    return XST_FAILURE;
  }

  XScuGic_Enable(IntcInstancePtr, TxIntrId);
  XScuGic_Enable(IntcInstancePtr, RxIntrId);

  Xil_ExceptionInit();
  Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                               (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                               (void *)IntcInstancePtr);
  Xil_ExceptionEnable();

  return XST_SUCCESS;
}

static void DisableIntrSystem(XScuGic *IntcInstancePtr, u16 TxIntrId, u16 RxIntrId)
{
  XScuGic_Disconnect(IntcInstancePtr, TxIntrId);
  XScuGic_Disconnect(IntcInstancePtr, RxIntrId);
}
