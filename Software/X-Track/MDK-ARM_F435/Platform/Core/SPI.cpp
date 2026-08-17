/*
 * MIT License
 * Copyright (c) 2017 - 2022 _VIFEXTech
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "SPI.h"

#define SPI1_CLOCK                     (F_CPU)
#define SPI2_CLOCK                     (F_CPU)
#define SPI3_CLOCK                     (F_CPU)

SPIClass::SPIClass(spi_type* spix)
    : SPIx(spix)
    , SPI_Clock(0)
    , _dmaReady(false)
{
    memset(&spi_init_struct, 0, sizeof(spi_init_struct));
}

void SPIClass::SPI_Settings(
    spi_master_slave_mode_type master_slave_mode,
    spi_frame_bit_num_type frame_bit_num,
    uint16_t SPI_MODEx,
    spi_cs_mode_type cs_mode,
    spi_mclk_freq_div_type mclk_freq_div,
    spi_first_bit_type first_bit)
{
    spi_clock_polarity_type clock_polarity;
    spi_clock_phase_type clock_phase;
    spi_enable(SPIx, FALSE);

    switch(SPI_MODEx)
    {
    case 0:
        clock_polarity = SPI_CLOCK_POLARITY_LOW;
        clock_phase = SPI_CLOCK_PHASE_1EDGE;
        break;
    case 1:
        clock_polarity = SPI_CLOCK_POLARITY_LOW;
        clock_phase = SPI_CLOCK_PHASE_2EDGE;
        break;
    case 2:
        clock_polarity = SPI_CLOCK_POLARITY_HIGH;
        clock_phase = SPI_CLOCK_PHASE_1EDGE;
        break;
    case 3:
        clock_polarity = SPI_CLOCK_POLARITY_HIGH;
        clock_phase = SPI_CLOCK_PHASE_2EDGE;
        break;
    default:
        return;
    }

    spi_default_para_init(&spi_init_struct);
    spi_init_struct.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
    spi_init_struct.master_slave_mode = master_slave_mode;
    spi_init_struct.frame_bit_num = frame_bit_num;
    spi_init_struct.clock_polarity = clock_polarity;
    spi_init_struct.clock_phase = clock_phase;
    spi_init_struct.cs_mode_selection = cs_mode;
    spi_init_struct.mclk_freq_division = mclk_freq_div;
    spi_init_struct.first_bit_transmission = first_bit;
    spi_init(SPIx, &spi_init_struct);

    spi_enable(SPIx, TRUE);
}

void SPIClass::begin(void)
{
    spi_i2s_reset(SPIx);
    if(SPIx == SPI1)
    {
        SPI_Clock = SPI1_CLOCK;
        crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, TRUE);
        pinMode(PA5, OUTPUT_AF_PP);
        pinMode(PA6, OUTPUT_AF_PP);
        pinMode(PA7, OUTPUT_AF_PP);

        gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE5, GPIO_MUX_5);
        gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE6, GPIO_MUX_5);
        gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE7, GPIO_MUX_5);
    }
    else if(SPIx == SPI2)
    {
        SPI_Clock = SPI2_CLOCK;
        crm_periph_clock_enable(CRM_SPI2_PERIPH_CLOCK, TRUE);
        pinMode(PB13, OUTPUT_AF_PP);
        pinMode(PB14, OUTPUT_AF_PP);
        pinMode(PB15, OUTPUT_AF_PP);

        gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE13, GPIO_MUX_5);
        gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE14, GPIO_MUX_5);
        gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE15, GPIO_MUX_5);
    }
    else if(SPIx == SPI3)
    {
        SPI_Clock = SPI3_CLOCK;
        crm_periph_clock_enable(CRM_SPI3_PERIPH_CLOCK, TRUE);
        pinMode(PB3, OUTPUT_AF_PP);
        pinMode(PB4, OUTPUT_AF_PP);
        pinMode(PB5, OUTPUT_AF_PP);

        gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE3, GPIO_MUX_6);
        gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE4, GPIO_MUX_6);
        gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE5, GPIO_MUX_6);
    }
    else
    {
        return;
    }

    SPI_Settings(
        SPI_MODE_MASTER,
        SPI_FRAME_8BIT,
        SPI_MODE0,
        SPI_CS_SOFTWARE_MODE,
        SPI_MCLK_DIV_8,
        SPI_FIRST_BIT_MSB
    );
}

void SPIClass::begin(uint32_t clock, uint16_t dataOrder, uint16_t dataMode)
{
    begin();
    setClock(clock);
    setBitOrder(dataOrder);
    setDataMode(dataMode);
    spi_enable(SPIx, TRUE);
}

void SPIClass::begin(SPISettings settings)
{
    begin();
    setClock(settings.clock);
    setBitOrder(settings.bitOrder);
    setDataMode(settings.dataMode);
    spi_enable(SPIx, TRUE);
}

void SPIClass::beginSlave(void)
{
    begin();
    SPI_Settings(
        SPI_MODE_SLAVE,
        SPI_FRAME_8BIT,
        SPI_MODE0,
        SPI_CS_SOFTWARE_MODE,
        SPI_MCLK_DIV_16,
        SPI_FIRST_BIT_MSB
    );
    spi_enable(SPIx, TRUE);
}

void SPIClass::end(void)
{
    spi_enable(SPIx, FALSE);
}

void SPIClass::setClock(uint32_t clock)
{
    if(clock == 0)
    {
        return;
    }

    static const spi_mclk_freq_div_type mclk_freq_div_map[] =
    {
        SPI_MCLK_DIV_2,
        SPI_MCLK_DIV_2,
        SPI_MCLK_DIV_4,
        SPI_MCLK_DIV_8,
        SPI_MCLK_DIV_16,
        SPI_MCLK_DIV_32,
        SPI_MCLK_DIV_64,
        SPI_MCLK_DIV_128,
        SPI_MCLK_DIV_256,
        SPI_MCLK_DIV_512,
        SPI_MCLK_DIV_1024,
    };
    const uint8_t mapSize = sizeof(mclk_freq_div_map) / sizeof(mclk_freq_div_map[0]);
    uint32_t clockDiv = SPI_Clock / clock;
    uint8_t mapIndex = 0;

    while(clockDiv > 1)
    {
        clockDiv = clockDiv >> 1;
        mapIndex++;
    }

    if(mapIndex >= mapSize)
    {
        mapIndex = mapSize - 1;
    }

    spi_init_struct.mclk_freq_division = mclk_freq_div_map[mapIndex];
    spi_init(SPIx, &spi_init_struct);
    spi_enable(SPIx, TRUE);
}

void SPIClass::setClockDivider(uint32_t Div)
{
    if(Div == 0)
    {
        Div = 1;
    }
#if SPI_CLASS_AVR_COMPATIBILITY_MODE
    setClock(16000000 / Div); // AVR:16MHz
#else
    setClock(SPI_Clock / Div);
#endif
}

void SPIClass::setBitOrder(uint16_t bitOrder)
{
    spi_init_struct.first_bit_transmission = (bitOrder == MSBFIRST) ? SPI_FIRST_BIT_MSB : SPI_FIRST_BIT_LSB;
    spi_init(SPIx, &spi_init_struct);
    spi_enable(SPIx, TRUE);
}

/*  Victor Perez. Added to test changing datasize from 8 to 16 bit modes on the fly.
*   Input parameter should be SPI_CR1_DFF set to 0 or 1 on a 32bit word.
*
*/
void SPIClass::setDataSize(uint32_t datasize)
{
    spi_init_struct.frame_bit_num = (spi_frame_bit_num_type)datasize;
    spi_init(SPIx, &spi_init_struct);
    spi_enable(SPIx, TRUE);
}

void SPIClass::setDataMode(uint8_t dataMode)
{
    /* Notes.  As far as I can tell, the AVR numbers for dataMode appear to match the numbers required by the STM32

    From the AVR doc http://www.atmel.com/images/doc2585.pdf section 2.4

    SPI Mode    CPOL    CPHA    Shift SCK-edge  Capture SCK-edge
    0           0       0       Falling         Rising
    1           0       1       Rising          Falling
    2           1       0       Rising          Falling
    3           1       1       Falling         Rising


    On the STM32 it appears to be

    bit 1 - CPOL : Clock polarity
        (This bit should not be changed when communication is ongoing)
        0 : CLK to 0 when idle
        1 : CLK to 1 when idle

    bit 0 - CPHA : Clock phase
        (This bit should not be changed when communication is ongoing)
        0 : The first clock transition is the first data capture edge
        1 : The second clock transition is the first data capture edge

    If someone finds this is not the case or sees a logic error with this let me know ;-)
     */
    spi_clock_polarity_type clock_polarity;
    spi_clock_phase_type clock_phase;
    spi_enable(SPIx, FALSE);

    switch(dataMode)
    {
    case 0:
        clock_polarity = SPI_CLOCK_POLARITY_LOW;
        clock_phase = SPI_CLOCK_PHASE_1EDGE;
        break;
    case 1:
        clock_polarity = SPI_CLOCK_POLARITY_LOW;
        clock_phase = SPI_CLOCK_PHASE_2EDGE;
        break;
    case 2:
        clock_polarity = SPI_CLOCK_POLARITY_HIGH;
        clock_phase = SPI_CLOCK_PHASE_1EDGE;
        break;
    case 3:
        clock_polarity = SPI_CLOCK_POLARITY_HIGH;
        clock_phase = SPI_CLOCK_PHASE_2EDGE;
        break;
    default:
        return;
    }

    spi_init_struct.clock_polarity = clock_polarity;
    spi_init_struct.clock_phase = clock_phase;
    spi_init(SPIx, &spi_init_struct);
    spi_enable(SPIx, TRUE);
}

void SPIClass::beginTransaction(SPISettings settings)
{
    SPISettings(settings.clock, settings.bitOrder, settings.dataMode);

    setClock(settings.clock);
    setBitOrder(settings.bitOrder);
    setDataMode(settings.dataMode);
    setDataSize(settings.dataSize);

    spi_enable(SPIx, TRUE);
}

void SPIClass::beginTransactionSlave(void)
{
    beginSlave();
}

void SPIClass::endTransaction(void)
{
    spi_enable(SPIx, FALSE);
}

uint16_t SPIClass::read(void)
{
    SPI_I2S_WAIT_RX(SPIx);
    return (uint16_t)(SPI_I2S_RXDATA(SPIx));
}

void SPIClass::read(uint8_t *buf, uint32_t len)
{
    if (len == 0)
        return;

    if (SPIx == SPI2 && len >= 32)
    {
        if (transferDMA(NULL, buf, len))
        {
            return;
        }
    }

    SPI_I2S_RXDATA_VOLATILE(SPIx);
    SPI_I2S_TXDATA(SPIx, 0x00FF);

    while((--len))
    {
        SPI_I2S_WAIT_TX(SPIx);
        noInterrupts();
        SPI_I2S_TXDATA(SPIx, 0x00FF);
        SPI_I2S_WAIT_RX(SPIx);
        *buf++ = (uint8_t)SPI_I2S_RXDATA(SPIx);
        interrupts();
    }
    SPI_I2S_WAIT_RX(SPIx);
    *buf++ = (uint8_t)SPI_I2S_RXDATA(SPIx);
}

void SPIClass::write(uint16_t data)
{
    SPI_I2S_TXDATA(SPIx, data);
    SPI_I2S_WAIT_TX(SPIx);
    SPI_I2S_WAIT_BUSY(SPIx);
}

void SPIClass::write(uint16_t data, uint32_t n)
{
    while ((n--) > 0)
    {
        SPI_I2S_TXDATA(SPIx, data); // write the data to be transmitted into the SPI_DR register (this clears the TXE flag)
        SPI_I2S_WAIT_TX(SPIx); // wait till Tx empty
    }

    SPI_I2S_WAIT_BUSY(SPIx); // wait until BSY=0 before returning
}

void SPIClass::write(const uint8_t *data, uint32_t length)
{
    if (length == 0)
        return;

    if (SPIx == SPI2 && length >= 32)
    {
        if (transferDMA(data, NULL, length))
        {
            return;
        }
    }

    while (length--)
    {
        SPI_I2S_WAIT_TX(SPIx);
        SPI_I2S_TXDATA(SPIx, *data++);
    }
    SPI_I2S_WAIT_TX(SPIx);
    SPI_I2S_WAIT_BUSY(SPIx);
}

void SPIClass::write(const uint16_t *data, uint32_t length)
{
    while (length--)
    {
        SPI_I2S_WAIT_TX(SPIx);
        SPI_I2S_TXDATA(SPIx, *data++);
    }
    SPI_I2S_WAIT_TX(SPIx);
    SPI_I2S_WAIT_BUSY(SPIx);
}

uint8_t SPIClass::transfer(uint8_t wr_data) const
{
    SPI_I2S_RXDATA_VOLATILE(SPIx);
    SPI_I2S_TXDATA(SPIx, wr_data);
    SPI_I2S_WAIT_TX(SPIx);
    SPI_I2S_WAIT_BUSY(SPIx);
    return (uint8_t)SPI_I2S_RXDATA(SPIx);
}

uint16_t SPIClass::transfer16(uint16_t wr_data) const
{
    SPI_I2S_RXDATA_VOLATILE(SPIx);
    SPI_I2S_TXDATA(SPIx, wr_data);
    SPI_I2S_WAIT_TX(SPIx);
    SPI_I2S_WAIT_BUSY(SPIx);
    return (uint16_t)SPI_I2S_RXDATA(SPIx);
}

uint8_t SPIClass::send(uint8_t data)
{
    this->write(data);
    return 1;
}

uint8_t SPIClass::send(uint8_t *buf, uint32_t len)
{
    this->write(buf, len);
    return len;
}

uint8_t SPIClass::recv(void)
{
    return this->read();
}

/*
 * ---------------------------------------------------------------------
 * DMA2 加速的 SPI2 批量全双工传输（用于 SD 卡，见 HAL_SD_CARD.cpp /
 * SdFat 的 CONFIG_SD_SPI = SPI_2）。
 * ---------------------------------------------------------------------
 * 原来的 read(buf,len)/write(buf,len) 是逐字节软件轮询 TDBE/RDBF，
 * 每字节都要 CPU 忙等几个时钟周期；SD 卡一次读写至少是 512 字节的
 * 扇区，累积开销不小。这里给 SPI2 接上一对 DMA2 通道，一次性把整个
 * 扇区搬完，CPU 只在发起传输和等待完成之间可以做别的事（当前实现是
 * 简单轮询"传输完成"标志，没有用中断/信号量，但轮询的已经是一个
 * "一整块传完了没有"的标志，而不是每字节都要响应的 TDBE/RDBF，对
 * CPU 的打扰次数从 O(字节数) 降到 O(1)）。
 *
 * 通道分配：
 *   DMA2 Channel1 <-> SPI2_RX (DMAMUX_DMAREQ_ID_SPI2_RX)
 *   DMA2 Channel2 <-> SPI2_TX (DMAMUX_DMAREQ_ID_SPI2_TX)
 * 与显示屏的 EDMA_STREAM1、GPS 的 DMA1 Channel4、ADC 的 DMA1 Channel1
 * 都不冲突。
 */
static uint8_t SPI_DMA_TxDummy = 0xFF;
static uint8_t SPI_DMA_RxTrash;

bool SPIClass::_initDMA()
{
    if(_dmaReady)
    {
        return true;
    }

    if(SPIx != SPI2)
    {
        // 目前只给 SD 卡用的 SPI2 接了 DMA2；显示屏用的 SPI1 走的是
        // HAL_Display.cpp 里独立的 EDMA_STREAM1（帧缓冲区大、TX-only、
        // 有自己的分块/回调逻辑，跟这里的通用双向阻塞式接口需求不同），
        // 其余 SPI 实例暂时没有 DMA 需求。
        return false;
    }

    crm_periph_clock_enable(CRM_DMA2_PERIPH_CLOCK, TRUE);

    dma_reset(DMA2_CHANNEL1);
    dma_reset(DMA2_CHANNEL2);

    dma_init_type dma_init_struct;
    dma_default_para_init(&dma_init_struct);

    dma_init_struct.peripheral_base_addr = (uint32_t)&(SPIx->dt);
    dma_init_struct.peripheral_inc_enable = FALSE;
    dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
    dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    dma_init_struct.loop_mode_enable = FALSE;

    // RX 通道：外设 -> 内存，具体的内存地址/长度/是否自增在每次
    // transferDMA() 里按 rxBuf 是否为空重新配置
    dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
    dma_init_struct.memory_base_addr = (uint32_t)&SPI_DMA_RxTrash;
    dma_init_struct.memory_inc_enable = FALSE;
    dma_init_struct.buffer_size = 1;
    dma_init(DMA2_CHANNEL1, &dma_init_struct);

    // TX 通道：内存 -> 外设
    dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
    dma_init_struct.memory_base_addr = (uint32_t)&SPI_DMA_TxDummy;
    dma_init_struct.memory_inc_enable = FALSE;
    dma_init_struct.buffer_size = 1;
    dma_init(DMA2_CHANNEL2, &dma_init_struct);

    dmamux_enable(DMA2, TRUE);
    dmamux_init(DMA2MUX_CHANNEL1, DMAMUX_DMAREQ_ID_SPI2_RX);
    dmamux_init(DMA2MUX_CHANNEL2, DMAMUX_DMAREQ_ID_SPI2_TX);

    // 注意：SPIx 自身的 RXDMAEN/TXDMAEN（CTRL2 里请求 DMA 的使能位）
    // 不在这里设置——transferDMA() 每次调用都会先完整 spi_i2s_reset()
    // 整个 SPI2 外设再重新置位这两个位，所以这里设了也会被覆盖，
    // 干脆不重复做。

    _dmaReady = true;
    return true;
}

bool SPIClass::transferDMA(const uint8_t* txBuf, uint8_t* rxBuf, uint32_t length, uint32_t timeoutMs)
{
    if(length == 0)
    {
        return true;
    }

    if(!_initDMA())
    {
        return false;
    }

    /* 1. 先关闭 SPI 的 DMA 请求，防止配置过程中发生竞争 */
    spi_i2s_dma_receiver_enable(SPIx, FALSE);
    spi_i2s_dma_transmitter_enable(SPIx, FALSE);

    /* 2. 等待上一轮 SPI 物理总线移位完成 (BUSY == 0) */
    while(spi_i2s_flag_get(SPIx, SPI_I2S_BF_FLAG) != RESET);

    /* 3. 排干 SPI RX FIFO 中的残余数据 (RDBF == 1 时不断读取) */
    while(spi_i2s_flag_get(SPIx, SPI_I2S_RDBF_FLAG) != RESET)
    {
        spi_i2s_data_receive(SPIx);
    }

    /* 4. 清除 SPI 接收溢出错误标志 (ROERR)，解锁 SPI 的 DMA Request 发送能力 */
    if(spi_i2s_flag_get(SPIx, SPI_I2S_ROERR_FLAG) != RESET)
    {
        spi_i2s_flag_clear(SPIx, SPI_I2S_ROERR_FLAG);
    }

    /* 5. 禁用通道并清除 DMA 控制器完成标志 */
    dma_channel_enable(DMA2_CHANNEL1, FALSE);
    dma_channel_enable(DMA2_CHANNEL2, FALSE);
    dma_flag_clear(DMA2_FDT1_FLAG);
    dma_flag_clear(DMA2_FDT2_FLAG);

    dma_init_type dma_init_struct;
    dma_default_para_init(&dma_init_struct);

    // RX 通道
    dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
    dma_init_struct.peripheral_base_addr = (uint32_t)&(SPIx->dt);
    dma_init_struct.peripheral_inc_enable = FALSE;
    dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
    dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    dma_init_struct.loop_mode_enable = FALSE;
    dma_init_struct.buffer_size = length;
    if(rxBuf != NULL)
    {
        dma_init_struct.memory_base_addr = (uint32_t)rxBuf;
        dma_init_struct.memory_inc_enable = TRUE;
    }
    else
    {
        dma_init_struct.memory_base_addr = (uint32_t)&SPI_DMA_RxTrash;
        dma_init_struct.memory_inc_enable = FALSE;
    }
    dma_init(DMA2_CHANNEL1, &dma_init_struct);

    // TX 通道
    dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
    if(txBuf != NULL)
    {
        dma_init_struct.memory_base_addr = (uint32_t)txBuf;
        dma_init_struct.memory_inc_enable = TRUE;
    }
    else
    {
        dma_init_struct.memory_base_addr = (uint32_t)&SPI_DMA_TxDummy;
        dma_init_struct.memory_inc_enable = FALSE;
    }
    dma_init(DMA2_CHANNEL2, &dma_init_struct);

    /* 6. 重新使能 SPI DMA 请求与 DMA 通道 */
    spi_i2s_dma_receiver_enable(SPIx, TRUE);
    spi_i2s_dma_transmitter_enable(SPIx, TRUE);

    dma_channel_enable(DMA2_CHANNEL1, TRUE);
    dma_channel_enable(DMA2_CHANNEL2, TRUE);

    /* 7. 轮询等待 DMA RX 传输完成 */
    uint32_t startTime = millis();
    while(dma_flag_get(DMA2_FDT1_FLAG) == RESET)
    {
        if((millis() - startTime) > timeoutMs)
        {
            dma_channel_enable(DMA2_CHANNEL1, FALSE);
            dma_channel_enable(DMA2_CHANNEL2, FALSE);
            return false;
        }
    }

    /* 8. 传输完成保护：等待物理移位完成并清空尾部残留 */
    while(spi_i2s_flag_get(SPIx, SPI_I2S_BF_FLAG) != RESET);
    while(spi_i2s_flag_get(SPIx, SPI_I2S_RDBF_FLAG) != RESET)
    {
        spi_i2s_data_receive(SPIx);
    }

    dma_channel_enable(DMA2_CHANNEL1, FALSE);
    dma_channel_enable(DMA2_CHANNEL2, FALSE);
    dma_flag_clear(DMA2_FDT1_FLAG);
    dma_flag_clear(DMA2_FDT2_FLAG);

    return true;
}

#if SPI_CLASS_1_ENABLE
SPIClass SPI(SPI_CLASS_1_SPI);
#endif

#if SPI_CLASS_2_ENABLE
SPIClass SPI_2(SPI_CLASS_2_SPI);
#endif

#if SPI_CLASS_3_ENABLE
SPIClass SPI_3(SPI_CLASS_3_SPI);
#endif
