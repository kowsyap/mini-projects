#include "xaxidma.h"
#include "xparameters.h"
#include "xil_exception.h"
#include "xdebug.h"
#include "xil_util.h"
#include "xinterrupt_wrap.h"
#include "xuartps_hw.h"
#include "xuartps.h"
#include "mayo_verify_support.h"
#include <string.h>

/******************** Constant Definitions **********************************/

#define MEM_BASE_ADDR		0x01000000

#define RX_BD_SPACE_BASE	(MEM_BASE_ADDR)
#define RX_BD_SPACE_HIGH	(MEM_BASE_ADDR + 0x0000FFFF)
#define TX_BD_SPACE_BASE	(MEM_BASE_ADDR + 0x00010000)
#define TX_BD_SPACE_HIGH	(MEM_BASE_ADDR + 0x0001FFFF)
#define TX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00100000)
#define RX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00300000)
#define RX_BUFFER_HIGH		(MEM_BASE_ADDR + 0x004FFFFF)

#define MAYO_ACCEL_BASEADDR  XPAR_MAYO_VERIFY_ACCELERATOR_0_BASEADDR

#define RESET_TIMEOUT_COUNTER	10000

#define MAX_PKT_LEN		256

#define POLL_TIMEOUT_COUNTER	1000000U
#define NUMBER_OF_EVENTS	1

#define COALESCING_COUNT	1
#define DELAY_TIMER_COUNT	100

#define ACCEL_CTRL_STATUS_OFFSET 0x00U
#define CTRL_IDLE                0x00000000U
#define CTRL_SIG_DEC             0x00000001U
#define CTRL_EPK                 0x00000002U
#define CTRL_QA                  0x00000004U
#define STATUS_SIG_DONE          0x00000001U
#define STATUS_EPK_DONE          0x00000002U
#define STATUS_QA_DONE           0x00000004U

#define MAYO_OUTPUT_BYTES        32U

/* ---- Round selection ---- */
#define MAYO_ROUND  1

#if MAYO_ROUND == 1
  #define MAYO_PARAMS   MAYO_R1_2
#elif MAYO_ROUND == 2
  #define MAYO_PARAMS   MAYO_R2_2
#else
  #error "MAYO_ROUND must be 1 or 2"
#endif

#define MSG_BUF_MAX  4096

/* UART result bytes sent back to Python host */
#define UART_PASS    0x01
#define UART_FAIL    0x00

#ifndef DEBUG
extern void xil_printf(const char *format, ...);
#endif

/**************************** Type Definitions *******************************/


/************************** Function Prototypes ******************************/
static inline void accel_write_ctrl(u32 value);
static inline u32 accel_read_status(void);

static int  VerifyData(const unsigned char *t_expected);
static void TxCallBack(XAxiDma_BdRing *TxRingPtr);
static void TxIntrHandler(void *Callback);
static void RxCallBack(XAxiDma_BdRing *RxRingPtr);
static void RxIntrHandler(void *Callback);

static int RxSetup(XAxiDma *AxiDmaInstPtr);
static int TxSetup(XAxiDma *AxiDmaInstPtr);
static int SendData(XAxiDma *AxiDmaInstPtr, const void *data, int total_bytes);
static int DmaSetup(XAxiDma *AxiDmaInstPtr);
static int RxPrepare(int total_bytes);
static int bd_count_for(int total_bytes);

/************************** Variable Definitions *****************************/

static XAxiDma       AxiDma;
static XAxiDma_BdRing *TxRingPtr;
static XAxiDma_BdRing *RxRingPtr;
static XAxiDma_Config *Config;

static XUartPs        myUart;

volatile u32 TxDone;
volatile u32 RxDone;
volatile u32 Error;

/* Static data buffers in cached DDR */
static unsigned char cpk[MAYO_MAX_cpk_bytes];
static unsigned char epk[MAYO_MAX_epk_bytes];
static unsigned char sig_buf[MAYO_MAX_sig_bytes];
static unsigned char msg_buf[MSG_BUF_MAX];
static unsigned char t_vec[MAYO_MAX_m_bytes];

/************************** UART Helpers *************************************/

static void uart_send_byte(u8 b)
{
	while (!XUartPs_IsTransmitEmpty(&myUart)) { }
	XUartPs_Send(&myUart, &b, 1);
}

static void uart_send_str(const char *s)
{
	while (*s)
		uart_send_byte((u8)(*s++));
}

/*
 * Receive exactly `len` bytes into buf (blocking).
 */
static void uart_recv_buf(XUartPs_Config *cfg, u8 *buf, unsigned int len)
{
	unsigned int i;
	for (i = 0; i < len; i++)
		buf[i] = XUartPs_RecvByte(cfg->BaseAddress);
}

/*
 * Receive a length-prefixed field: [2-byte big-endian length][payload].
 * Returns number of bytes received, or 0 if the field exceeds buf_max.
 * A length of 0 from the host signals end-of-cases.
 */
static unsigned int uart_recv_field(XUartPs_Config *cfg,
                                    u8 *buf, unsigned int buf_max)
{
	u8 hdr[2];
	unsigned int field_len;

	uart_recv_buf(cfg, hdr, 2);
	field_len = ((unsigned int)hdr[0] << 8) | hdr[1];

	if (field_len == 0)
		return 0;  /* sentinel: host signals no more cases */

	if (field_len > buf_max) {
		xil_printf("ERROR: field too large (%u > %u)\r\n", field_len, buf_max);
		return 0;
	}

	uart_recv_buf(cfg, buf, field_len);
	return field_len;
}

/***************************** Main Function *********************************/

int main(void)
{
	XUartPs_Config *myUartConfig;
	int Status;
	int msg_len;
	unsigned int cpk_len, sig_len;
	int num_bds;
	int case_num = 0;

	xil_printf("\r\n--- Entering main() ---\r\n");
	xil_printf("MAYO Round %d\r\n", MAYO_ROUND);

	/* ---- UART init (same pattern as serialtest_2025.c) ---- */
	myUartConfig = XUartPs_LookupConfig(XPAR_UART1_BASEADDR);
	XUartPs_CfgInitialize(&myUart, myUartConfig, myUartConfig->BaseAddress);
	XUartPs_EnableUart(&myUart);

	/* ---- DMA + interrupt setup ---- */
	Status = DmaSetup(&AxiDma);
	if (Status != XST_SUCCESS) {
		xil_printf("DMA setup failed\r\n");
		return XST_FAILURE;
	}

	/* ---- Sync: send newline so Python host unblocks ---- */
	uart_send_str("UART Synchronized\n");

	/* ---- Test case loop: receive until host sends zero-length MSG ---- */
	while (1) {

		/* Receive MSG */
		msg_len = (int)uart_recv_field(myUartConfig, msg_buf, sizeof(msg_buf));
		if (msg_len == 0)
			break;  /* host signalled end of cases */

		/* Receive CPK */
		cpk_len = uart_recv_field(myUartConfig, cpk, sizeof(cpk));
		if (cpk_len == 0) { uart_send_byte(UART_FAIL); break; }

		/* Receive SIG */
		sig_len = uart_recv_field(myUartConfig, sig_buf, sizeof(sig_buf));
		if (sig_len == 0) { uart_send_byte(UART_FAIL); break; }

		case_num++;
		xil_printf("\r\n=== Case %d ===\r\n", case_num);
		xil_printf("MSG %d B  CPK %u B  SIG %u B\r\n", msg_len, cpk_len, sig_len);

		/* ---- SW: expand CPK and derive T ---- */
		mayo_expand_pk(&MAYO_PARAMS, cpk, epk);
		deriveT(&MAYO_PARAMS, msg_buf, (unsigned long long)msg_len, sig_buf, t_vec);

		/* Prepare RX for QA output before any transfers */
		Status = RxPrepare(MAYO_OUTPUT_BYTES);
		if (Status != XST_SUCCESS) {
			xil_printf("RxPrepare failed\r\n");
			uart_send_byte(UART_FAIL);
			goto Done;
		}

		/* ---- Phase 1: SIG_DEC ---- */
		accel_write_ctrl(CTRL_SIG_DEC);

		TxDone = 0; Error = 0;
		num_bds = SendData(&AxiDma, sig_buf, MAYO_PARAMS.sig_bytes - MAYO_PARAMS.salt_bytes);
		if (num_bds < 0) {
			xil_printf("SendData failed (SIG_DEC)\r\n");
			uart_send_byte(UART_FAIL);
			goto Done;
		}

		Status = Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &Error);
		if (Status == XST_SUCCESS) {
			if (!TxDone) {
				xil_printf("DMA error (SIG_DEC)\r\n");
				uart_send_byte(UART_FAIL);
				goto Done;
			}
		}

		Status = Xil_WaitForEvent((UINTPTR)&TxDone, num_bds, num_bds, POLL_TIMEOUT_COUNTER);
		if (Status != XST_SUCCESS) {
			xil_printf("TX timeout (SIG_DEC)\r\n");
			uart_send_byte(UART_FAIL);
			goto Done;
		}

		while (!(accel_read_status() & STATUS_SIG_DONE));

		/* ---- Phase 2: EPK ---- */
		accel_write_ctrl(CTRL_EPK);

		TxDone = 0; Error = 0;
		num_bds = SendData(&AxiDma, epk, MAYO_PARAMS.epk_bytes);
		if (num_bds < 0) {
			xil_printf("SendData failed (EPK)\r\n");
			uart_send_byte(UART_FAIL);
			goto Done;
		}

		Status = Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &Error);
		if (Status == XST_SUCCESS) {
			if (!TxDone) {
				xil_printf("DMA error (EPK)\r\n");
				uart_send_byte(UART_FAIL);
				goto Done;
			}
		}

		Status = Xil_WaitForEvent((UINTPTR)&TxDone, num_bds, num_bds, POLL_TIMEOUT_COUNTER);
		if (Status != XST_SUCCESS) {
			xil_printf("TX timeout (EPK)\r\n");
			uart_send_byte(UART_FAIL);
			goto Done;
		}

		while (!(accel_read_status() & STATUS_EPK_DONE));

		/* ---- Phase 3: QA ---- */
		accel_write_ctrl(CTRL_QA);

		RxDone = 0; Error = 0;
		num_bds = bd_count_for(MAYO_OUTPUT_BYTES);

		Status = Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &Error);
		if (Status == XST_SUCCESS) {
			if (!RxDone) {
				xil_printf("DMA error (QA)\r\n");
				uart_send_byte(UART_FAIL);
				goto Done;
			}
		}

		Status = Xil_WaitForEvent((UINTPTR)&RxDone, num_bds, num_bds, POLL_TIMEOUT_COUNTER);
		if (Status != XST_SUCCESS) {
			xil_printf("RX timeout (QA)\r\n");
			uart_send_byte(UART_FAIL);
			goto Done;
		}

		while (!(accel_read_status() & STATUS_QA_DONE));

		accel_write_ctrl(CTRL_IDLE);

		/* ---- Compare HW output vs SW T, send result to host ---- */
		Status = VerifyData(t_vec);
		uart_send_byte((Status == XST_SUCCESS) ? UART_PASS : UART_FAIL);
	}

	xil_printf("\r\nAll cases done (%d total)\r\n", case_num);

Done:
	XDisconnectInterruptCntrl(Config->IntrId[0], Config->IntrParent);
	XDisconnectInterruptCntrl(Config->IntrId[1], Config->IntrParent);
	xil_printf("--- Exiting main() ---\r\n");
	return XST_SUCCESS;
}

/************************** Accelerator Helpers ******************************/

static inline void accel_write_ctrl(u32 value)
{
	Xil_Out32(MAYO_ACCEL_BASEADDR + ACCEL_CTRL_STATUS_OFFSET, value);
}

static inline u32 accel_read_status(void)
{
	return Xil_In32(MAYO_ACCEL_BASEADDR + ACCEL_CTRL_STATUS_OFFSET);
}

/************************** VerifyData ****************************************/

static int VerifyData(const unsigned char *t_expected)
{
	unsigned char *rx = (unsigned char *)RX_BUFFER_BASE;

	Xil_DCacheInvalidateRange((UINTPTR)rx, MAYO_OUTPUT_BYTES);

	int pass = (memcmp(rx, t_expected, MAYO_OUTPUT_BYTES) == 0);
	xil_printf("Result: %s\r\n", pass ? "PASS" : "FAIL");

	if (!pass) {
		xil_printf("  HW : ");
		for (int i = 0; i < (int)MAYO_OUTPUT_BYTES; i++) xil_printf("%02x", rx[i]);
		xil_printf("\r\n  SW : ");
		for (int i = 0; i < (int)MAYO_OUTPUT_BYTES; i++) xil_printf("%02x", t_expected[i]);
		xil_printf("\r\n");
	}

	return pass ? XST_SUCCESS : XST_FAILURE;
}

/************************** TX Interrupt Handlers ****************************/

static void TxCallBack(XAxiDma_BdRing *TxRingPtr)
{
	int BdCount;
	u32 BdSts;
	XAxiDma_Bd *BdPtr;
	XAxiDma_Bd *BdCurPtr;
	int Status;
	int Index;

	BdCount = XAxiDma_BdRingFromHw(TxRingPtr, XAXIDMA_ALL_BDS, &BdPtr);

	BdCurPtr = BdPtr;
	for (Index = 0; Index < BdCount; Index++) {
		BdSts = XAxiDma_BdGetSts(BdCurPtr);
		if ((BdSts & XAXIDMA_BD_STS_ALL_ERR_MASK) ||
		    (!(BdSts & XAXIDMA_BD_STS_COMPLETE_MASK))) {
			Error = 1;
			break;
		}
		BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(TxRingPtr, BdCurPtr);
	}

	Status = XAxiDma_BdRingFree(TxRingPtr, BdCount, BdPtr);
	if (Status != XST_SUCCESS) {
		Error = 1;
	}

	if (!Error) {
		TxDone += BdCount;
	}
}

static void TxIntrHandler(void *Callback)
{
	XAxiDma_BdRing *TxRingPtr = (XAxiDma_BdRing *)Callback;
	u32 IrqStatus;
	int TimeOut;

	IrqStatus = XAxiDma_BdRingGetIrq(TxRingPtr);
	XAxiDma_BdRingAckIrq(TxRingPtr, IrqStatus);

	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK))
		return;

	if (IrqStatus & XAXIDMA_IRQ_ERROR_MASK) {
		XAxiDma_BdRingDumpRegs(TxRingPtr);
		Error = 1;
		XAxiDma_Reset(&AxiDma);
		TimeOut = RESET_TIMEOUT_COUNTER;
		while (TimeOut) {
			if (XAxiDma_ResetIsDone(&AxiDma)) break;
			TimeOut -= 1;
		}
		return;
	}

	if (IrqStatus & (XAXIDMA_IRQ_DELAY_MASK | XAXIDMA_IRQ_IOC_MASK))
		TxCallBack(TxRingPtr);
}

/************************** RX Interrupt Handlers ****************************/

static void RxCallBack(XAxiDma_BdRing *RxRingPtr)
{
	int BdCount;
	XAxiDma_Bd *BdPtr;
	XAxiDma_Bd *BdCurPtr;
	u32 BdSts;
	int Index;

	BdCount = XAxiDma_BdRingFromHw(RxRingPtr, XAXIDMA_ALL_BDS, &BdPtr);

	BdCurPtr = BdPtr;
	for (Index = 0; Index < BdCount; Index++) {
		BdSts = XAxiDma_BdGetSts(BdCurPtr);
		if ((BdSts & XAXIDMA_BD_STS_ALL_ERR_MASK) ||
		    (!(BdSts & XAXIDMA_BD_STS_COMPLETE_MASK))) {
			Error = 1;
			break;
		}
		BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(RxRingPtr, BdCurPtr);
		RxDone += 1;
	}

	XAxiDma_BdRingFree(RxRingPtr, BdCount, BdPtr);
}

static void RxIntrHandler(void *Callback)
{
	XAxiDma_BdRing *RxRingPtr = (XAxiDma_BdRing *)Callback;
	u32 IrqStatus;
	int TimeOut;

	IrqStatus = XAxiDma_BdRingGetIrq(RxRingPtr);
	XAxiDma_BdRingAckIrq(RxRingPtr, IrqStatus);

	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK))
		return;

	if (IrqStatus & XAXIDMA_IRQ_ERROR_MASK) {
		XAxiDma_BdRingDumpRegs(RxRingPtr);
		Error = 1;
		XAxiDma_Reset(&AxiDma);
		TimeOut = RESET_TIMEOUT_COUNTER;
		while (TimeOut) {
			if (XAxiDma_ResetIsDone(&AxiDma)) break;
			TimeOut -= 1;
		}
		return;
	}

	if (IrqStatus & (XAXIDMA_IRQ_DELAY_MASK | XAXIDMA_IRQ_IOC_MASK))
		RxCallBack(RxRingPtr);
}

/************************** Ring Setup ***************************************/

static int RxSetup(XAxiDma *AxiDmaInstPtr)
{
	int Status;
	XAxiDma_Bd BdTemplate;
	int BdCount;

	RxRingPtr = XAxiDma_GetRxRing(&AxiDma);

	XAxiDma_BdRingIntDisable(RxRingPtr, XAXIDMA_IRQ_ALL_MASK);

	BdCount = XAxiDma_BdRingCntCalc(XAXIDMA_BD_MINIMUM_ALIGNMENT,
	                                 RX_BD_SPACE_HIGH - RX_BD_SPACE_BASE + 1);

	Status = XAxiDma_BdRingCreate(RxRingPtr, RX_BD_SPACE_BASE, RX_BD_SPACE_BASE,
	                              XAXIDMA_BD_MINIMUM_ALIGNMENT, BdCount);
	if (Status != XST_SUCCESS) {
		xil_printf("Rx bd create failed with %d\r\n", Status);
		return XST_FAILURE;
	}

	XAxiDma_BdClear(&BdTemplate);
	Status = XAxiDma_BdRingClone(RxRingPtr, &BdTemplate);
	if (Status != XST_SUCCESS) {
		xil_printf("Rx bd clone failed with %d\r\n", Status);
		return XST_FAILURE;
	}

	Status = XAxiDma_BdRingSetCoalesce(RxRingPtr, COALESCING_COUNT, DELAY_TIMER_COUNT);
	if (Status != XST_SUCCESS) {
		xil_printf("Rx set coalesce failed with %d\r\n", Status);
		return XST_FAILURE;
	}

	Status = RxPrepare(MAYO_OUTPUT_BYTES);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	XAxiDma_BdRingIntEnable(RxRingPtr, XAXIDMA_IRQ_ALL_MASK);

	Status = XAxiDma_BdRingStart(RxRingPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Rx start BD ring failed with %d\r\n", Status);
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

static int TxSetup(XAxiDma *AxiDmaInstPtr)
{
	XAxiDma_BdRing *TxRingPtr = XAxiDma_GetTxRing(&AxiDma);
	XAxiDma_Bd BdTemplate;
	int Status;
	u32 BdCount;

	XAxiDma_BdRingIntDisable(TxRingPtr, XAXIDMA_IRQ_ALL_MASK);

	BdCount = XAxiDma_BdRingCntCalc(XAXIDMA_BD_MINIMUM_ALIGNMENT,
	                                 (UINTPTR)TX_BD_SPACE_HIGH - (UINTPTR)TX_BD_SPACE_BASE + 1);

	Status = XAxiDma_BdRingCreate(TxRingPtr, TX_BD_SPACE_BASE, TX_BD_SPACE_BASE,
	                              XAXIDMA_BD_MINIMUM_ALIGNMENT, BdCount);
	if (Status != XST_SUCCESS) {
		xil_printf("Failed create BD ring\r\n");
		return XST_FAILURE;
	}

	XAxiDma_BdClear(&BdTemplate);
	Status = XAxiDma_BdRingClone(TxRingPtr, &BdTemplate);
	if (Status != XST_SUCCESS) {
		xil_printf("Failed clone BDs\r\n");
		return XST_FAILURE;
	}

	Status = XAxiDma_BdRingSetCoalesce(TxRingPtr, COALESCING_COUNT, DELAY_TIMER_COUNT);
	if (Status != XST_SUCCESS) {
		xil_printf("Failed set coalescing %d/%d\r\n", COALESCING_COUNT, DELAY_TIMER_COUNT);
		return XST_FAILURE;
	}

	XAxiDma_BdRingIntEnable(TxRingPtr, XAXIDMA_IRQ_ALL_MASK);

	Status = XAxiDma_BdRingStart(TxRingPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Failed bd start\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

/************************** BD Helpers ***************************************/

static int bd_count_for(int total_bytes)
{
	return (total_bytes + MAX_PKT_LEN - 1) / MAX_PKT_LEN;
}

static int RxPrepare(int total_bytes)
{
	XAxiDma_Bd *BdPtr;
	XAxiDma_Bd *BdCurPtr;
	int num_bds;
	UINTPTR RxBufferPtr;
	int Status;
	int Index;

	num_bds = bd_count_for(total_bytes);

	Xil_DCacheInvalidateRange(RX_BUFFER_BASE, total_bytes);

	Status = XAxiDma_BdRingAlloc(RxRingPtr, num_bds, &BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("RxPrepare: bd alloc failed\r\n");
		return XST_FAILURE;
	}

	BdCurPtr = BdPtr;
	RxBufferPtr = RX_BUFFER_BASE;
	for (Index = 0; Index < num_bds; Index++) {
		int len = (Index == num_bds - 1 && total_bytes % MAX_PKT_LEN != 0)
		          ? total_bytes % MAX_PKT_LEN
		          : MAX_PKT_LEN;

		Status = XAxiDma_BdSetBufAddr(BdCurPtr, RxBufferPtr);
		if (Status != XST_SUCCESS) {
			xil_printf("RxPrepare: set buf addr failed\r\n");
			return XST_FAILURE;
		}
		Status = XAxiDma_BdSetLength(BdCurPtr, len, RxRingPtr->MaxTransferLen);
		if (Status != XST_SUCCESS) {
			xil_printf("RxPrepare: set length failed\r\n");
			return XST_FAILURE;
		}
		XAxiDma_BdSetCtrl(BdCurPtr, 0);
		XAxiDma_BdSetId(BdCurPtr, RxBufferPtr);
		RxBufferPtr += len;
		BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(RxRingPtr, BdCurPtr);
	}

	Status = XAxiDma_BdRingToHw(RxRingPtr, num_bds, BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("RxPrepare: ToHw failed\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

static int SendData(XAxiDma *AxiDmaInstPtr, const void *data, int total_bytes)
{
	XAxiDma_BdRing *TxRingPtr = XAxiDma_GetTxRing(AxiDmaInstPtr);
	XAxiDma_Bd *BdPtr, *BdCurPtr;
	int Status;
	int Index;
	int num_bds = bd_count_for(total_bytes);
	UINTPTR BufferAddr = TX_BUFFER_BASE;

	memcpy((void *)TX_BUFFER_BASE, data, total_bytes);
	Xil_DCacheFlushRange(TX_BUFFER_BASE, total_bytes);

	Status = XAxiDma_BdRingAlloc(TxRingPtr, num_bds, &BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("SendData: bd alloc failed\r\n");
		return -1;
	}

	BdCurPtr = BdPtr;
	for (Index = 0; Index < num_bds; Index++) {
		int len = (Index == num_bds - 1 && total_bytes % MAX_PKT_LEN != 0)
		          ? total_bytes % MAX_PKT_LEN
		          : MAX_PKT_LEN;
		u32 CrBits = 0;

		Status = XAxiDma_BdSetBufAddr(BdCurPtr, BufferAddr);
		if (Status != XST_SUCCESS) {
			xil_printf("SendData: set buf addr failed\r\n");
			return -1;
		}
		Status = XAxiDma_BdSetLength(BdCurPtr, len, TxRingPtr->MaxTransferLen);
		if (Status != XST_SUCCESS) {
			xil_printf("SendData: set length failed\r\n");
			return -1;
		}

		if (Index == 0)
			CrBits |= XAXIDMA_BD_CTRL_TXSOF_MASK;
		if (Index == num_bds - 1)
			CrBits |= XAXIDMA_BD_CTRL_TXEOF_MASK;

		XAxiDma_BdSetCtrl(BdCurPtr, CrBits);
		XAxiDma_BdSetId(BdCurPtr, BufferAddr);

		BufferAddr += len;
		BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(TxRingPtr, BdCurPtr);
	}

	Status = XAxiDma_BdRingToHw(TxRingPtr, num_bds, BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("SendData: ToHw failed\r\n");
		return -1;
	}

	return num_bds;
}
