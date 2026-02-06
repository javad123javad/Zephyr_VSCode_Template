#include "modem_led.h"
LOG_MODULE_DECLARE(nrf24l1_radio, CONFIG_KERNEL_LOG_LEVEL);

int led_init(const struct gpio_dt_spec *const pled_dev)
{
    int ret = 0;
    if (!gpio_is_ready_dt(pled_dev)) {
        LOG_INF("LED init problem");
        return 0;
    }
    ret = gpio_pin_configure_dt(pled_dev, GPIO_OUTPUT_ACTIVE);

    if (ret < 0) {
        LOG_INF("LED configure Probelem");
        return 0;
    }
    gpio_pin_set_dt(pled_dev, GPIO_OUTPUT_HIGH);

    return ret;
}