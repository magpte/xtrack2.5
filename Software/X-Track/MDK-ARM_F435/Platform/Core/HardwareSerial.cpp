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
#include "HardwareSerial.h"

typedef struct
{
    usart_data_bit_num_type data_bit;
    usart_parity_selection_type parity_selection;
    usart_stop_bit_num_type stop_bit;
} SERIAL_ConfigGrp_t;

static const SERIAL_ConfigGrp_t SERIAL_ConfigGrp[] =
{
    {USART_DATA_7BITS, USART_PARITY_NONE, USART_STOP_1_BIT},   // SERIAL_7N1
    {USART_DATA_7BITS, USART_PARITY_NONE, USART_STOP_2_BIT},   // SERIAL_7N2
    {USART_DATA_7BITS, USART_PARITY_EVEN, USART_STOP_1_BIT},   // SERIAL_7E1
    {USART_DATA_7BITS, USART_PARITY_EVEN, USART_STOP_2_BIT},   // SERIAL_7E2
    {USART_DATA_7BITS, USART_PARITY_ODD,  USART_STOP_1_BIT},   // SERIAL_7O1
    {USART_DATA_7BITS, USART_PARITY_ODD,  USART_STOP_2_BIT},   // SERIAL_7O2
    {USART_DATA_7BITS, USART_PARITY_NONE, USART_STOP_0_5_BIT}, // SERIAL_7N0_5
    {USART_DATA_7BITS, USART_PARITY_NONE, USART_STOP_1_5_BIT}, // SERIAL_7N1_5
    {USART_DATA_7BITS, USART_PARITY_EVEN, USART_STOP_0_5_BIT}, // SERIAL_7E0_5
    {USART_DATA_7BITS, USART_PARITY_EVEN, USART_STOP_1_5_BIT}, // SERIAL_7E1_5
    {USART_DATA_7BITS, USART_PARITY_ODD,  USART_STOP_0_5_BIT}, // SERIAL_7O0_5
    {USART_DATA_7BITS, USART_PARITY_ODD,  USART_STOP_1_5_BIT}, // SERIAL_7O1_5

    {USART_DATA_8BITS, USART_PARITY_NONE, USART_STOP_1_BIT},   // SERIAL_8N1
    {USART_DATA_8BITS, USART_PARITY_NONE, USART_STOP_2_BIT},   // SERIAL_8N2
    {USART_DATA_8BITS, USART_PARITY_EVEN, USART_STOP_1_BIT},   // SERIAL_8E1
    {USART_DATA_8BITS, USART_PARITY_EVEN, USART_STOP_2_BIT},   // SERIAL_8E2
    {USART_DATA_8BITS, USART_PARITY_ODD,  USART_STOP_1_BIT},   // SERIAL_8O1
    {USART_DATA_8BITS, USART_PARITY_ODD,  USART_STOP_2_BIT},   // SERIAL_8O2
    {USART_DATA_8BITS, USART_PARITY_NONE, USART_STOP_0_5_BIT}, // SERIAL_8N0_5
    {USART_DATA_8BITS, USART_PARITY_NONE, USART_STOP_1_5_BIT}, // SERIAL_8N1_5
    {USART_DATA_8BITS, USART_PARITY_EVEN, USART_STOP_0_5_BIT}, // SERIAL_8E0_5
    {USART_DATA_8BITS, USART_PARITY_EVEN, USART_STOP_1_5_BIT}, // SERIAL_8E1_5
    {USART_DATA_8BITS, USART_PARITY_ODD,  USART_STOP_0_5_BIT}, // SERIAL_8O0_5
    {USART_DATA_8BITS, USART_PARITY_ODD,  USART_STOP_1_5_BIT}, // SERIAL_8O1_5

    {USART_DATA_9BITS, USART_PARITY_NONE, USART_STOP_1_BIT},   // SERIAL_9N1
    {USART_DATA_9BITS, USART_PARITY_NONE, USART_STOP_2_BIT},   // SERIAL_9N2
    {USART_DATA_9BITS, USART_PARITY_EVEN, USART_STOP_1_BIT},   // SERIAL_9E1
    {USART_DATA_9BITS, USART_PARITY_EVEN, USART_STOP_2_BIT},   // SERIAL_9E2
    {USART_DATA_9BITS, USART_PARITY_ODD,  USART_STOP_1_BIT},   // SERIAL_9O1
    {USART_DATA_9BITS, USART_PARITY_ODD,  USART_STOP_2_BIT},   // SERIAL_9O2
    {USART_DATA_9BITS, USART_PARITY_NONE, USART_STOP_0_5_BIT}, // SERIAL_9N0_5
    {USART_DATA_9BITS, USART_PARITY_NONE, USART_STOP_1_5_BIT}, // SERIAL_9N1_5
    {USART_DATA_9BITS, USART_PARITY_EVEN, USART_STOP_0_5_BIT}, // SERIAL_9E0_5
    {USART_DATA_9BITS, USART_PARITY_EVEN, USART_STOP_1_5_BIT}, // SERIAL_9E1_5
    {USART_DATA_9BITS, USART_PARITY_ODD,  USART_STOP_0_5_BIT}, // SERIAL_9O0_5
    {USART_DATA_9BITS, USART_PARITY_ODD,  USART_STOP_1_5_BIT}, // SERIAL_9O1_5
};

/**
  * @brief  串口对象构造函数
  * @param  串口外设地址
  * @retval 无
  */
HardwareSerial::HardwareSerial(usart_type* usart)
    : _USARTx(usart)
    , _callbackFunction(NULL)
    , _rxBufferHead(0)
    , _rxBufferTail(0)
    , _rxDmaChannel(NULL)
{
    memset(_rxBuffer, 0, sizeof(_rxBuffer));
}

/**
  * @brief  从 DMA 通道的剩余计数寄存器反推 _rxBufferHead
  * @note   仅在 enableRxDMA() 之后生效 (_rxDmaChannel != NULL)。
  *         DMA 以循环模式把 USARTx->dt 不断写入 _rxBuffer，硬件从不
  *         停下来通知"写到哪了"，所以头指针不是被动累加出来的，而是
  *         每次 available()/read()/peek()/flush() 被调用时，用
  *         "缓冲区大小 - 剩余未写字节数" 现算出来的——这是 STM32/AT32
  *         系列做"DMA 循环接收环形缓冲区"的标准写法，好处是完全不需要
  *         逐字节中断，DMA 传输过程中 CPU 可以做任何别的事。
  */
void HardwareSerial::_syncHeadFromDMA()
{
    uint16_t remain = dma_data_number_get(_rxDmaChannel);
    _rxBufferHead = (uint16_t)(SERIAL_RX_BUFFER_SIZE - remain) % SERIAL_RX_BUFFER_SIZE;
}

/**
  * @brief  串口中断入口
  * @param  无
  * @retval 无
  */
void HardwareSerial::IRQHandler()
{
    if(_rxDmaChannel != NULL)
    {
        /* DMA RX 模式：数据搬运完全由 DMA 完成，RDBF 中断从未被使能。
         * 1. 检查并清除 Overrun / Framing / Noise 硬件错误标志，防止 USART 接收器卡死。
         *    AT32/STM32 硬件规定：在 DMA 模式下若抛出 ROERR 错误，USART 硬件接收器
         *    会终止发送 DMA 请求，必须由软件手动清除标志位才能恢复。 */
        if(usart_flag_get(_USARTx, USART_ROERR_FLAG) != RESET ||
           usart_flag_get(_USARTx, USART_FERR_FLAG) != RESET ||
           usart_flag_get(_USARTx, USART_NERR_FLAG) != RESET)
        {
            usart_flag_clear(_USARTx, USART_ROERR_FLAG | USART_FERR_FLAG | USART_NERR_FLAG);
            usart_data_receive(_USARTx);
        }

        /* 2. IDLE (空闲线) 中断处理，通知上层有数据到达 */
        if(usart_flag_get(_USARTx, USART_IDLEF_FLAG) != RESET)
        {
            usart_data_receive(_USARTx);   // 读 DT 寄存器是硬件规定的清除 IDLE 标志位的方式之一
            usart_flag_clear(_USARTx, USART_IDLEF_FLAG);

            if(_callbackFunction)
            {
                _callbackFunction(this);
            }
        }
        return;
    }

    if(usart_flag_get(_USARTx, USART_RDBF_FLAG) != RESET)
    {
        uint8_t c = usart_data_receive(_USARTx);
        uint16_t i = (uint16_t)(_rxBufferHead + 1) % SERIAL_RX_BUFFER_SIZE;
        if (i != _rxBufferTail)
        {
            _rxBuffer[_rxBufferHead] = c;
            _rxBufferHead = i;
        }

        if(_callbackFunction)
        {
            _callbackFunction(this);
        }
        usart_flag_clear(_USARTx, USART_RDBF_FLAG);
    }
}

/**
  * @brief  串口初始化
  * @param  BaudRate: 波特率
  * @param  Config: 配置参数
  * @param  PreemptionPriority: 抢占优先级
  * @param  SubPriority: 从优先级
  * @retval 无
  */
void HardwareSerial::begin(
    uint32_t baudRate,
    SERIAL_Config_t config,
    uint8_t preemptionPriority,
    uint8_t subPriority
)
{
    gpio_type *GPIOx;
    gpio_init_type gpio_init_struct;
    uint16_t Tx_Pin, Rx_Pin;
    IRQn_Type USARTx_IRQn;

    if(_USARTx == USART1)
    {
        GPIOx = GPIOA;
        Tx_Pin = GPIO_Pin_9;
        Rx_Pin = GPIO_Pin_10;
        USARTx_IRQn = USART1_IRQn;

        crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);
    }
    else if(_USARTx == USART2)
    {
        GPIOx = GPIOA;
        Tx_Pin = GPIO_Pin_2;
        Rx_Pin = GPIO_Pin_3;
        USARTx_IRQn = USART2_IRQn;

        crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_USART2_PERIPH_CLOCK, TRUE);
    }
    else if(_USARTx == USART3)
    {
        GPIOx = GPIOB;
        Tx_Pin = GPIO_Pin_10;
        Rx_Pin = GPIO_Pin_11;
        USARTx_IRQn = USART3_IRQn;

        crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_USART3_PERIPH_CLOCK, TRUE);
    }
    else
    {
        return;
    }

    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_pins =  Tx_Pin | Rx_Pin;
    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
    gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
    gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
    gpio_init(GPIOx, &gpio_init_struct);

    gpio_pin_mux_config(GPIOx, GPIO_GetPinSource(Tx_Pin), GPIO_MUX_7);
    gpio_pin_mux_config(GPIOx, GPIO_GetPinSource(Rx_Pin), GPIO_MUX_7);

    usart_init(_USARTx, baudRate, SERIAL_ConfigGrp[config].data_bit, SERIAL_ConfigGrp[config].stop_bit);
    usart_parity_selection_config(_USARTx, SERIAL_ConfigGrp[config].parity_selection);
    usart_transmitter_enable(_USARTx, TRUE);
    usart_receiver_enable(_USARTx, TRUE);

    nvic_irq_enable(USARTx_IRQn, preemptionPriority, subPriority);
    usart_interrupt_enable(_USARTx, USART_RDBF_INT, TRUE);

    usart_enable(_USARTx, TRUE);
}

/**
  * @brief  为该串口开启 DMA 循环接收，替代逐字节 RDBF 中断
  * @note   典型用法（以 GPS 用的 Serial2/USART2 为例，在 HAL_GPS.cpp
  *         调用 GPS_SERIAL.begin(9600) 之后接一句）：
  *
  *             Serial2.enableRxDMA(
  *                 DMA1_CHANNEL4, DMA1MUX_CHANNEL4,
  *                 DMAMUX_DMAREQ_ID_USART2_RX, DMA1_Channel4_IRQn
  *             );
  *
  *         DMA 通道号/请求号只要在项目里没被别的外设占用即可，与
  *         HAL_Display.cpp 用的 EDMA_STREAM1（显示）、SPI.cpp 用的
  *         DMA2 Channel1/2（SD 卡）都不冲突（ADC 占用的是 DMA1
  *         Channel1，见 adc.c）。
  * @retval true=成功, false=参数为空
  */
bool HardwareSerial::enableRxDMA(
    dma_channel_type* dmaChannel,
    dmamux_channel_type* muxChannel,
    dmamux_requst_id_sel_type muxRequestId,
    IRQn_Type dmaIRQn,
    uint8_t preemptionPriority,
    uint8_t subPriority
)
{
    if(dmaChannel == NULL || muxChannel == NULL)
    {
        return false;
    }

    _rxDmaChannel = dmaChannel;

    /* 逐字节中断和 DMA 接收二选一：开启 DMA 循环收之后，RDBF 中断
     * 必须关掉，否则 USARTx->dt 会被 DMA 和 CPU 同时争着读，谁先读到
     * 谁清标志位，两边都可能丢数据。 */
    usart_interrupt_enable(_USARTx, USART_RDBF_INT, FALSE);

    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_DMA2_PERIPH_CLOCK, TRUE);

    dma_reset(dmaChannel);

    dma_init_type dma_init_struct;
    dma_default_para_init(&dma_init_struct);

    dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
    dma_init_struct.buffer_size = SERIAL_RX_BUFFER_SIZE;

    dma_init_struct.peripheral_base_addr = (uint32_t)&(_USARTx->dt);
    dma_init_struct.peripheral_inc_enable = FALSE;
    dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;

    dma_init_struct.memory_base_addr = (uint32_t)_rxBuffer;
    dma_init_struct.memory_inc_enable = TRUE;
    dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;

    dma_init_struct.priority = DMA_PRIORITY_MEDIUM;
    /* 循环模式：DTCNT 计数到 0 后硬件自动重装，_rxBuffer 被当作真正
     * 的环形缓冲区连续写，永不停止，不需要任何中断介入就能一直收。 */
    dma_init_struct.loop_mode_enable = TRUE;

    dma_init(dmaChannel, &dma_init_struct);

    dmamux_enable(DMA1, TRUE);
    dmamux_enable(DMA2, TRUE);
    dmamux_init(muxChannel, muxRequestId);

    usart_dma_receiver_enable(_USARTx, TRUE);

    // 注意：这里不调用 nvic_irq_enable(dmaIRQn, ...)。当前实现完全靠轮询
    // dma_data_number_get()（见 _syncHeadFromDMA()）来知道收到了多少数据，
    // 没有对这条 DMA 通道调用 dma_interrupt_enable() 去武装任何传输完成/
    // 半传输/出错中断源，也没有实现对应的 DMA1_Channel4_IRQHandler()。
    // 之前的版本在没有实际中断源、也没有 ISR 的情况下把这个向量在 NVIC
    // 里使能了，属于占着位置不干活的隐患；真要加半传输/错误中断，
    // 到时候再把 nvic_irq_enable() 和对应的 IRQHandler 一起加回来。
    (void)dmaIRQn; (void)preemptionPriority; (void)subPriority;

    usart_interrupt_enable(_USARTx, USART_IDLE_INT, TRUE);

    dma_channel_enable(dmaChannel, TRUE);

    return true;
}

/**
  * @brief  关闭串口
  * @param  无
  * @retval 无
  */
void HardwareSerial::end(void)
{
    usart_interrupt_enable(_USARTx, USART_RDBF_INT, FALSE);

    if(_rxDmaChannel != NULL)
    {
        usart_interrupt_enable(_USARTx, USART_IDLE_INT, FALSE);
        usart_dma_receiver_enable(_USARTx, FALSE);
        dma_channel_enable(_rxDmaChannel, FALSE);
        _rxDmaChannel = NULL;
    }

    usart_enable(_USARTx, FALSE);
}

/**
  * @brief  串口中断回调
  * @param  Function: 回调函数
  * @retval 无
  */
void HardwareSerial::attachInterrupt(CallbackFunction_t func)
{
    _callbackFunction = func;
}

/**
  * @brief  获取可从串行端口读取的字节数
  * @param  无
  * @retval 可读取的字节数
  */
int HardwareSerial::available(void)
{
    if(_rxDmaChannel != NULL)
    {
        _syncHeadFromDMA();
    }
    return ((unsigned int)(SERIAL_RX_BUFFER_SIZE + _rxBufferHead - _rxBufferTail)) % SERIAL_RX_BUFFER_SIZE;
}

/**
  * @brief  读取传入的串行数据(字符)
  * @param  无
  * @retval 可用的传入串行数据的第一个字节 (如果没有可用的数据, 则为-1)
  */
int HardwareSerial::read(void)
{
    if(_rxDmaChannel != NULL)
    {
        _syncHeadFromDMA();
    }

    // if the head isn't ahead of the tail, we don't have any characters
    if (_rxBufferHead == _rxBufferTail)
    {
        return -1;
    }
    else
    {
        uint8_t c = _rxBuffer[_rxBufferTail];
        _rxBufferTail = (uint16_t)(_rxBufferTail + 1) % SERIAL_RX_BUFFER_SIZE;
        return c;
    }
}

/**
  * @brief  返回传入串行数据的下一个字节(字符), 而不将其从内部串行缓冲区中删除
  * @param  无
  * @retval 可用的传入串行数据的第一个字节 (如果没有可用的数据, 则为-1)
  */
int HardwareSerial::peek(void)
{
    if(_rxDmaChannel != NULL)
    {
        _syncHeadFromDMA();
    }

    if (_rxBufferHead == _rxBufferTail)
    {
        return -1;
    }
    else
    {
        return _rxBuffer[_rxBufferTail];
    }
}

/**
  * @brief  清空串口缓存
  * @param  无
  * @retval 无
  */
void HardwareSerial::flush(void)
{
    if(_rxDmaChannel != NULL)
    {
        /* DMA 模式下 _rxBufferHead 是由硬件计数寄存器现算出来的只读值，
         * 不能像普通模式那样反向赋值；要丢弃已收到的数据，只能把
         * _rxBufferTail 追到当前算出来的 head。 */
        _syncHeadFromDMA();
        _rxBufferTail = _rxBufferHead;
    }
    else
    {
        _rxBufferHead = _rxBufferTail;
    }
}

/**
  * @brief  串口写入一个字节
  * @param  写入的字节
  * @retval 字节
  */
size_t HardwareSerial::write(uint8_t n)
{
    while(usart_flag_get(_USARTx, USART_TDBE_FLAG) == RESET) {};
    usart_data_transmit(_USARTx, n);
    return 1;
}

#if SERIAL_1_ENABLE
HardwareSerial Serial(SERIAL_1_USART);

extern "C" SERIAL_1_IRQ_HANDLER_DEF()
{
    Serial.IRQHandler();
}
#endif

#if SERIAL_2_ENABLE
HardwareSerial Serial2(SERIAL_2_USART);

extern "C" SERIAL_2_IRQ_HANDLER_DEF()
{
    Serial2.IRQHandler();
}
#endif

#if SERIAL_3_ENABLE
HardwareSerial Serial3(SERIAL_3_USART);

extern "C" SERIAL_3_IRQ_HANDLER_DEF()
{
    Serial3.IRQHandler();
}
#endif
