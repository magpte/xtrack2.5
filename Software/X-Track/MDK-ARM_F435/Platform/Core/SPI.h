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
#ifndef __SPI_H
#define __SPI_H

#include "Arduino.h"

/*
 * Tells Libraries/SdFat/src/SpiDriver/SdSpiDriver.h that SPIClass below
 * provides transferDMA() (DMA2 Channel1/2, SPI2 only - see SPI.cpp), so
 * it can route SD card bulk reads/writes through it instead of the
 * byte-at-a-time transfer() loop. Must be defined before SdSpiDriver.h
 * is included; SdSpiDriver.h defaults it to 0 if nothing defines it.
 */
#define SD_SPI_HAS_DMA_TRANSFER 1

#ifndef LSBFIRST
#  define LSBFIRST 0
#endif
#ifndef MSBFIRST
#  define MSBFIRST 1
#endif

#if SPI_CLASS_PIN_DEFINE_ENABLE
#  define SS      PA4
#  define SCK     PA5
#  define MISO    PA6
#  define MOSI    PA7
#endif

#define DATA_SIZE_8BIT  SPI_FRAME_8BIT
#define DATA_SIZE_16BIT SPI_FRAME_16BIT

#define SPI_I2S_GET_FLAG(spix, SPI_I2S_FLAG) (spix->sts & SPI_I2S_FLAG)
#define SPI_I2S_RXDATA(spix)                 (spix->dt)
#define SPI_I2S_RXDATA_VOLATILE(spix)        volatile uint16_t vn = SPI_I2S_RXDATA(spix)
#define SPI_I2S_TXDATA(spix, data)           (spix->dt = (data))
#define SPI_I2S_WAIT_RX(spix)                do{ while (!SPI_I2S_GET_FLAG(spix, SPI_I2S_RDBF_FLAG)); } while(0)
#define SPI_I2S_WAIT_TX(spix)                do{ while (!SPI_I2S_GET_FLAG(spix, SPI_I2S_TDBE_FLAG)); } while(0)
#define SPI_I2S_WAIT_BUSY(spix)              do{ while (SPI_I2S_GET_FLAG(spix,  SPI_I2S_BF_FLAG));   } while(0)

typedef enum
{
    SPI_MODE0,
    SPI_MODE1,
    SPI_MODE2,
    SPI_MODE3
} SPI_MODE_TypeDef;

typedef enum
{
    SPI_STATE_IDLE,
    SPI_STATE_READY,
    SPI_STATE_RECEIVE,
    SPI_STATE_TRANSMIT,
    SPI_STATE_TRANSFER
} spi_mode_t;

class SPISettings
{
public:
    SPISettings(uint32_t clock, uint16_t bitOrder, uint8_t dataMode)
    {
        init_AlwaysInline(clock, bitOrder, dataMode, DATA_SIZE_8BIT);
    }
    SPISettings(uint32_t clock, uint16_t bitOrder, uint8_t dataMode, uint32_t dataSize)
    {
        init_AlwaysInline(clock, bitOrder, dataMode, dataSize);
    }
    SPISettings(uint32_t clock)
    {
        init_AlwaysInline(clock, MSBFIRST, SPI_MODE0, DATA_SIZE_8BIT);
    }
    SPISettings()
    {
        init_AlwaysInline(4000000, MSBFIRST, SPI_MODE0, DATA_SIZE_8BIT);
    }
private:
    void init_MightInline(uint32_t clock, uint16_t bitOrder, uint8_t dataMode, uint32_t dataSize)
    {
        init_AlwaysInline(clock, bitOrder, dataMode, dataSize);
    }
    void init_AlwaysInline(uint32_t clock, uint16_t bitOrder, uint8_t dataMode, uint32_t dataSize) __attribute__((__always_inline__))
    {
        this->clock = clock;
        this->bitOrder = bitOrder;
        this->dataMode = dataMode;
        this->dataSize = dataSize;
    }
    uint32_t clock;
    uint16_t bitOrder;
    uint8_t dataMode;
    uint32_t dataSize;

    friend class SPIClass;
};

class SPIClass
{
public:
    SPIClass(spi_type* spix);
    void SPI_Settings(
        spi_master_slave_mode_type master_slave_mode,
        spi_frame_bit_num_type frame_bit_num,
        uint16_t SPI_MODEx,
        spi_cs_mode_type cs_mode,
        spi_mclk_freq_div_type mclk_freq_div,
        spi_first_bit_type first_bit
    );
    void begin(void);
    void begin(uint32_t clock, uint16_t dataOrder, uint16_t dataMode);
    void begin(SPISettings settings);
    void beginSlave(uint32_t bitOrder, uint32_t mode);
    void beginSlave(void);
    void beginTransactionSlave(void);
    void beginTransaction(SPISettings settings);

    void endTransaction(void);
    void end(void);

    void setClock(uint32_t clock);
    void setClockDivider(uint32_t Div);
    void setBitOrder(uint16_t bitOrder);
    void setDataMode(uint8_t dataMode);
    void setDataSize(uint32_t datasize);

    uint16_t read(void);
    void read(uint8_t *buffer, uint32_t length);
    void write(uint16_t data);
    void write(uint16_t data, uint32_t n);
    void write(const uint8_t *data, uint32_t length);
    void write(const uint16_t *data, uint32_t length);
    uint8_t transfer(uint8_t data) const;
    uint16_t transfer16(uint16_t data) const;
    uint8_t send(uint8_t data);
    uint8_t send(uint8_t *data, uint32_t length);
    uint8_t recv(void);

    /*
     * DMA 加速的批量全双工传输（一次 DMA 搬运一整块数据，而不是像
     * transfer()/send()/read() 那样每字节软件轮询 TDBE/RDBF 标志位）。
     * 目前只为 SPI2（SD 卡总线，见 HAL_Config.h 里的 CONFIG_SD_SPI）
     * 接了 DMA2 Channel1(RX)/Channel2(TX)；其它 SPI 实例调用会直接
     * 回退到 write()/read() 的逐字节实现，行为不变、只是没有加速。
     *
     *   txBuf == nullptr : 发送方向填充 0xFF（SD 卡协议读操作的标准
     *                       占位字节），不占用一块额外的发送缓冲区
     *   rxBuf == nullptr : 接收到的数据直接丢弃（SD 卡协议写操作只
     *                       关心发出去的数据，不关心收回来的内容）
     *
     * 返回 false 表示这次调用没有用上 DMA（要么不是 SPI2，要么等待
     * 完成超时），调用方此时应自行回退到逐字节 transfer()。
     */
    bool transferDMA(const uint8_t* txBuf, uint8_t* rxBuf, uint32_t length, uint32_t timeoutMs = 200);

    spi_type* getSPI()
    {
        return SPIx;
    }

private:
    bool _initDMA();

    spi_type* SPIx;
    spi_init_type spi_init_struct;
    uint32_t SPI_Clock;
    bool _dmaReady;
};

#if SPI_CLASS_1_ENABLE
extern SPIClass SPI;
#endif

#if SPI_CLASS_2_ENABLE
extern SPIClass SPI_2;
#endif

#if SPI_CLASS_3_ENABLE
extern SPIClass SPI_3;
#endif

#endif
