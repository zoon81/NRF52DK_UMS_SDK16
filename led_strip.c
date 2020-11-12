/**
 * Copyright (c) 2017 - 2019, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "led_strip.h"
#include "nrfx_spim.h"
#include "app_util_platform.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "boards.h"
#include "app_error.h"
#include <string.h>
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "ff.h"

#define NRFX_SPIM_SCK_PIN  (32 + 11)
#define NRFX_SPIM_MOSI_PIN (32 + 10)
#define APA102_GLOBAL_VALUE 0xFF

#define SPI_INSTANCE 3                                           /**< SPI instance index. */
static const nrfx_spim_t spi = NRFX_SPIM_INSTANCE(SPI_INSTANCE); /**< SPI instance. */
static volatile bool spi_xfer_done; /**< Flag used to indicate that SPI instance completed the transfer. */

#define PRELOADBUFFER_SIZE 8 // This is define the size of how many preloaded ledstrip data buffer can be stored in preloadbuffer
static uint8_t m_preload_buffer[NUMBER_OF_LEDS * 3 * PRELOADBUFFER_SIZE];
struct preload_ringbuf_s{
    uint8_t *buffer;
    uint16_t buffer_len;
    uint16_t buffer_head;
    uint16_t buffer_tail;
    uint16_t remaining_space;
};
struct preload_ringbuf_s preload_buffer;

static uint8_t m_tx_buf[NUMBER_OF_LEDS * 4];

nrfx_spim_xfer_desc_t xfer_desc;

void spim_event_handler(nrfx_spim_evt_t const *p_event,
                        void *p_context)
{
    spi_xfer_done = true;
    NRF_LOG_INFO("Transfer completed.");
}

void led_strip_init(void)
{
    xfer_desc.p_tx_buffer = (const uint8_t *) m_tx_buf;
    xfer_desc.p_rx_buffer = NULL;
    xfer_desc.tx_length = sizeof(m_tx_buf);

    nrfx_spim_config_t spi_config = NRFX_SPIM_DEFAULT_CONFIG;
    spi_config.frequency = NRF_SPIM_FREQ_1M;
    spi_config.mosi_pin = NRFX_SPIM_MOSI_PIN;
    spi_config.sck_pin = NRFX_SPIM_SCK_PIN;
    spi_config.use_hw_ss = false;
    spi_config.ss_active_high = false;
    APP_ERROR_CHECK(nrfx_spim_init(&spi, &spi_config, spim_event_handler, NULL));

    NRF_LOG_INFO("NRFX SPIM example started.");

    // Reset rx buffer and transfer done flag
    // Set the constant startFrame and endFrame on framebuffer
    memset(&m_tx_buf[0], 0x00, 4);
    memset(&m_tx_buf[sizeof(m_tx_buf) - 1 -4], 0xff, 4);

    struct preload_ringbuf_s preload_buffer;
    preload_buffer.buffer = m_preload_buffer;
    preload_buffer.buffer_len = sizeof(m_preload_buffer);
    preload_buffer.buffer_head = 0;
    preload_buffer.buffer_tail = 0;
    preload_buffer.remaining_space = preload_buffer.buffer_len;
 
    led_strip_setAllLedColor(16,0,0);
    while (!spi_xfer_done)
    {
        __WFE();
    }

    NRF_LOG_FLUSH();
}

void led_strip_setAllLedColor(uint8_t r, uint8_t g, uint8_t b){
    uint8_t i;
    for(i = 0; i < NUMBER_OF_LEDS; i++){
       led_strip_setColor(r, g, b, i);
    }
    spi_xfer_done = false;
    APP_ERROR_CHECK(nrfx_spim_xfer_dcx(&spi, &xfer_desc, 0, 0));
}

void led_strip_setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t led_number){
    m_tx_buf[(led_number + 1) * 4 + 0] = APA102_GLOBAL_VALUE;
    m_tx_buf[(led_number + 1) * 4 + 1] = b;
    m_tx_buf[(led_number + 1) * 4 + 2] = g;
    m_tx_buf[(led_number + 1) * 4 + 3] = r;
}
// Send m_tx_buff to led_strip
void led_strip_writeFrameBuffer(){
    // Remove memset later, it is unneceserly
    memset(&m_tx_buf[0], 0x00, 4);
    memset(&m_tx_buf[sizeof(m_tx_buf) - 1 -4], 0xff, 4);
    APP_ERROR_CHECK(nrfx_spim_xfer_dcx(&spi, &xfer_desc, 0, 0));
}

// Preload data from SDCard, this can be called after data cipied to led_strip m_tx_buff or in init phase
bool led_strip_preloadFrame(FIL *fh){
    uint16_t readed_bytes;
    f_read(fh, m_preload_buffer, preload_buffer.remaining_space, &readed_bytes);
    if(readed_bytes == preload_buffer.remaining_space){
        ringbuf_bufferWritten(&preload_buffer, readed_bytes);
        return false;
    } else {
        return true;
    }
}
// Inform the ringbuffer handler about data buffer filled, calculate head pos, and remaining space
void ringbuf_bufferWritten(struct preload_ringbuf_s * ringbuff, uint16_t bw){
    if(ringbuff->buffer_len <= ringbuff->buffer_head + bw){
        ringbuff->buffer_head = 0;
        ringbuff->remaining_space = 0;
    } else {
        ringbuff->buffer_head += bw;
        ringbuff->remaining_space -= bw;
    }
}
