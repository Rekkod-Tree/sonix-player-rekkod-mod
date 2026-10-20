#!/bin/sh

# This overlay supports the BCM R1 board. HiBy 2.0's AIC MAC setup must not
# wait for another board's radio or change the MAC owned by Sonix.
mkdir -p /run
printf 'bcm\n' > /run/r1_wireless_type

sh utils.sh
sh rmem_manager.sh
sh soc_utils.sh
sh soc_i2c.sh
sh i2c_gpio_add.sh
sh axp2101.sh
sh cw2015.sh
sh keyboard_gpio_add.sh
sh soc_pwm.sh
sh pwm_backlight.sh
sh soc_fb.sh
sh soc_aic.sh
sh soc_msc.sh
sh soc_adc.sh
sh lcd_lg35583.sh
sh sa_sound_switch.sh
sh codec_cs43131.sh
sh cst8xx_touch.sh
sh bcm_wlbt_power.sh
sh keyboard_adc_multifunc.sh
sh leds_pwm_add.sh
sh sa_config.sh
sh sa_earpods_adc.sh
sh soc_efuse.sh
sh tcs1421.sh
sh x1600_hiby_r1_sound_card.sh
