
/***************************** Include Files *********************************/

#include "xparameters.h"
#include "xtmrctr.h"
#include "xil_printf.h"
#include <arm_neon.h>
#include <stdio.h>
#include <stdlib.h>

/************************** Constant Definitions *****************************/

/*
 * The following constants map to the XPAR parameters created in the
 * xparameters.h file. They are only defined here such that a user can easily
 * change all the needed parameters in one place.
 */
#define TMRCTR_DEVICE_ID	0


/*
 * This example only uses the 1st of the 2 timer counters contained in a
 * single timer counter hardware device
 */
#define TIMER_COUNTER_0	 0
#define MAXRAND 8 // maximum size of a random number in a floating point matrix element
#define N 64 // size of matrix will be N x N, where N in {4, 8, 16, 32, 48, 64}
#define BS 16

/**************************** Type Definitions *******************************/



/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/

void classicMatMult(float [], float [], float []);
float checkCorrect(float [], float []);
void neonMatMult(float [], float [], float []);

/************************** Variable Definitions *****************************/

XTmrCtr TimerCounter; /* The instance of the Tmrctr Device */
float A[N*N], B[N*N], C[N*N], D[N*N], E[N*N] = {0};

int main(void)
{

/* Generate pseudorandom arrays */
    
	xil_printf("Matrix size: %d x %d\n\r",N,N);
	for (int i = 0; i < N*N; i++){
		A[i] = (float)((rand()%MAXRAND)-(rand()%MAXRAND));
		B[i] = (float)((rand()%MAXRAND)-(rand()%MAXRAND));
	}

	int Status;

	/*
	 * Initialize the timer counter so that it's ready to use,
	 * specify the device ID that is generated in xparameters.h
	 */
	Status = XTmrCtr_Initialize(&TimerCounter, TMRCTR_DEVICE_ID);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Perform a self-test to ensure that the hardware was built
	 * correctly, use the 1st timer in the device (0)
	 */
	Status = XTmrCtr_SelfTest(&TimerCounter, TIMER_COUNTER_0);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Enable the Autoreload mode of the timer counters.
	 */
	XTmrCtr_SetOptions(&TimerCounter, TIMER_COUNTER_0,
				XTC_AUTO_RELOAD_OPTION);

	/*
	 * Get a snapshot of the timer counter value before it's started
	 * to compare against later
	 */
	u32 Value1 = XTmrCtr_GetValue(&TimerCounter, TIMER_COUNTER_0);

	xil_printf("Starting timer value: %d\n\r",Value1);

	/* Time critical region for classic matrix multiplication */
	XTmrCtr_Start(&TimerCounter, TIMER_COUNTER_0);

	classicMatMult(A, B, C);

	XTmrCtr_Stop(&TimerCounter, TIMER_COUNTER_0);
	/* End time critical region for classic matrix multiplication */

	u32 Value2 = XTmrCtr_GetValue(&TimerCounter, TIMER_COUNTER_0);
	xil_printf("Classic Multiplication Timer: %d \n\r",Value2);

    if (N<=8){
    	xil_printf("Classic Multiplication Results:\n\r");
    	//output
    	for (int i = 0; i < N; i++){
    		for (int j = 0; j < N; j++)
    			xil_printf("%d \t",(int)C[i*N+j]);
    		xil_printf("\n\r");
    	}
    }

    XTmrCtr_Reset(&TimerCounter, TIMER_COUNTER_0);
	/* Time critical region for Neon matrix multiplication */

    XTmrCtr_Start(&TimerCounter, TIMER_COUNTER_0);

	neonMatMult(A, B, D);
	
    XTmrCtr_Stop(&TimerCounter, TIMER_COUNTER_0);
	/* End time critical region for Neon matrix multiplication */

    Value2 = XTmrCtr_GetValue(&TimerCounter, TIMER_COUNTER_0);
	xil_printf("NEON Multiplier Timer: %d \n\r",Value2);

    if (N<=8){
    	xil_printf("NEON Multiplier output\n\r");

    	//output
    	for (int i = 0; i < N; i++){
    		for (int j = 0; j < N; j++)
    			xil_printf("%d \t",(int)D[i*N+j]);
    		xil_printf("\n\r");
     	}
    }

/* Verify correct output */

    xil_printf("Error percent: %d \n\r\n\n",(int)checkCorrect(C, D));
    return XST_SUCCESS;

}

void classicMatMult(float A[], float B[], float C[]){
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            for (int k = 0; k < N; k++)
                C[i*N+j] = C[i*N+j] + A[i*N+k]*B[k*N+j];

}

void neonMatMult(float A[], float B[], float D[])
{
    for (int ii = 0; ii < N; ii += BS) {
        for (int jj = 0; jj < N; jj += BS) {
            for (int i = ii; i < ii + BS && i < N; i++) {

                for (int j = jj; j < jj + BS && j < N; j += 4) {
                    float32x4_t acc = vdupq_n_f32(0.0f);
                    for (int kk = 0; kk < N; kk += BS) {
                        for (int k = kk; k < kk + BS && k < N; k++) {
                            float32x4_t a = vdupq_n_f32(A[i*N + k]);
                            float32x4_t b = vld1q_f32(&B[k*N + j]);
                            acc = vmlaq_f32(acc, a, b);
                        }
                    }

                    vst1q_f32(&D[i*N + j], acc);
                }
            }
        }
    }
}



float checkCorrect(float A[], float B[]){
	int result = 0;
	float cumerror = 0;
	int errorcntr = 0;
	for (int i = 0; i < N*N; i++)
		if (B[i]!= 0){
			cumerror += abs((A[i]-B[i])/B[i]);
			errorcntr +=1;
		}
		
	return cumerror*100/errorcntr;
}

