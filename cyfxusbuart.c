/*
 ## Cypress USB 3.0 Platform header file (cyfxusbuart.c)
 ## ===========================
 ##
 ##  Copyright Cypress Semiconductor Corporation, 2010-2018,
 ##  All Rights Reserved
 ##  UNPUBLISHED, LICENSED SOFTWARE.
 ##
 ##  CONFIDENTIAL AND PROPRIETARY INFORMATION
 ##  WHICH IS THE PROPERTY OF CYPRESS.
 ##
 ##  Use of this file is governed
 ##  by the license agreement included in the file
 ##
 ##     <install>/license/license.txt
 ##
 ##  where <install> is the Cypress software
 ##  installation root directory path.
 ##
 ## ===========================
*/

/* This file implements a usb-uart application using DMA channel */


#include <cyu3system.h>
#include <cyu3os.h>
#include <cyu3error.h>
#include <cyu3uart.h>
#include <cyu3dma.h>
#include <cyu3usb.h>
#include <uart_regs.h>
#include <cyu3utils.h>
#include "cyfxusbuart.h"

CyU3PThread       USBUARTAppThread;
CyU3PDmaChannel   glChHandleUsbtoUart;          /* DMA AUTO (USB TO UART) channel handle.*/
CyU3PDmaChannel   glChHandleUarttoUsb;          /* DMA AUTO_SIG(UART TO USB) channel handle.*/
CyU3PDmaChannel   glChHandleDebug;              /* DMA MANUAL_OUT (Debug console) channel handle. */
CyBool_t          glIsApplnActive = CyFalse;    /* Whether the application is active or not. */
CyU3PUartConfig_t glUartConfig = {0};           /* Current UART configuration. */
volatile uint16_t glPktsPending = 0;            /* Number of packets that have been committed since last check. */
CyU3PReturnStatus_t DmaResetReturn=0;

/* CDC Class specific requests to be handled by this application. */
#define SET_LINE_CODING        0x20
#define GET_LINE_CODING        0x21
#define SET_CONTROL_LINE_STATE 0x22

void
CyFxAppErrorHandler (
        CyU3PReturnStatus_t apiRetStatus    /* API return status */
        )
{
    /* Application failed with the error code apiRetStatus */

    /* Add custom debug or recovery actions here */

    /* Loop Indefinitely */
    for (;;)
    {
        /* Thread sleep : 100 ms */
        CyU3PThreadSleep (100);
    }
}

void
CyFxUSBUARTDmaCallback(
        CyU3PDmaChannel   *chHandle, /* Handle to the DMA channel. */
        CyU3PDmaCbType_t   type,     /* Callback type.             */
        CyU3PDmaCBInput_t *input)    /* Callback status.           */
{
    switch (type)
    {
        case CY_U3P_DMA_CB_PROD_EVENT:
            CyU3PDmaChannelCommitBuffer (&glChHandleUarttoUsb, input->buffer_p.count, 0);
            glPktsPending++;
            break;

        case CY_U3P_DMA_CB_CONS_EVENT:
        CyFxUsbUartDebugPrint(
            "D[CyFxUSBUARTDmaCallback] : CY_U3P_DMA_CB_CONS_EVENT\r\n");
        break;

        case CY_U3P_DMA_CB_ABORTED:
        CyFxUsbUartDebugPrint(
            "[CyFxUSBUARTDmaCallback] : CY_U3P_DMA_CB_ABORTED\r\n");
        break;

        case CY_U3P_DMA_CB_ERROR:
        CyFxUsbUartDebugPrint("[CyFxUSBUARTDmaCallback] : CY_U3P_DMA_CB_ERROR\r\n");

        DmaResetReturn = CyU3PDmaChannelReset(&glChHandleUarttoUsb); /*pass handle uartusb - return print*/
        switch(DmaResetReturn){

			case CY_U3P_SUCCESS:
				CyFxUsbUartDebugPrint(
				            "[CyFxUSBUARTDmaCallback - Dma Reset] : CY_U3P_DMA_RESET_SUCCESS\r\n");
				break;

			case CY_U3P_ERROR_NULL_POINTER:
				CyFxUsbUartDebugPrint(
				            "[CyFxUSBUARTDmaCallback - Dma Reset] : CY_U3P_DMA_RESET_ERROR_NULL_POINTER\r\n");
				break;
			case CY_U3P_ERROR_NOT_CONFIGURED:
				CyFxUsbUartDebugPrint(
							"[CyFxUSBUARTDmaCallback - Dma Reset] : CY_U3P_DMA_RESET_ERROR_NOT_CONIGIURED\r\n");
				break;
			case CY_U3P_ERROR_MUTEX_FAILURE:
				CyFxUsbUartDebugPrint(
							"[CyFxUSBUARTDmaCallback - Dma Reset] : CY_U3P_DMA_RESET_ERROR_MUTEX_FAILURE\r\n");
				break;
			default:
				CyFxUsbUartDebugPrint(
							"[CyFxUSBUARTDmaCallback - Dma Reset] : CY_U3P_DMA_RESET_ERROR_UNKNOWN\r\n");
				break;



        }

        CyU3PDmaChannelSetXfer (&glChHandleUarttoUsb, 0 );
        break;

        case CY_U3P_DMA_CB_PROD_SUSP:
        CyFxUsbUartDebugPrint(
            "[CyFxUSBUARTDmaCallback] : CY_U3P_DMA_CB_PROD_SUSP\r\n");
        break;

        case CY_U3P_DMA_CB_CONS_SUSP:
        CyFxUsbUartDebugPrint(
            "[CyFxUSBUARTDmaCallback] : CY_U3P_DMA_CB_CONS_SUSP\r\n");
        break;

        default:
        	CyFxUsbUartDebugPrint(
				"[CyFxUSBUARTDmaCallback] : CY_U3P_DMA_RESET_ERROR_UNKNOWN\r\n");
            break;
    }
}

/* This function starts the USBUART application */
void
CyFxUSBUARTAppStart(
        void )
{ 
    uint16_t size = 0;
    CyU3PEpConfig_t epCfg;
    CyU3PDmaChannelConfig_t dmaCfg;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
    CyU3PUSBSpeed_t usbSpeed = CyU3PUsbGetSpeed();

    /* Based on the Bus speed configure the endpoint packet size */
    switch (usbSpeed)
    {
        case CY_U3P_FULL_SPEED:
            size = 64;
            break;

        case CY_U3P_HIGH_SPEED:
            size = 512;
            break;

        case  CY_U3P_SUPER_SPEED:
            /* Turning low power mode off to avoid USB transfer delays. */
            CyU3PUsbLPMDisable ();
            size = 1024;
            break;

        default:
            CyFxAppErrorHandler (CY_U3P_ERROR_FAILURE);
            break;
    }

    CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
    epCfg.enable = CyTrue;
    epCfg.epType = CY_U3P_USB_EP_BULK;
    epCfg.burstLen = 1;
    epCfg.streams = 0;
    epCfg.pcktSize = size;

    /* Producer endpoint configuration */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_PRODUCER , &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {    
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Consumer endpoint configuration */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {       
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Interrupt endpoint configuration */
    epCfg.epType = CY_U3P_USB_EP_INTR;
    epCfg.pcktSize = 64;
    epCfg.isoPkts = 1;

    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_INTERRUPT, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Debug Endpoint Configuration */
    /* Debug Interrupt Endpoint */
    epCfg.epType = CY_U3P_USB_EP_INTR;
    epCfg.pcktSize = (usbSpeed == CY_U3P_SUPER_SPEED) ? 1024 : 64;
    epCfg.isoPkts = 1;
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_DEBUG_INTERRUPT, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Debug Consumer Endpoint (Bulk IN) */
    epCfg.epType = CY_U3P_USB_EP_BULK;
    epCfg.pcktSize = size;
    epCfg.streams = 0;
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_DEBUG_CONSUMER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Debug Producer Endpoint (Bulk OUT - Dummy) */
    epCfg.epType = CY_U3P_USB_EP_BULK;
    epCfg.pcktSize = size;
    epCfg.streams = 0;
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_DEBUG_PRODUCER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }


    /* Create a DMA_AUTO channel between usb producer socket and uart consumer socket */
    dmaCfg.size = size;
    dmaCfg.count = CY_FX_USBUART_DMA_BUF_COUNT;
    dmaCfg.prodSckId = CY_FX_EP_PRODUCER1_SOCKET;
    dmaCfg.consSckId = CY_FX_EP_CONSUMER1_SOCKET;
    dmaCfg.dmaMode = CY_U3P_DMA_MODE_BYTE;
    dmaCfg.notification = 0;
    dmaCfg.cb = NULL;
    dmaCfg.prodHeader = 0;
    dmaCfg.prodFooter = 0;
    dmaCfg.consHeader = 0;
    dmaCfg.prodAvailCount = 0;

    apiRetStatus = CyU3PDmaChannelCreate (&glChHandleUsbtoUart,
            CY_U3P_DMA_TYPE_AUTO, &dmaCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {       
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Create a DMA_MANUAL channel between uart producer socket and usb consumer socket */    
    /* Use a smaller buffer size (32 bytes) to ensure that packets get filled in a short time. */
    dmaCfg.size         = 32;
    dmaCfg.prodSckId    = CY_FX_EP_PRODUCER2_SOCKET;
    dmaCfg.consSckId    = CY_FX_EP_CONSUMER2_SOCKET;    
    dmaCfg.notification = CY_U3P_DMA_CB_PROD_EVENT;
    dmaCfg.cb           = CyFxUSBUARTDmaCallback;

    apiRetStatus = CyU3PDmaChannelCreate (&glChHandleUarttoUsb,
            CY_U3P_DMA_TYPE_MANUAL, &dmaCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {       
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set DMA Channel transfer size */
    apiRetStatus = CyU3PDmaChannelSetXfer (&glChHandleUsbtoUart,0);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {       
        CyFxAppErrorHandler(apiRetStatus);
    }

    apiRetStatus = CyU3PDmaChannelSetXfer (&glChHandleUarttoUsb, 0);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {       
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Create DMA Channel for Debug Console (CPU to USB) */
    dmaCfg.size = size;
    dmaCfg.count = 4;
    dmaCfg.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    dmaCfg.consSckId = CY_FX_EP_DEBUG_CONS_SOCKET;
    dmaCfg.dmaMode = CY_U3P_DMA_MODE_BYTE;
    dmaCfg.notification = 0;
    dmaCfg.cb = NULL;
    dmaCfg.prodHeader = 0;
    dmaCfg.prodFooter = 0;
    dmaCfg.consHeader = 0;
    dmaCfg.prodAvailCount = 0;

    apiRetStatus = CyU3PDmaChannelCreate (&glChHandleDebug,
            CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set Debug DMA Channel transfer size */
    apiRetStatus = CyU3PDmaChannelSetXfer (&glChHandleDebug, 0);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Update the status flag. */
    glIsApplnActive = CyTrue;
} 

void
CyFxUSBUARTAppStop (
        void)
{
    CyU3PEpConfig_t epCfg;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Update the flag. */
    glIsApplnActive = CyFalse;

    /* Flush the endpoint memory */
    CyU3PUsbFlushEp(CY_FX_EP_PRODUCER);
    CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);
    CyU3PUsbFlushEp(CY_FX_EP_INTERRUPT);

    /* Destroy the channel */
    CyU3PDmaChannelDestroy (&glChHandleUsbtoUart);
    CyU3PDmaChannelDestroy (&glChHandleUarttoUsb);

    /* Disable endpoints. */
    CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
    epCfg.enable = CyFalse;

    /* Producer endpoint configuration. */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_PRODUCER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Consumer endpoint configuration. */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Interrupt endpoint configuration. */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_INTERRUPT, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Disable Debug Endpoints */
    /* Debug Interrupt */
    CyU3PUsbFlushEp(CY_FX_EP_DEBUG_INTERRUPT);
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_DEBUG_INTERRUPT, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler (apiRetStatus);
    }
    /* Debug Consumer */
    CyU3PUsbFlushEp(CY_FX_EP_DEBUG_CONSUMER);
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_DEBUG_CONSUMER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Debug Producer (Dummy) */
    CyU3PUsbFlushEp(CY_FX_EP_DEBUG_PRODUCER);
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_DEBUG_PRODUCER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Destroy Debug Channel */
    CyU3PDmaChannelDestroy (&glChHandleDebug);
}

/* This is the callback function to handle the USB events. */
void
CyFxUSBUARTAppUSBEventCB (
        CyU3PUsbEventType_t evtype, /* Event type */
        uint16_t            evdata  /* Event data */
        )
{
    switch (evtype)
    {
        case CY_U3P_USB_EVENT_SETCONF:
            /* Stop the application before re-starting. */
            if (glIsApplnActive)
            {
                CyFxUSBUARTAppStop ();
            }
            /* Start the loop back function. */
            CyFxUSBUARTAppStart ();
            break;

        case CY_U3P_USB_EVENT_RESET:
        case CY_U3P_USB_EVENT_CONNECT:
        case CY_U3P_USB_EVENT_DISCONNECT:
            /* Stop the loop back function. */
            if (glIsApplnActive)
            {
                CyU3PUsbLPMEnable ();
                CyFxUSBUARTAppStop ();
            }
            break;

        default:
            break;
    }
}


/* Callback to handle the USB Setup Requests and CDC Class events */
static CyBool_t
CyFxUSBUARTAppUSBSetupCB (
        uint32_t setupdat0, /* SETUP Data 0 */
        uint32_t setupdat1  /* SETUP Data 1 */
        )
{
    uint16_t readCount = 0;
    uint8_t  bRequest, bReqType;
    uint8_t  bType, bTarget;
    uint16_t wValue, wIndex;
    uint8_t config_data[7];
    CyBool_t isHandled = CyFalse;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
    CyU3PUartConfig_t uartConfig;

    /* Fast enumeration is used. Only requests addressed to the interface, class,
     * vendor and unknown control requests are received by this function. */

    /* Decode the fields from the setup request. */
    bReqType = (setupdat0 & CY_U3P_USB_REQUEST_TYPE_MASK);
    bType    = (bReqType & CY_U3P_USB_TYPE_MASK);
    bTarget  = (bReqType & CY_U3P_USB_TARGET_MASK);
    bRequest = ((setupdat0 & CY_U3P_USB_REQUEST_MASK) >> CY_U3P_USB_REQUEST_POS);
    wValue   = ((setupdat0 & CY_U3P_USB_VALUE_MASK)   >> CY_U3P_USB_VALUE_POS);
    wIndex   = (setupdat1 & CY_U3P_USB_INDEX_MASK);

    if (bType == CY_U3P_USB_STANDARD_RQT)
    {
        /* Handle SET_FEATURE(FUNCTION_SUSPEND) and CLEAR_FEATURE(FUNCTION_SUSPEND)
         * requests here. It should be allowed to pass if the device is in configured
         * state and failed otherwise. */
        if ((bTarget == CY_U3P_USB_TARGET_INTF) && ((bRequest == CY_U3P_USB_SC_SET_FEATURE)
                    || (bRequest == CY_U3P_USB_SC_CLEAR_FEATURE)) && (wValue == 0))
        {
            if (glIsApplnActive)
                CyU3PUsbAckSetup ();
            else
                CyU3PUsbStall (0, CyTrue, CyFalse);

            isHandled = CyTrue;
        }
    }

    /* Check for CDC Class Requests */
    if (bType == CY_U3P_USB_CLASS_RQT)
    {
        isHandled = CyTrue;

        /* Handle Debug Interface (Interface 2) Requests */
        if (wIndex == 0x02)
        {
            if (bRequest == SET_LINE_CODING)
            {
                /* Read and discard data */
                status = CyU3PUsbGetEP0Data(0x07, config_data, &readCount);
                if (status == CY_U3P_SUCCESS)
                {
                    CyU3PUsbAckSetup();
                }
            }
            else if (bRequest == GET_LINE_CODING)
            {
                /* Send dummy 115200 8N1 */
                config_data[0] = 0x00; config_data[1] = 0xC2; config_data[2] = 0x01; config_data[3] = 0x00;
                config_data[4] = 0x00; config_data[5] = 0x00; config_data[6] = 0x08;
                status = CyU3PUsbSendEP0Data(0x07, config_data);
            }
            else if (bRequest == SET_CONTROL_LINE_STATE)
            {
                CyU3PUsbAckSetup();
            }
            
            if (status != CY_U3P_SUCCESS)
            {
                CyFxAppErrorHandler(status);
            }
            return CyTrue;
        }

        /* CDC Specific Requests for UART */
        /* set_line_coding */
        if (bRequest == SET_LINE_CODING)                                                      
        {
            status = CyU3PUsbGetEP0Data(0x07, config_data, &readCount);
            if (status != CY_U3P_SUCCESS)
            {
                CyFxAppErrorHandler(status);
            }
            if (readCount != 0x07)
            {
                CyFxAppErrorHandler(CY_U3P_ERROR_BAD_SIZE);
            }
            else
            {
                CyU3PMemSet ((uint8_t *)&uartConfig, 0, sizeof (uartConfig));
                uartConfig.baudRate = (CyU3PUartBaudrate_t)(config_data[0] | (config_data[1]<<8)|
                        (config_data[2]<<16)|(config_data[3]<<24));
                if (config_data[4] == 0)
                {
                    uartConfig.stopBit = CY_U3P_UART_ONE_STOP_BIT;
                }
                else if (config_data[4] == 2)
                {
                    uartConfig.stopBit = CY_U3P_UART_TWO_STOP_BIT;
                }
                else
                {
                    /* Give invalid value. */
                    uartConfig.stopBit = (CyU3PUartStopBit_t)0;
                }
                if (config_data[5] == 1)
                {
                    uartConfig.parity = CY_U3P_UART_ODD_PARITY;
                }
                else if (config_data[5] == 2)
                {
                    uartConfig.parity = CY_U3P_UART_EVEN_PARITY;
                }
                else
                {
                    /* 0 = no parity; any other value - invalid parity. */
                    uartConfig.parity = CY_U3P_UART_NO_PARITY;
                }

                uartConfig.txEnable = CyTrue;
                uartConfig.rxEnable = CyTrue;
                uartConfig.flowCtrl = CyFalse;
                uartConfig.isDma = CyTrue;

                /* Set the uart configuration */
                apiRetStatus = CyU3PUartSetConfig (&uartConfig, NULL);
                if (apiRetStatus == CY_U3P_SUCCESS)
                {
                    CyU3PMemCopy ((uint8_t *)&glUartConfig, (uint8_t *)&uartConfig,
                            sizeof (CyU3PUartConfig_t));
                }
            }
        }
        /* get_line_coding */
        else if (bRequest == GET_LINE_CODING )                                                   
        {
            /* get current uart config */
            config_data[0] = glUartConfig.baudRate&(0x000000FF);
            config_data[1] = ((glUartConfig.baudRate&(0x0000FF00))>> 8);
            config_data[2] = ((glUartConfig.baudRate&(0x00FF0000))>>16);
            config_data[3] = ((glUartConfig.baudRate&(0xFF000000))>>24);
            if (glUartConfig.stopBit == CY_U3P_UART_ONE_STOP_BIT)
            {
                config_data[4] = 0;
            }
            else /* CY_U3P_UART_TWO_STOP_BIT */
            {
                config_data[4] = 2;
            }

            if (glUartConfig.parity == CY_U3P_UART_EVEN_PARITY)
            {
                config_data[5] = 2;
            }
            else if (glUartConfig.parity == CY_U3P_UART_ODD_PARITY)
            {
                config_data[5] = 1;
            }
            else
            {
                config_data[5] = 0;
            }
            config_data[6] =  0x08;
            status = CyU3PUsbSendEP0Data( 0x07, config_data);
            if (status != CY_U3P_SUCCESS)
            {
                CyFxAppErrorHandler(status);
            }
        }
        /* SET_CONTROL_LINE_STATE */
        else if (bRequest == SET_CONTROL_LINE_STATE)                                                   
        {
            if (glIsApplnActive)
            {    
                CyU3PUsbAckSetup ();
            }   
            else
                CyU3PUsbStall (0, CyTrue, CyFalse);
        }
        else
        {
            status = CY_U3P_ERROR_FAILURE;
        }

        if (status != CY_U3P_SUCCESS)
        {
            isHandled = CyFalse;
        }
    }

    return isHandled;
}

CyBool_t
CyFxUSBUARTAppLPMRqtCB (
        CyU3PUsbLinkPowerMode link_mode)
{
    return CyTrue;
}

/* This function initializes the USB module, UART module and sets the enumeration descriptors */
void
CyFxUSBUARTAppInit (
        void )

{
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Start the USB functionality. */
    apiRetStatus = CyU3PUsbStart();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {       
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Initialize the UART module */
    apiRetStatus = CyU3PUartInit ();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error handling */
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Configure the UART */
    CyU3PMemSet ((uint8_t *)&glUartConfig, 0, sizeof (glUartConfig));
    glUartConfig.baudRate = CY_U3P_UART_BAUDRATE_115200;
    glUartConfig.stopBit = CY_U3P_UART_ONE_STOP_BIT;
    glUartConfig.parity = CY_U3P_UART_NO_PARITY;
    glUartConfig.flowCtrl = CyFalse;
    glUartConfig.txEnable = CyTrue;
    glUartConfig.rxEnable = CyTrue;
    glUartConfig.isDma = CyTrue;

    /* Set the UART configuration */
    apiRetStatus = CyU3PUartSetConfig (&glUartConfig, NULL);
    if (apiRetStatus != CY_U3P_SUCCESS )
    {
        /* Error handling */
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Setup the callback to handle the setup requests */
    CyU3PUsbRegisterSetupCallback(CyFxUSBUARTAppUSBSetupCB, CyTrue);

    /* Setup the callback to handle the USB events. */
    CyU3PUsbRegisterEventCallback(CyFxUSBUARTAppUSBEventCB);

    /* Register a callback to handle LPM requests from the USB 3.0 host. */
    CyU3PUsbRegisterLPMRequestCallback(CyFxUSBUARTAppLPMRqtCB);    

    /* Set the USB enumeration descriptors */

    /* Super speed device descriptor. */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSB30DeviceDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {       
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* High speed device descriptor. */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSB20DeviceDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {       
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* BOS descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_BOS_DESCR, 0, (uint8_t *)CyFxUSBBOSDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {       
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Device qualifier descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_DEVQUAL_DESCR, 0, (uint8_t *)CyFxUSBDeviceQualDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {        
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Super speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBSSConfigDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {      
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* High speed configuration descriptor*/
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBHSConfigDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {        
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Full speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_FS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBFSConfigDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {       
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 0 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {        
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 1 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {        
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 2 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 2, (uint8_t *)CyFxUSBProductDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {        
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Connect the USB Pins with super speed operation enabled. */
    apiRetStatus = CyU3PConnectState(CyTrue, CyTrue);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {        
        CyFxAppErrorHandler(apiRetStatus);
    }
}

/* Entry function for the USBUARTAppThread */
void
USBUARTAppThread_Entry (
        uint32_t input)
{   
#ifdef EN_UART_RCV_BLOCK_EN_DIS
    uint32_t regValueEn = 0, regValueDs = 0;
#endif

    /* Initialize the USBUART Example Application */
    CyFxUSBUARTAppInit();

#ifdef EN_UART_RCV_BLOCK_EN_DIS
    /* UART Config Value for Enabling Rx Block */
    regValueEn = UART->lpp_uart_config;

    /* UART Config Value for Disabling the Rx Block  */
    regValueDs = UART->lpp_uart_config & (~(CY_U3P_LPP_UART_RTS | CY_U3P_LPP_UART_RX_ENABLE));
#endif

    for (;;)
    {
        if (glIsApplnActive)
        {
            /* While the application is active, check for data sent during the last 50 ms. If no data
               has been sent to the host, use the channel wrap-up feature to send any partial buffer to
               the USB host.
            */
            if (glPktsPending == 0)
            {
#ifdef EN_UART_RCV_BLOCK_EN_DIS
                /* Disable UART Receiver Block */
                UART->lpp_uart_config = regValueDs;
#endif
                CyU3PDmaChannelSetWrapUp (&glChHandleUarttoUsb);

#ifdef EN_UART_RCV_BLOCK_EN_DIS
                /* Enable UART Receiver Block */
                UART->lpp_uart_config = regValueEn;
#endif
            }

            glPktsPending = 0;

            static uint16_t counter = 0;
            counter++;
            if (counter == 1200)
            {
                CyFxUsbUartDebugPrint("Dbg Port Alive | Uptime: %lu ms\r\n", CyU3PGetTime());
                counter = 0;
            }
        }

        CyU3PThreadSleep (50);
    }
}

/* Application define function which creates the threads */
void
CyFxApplicationDefine (
        void )
{
    void *ptr = NULL;
    uint32_t retThrdCreate = CY_U3P_SUCCESS;

    /* Allocate the memory for the thread*/
    ptr = CyU3PMemAlloc (CY_FX_USBUART_THREAD_STACK);

    /* Create the thread for the application */
    retThrdCreate = CyU3PThreadCreate (&USBUARTAppThread,          /* USBUART Example App Thread structure */
            "21:USBUART_DMA_mode",                   /* Thread ID and Thread name */
            USBUARTAppThread_Entry,                  /* USBUART Example App Thread Entry function */
            0,                                       /* No input parameter to thread */
            ptr,                                     /* Pointer to the allocated thread stack */
            CY_FX_USBUART_THREAD_STACK,              /* USBUART Example App Thread stack size */
            CY_FX_USBUART_THREAD_PRIORITY,            /* USBUART Example App Thread priority */
            CY_FX_USBUART_THREAD_PRIORITY,            /* USBUART Example App Thread priority */
            CYU3P_NO_TIME_SLICE,                     /* No time slice for the application thread */
            CYU3P_AUTO_START                         /* Start the Thread immediately */
            );
    /* Check the return code */
    if (retThrdCreate != 0)
    {

        /* Loop indefinitely */
        while(1);
    }
}

/* Main function */
int
main (
        void)
{
    CyU3PIoMatrixConfig_t io_cfg;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /* Initialize the device */
    status = CyU3PDeviceInit (0);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Initialize the caches. Enable the instruction cache only. */
    status = CyU3PDeviceCacheControl (CyTrue, CyFalse, CyFalse);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Configure the IO matrix */
    CyU3PMemSet((uint8_t *)&io_cfg,0,sizeof(io_cfg));
    io_cfg.isDQ32Bit = CyFalse;
    io_cfg.useUart   = CyTrue;
    io_cfg.useI2C    = CyFalse;
    io_cfg.useI2S    = CyFalse;
    io_cfg.useSpi    = CyFalse;
    io_cfg.lppMode   = CY_U3P_IO_MATRIX_LPP_UART_ONLY;
    io_cfg.gpioSimpleEn[0]  = 0;
    io_cfg.gpioSimpleEn[1]  = 0;
    io_cfg.gpioComplexEn[0] = 0;
    io_cfg.gpioComplexEn[1] = 0;
    status = CyU3PDeviceConfigureIOMatrix (&io_cfg);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Initialize the RTOS kernel */
    CyU3PKernelEntry ();

    return 0;

handle_fatal_error:
    while(1);

}

/* Function to send debug strings over the second CDC interface */
CyU3PReturnStatus_t
CyFxUsbUartDebugPrint (
        const char *debugMsg
        )
{
    CyU3PDmaBuffer_t dmaInfo;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
    uint16_t length = 0;

    if (!glIsApplnActive)
    {
        return CY_U3P_ERROR_NOT_STARTED;
    }
    
    /* Calculate length */
    while (debugMsg[length] != '\0')
    {
        length++;
    }

    if (length == 0)
    {
        return CY_U3P_SUCCESS;
    }

    /* Acquire a buffer */
    status = CyU3PDmaChannelGetBuffer (&glChHandleDebug, &dmaInfo, CYU3P_WAIT_FOREVER);
    if (status != CY_U3P_SUCCESS)
    {
        return status;
    }

    /* Copy data. Note: If message is longer than buffer size, it will be truncated. */
    if (length > dmaInfo.size)
    {
        length = dmaInfo.size;
    }

    CyU3PMemCopy (dmaInfo.buffer, (uint8_t *)debugMsg, length);
    dmaInfo.count = length;

    /* Commit buffer */
    status = CyU3PDmaChannelCommitBuffer (&glChHandleDebug, dmaInfo.count, 0);

    return status;
}