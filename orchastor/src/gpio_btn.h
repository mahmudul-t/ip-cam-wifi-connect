// ========================= src/gpio_btn.h =========================
#ifndef GPIO_BTN_H
#define GPIO_BTN_H
int gpio_init(void);
int gpio_wait_long_press(void); // returns 1 on long press, 0 otherwise
#endif