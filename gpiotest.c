#include "xparameters.h"
#include "xgpio.h"
#include "xil_exception.h"
#include "xscugic.h"
#include "xil_printf.h"
#include "xtmrctr.h"


#define GPIO_SW_DEVICE_ID XPAR_GPIO_0_DEVICE_ID
#define GPIO_LED_BTN_DEVICE_ID XPAR_GPIO_1_DEVICE_ID

#define GPIO_CHANNEL1		1
#define GPIO_CHANNEL2		2

// for led/btns interrupt
#define BTN_INTR_ID	XPAR_FABRIC_AXI_GPIO_1_IP2INTC_IRPT_INTR
#define TMR_INTR_ID XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR

#define TMR_DEVICE_ID XPAR_TMRCTR_0_DEVICE_ID
#define INTC_DEVICE_ID	XPAR_SCUGIC_SINGLE_DEVICE_ID


#define GPIO_ALL_LEDS		0xFFFF
#define GPIO_ALL_BUTTONS	0xFFFF

#define BUTTON_CHANNEL	 2
#define LED_CHANNEL	 1
#define SWITCH_CHANNEL 1

#define BUTTON_INTERRUPT XGPIO_IR_CH2_MASK  /* Channel 2 Interrupt Mask */

#define INTR_DELAY	0x00FFFFFF

XGpio GpioLB;
XGpio GpioSW;
XScuGic Intc;
XTmrCtr Timer;

static u16 GlobalIntrMask;
static volatile u32 BtnValue=0;
static volatile u8 BtnEvent=0;
static volatile u8 TickEvent=0;

static volatile u8 visible = 1;
static volatile u8 running = 0;
static volatile s8 dir = +1;
static volatile u8 pos = 0;

void GpioHandler(void *CallbackRef)
{
	XGpio *GpioPtr = (XGpio *)CallbackRef;
	BtnEvent = 1;
	BtnValue = XGpio_DiscreteRead(GpioPtr, BUTTON_CHANNEL) & 0xF;
	XGpio_InterruptClear(GpioPtr, GlobalIntrMask);
}

void TimerTickHook(void *CallBackRef, u8 TmrNum)
{
    (void)CallBackRef;
    (void)TmrNum;
    TickEvent = 1;
}

void GpioDisableIntr(XScuGic *IntcInstancePtr, XGpio *InstancePtr,
		     u16 IntrId, u16 IntrMask)
{
	XGpio_InterruptDisable(InstancePtr, IntrMask);
	XScuGic_Disable(IntcInstancePtr, IntrId);
	XScuGic_Disconnect(IntcInstancePtr, IntrId);
	return;
}

static inline u32 onehot(u8 p) { return (1u << (p & 3)); }

/* Map switch value 0..15 => timer reset value (bigger=slower). Tune if needed. */
static u32 speed_count_to_reset(u8 s)
{
    const u32 slow = 40 * 1000 * 1000; // slowest
    const u32 fast =  2.5 * 1000 * 1000; // fastest
    if (s > 15) s = 15;
    return slow - ((slow - fast) * s) / 15;
}


int GpioSetupIntrSystem(XScuGic *IntcInstancePtr, XGpio *InstancePtr,
			u16 DeviceId, u16 IntrId, u16 IntrMask)
{
	int Result;
	GlobalIntrMask = IntrMask;
	XScuGic_Config *IntcConfig;

	IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (NULL == IntcConfig) {
		return XST_FAILURE;
	}

	Result = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig,
				       IntcConfig->CpuBaseAddress);
	if (Result != XST_SUCCESS) {
		return XST_FAILURE;
	}

	XScuGic_SetPriorityTriggerType(IntcInstancePtr, IntrId,
				       0xA0, 0x3);

	Result = XScuGic_Connect(IntcInstancePtr, IntrId,
				 (Xil_InterruptHandler)GpioHandler, InstancePtr);
	if (Result != XST_SUCCESS) {
		return Result;
	}

	XScuGic_Enable(IntcInstancePtr, IntrId);

	XGpio_InterruptClear(InstancePtr, IntrMask);
	XGpio_InterruptEnable(InstancePtr, IntrMask);
	XGpio_InterruptGlobalEnable(InstancePtr);

	Xil_ExceptionInit();

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler, IntcInstancePtr);

	Xil_ExceptionEnable();

	return XST_SUCCESS;
}

int SetupTimerInterrupt(XScuGic *IntcInstancePtr, XTmrCtr *TmrInstancePtr, u16 DeviceId,  u16 TimerIntrId)
{
	int Status;
	Status = XTmrCtr_Initialize(TmrInstancePtr, DeviceId);
	if (Status != XST_SUCCESS) return XST_FAILURE;

	XTmrCtr_SetHandler(TmrInstancePtr, TimerTickHook, TmrInstancePtr);
	XTmrCtr_SetOptions(TmrInstancePtr, 0, XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION | XTC_DOWN_COUNT_OPTION);
    XScuGic_SetPriorityTriggerType(IntcInstancePtr, TimerIntrId, 0xB0, 0x1);

    Status = XScuGic_Connect(IntcInstancePtr, TimerIntrId,
        (Xil_InterruptHandler)XTmrCtr_InterruptHandler, TmrInstancePtr);
    if (Status != XST_SUCCESS) return XST_FAILURE;

    XScuGic_Enable(IntcInstancePtr, TimerIntrId);
    return XST_SUCCESS;
}

int main(void)
{
	int Status, Status2, Status3, Status4;
	u32 DataRead;

	Status = XGpio_Initialize(&GpioLB, GPIO_LED_BTN_DEVICE_ID);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	Status2 = XGpio_Initialize(&GpioSW, GPIO_SW_DEVICE_ID);
	if (Status2 != XST_SUCCESS) {
		return XST_FAILURE;
	}

	XGpio_SetDataDirection(&GpioLB, LED_CHANNEL, 0x0);
	XGpio_SetDataDirection(&GpioLB, BUTTON_CHANNEL, 0xF);
	XGpio_SetDataDirection(&GpioSW, SWITCH_CHANNEL, 0xF);
	XGpio_DiscreteWrite(&GpioLB, LED_CHANNEL, 0x0);

	Status3 = GpioSetupIntrSystem(&Intc, &GpioLB, GPIO_LED_BTN_DEVICE_ID,
			BTN_INTR_ID, BUTTON_INTERRUPT);
	if (Status3 != XST_SUCCESS) {
		return XST_FAILURE;
	}

	Status4 = SetupTimerInterrupt(&Intc, &Timer, TMR_DEVICE_ID, TMR_INTR_ID);
	if (Status4 != XST_SUCCESS) {
		return XST_FAILURE;
	}

	u8 sw = (u8)(XGpio_DiscreteRead(&GpioSW, SWITCH_CHANNEL) & 0xF);
	u32 reset = speed_count_to_reset(sw);
	XTmrCtr_SetResetValue(&Timer, 0, reset);
	XTmrCtr_Start(&Timer, 0);
	u8 last_speed = sw;
	xil_printf("Speed set to level %d \r\n", sw);

	while(1){
		sw = (u8)(XGpio_DiscreteRead(&GpioSW, SWITCH_CHANNEL) & 0xF);
		if (sw != last_speed) {
			last_speed = sw;
			u32 new_reset = speed_count_to_reset(sw);
			XTmrCtr_SetResetValue(&Timer, 0, new_reset);
			XTmrCtr_Reset(&Timer, 0);
			xil_printf("Speed set to level %d \r\n", sw);
		}

		if (BtnEvent) {
			BtnEvent = 0;

			if (BtnValue & 0x1) {
				visible ^= 1;
				if (visible == 1){
					xil_printf("BTN0: visible\r\n");
				} else{
					xil_printf("BTN0: Invisible\r\n");
				}
			}
			if (BtnValue & 0x2) {
				running = 1;
				xil_printf("BTN1: start\r\n");
			}
			if (BtnValue & 0x4) {
				running = 0;
				xil_printf("BTN2: stop\r\n");
			}
			if (BtnValue & 0x8) {
				dir = (dir > 0) ? -1 : +1;
				xil_printf("BTN3: reverse\r\n");
			}
		}

		/* Handle timer tick */
		if (TickEvent) {
			TickEvent = 0;

			if (running) {
				if (dir > 0) pos = (pos + 1) & 3;
				else         pos = (pos + 3) & 3;
			}

			u32 out = 0;
			if (visible) out = 1u << (pos & 3);
			XGpio_DiscreteWrite(&GpioLB, LED_CHANNEL, out);
		}
	}
}

