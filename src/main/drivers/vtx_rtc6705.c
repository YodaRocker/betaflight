/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Author: Giles Burgess (giles@multiflite.co.uk)
 *
 * This source code is provided as is and can be used/modified so long
 * as this header is maintained with the file at all times.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#ifdef VTX

#include "common/maths.h"

#include "drivers/vtx_rtc6705.h"
#include "drivers/bus_spi.h"
#include "drivers/system.h"

#define RTC6705_SET_HEAD 0x3210 //fosc=8mhz r=400
#define RTC6705_SET_A1 0x8F3031 //5865
#define RTC6705_SET_A2 0x8EB1B1 //5845
#define RTC6705_SET_A3 0x8E3331 //5825
#define RTC6705_SET_A4 0x8DB4B1 //5805
#define RTC6705_SET_A5 0x8D3631 //5785
#define RTC6705_SET_A6 0x8CB7B1 //5765
#define RTC6705_SET_A7 0x8C4131 //5745
#define RTC6705_SET_A8 0x8BC2B1 //5725
#define RTC6705_SET_B1 0x8BF3B1 //5733
#define RTC6705_SET_B2 0x8C6711 //5752
#define RTC6705_SET_B3 0x8CE271 //5771
#define RTC6705_SET_B4 0x8D55D1 //5790
#define RTC6705_SET_B5 0x8DD131 //5809
#define RTC6705_SET_B6 0x8E4491 //5828
#define RTC6705_SET_B7 0x8EB7F1 //5847
#define RTC6705_SET_B8 0x8F3351 //5866
#define RTC6705_SET_E1 0x8B4431 //5705
#define RTC6705_SET_E2 0x8AC5B1 //5685
#define RTC6705_SET_E3 0x8A4731 //5665
#define RTC6705_SET_E4 0x89D0B1 //5645
#define RTC6705_SET_E5 0x8FA6B1 //5885
#define RTC6705_SET_E6 0x902531 //5905
#define RTC6705_SET_E7 0x90A3B1 //5925
#define RTC6705_SET_E8 0x912231 //5945
#define RTC6705_SET_F1 0x8C2191 //5740
#define RTC6705_SET_F2 0x8CA011 //5760
#define RTC6705_SET_F3 0x8D1691 //5780
#define RTC6705_SET_F4 0x8D9511 //5800
#define RTC6705_SET_F5 0x8E1391 //5820
#define RTC6705_SET_F6 0x8E9211 //5840
#define RTC6705_SET_F7 0x8F1091 //5860
#define RTC6705_SET_F8 0x8F8711 //5880
#define RTC6705_SET_R1 0x8A2151 //5658
#define RTC6705_SET_R2 0x8B04F1 //5695
#define RTC6705_SET_R3 0x8BF091 //5732
#define RTC6705_SET_R4 0x8CD431 //5769
#define RTC6705_SET_R5 0x8DB7D1 //5806
#define RTC6705_SET_R6 0x8EA371 //5843
#define RTC6705_SET_R7 0x8F8711 //5880
#define RTC6705_SET_R8 0x9072B1 //5917

#define RTC6705_SET_R  400     //Reference clock
#define RTC6705_SET_FDIV 1024  //128*(fosc/1000000)
#define RTC6705_SET_NDIV 16    //Remainder divider to get 'A' part of equation
#define RTC6705_SET_WRITE 0x11 //10001b to write to register
#define RTC6705_SET_DIVMULT 1000000 //Division value (to fit into a uint32_t) (Hz to MHz)

#define DISABLE_RTC6705 GPIO_SetBits(RTC6705_CS_GPIO,   RTC6705_CS_PIN)
#define ENABLE_RTC6705  GPIO_ResetBits(RTC6705_CS_GPIO, RTC6705_CS_PIN)

// Define variables
static const uint32_t channelArray[RTC6705_BAND_MAX][RTC6705_CHANNEL_MAX] = {
    { RTC6705_SET_A1, RTC6705_SET_A2, RTC6705_SET_A3, RTC6705_SET_A4, RTC6705_SET_A5, RTC6705_SET_A6, RTC6705_SET_A7, RTC6705_SET_A8 },
    { RTC6705_SET_B1, RTC6705_SET_B2, RTC6705_SET_B3, RTC6705_SET_B4, RTC6705_SET_B5, RTC6705_SET_B6, RTC6705_SET_B7, RTC6705_SET_B8 },
    { RTC6705_SET_E1, RTC6705_SET_E2, RTC6705_SET_E3, RTC6705_SET_E4, RTC6705_SET_E5, RTC6705_SET_E6, RTC6705_SET_E7, RTC6705_SET_E8 },
    { RTC6705_SET_F1, RTC6705_SET_F2, RTC6705_SET_F3, RTC6705_SET_F4, RTC6705_SET_F5, RTC6705_SET_F6, RTC6705_SET_F7, RTC6705_SET_F8 },
    { RTC6705_SET_R1, RTC6705_SET_R2, RTC6705_SET_R3, RTC6705_SET_R4, RTC6705_SET_R5, RTC6705_SET_R6, RTC6705_SET_R7, RTC6705_SET_R8 },
};

/**
 * Send a command and return if good
 * TODO chip detect
 */
static bool rtc6705IsReady(void)
{
    // Sleep a little bit to make sure it has booted
    delay(50);

    // TODO Do a read and get current config (note this would be reading on MOSI (data) line)

    return true;
}

/**
 * Reverse a uint32_t (LSB to MSB)
 * This is easier for when generating the frequency to then
 * reverse the bits afterwards
 */
static uint32_t reverse32(uint32_t in)
{
    uint32_t out = 0;

    for (uint8_t i = 0 ; i < 32 ; i++)
    {
        out |= ((in>>i) & 1)<<(31-i);
    }

    return out;
}

/**
 * Start chip if available
 */
bool rtc6705Init(void)
{
    DISABLE_RTC6705;
    spiSetDivisor(RTC6705_SPI_INSTANCE, SPI_CLOCK_SLOW);
    return rtc6705IsReady();
}

/**
 * Transfer a 25bit packet to RTC6705
 * This will just send it as a 32bit packet LSB meaning
 * extra 0's get truncated on RTC6705 end
 */
static void rtc6705Transfer(uint32_t command)
{
    command = reverse32(command);

    ENABLE_RTC6705;

    spiTransferByte(RTC6705_SPI_INSTANCE, (command >> 24) & 0xFF);
    spiTransferByte(RTC6705_SPI_INSTANCE, (command >> 16) & 0xFF);
    spiTransferByte(RTC6705_SPI_INSTANCE, (command >> 8) & 0xFF);
    spiTransferByte(RTC6705_SPI_INSTANCE, (command >> 0) & 0xFF);

    DISABLE_RTC6705;
}

/**
 * Set a band and channel
 */
void rtc6705SetChannel(uint8_t band, uint8_t channel)
{
    band = constrain(band, RTC6705_BAND_MIN, RTC6705_BAND_MAX);
    channel = constrain(channel, RTC6705_CHANNEL_MIN, RTC6705_CHANNEL_MAX);

    rtc6705Transfer(RTC6705_SET_HEAD);
    rtc6705Transfer(channelArray[band-1][channel-1]);
}

 /**
 * Set a freq in mhz
 * Formula derived from datasheet
 */
void rtc6705SetFreq(uint16_t freq)
{
    freq = constrain(freq, RTC6705_FREQ_MIN, RTC6705_FREQ_MAX);

    uint32_t val_hex = 0;

    uint32_t val_a = ((((uint64_t)freq*(uint64_t)RTC6705_SET_DIVMULT*(uint64_t)RTC6705_SET_R)/(uint64_t)RTC6705_SET_DIVMULT) % RTC6705_SET_FDIV) / RTC6705_SET_NDIV; //Casts required to make sure correct math (large numbers)
    uint32_t val_n = (((uint64_t)freq*(uint64_t)RTC6705_SET_DIVMULT*(uint64_t)RTC6705_SET_R)/(uint64_t)RTC6705_SET_DIVMULT) / RTC6705_SET_FDIV; //Casts required to make sure correct math (large numbers)
 
    val_hex |= RTC6705_SET_WRITE;
    val_hex |= (val_a << 5);
    val_hex |= (val_n << 12);

    rtc6705Transfer(RTC6705_SET_HEAD);
    rtc6705Transfer(val_hex);
}

#endif
