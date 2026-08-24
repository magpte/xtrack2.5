#include "HAL/HAL.h"
#include "Adafruit_ST7789/Adafruit_ST7789.h"
#include "Adafruit_GFX_Library/Fonts/FreeMono12pt7b.h"
#include "cm_backtrace/cm_backtrace.h" 

#define DISP_USE_FPS_TEST    0

/*
 * ---------------------------------------------------------------------
 * Display SPI1 TX acceleration - AT32F435/437 EDMA (Enhanced DMA)
 * ---------------------------------------------------------------------
 * Previously this driver used the classic DMA1 Channel3 (single-beat,
 * no FIFO) to push the LVGL frame buffer out over SPI1. AT32F435/437 also
 * has a separate, more capable EDMA controller (8 independent streams,
 * each with its own 4-word FIFO, burst transfers of 4/8/16 beats, and
 * the same DMAMUX flexible request routing) - see Artery AN0090
 * "AT32F435/437 EDMA Application Note". Moving the LVGL flush path to
 * EDMA frees DMA1/DMA2 entirely for the GPS UART and SD card SPI
 * (see HardwareSerial.cpp / SPI.cpp), and the FIFO+burst mode reduces
 * the number of AHB bus arbitrations needed to push a full 240x320
 * RGB565 frame (150 KB) out over SPI1.
 *
 * Stream / request mapping used here (arbitrary but documented):
 *   EDMA_STREAM1  <-> EDMAMUX_CHANNEL1 <-> EDMAMUX_DMAREQ_ID_SPI1_TX
 *
 * NOTE: at32f435_437_edma.h is provided by the Artery Keil device pack
 * (installed into the Keil toolchain, not vendored in this repository),
 * so the exact enum/field names below could not be cross-checked against
 * the header from this build environment. They were taken verbatim from
 * Artery's AN0090 application note (Ver 2.0.2) and from the existing,
 * working DMA1 code in this same file (which already uses the sibling
 * identifier DMAMUX_DMAREQ_ID_SPI1_TX for the classic DMA). If a name
 * differs slightly in your installed pack, the compiler error will point
 * directly at the mismatched identifier below.
 */
#define DISP_USE_EDMA          1
#define DISP_EDMA_STREAM       EDMA_STREAM1
#define DISP_EDMA_MUX_CHANNEL  EDMAMUX_CHANNEL1
#define DISP_EDMA_IRQn         EDMA_Stream1_IRQn
#define DISP_EDMA_FDT_FLAG     EDMA_FDT1_FLAG
#define DISP_DMA_MAX_SIZE      65535

typedef Adafruit_ST7789 SCREEN_CLASS;

static SCREEN_CLASS screen(
    CONFIG_SCREEN_CS_PIN,
    CONFIG_SCREEN_DC_PIN,
    CONFIG_SCREEN_RST_PIN,
    &CONFIG_SCREEN_SPI,
    CONFIG_SCREEN_HOR_RES,
    CONFIG_SCREEN_VER_RES
);

static uint8_t* Disp_DMA_TragetPoint = nullptr;
static uint8_t* Disp_DMA_CurrentPoint = nullptr;
static HAL::Display_CallbackFunc_t Disp_Callback = nullptr;

#if (DISP_USE_FPS_TEST == 1)
static float Display_GetFPS(SCREEN_CLASS* scr, uint32_t loopNum)
{
    uint32_t startTime = millis();
    for(uint32_t f = 0; f < loopNum; f++)
    {
        scr->fillScreen(SCREEN_CLASS::COLOR_RED);
        scr->fillScreen(SCREEN_CLASS::COLOR_GREEN);
        scr->fillScreen(SCREEN_CLASS::COLOR_BLUE);
    }
    uint32_t costTime = millis() - startTime;
    float fps = loopNum * 3 / (costTime / 1000.0f);

    scr->fillScreen(SCREEN_CLASS::COLOR_BLUE);
    scr->setTextSize(1);
    scr->setTextColor(SCREEN_CLASS::COLOR_WHITE);
    scr->setCursor(0, scr->height() / 2);

    scr->print("Frame:");
    scr->println(loopNum * 3);

    scr->print("Time:");
    scr->print(costTime);
    scr->println("ms");

    scr->print("FPS:");
    scr->println(fps);

    return fps;
}
#endif

/*
 * Re-arm the EDMA stream for the next chunk. EDMA_STREAM1's DTCNT field
 * is 16-bit (max 65535), same limit as the classic DMA it replaces, so a
 * full 240x320x2 = 153600 byte frame still needs to be split into 3
 * chunks; the split/continuation bookkeeping (Disp_DMA_CurrentPoint /
 * Disp_DMA_TragetPoint) is unchanged from the original driver.
 *
 * Each chunk goes through the public edma_init()/edma_stream_enable()
 * API rather than poking stream registers directly: with FIFO+burst
 * enabled the stream must always be (re)configured through a full
 * edma_init() call (burst size, FIFO threshold and DTCNT interact - see
 * AN0090 Table 3), and re-running edma_init() per chunk costs at most a
 * few hundred CPU cycles, which is negligible next to the ~ms-scale SPI
 * transfer time of a 64 KB chunk.
 */
static edma_init_type Disp_Edma_InitStruct;

static void Display_SPI_DMA_Send(const void* buf, uint32_t size)
{
    if(size > DISP_DMA_MAX_SIZE)
    {
        if(Disp_DMA_TragetPoint == NULL)
        {
            Disp_DMA_TragetPoint = (uint8_t*)buf + size;
        }
        Disp_DMA_CurrentPoint = (uint8_t*)buf + DISP_DMA_MAX_SIZE;
        size = DISP_DMA_MAX_SIZE;
    }
    else
    {
        Disp_DMA_CurrentPoint = NULL;
        Disp_DMA_TragetPoint = NULL;
    }

    edma_stream_enable(DISP_EDMA_STREAM, FALSE);

    Disp_Edma_InitStruct.memory0_base_addr = (uint32_t)buf;
    Disp_Edma_InitStruct.buffer_size = size;
    edma_init(DISP_EDMA_STREAM, &Disp_Edma_InitStruct);

    edma_stream_enable(DISP_EDMA_STREAM, TRUE);
}

extern "C" void EDMA_Stream1_IRQHandler(void)
{
    if(edma_flag_get(EDMA_DTERR1_FLAG) != RESET || edma_flag_get(EDMA_FERR1_FLAG) != RESET)
    {
        edma_flag_clear(EDMA_DTERR1_FLAG | EDMA_FERR1_FLAG);
    }

    if(edma_flag_get(DISP_EDMA_FDT_FLAG) != RESET)
    {
        edma_flag_clear(DISP_EDMA_FDT_FLAG);
        if(Disp_DMA_CurrentPoint != NULL && Disp_DMA_CurrentPoint < Disp_DMA_TragetPoint)
        {
            Display_SPI_DMA_Send(Disp_DMA_CurrentPoint, Disp_DMA_TragetPoint - Disp_DMA_CurrentPoint);
        }
        else
        {
            Disp_DMA_CurrentPoint = NULL;
            Disp_DMA_TragetPoint = NULL;

            /* 等待 SPI1 物理总线移位完成后再拉高 CS 脚，消除边缘截断 */
            while(spi_i2s_flag_get(SPI1, SPI_I2S_BF_FLAG) != RESET);
            digitalWrite_HIGH(CONFIG_SCREEN_CS_PIN);

            if(Disp_Callback)
            {
                Disp_Callback();
            }
        }
    }
}

static void Display_SPI_DMA_Init()
{
    crm_periph_clock_enable(CRM_EDMA_PERIPH_CLOCK, TRUE);

    edma_reset(DISP_EDMA_STREAM);

    edma_default_para_init(&Disp_Edma_InitStruct);

    Disp_Edma_InitStruct.direction = EDMA_DIR_MEMORY_TO_PERIPHERAL;
    Disp_Edma_InitStruct.buffer_size = DISP_DMA_MAX_SIZE;

    /* Peripheral side: SPI1 data register, fixed address, single beat
     * (the SPI FIFO only accepts one write per DMA request). */
    Disp_Edma_InitStruct.peripheral_base_addr = (uint32_t)&SPI1->dt;
    Disp_Edma_InitStruct.peripheral_data_width = EDMA_PERIPHERAL_DATA_WIDTH_BYTE;
    Disp_Edma_InitStruct.peripheral_inc_enable = FALSE;
    Disp_Edma_InitStruct.peripheral_burst_mode = EDMA_PERIPHERAL_SINGLE;

    /* Memory side: LVGL frame buffer, auto-increment, burst-4. Combined
     * with a FULL FIFO threshold this yields "4 burst transfers of 4
     * beats" per AN0090 Table 3 (Byte / Full / MBURST=INCR4 is one of
     * the documented-valid combinations), i.e. EDMA pulls 16 bytes out
     * of SRAM per FIFO refill instead of 1, cutting AHB arbitration
     * overhead roughly 4x versus the old single-beat DMA1 transfer. */
    Disp_Edma_InitStruct.memory0_base_addr = (uint32_t)NULL;
    Disp_Edma_InitStruct.memory_data_width = EDMA_MEMORY_DATA_WIDTH_BYTE;
    Disp_Edma_InitStruct.memory_inc_enable = TRUE;
    Disp_Edma_InitStruct.memory_burst_mode = EDMA_MEMORY_BURST_4;

    Disp_Edma_InitStruct.fifo_mode_enable = TRUE;
    Disp_Edma_InitStruct.fifo_threshold = EDMA_FIFO_THRESHOLD_FULL;

    Disp_Edma_InitStruct.priority = EDMA_PRIORITY_HIGH;
    Disp_Edma_InitStruct.loop_mode_enable = FALSE;

    edma_init(DISP_EDMA_STREAM, &Disp_Edma_InitStruct);

    edmamux_enable(TRUE);
    edmamux_init(DISP_EDMA_MUX_CHANNEL, EDMAMUX_DMAREQ_ID_SPI1_TX);

    spi_i2s_dma_transmitter_enable(SPI1, TRUE);

    NVIC_EnableIRQ(DISP_EDMA_IRQn);

    /* 项 2：使能传输完成、传输错误与 FIFO 错误中断 */
    edma_interrupt_enable(DISP_EDMA_STREAM, EDMA_FDT_INT | EDMA_DTERR_INT | EDMA_FERR_INT, TRUE);
}

void HAL::Display_Init()
{
    Serial.print("Display: init...");
    screen.begin();
    screen.setRotation(0);
    screen.fillScreen(SCREEN_CLASS::COLOR_BLACK);

    screen.setTextWrap(true);
    screen.setTextSize(1);
    screen.setCursor(0, 0);
    screen.setFont();
    screen.setTextColor(SCREEN_CLASS::COLOR_WHITE, SCREEN_CLASS::COLOR_BLUE);

    Display_SPI_DMA_Init();

#if (DISP_USE_FPS_TEST == 1)
    HAL::Backlight_ForceLit(true);
    Display_GetFPS(&screen, 100);
    while(1);
#endif
    
    HAL::Backlight_SetGradual(1000, 1000);
    Serial.println("success");
}

void HAL::Display_DumpCrashInfo(const char* info)  
{  
    HAL::Backlight_ForceLit(true);  
      
    screen.fillScreen(SCREEN_CLASS::COLOR_BLUE);  
    screen.setTextColor(SCREEN_CLASS::COLOR_WHITE);  
    screen.setFont(&FreeMono12pt7b);  
    screen.setTextSize(2);  
    screen.setCursor(0, 34);  
    screen.print(":(");  
      
    screen.setFont();  
    screen.setTextSize(1);  
    screen.setCursor(0, screen.height() / 2 - 8 - 5);  // ֱ  ʹ  8      TEXT_HEIGHT_1  
    screen.println(info);  
      
    //   ʾ    ջ  Ϣ  
    screen.setCursor(0, 60);  
    screen.println("Call Stack:");  
    uint32_t call_stack_buf[4];  
    size_t depth = cm_backtrace_call_stack(call_stack_buf, 4, __get_MSP());  
    for (size_t i = 0; i < depth && i < 3; i++) {  
        screen.printf("%d:0x%08X\n", i, call_stack_buf[i]);  
    }  
      
    screen.setCursor(0, screen.height() - 8 * 6);  // 直接使用 8 像素高
    screen.println("Error code:");  
    screen.printf("MMFAR = 0x%08X\r\n", SCB->MMFAR);  
    screen.printf("BFAR  = 0x%08X\r\n", SCB->BFAR);  
    screen.printf("CFSR  = 0x%08X\r\n", SCB->CFSR);  
    screen.printf("HFSR  = 0x%08X\r\n", SCB->HFSR);  
    screen.printf("DFSR  = 0x%08X\r\n", SCB->DFSR);  
      
    screen.setCursor(0, screen.height() - 8);  // 直接使用 8 像素高
    screen.print("Press KEY to reboot..");  
}

void HAL::Display_SetAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    screen.setAddrWindow(x0, y0, x1, y1);
}

void HAL::Display_SendPixels(const uint16_t* pixels, uint32_t len)
{
    digitalWrite_LOW(CONFIG_SCREEN_CS_PIN);
    digitalWrite_HIGH(CONFIG_SCREEN_DC_PIN);

    Display_SPI_DMA_Send(pixels, len * sizeof(uint16_t));
}

void HAL::Display_SetSendFinishCallback(Display_CallbackFunc_t func)
{
    Disp_Callback = func;
}
