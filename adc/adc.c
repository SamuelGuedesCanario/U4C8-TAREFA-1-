#include <stdio.h>
#include <stdlib.h>
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "ssd1306.h"
#include "font.h"

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define DISPLAY_ADDR 0x3C

#define JOYSTICK_X 26  // Pino GPIO do eixo X
#define JOYSTICK_Y 27  // Pino GPIO do eixo Y
#define JOYSTICK_BTN 22 // Pino GPIO do botão do joystick
#define BUTTON_A 5 
#define BUTTON_B 6

#define GREEN_LED 11
#define BLUE_LED 12
#define RED_LED 13

// Variáveis globais
static volatile uint32_t last_button_press_time = 0; // Armazena o tempo do último evento para debounce
ssd1306_t display; // Estrutura para controle do display
const float pwm_clock_div = 255.0;
const uint16_t pwm_wrap_value = 4095;
uint16_t pwm_blue_slice;
uint16_t pwm_red_slice;
bool pwm_enabled = true;  // Estado do PWM

// Função de interrupção para tratamento de eventos nos botões (com debounce)
void gpio_interrupt_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if (current_time - last_button_press_time > 200000) { // 200 ms de debounce
        last_button_press_time = current_time;

        if (gpio == BUTTON_A) {
            // Alterna o estado do PWM dos LEDs vermelho e azul
            pwm_set_enabled(pwm_blue_slice, pwm_enabled = !pwm_enabled);
            pwm_set_enabled(pwm_red_slice, pwm_enabled);
        }

        if (gpio == BUTTON_B) {
            // Reinicia o microcontrolador via USB
            reset_usb_boot(0, 0);
        }

        if (gpio == JOYSTICK_BTN) {
            // Alterna o estado do LED verde
            gpio_put(GREEN_LED, !gpio_get(GREEN_LED));

            // Adiciona um efeito visual no display
            ssd1306_rect(&display, 6, 6, 115, 55, gpio_get(GREEN_LED), 0); 
        }
    }
}

int main() {
    // Configuração dos botões e LEDs
    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);
    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_interrupt_handler);

    gpio_init(JOYSTICK_BTN);
    gpio_set_dir(JOYSTICK_BTN, GPIO_IN);
    gpio_pull_up(JOYSTICK_BTN);
    gpio_set_irq_enabled_with_callback(JOYSTICK_BTN, GPIO_IRQ_EDGE_FALL, true, &gpio_interrupt_handler);

    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_pull_up(BUTTON_A);
    gpio_set_irq_enabled_with_callback(BUTTON_A, GPIO_IRQ_EDGE_FALL, true, &gpio_interrupt_handler);

    gpio_init(GREEN_LED);
    gpio_set_dir(GREEN_LED, GPIO_OUT);
    gpio_pull_up(GREEN_LED);

    // Configuração do LED azul como PWM
    gpio_set_function(BLUE_LED, GPIO_FUNC_PWM);
    pwm_blue_slice = pwm_gpio_to_slice_num(BLUE_LED);
    pwm_set_clkdiv(pwm_blue_slice, pwm_clock_div);
    pwm_set_wrap(pwm_blue_slice, pwm_wrap_value);
    pwm_set_gpio_level(BLUE_LED, 0);
    pwm_set_enabled(pwm_blue_slice, true);
    
    // Configuração do LED vermelho como PWM
    gpio_set_function(RED_LED, GPIO_FUNC_PWM);
    pwm_red_slice = pwm_gpio_to_slice_num(RED_LED);
    pwm_set_clkdiv(pwm_red_slice, pwm_clock_div);
    pwm_set_wrap(pwm_red_slice, pwm_wrap_value);
    pwm_set_gpio_level(RED_LED, 0);
    pwm_set_enabled(pwm_red_slice, true);

    // Inicialização do display OLED
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&display, WIDTH, HEIGHT, false, DISPLAY_ADDR, I2C_PORT);
    ssd1306_config(&display);
    ssd1306_send_data(&display);
    ssd1306_fill(&display, false);
    ssd1306_send_data(&display);

    // Desenha uma borda inicial no display
    ssd1306_rect(&display, 3, 3, 122, 60, 1, 0);

    // Inicialização do ADC para leitura do joystick
    adc_init();
    adc_gpio_init(JOYSTICK_X);
    adc_gpio_init(JOYSTICK_Y);

    // Variáveis para rastrear o movimento do quadrado
    uint16_t adc_x_value, adc_y_value;
    uint16_t adc_x_prev, adc_y_prev;
    uint16_t x_offset, y_offset;
    
    // Calibração inicial do joystick
    adc_select_input(1);
    adc_x_prev = adc_read();
    x_offset = adc_read();
    
    adc_select_input(0);
    y_offset = adc_read();
    adc_y_prev = adc_read();

    while (true) {
        // Leitura do ADC para o eixo X
        adc_select_input(1);
        adc_x_value = adc_read();
        pwm_set_gpio_level(RED_LED, abs(adc_x_value - x_offset));

        // Leitura do ADC para o eixo Y
        adc_select_input(0);
        adc_y_value = adc_read();
        pwm_set_gpio_level(BLUE_LED, abs(adc_y_value - y_offset));

        // Ajuste da posição do quadrado para não ultrapassar as bordas
        adc_x_value = adc_x_value / 39 + 8;           
        adc_y_value = (4096 - adc_y_value) / 91 + 7;  

        // Apaga o quadrado na posição anterior e desenha na nova posição
        ssd1306_rect(&display, adc_y_prev, adc_x_prev, 8, 8, 0, 1);
        ssd1306_rect(&display, adc_y_value, adc_x_value, 8, 8, 1, 1);

        // Atualiza os valores para a próxima iteração
        adc_x_prev = adc_x_value;
        adc_y_prev = adc_y_value;

        ssd1306_send_data(&display); // Atualiza o display

        sleep_ms(100);
    }
}
