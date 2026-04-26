// SPDX-License-Identifier: GPL-2.0
/*
 * project.c - Main initialization sequence for AVerMedia C985
 *
 * Boot sequence:
 * 1. Hardware init (NUC100, TI3101)
 * 2. Firmware upload (done in firmware.c via cqlcodec)
 * 3. Ring doorbell → ARM boots
 * 4. QPFWAPI_Init → verify mailbox ready
 * 5. QPFWCODECAPI_SystemOpen → initialize encoder subsystem
 * 6. QPFWCODECAPI_SystemLink → connect video/audio paths
 * 7. Register V4L2 device
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "avermedia_c985.h"
#include "nuc100.h"
#include "ti3101.h"
#include "cpr.h"
#include "qpfwapi.h"
#include "qpfwencapi.h"
#include "i2c_bitbang.h"
#include "v4l2.h"
#include "cqlcodec.h"

/* GPIO registers */
#define REG_GPIO_DIR    0x0610
#define REG_GPIO_VAL    0x0614

/*
 * init_hardware - Initialize NUC100 MCU and TI3101 HDMI receiver
 */
static int init_hardware(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "Initializing hardware...\n");

    /* Use hw_config from project struct for MCU address */
    d->m_McuAddr = d->project.m_averHwConfig.mcu_addr;

    /* NUC100 reset via project GPIO config */
    gpio_drive_low(d, d->project.m_gpio_mcu_reset);
    msleep(10);
    gpio_release(d, d->project.m_gpio_mcu_reset);
    msleep(100);

    ret = nuc100_check_device(d);
    if (ret) {
        dev_err(&d->pdev->dev, "NUC100 not responding\n");
        return ret;
    }

    /* TI3101 HDMI receiver (GPIO 0/2) */
    ti3101_hw_reset(d);
    ret = ti3101_probe(d);
    if (ret) {
        dev_err(&d->pdev->dev, "TI3101 probe failed\n");
        return ret;
    }

    ret = ti3101_init(d);
    if (ret) {
        dev_err(&d->pdev->dev, "TI3101 init failed\n");
        return ret;
    }

    dev_info(&d->pdev->dev, "Hardware init complete\n");
    return 0;
}

/*
 * check_hdmi_signal - Check if HDMI input is present
 *
 * Populates project.m_hdmi_video_info with detected timing.
 */
static int check_hdmi_signal(struct c985_poc *d)
{
    struct hdmi_info *info = &d->project.m_hdmi_video_info;
    int valid = 0;
    int ret;

    ret = nuc100_getHdmiVideo_6604(d, info, &valid);
    if (ret == 0 && valid) {
        u32 fps = 0;

        if (info->HTotal && info->VTotal)
            fps = (info->PCLK * 1000) / (info->HTotal * info->VTotal);

        dev_info(&d->pdev->dev,
                 "HDMI: %ux%u @ %u Hz (pclk %d kHz)\n",
                 info->HActive, info->VActive, fps, info->PCLK);

        /* Store detected resolution for V4L2 */
        d->width = info->HActive;
        d->height = info->VActive;
        d->hdmi_valid = 1;
        d->project.m_hdmi_status = 1;
        return 0;
    }

    dev_info(&d->pdev->dev, "No valid HDMI signal detected\n");
    d->hdmi_valid = 0;
    d->project.m_hdmi_status = 0;

    /* Default resolution */
    d->width = 1920;
    d->height = 1080;

    return -ENOLINK;
}

/*
 * init_encoder - Initialize encoder subsystem via firmware API
 */
static int init_encoder(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "Initializing encoder...\n");

    /* Ring doorbell to wake ARM from WFI */
    arm_ring_doorbell(d);

    /* Wait for firmware to boot */
    msleep(500);

    /* Initialize firmware API */
    ret = QPFWAPI_Init(d);
    if (ret) {
        dev_err(&d->pdev->dev, "QPFWAPI_Init failed: %d\n", ret);
        return ret;
    }

    /* SystemOpen: use configured encoder function */
 /*   dev_dbg(&d->pdev->dev, "Calling SystemOpen (function=0x%08x)\n",
            d->m_EncFunction);
    ret = QPFWCODECAPI_SystemOpen(&d->codec, 0, d->m_EncFunction);
    if (ret) {
        dev_err(&d->pdev->dev, "SystemOpen failed: %d\n", ret);
        return ret;
    }
*/
    /*
     * SystemLink: Connect video/audio paths using configured link params
     *
     * Values come from project config (m_EncLink{Vin,Vout,Ain,Aout})
     */
  /*  dev_dbg(&d->pdev->dev, "Calling SystemLink (vin=%u vout=%u ain=%u aout=%u)\n",
            d->m_EncLinkVin, d->m_EncLinkVout,
            d->m_EncLinkAin, d->m_EncLinkAout);
    ret = QPFWCODECAPI_SystemLink(&d->codec, 0,
                                  d->m_EncLinkVin, d->m_VidInputChannel,
                                  d->m_EncLinkVout, 0,
                                  d->m_EncLinkAin, 0,
                                  d->m_EncLinkAout, 0);
    if (ret) {
        dev_err(&d->pdev->dev, "SystemLink failed: %d\n", ret);
        return ret;
    }
*/
    dev_info(&d->pdev->dev, "Encoder initialized\n");
    return 0;
}

/*
 * project_c985_init - Main initialization entry point
 *
 * Called after firmware has been loaded and interrupts registered.
 */
int project_c985_init(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "=== C985 Project Init ===\n");
    c985_debugfs_init(d);

    /* Step 1: Hardware init (NUC100, TI3101) */
    ret = init_hardware(d);
    if (ret)
        return ret;

    /* Step 2: Check HDMI signal (non-fatal) */
    check_hdmi_signal(d);

    /* Step 3: Initialize encoder subsystem */
    ret = init_encoder(d);
    if (ret)
        return ret;

    /* Step 4: Register V4L2 device */
    ret = c985_v4l2_register(d);
    if (ret) {
        dev_err(&d->pdev->dev, "V4L2 registration failed: %d\n", ret);
        return ret;
    }

    dev_info(&d->pdev->dev, "C985 initialization complete\n");
    return 0;
}

/*
 * project_c985_cleanup - Cleanup on module unload
 */
void project_c985_cleanup(struct c985_poc *d)
{
    dev_info(&d->pdev->dev, "=== C985 Project Cleanup ===\n");
    c985_debugfs_cleanup(d);

    /* Unregister V4L2 */
    c985_v4l2_unregister(d);

    /* Stop encoder if running */
    if (d->encoder_running) {
        QPFWENCAPI_StopEncoder(d, 8, 0, 0);
        d->encoder_running = false;
    }

    dev_info(&d->pdev->dev, "Cleanup complete\n");
}

/*
 * ProjectC985_selectInputSource - Switch audio input source via codec GPIOs
 *
 * This uses CQLCodec GPIO bit direction/value to route audio through
 * the hardware mux on the C985 board.
 *
 * GPIO select_1 / select_2 control the analog mux:
 *   External jack: select_1=HIGH, select_2=LOW  → TI3101 audio path
 *   HDMI audio:    select_1=LOW,  select_2=HIGH → SiI6604 audio path
 */
void ProjectC985_selectInputSource(struct c985_poc *d,
                                   enum project_input_control param_1)
{
    struct gpio_bit_direction dir;
    struct gpio_bit_value val;

    dev_dbg(&d->pdev->dev, "ProjectC985_selectInputSource() entry (%d)\n",
            param_1);

    if (param_1 == PROJECT_AUD_EXTERNAL_INPUT_JACK) {
        /* Set GPIO direction for audio select pins (output) */
        dir.bBitNumber = d->project.m_gpio_audio_select_1;
        dir.Direction = GPIO_DIR_OUTPUT;
        CQLCodec_SetGPIOBitDirection(d, &dir);

        dir.bBitNumber = d->project.m_gpio_audio_select_2;
        dir.Direction = GPIO_DIR_OUTPUT;
        CQLCodec_SetGPIOBitDirection(d, &dir);

        /* Set GPIO values: select_1=HIGH, select_2=LOW */
        val.bBitNumber = d->project.m_gpio_audio_select_1;
        val.bValue = 1;
        CQLCodec_SetGPIOBitValue(d, &val);

        val.bBitNumber = d->project.m_gpio_audio_select_2;
        val.bValue = 0;
        CQLCodec_SetGPIOBitValue(d, &val);

        d->project.m_c985_audio_in = C985_AUDIO_INPUT_3101;

    } else if (param_1 == PROJECT_AUD_HDMI_INPUT) {
        /* Set GPIO direction for audio select pins (output) */
        dir.bBitNumber = d->project.m_gpio_audio_select_1;
        dir.Direction = GPIO_DIR_OUTPUT;
        CQLCodec_SetGPIOBitDirection(d, &dir);

        dir.bBitNumber = d->project.m_gpio_audio_select_2;
        dir.Direction = GPIO_DIR_OUTPUT;
        CQLCodec_SetGPIOBitDirection(d, &dir);

        /* Set GPIO values: select_1=LOW, select_2=HIGH */
        val.bBitNumber = d->project.m_gpio_audio_select_1;
        val.bValue = 0;
        CQLCodec_SetGPIOBitValue(d, &val);

        val.bBitNumber = d->project.m_gpio_audio_select_2;
        val.bValue = 1;
        CQLCodec_SetGPIOBitValue(d, &val);

        d->project.m_c985_audio_in = C985_AUDIO_INPUT_6604;
    }
}


