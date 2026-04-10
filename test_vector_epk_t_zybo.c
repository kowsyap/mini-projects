/* test_vector_epk_t_zybo.c
 * Zybo Z7-10 — receives CPK, MSG, SIG over UART from the Python host,
 * computes EPK and T, prints T and first 64 bytes of EPK via xil_printf,
 * then sends back 0xCC (11001100) as a 1-byte result.
 *
 * Wire protocol (matches kat_uart_transfer.py):
 *   Zybo -> Host : "MAYO Zybo ready\n"     (sync, host waits for \n)
 *   Host -> Zybo : [2-byte big-endian len][MSG bytes]
 *   Host -> Zybo : [2-byte big-endian len][CPK bytes]
 *   Host -> Zybo : [2-byte big-endian len][SIG bytes]
 *   Zybo -> Host : 0xCC                    (result byte, 11001100)
 */

/***************************** Include Files *********************************/

#include "xparameters.h"
#include "xdebug.h"
#include "xuartps_hw.h"
#include "xuartps.h"
#include "xil_printf.h"
#include "../mayo_verify_support.h"

#ifndef DEBUG
extern void xil_printf(const char *format, ...);
#endif

/************************** Variable Definitions *****************************/

static XUartPs myUart;

/* Static buffers — EPK (up to 106 KB) must not be on the stack */
static unsigned char cpk[MAYO_MAX_cpk_bytes];
static unsigned char msg[256];
static unsigned char sig[MAYO_MAX_sig_bytes];
static unsigned char epk[MAYO_MAX_epk_bytes];
static unsigned char t[MAYO_MAX_m_bytes];

#define EPK_PRINT_BYTES  64
#define RESULT_BYTE      0xCC   /* 11001100 */

/************************** Helper Functions *********************************/

/* Send a single byte, polling until the TX FIFO has space */
static void uart_send_byte(u8 b)
{
    while (!XUartPs_IsTransmitEmpty(&myUart)) { }
    XUartPs_Send(&myUart, &b, 1);
}

/* Send a buffer byte-by-byte */
static void uart_send_buf(const u8 *buf, unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++)
        uart_send_byte(buf[i]);
}

/* Send a null-terminated sync/status string ending with \n */
static void uart_send_str(const char *s)
{
    while (*s)
        uart_send_byte((u8)(*s++));
}

/*
 * Receive exactly `len` bytes into `buf` using RecvByte (blocking).
 */
static void uart_recv_buf(XUartPs_Config *cfg, u8 *buf, unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++)
        buf[i] = XUartPs_RecvByte(cfg->BaseAddress);
}

/*
 * Receive a 2-byte big-endian length prefix then that many payload bytes.
 * Returns the number of bytes received, or 0 if the field exceeds buf_max.
 */
static unsigned int recv_field(XUartPs_Config *cfg,
                               u8 *buf, unsigned int buf_max)
{
    u8 hdr[2];
    unsigned int field_len;

    uart_recv_buf(cfg, hdr, 2);
    field_len = ((unsigned int)hdr[0] << 8) | hdr[1];

    if (field_len > buf_max) {
        xil_printf("ERROR: field too large (%u > %u)\r\n", field_len, buf_max);
        return 0;
    }

    uart_recv_buf(cfg, buf, field_len);
    return field_len;
}

/* Print a hex dump via xil_printf */
static void print_hex(const char *label, const unsigned char *buf, int len)
{
    int i;
    xil_printf("%s (%d bytes):\r\n", label, len);
    for (i = 0; i < len; i++) {
        xil_printf("%02X", buf[i]);
        if ((i + 1) % 16 == 0)
            xil_printf("\r\n");
    }
    if (len % 16 != 0)
        xil_printf("\r\n");
}

/************************** Main *********************************************/

int main(void)
{
    XUartPs_Config *myUartConfig;
    unsigned int msg_len, cpk_len, sig_len;
    int ret;

    /* ---- UART init (same pattern as serialtest_2025.c) ---- */
    myUartConfig = XUartPs_LookupConfig(XPAR_UART1_BASEADDR);
    XUartPs_CfgInitialize(&myUart, myUartConfig, myUartConfig->BaseAddress);
    XUartPs_EnableUart(&myUart);

    /* ---- Sync: send newline-terminated string so Python unblocks ---- */
    uart_send_str("MAYO Zybo ready\n");

    /* ---- Receive MSG ---- */
    xil_printf("Waiting for MSG...\r\n");
    msg_len = recv_field(myUartConfig, msg, sizeof(msg));
    if (msg_len == 0) { uart_send_byte(0x00); return 1; }
    xil_printf("MSG received: %u bytes\r\n", msg_len);

    /* ---- Receive CPK ---- */
    xil_printf("Waiting for CPK...\r\n");
    cpk_len = recv_field(myUartConfig, cpk, sizeof(cpk));
    if (cpk_len == 0) { uart_send_byte(0x00); return 1; }
    xil_printf("CPK received: %u bytes\r\n", cpk_len);

    /* ---- Receive SIG ---- */
    xil_printf("Waiting for SIG...\r\n");
    sig_len = recv_field(myUartConfig, sig, sizeof(sig));
    if (sig_len == 0) { uart_send_byte(0x00); return 1; }
    xil_printf("SIG received: %u bytes\r\n", sig_len);

    /* ---- Expand CPK -> EPK ---- */
    ret = mayo_expand_pk(&MAYO_R1_2, cpk, epk);
    if (ret != 0) {
        xil_printf("ERROR: mayo_expand_pk failed (%d)\r\n", ret);
        uart_send_byte(0x00);
        return 1;
    }
    xil_printf("mayo_expand_pk: OK\r\n");

    /* ---- Derive T ---- */
    ret = deriveT(&MAYO_R1_2, msg, (unsigned long long)msg_len, sig, t);
    if (ret != 0) {
        xil_printf("ERROR: deriveT failed (%d)\r\n", ret);
        uart_send_byte(0x00);
        return 1;
    }
    xil_printf("deriveT: OK\r\n\r\n");

    /* ---- Print T and first 64 bytes of EPK ---- */
    print_hex("T", t, MAYO_R1S2_m_bytes);
    xil_printf("\r\n");
    print_hex("EPK[0..63]", epk, EPK_PRINT_BYTES);
    xil_printf("\r\n");

    /* ---- Send result byte 0xCC (11001100) ---- */
    uart_send_byte(RESULT_BYTE);
    xil_printf("Sent result: 0xCC\r\n");

    return 0;
}
