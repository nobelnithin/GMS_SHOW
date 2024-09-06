#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "ssd1306.h"
#include "font8x8_basic.h"
#include <string.h>
#include "esp_timer.h"
#include "image.h"
#include "ssd1306_i2c_new.c"
#include "esp_sleep.h"

SSD1306_t dev;
#define CONFIG_SDA_GPIO 1
#define CONFIG_SCL_GPIO 2
#define CONFIG_RESET_GPIO 0

#define BTN_PLUS GPIO_NUM_11
#define BTN_MINUS GPIO_NUM_10
#define BTN_UP GPIO_NUM_9
#define BTN_DOWN GPIO_NUM_8
#define BTN_PWR GPIO_NUM_6
#define BTN_OK GPIO_NUM_7
#define CH1 GPIO_NUM_5
#define CH2 GPIO_NUM_4

#define IN1 GPIO_NUM_12
#define IN2 GPIO_NUM_14
#define EN1 GPIO_NUM_13
#define EN2 GPIO_NUM_15
#define IN3 GPIO_NUM_16
#define IN4 GPIO_NUM_18
#define EN3 GPIO_NUM_17
#define EN4 GPIO_NUM_19
#define nRESET GPIO_NUM_34
#define nSLEEP GPIO_NUM_33

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (21) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (0) // 8192 for 100%. To be decided.
#define LEDC_FREQUENCY          (5000) // Frequency in Hertz. Set frequency at 5 kHz



int STIMStrength[] = {0, 819, 1638, 2457, 3276, 4095, 4914, 5733, 6552, 7371};

xQueueHandle BTN_PLUSQueue;
xQueueHandle BTN_MINUSQueue;
xQueueHandle BTN_UPQueue;
xQueueHandle BTN_DOWNQueue;
xQueueHandle BTN_PWRQueue;
xQueueHandle BTN_OKQueue;
xQueueHandle CH1Queue;
xQueueHandle CH2Queue;

bool isDisp_menu;
bool ok_btn=true;
bool isDisp_setup;
bool CH1_flag =true;
bool CH2_flag =true;
int level ;
int disp_strenght = 0;

uint32_t pre_time_plus = 0;
uint32_t pre_time_minus = 0;
uint32_t pre_time_up = 0;
uint32_t pre_time_down = 0;
uint32_t pre_time_pwr = 0;
uint32_t pre_time_ok = 0;
uint64_t pre_time_CH1 = 0;
uint64_t pre_time_CH2 = 0;
uint64_t intr_time = 0;
uint64_t curr_time = 0;
bool long_press_detected = false;

static void example_ledc_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = LEDC_DUTY, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}



int num_str =0;

void enter_deep_sleep() {
    ESP_LOGI("NO TAG", "Entering deep sleep in 500ms");
    vTaskDelay(500 / portTICK_PERIOD_MS); // Wait for 500ms to ensure button is released
    esp_sleep_enable_ext0_wakeup(BTN_PWR, 0); // Wake up when button is pressed (falling edge)
    ssd1306_clear_screen(&dev, false);
    esp_deep_sleep_start();
}



void display_logo()
{
    ssd1306_init(&dev, 128, 64);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    ssd1306_bitmaps(&dev, 0, 0, biostim_logo, 128, 64, false);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ssd1306_clear_screen(&dev, false);
}


void disp_menu()
{
    
    isDisp_menu=true;
    isDisp_setup=false;
    ssd1306_bitmaps(&dev, 100, 1, battery_50, 32, 17, false);
    // ssd1306_bitmaps(&dev, 3, 3, channel1, 32,17 , false);
    // ssd1306_bitmaps(&dev, 20, 3, channel2, 36, 17, false);
    ssd1306_display_text(&dev, 4, " Setup Mode     ", 17, true);
    if(!CH2_flag)
    {
        ssd1306_display_text(&dev, 1, "    ", 5, false);
        ssd1306_display_text(&dev, 1, " CH2", 5, false);
    }
    if(!CH1_flag)
    {
        ssd1306_display_text(&dev, 1, "    ", 5, false);
        ssd1306_display_text(&dev, 1, " CH1", 5, false);
    }

}

void display_setup()
{
    isDisp_menu=false;
    isDisp_setup=true;
    // ssd1306_bitmaps(&dev, 34, 66, number, 32, 23, false);
    char buffer[10];
    snprintf(buffer, sizeof(buffer), "  %d", disp_strenght); // Convert integer to string
    // ssd1306_clear_screen(&dev, false); // Clear the display
    ssd1306_display_text_x3(&dev, 3, buffer, strlen(buffer), false);
}


void STIMTask(void *params)
{
    example_ledc_init();
    gpio_set_direction(IN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(EN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(EN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN3, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN4, GPIO_MODE_OUTPUT);
    gpio_set_direction(EN3, GPIO_MODE_OUTPUT);
    gpio_set_direction(EN4, GPIO_MODE_OUTPUT);
    gpio_set_direction(nRESET, GPIO_MODE_OUTPUT);
    gpio_set_direction(nSLEEP, GPIO_MODE_OUTPUT);
    gpio_set_level(nRESET, 1);
    gpio_set_level(nSLEEP, 1);
    gpio_set_level(EN1, 1);
    gpio_set_level(EN2, 1);
    gpio_set_level(EN3, 1);
    gpio_set_level(EN4, 1);
    // ets_printf("STIMTask setup complete");
    ESP_LOGI("NO TAG","STIMTask setup complete");
    int btn_num=0;
    while(1)
    {
        if(isDisp_setup)
        {
                // ets_printf("Driver on, strength set : %d \n", STIMStrength[freq_list_index]);
                // ESP_LOGI("NO TAG ","Driver on, strength set : %d \n", STIMStrength[freq_list_index]);
            printf("Simulation strength: %d\n",STIMStrength[disp_strenght]);
            if(!CH1_flag)
            {
                for(int i= 0;i<20;i++)
                {
        
                    gpio_set_level(IN1, 1);
                    gpio_set_level(IN2, 0);
                    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, STIMStrength[disp_strenght]));
                    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
                    printf("Simulation strength: UP\n");
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    gpio_set_level(IN1, 0);
                    gpio_set_level(IN2, 1);       
                    printf("Simulation strength: DOWN\n");      
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0));
                    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
                    gpio_set_level(IN1, 0);
                    gpio_set_level(IN2, 0);
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    
    
                }
                

            }


            if(!CH2_flag)
            {
                for(int i= 0;i<20;i++)
                {
        
                    gpio_set_level(IN3, 1);
                    gpio_set_level(IN4, 0);
                    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, STIMStrength[disp_strenght]));
                    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    gpio_set_level(IN3, 0);
                    gpio_set_level(IN4, 1);             
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0));
                    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
                    gpio_set_level(IN3, 0);
                    gpio_set_level(IN4, 0);
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    
    
                }
                
            }
                
    
                //gpio_set_level(IN1, 1);
                //gpio_set_level(IN2, 0);
    
    
               // ets_printf("Stimulation fired : driver off for 2 sec\n");
                ESP_LOGI("NO TAG","Stimulation fired : driver off for 2 sec");
                // ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0));
                // ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
                vTaskDelay(2000 / portTICK_PERIOD_MS);

        }
        if(!isDisp_setup){
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

        }
    vTaskDelay(20 / portTICK_PERIOD_MS);    
    }
}

void CH1Task(void *params)
{
    gpio_set_direction(CH1, GPIO_MODE_INPUT);
    gpio_set_intr_type(CH1, GPIO_INTR_POSEDGE);
    int BTN_NUMBER = 0;
    while (1)
    {
        if (xQueueReceive(CH1Queue, &BTN_NUMBER, portMAX_DELAY))
        {
            
            if(CH1_flag)
            {
                printf("pluged IN 1....!\n");
                CH2_flag=true;
                CH1_flag=false;
                disp_menu();
            }

            xQueueReset(CH1Queue);
        }
        vTaskDelay(100/portTICK_PERIOD_MS);
    }
}


void CH2Task(void *params)
{
    gpio_set_direction(CH2, GPIO_MODE_INPUT);
    gpio_set_intr_type(CH2, GPIO_INTR_POSEDGE);
    int BTN_NUMBER = 0;
    while (1)
    {
        if (xQueueReceive(CH2Queue, &BTN_NUMBER, portMAX_DELAY))
        {
            
            
            if(CH2_flag)
            {
                printf("pluged IN 2....!\n");
                CH2_flag=false;
                CH1_flag=true;
                disp_menu();
            }

            xQueueReset(CH2Queue);
        }
        vTaskDelay(100/portTICK_PERIOD_MS);
    }
}

void BTN_PLUSTask(void *param)
{
    gpio_set_direction(BTN_PLUS, GPIO_MODE_INPUT);
    gpio_set_intr_type(BTN_PLUS, GPIO_INTR_NEGEDGE);
    int BTN_NUMBER = 0;
    while (1)
    {
        if (xQueueReceive(BTN_PLUSQueue, &BTN_NUMBER, portMAX_DELAY))
        {
            printf("Button PLUS pressed\n");
            if(isDisp_setup && disp_strenght<9)
            {
                disp_strenght++;
                display_setup();
            }
            xQueueReset(BTN_PLUSQueue);
        }
    }
}

void BTN_MINUSTask(void *params)
{
    gpio_set_direction(BTN_MINUS, GPIO_MODE_INPUT);
    gpio_set_intr_type(BTN_MINUS, GPIO_INTR_NEGEDGE);
     level = gpio_get_level(CH2);
    int BTN_NUMBER = 0;
    while (1)
    {
        
        if (xQueueReceive(BTN_MINUSQueue, &BTN_NUMBER, portMAX_DELAY))
        {
            printf("MINUS_button_works\n");
            if(isDisp_setup && disp_strenght>0)
            {
                disp_strenght--;
                display_setup();
            }
            xQueueReset(BTN_MINUSQueue);
        }
    }
}




void BTN_UPTask(void *param)
{
    gpio_set_direction(BTN_UP, GPIO_MODE_INPUT);
    gpio_set_intr_type(BTN_UP, GPIO_INTR_NEGEDGE);
    int BTN_NUMBER = 0;
    while (1)
    {
        if (xQueueReceive(BTN_UPQueue, &BTN_NUMBER, portMAX_DELAY))
        {
            printf("Button up pressed\n");
            xQueueReset(BTN_UPQueue);
        }
    }
}

void BTN_DOWNTask(void *params)
{
    gpio_set_direction(BTN_DOWN, GPIO_MODE_INPUT);
    gpio_set_intr_type(BTN_DOWN, GPIO_INTR_NEGEDGE);
    int BTN_NUMBER = 0;
    while (1)
    {

        if (xQueueReceive(BTN_DOWNQueue, &BTN_NUMBER, portMAX_DELAY))
        {
            printf("Down_button_works\n");
            xQueueReset(BTN_DOWNQueue);
        }
    }
}


void BTN_PWRTask(void *params)
{
    gpio_set_direction(BTN_OK, GPIO_MODE_INPUT);
    gpio_set_intr_type(BTN_OK, GPIO_INTR_NEGEDGE);
    int BTN_NUMBER = 0;
    while (1)
    {
if (xQueueReceive(BTN_PWRQueue, &BTN_NUMBER, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Task BTN_PWRTask: Button power pressed (15)!");
            long_press_detected = false;

            while (gpio_get_level(BTN_PWR) == 0 && !long_press_detected)
            {
                curr_time = esp_timer_get_time();

                if (curr_time - intr_time >= 1000000) // Check for long press duration
                {
                    ESP_LOGI("NO TAG", "Long Press Detected");
                    long_press_detected = true; // Set long press flag
                    enter_deep_sleep(); // Enter deep sleep on long press
                }

                if (gpio_get_level(BTN_PWR) == 1)
                {
                    if (curr_time - intr_time < 1000000)
                    {
                        ESP_LOGI("NO TAG", "Short Press Detected");
                        long_press_detected = true;
                    }
                }
            }

            xQueueReset(BTN_PWRQueue);
        }
    }
            
}


void BTN_OKTask(void *params)
{
    gpio_set_direction(BTN_PWR, GPIO_MODE_INPUT);
    gpio_set_intr_type(BTN_PWR, GPIO_INTR_NEGEDGE);
    int BTN_NUMBER = 0;
    while (1)
    {
        if (xQueueReceive(BTN_OKQueue, &BTN_NUMBER, portMAX_DELAY))
        {
            if(ok_btn)
            {
                ssd1306_clear_screen(&dev, false);
                ssd1306_bitmaps(&dev, 0, 0, arrow, 128, 64, false);
                display_setup();
                ok_btn = false;
            }
            else if(!ok_btn)
            {
                ssd1306_clear_screen(&dev, false);
                disp_menu();
                ok_btn =true;
                
            }
            printf("Ok button pressed\n");

            
            xQueueReset(BTN_OKQueue);

        }
        vTaskDelay(10/portTICK_PERIOD_MS);
    }
}

static void IRAM_ATTR CH1_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    if(esp_timer_get_time() - pre_time_CH1 > 400000){
    xQueueSendFromISR(CH1Queue, &pinNumber, NULL);
    }
    pre_time_CH1 = esp_timer_get_time();

}

static void IRAM_ATTR CH2_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    if(esp_timer_get_time() - pre_time_CH2 > 400000){
    xQueueSendFromISR(CH2Queue, &pinNumber, NULL);
    }
    pre_time_CH2 = esp_timer_get_time();

}

static void IRAM_ATTR BTN_PLUS_interrupt_handler(void *args)
{
    
    int pinNumber = (int)args;
    if(esp_timer_get_time() - pre_time_plus > 500000)
    {
        xQueueSendFromISR(BTN_PLUSQueue, &pinNumber, NULL);

    }
    pre_time_plus= esp_timer_get_time();
}

static void IRAM_ATTR BTN_MINUS_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    if(esp_timer_get_time() - pre_time_minus > 500000)
    {
        xQueueSendFromISR(BTN_MINUSQueue, &pinNumber, NULL);
    }
    pre_time_minus = esp_timer_get_time();
}

static void IRAM_ATTR BTN_UP_interrupt_handler(void *args)
{
    
    int pinNumber = (int)args;
    if(esp_timer_get_time() - pre_time_up > 500000)
    {
        xQueueSendFromISR(BTN_UPQueue, &pinNumber, NULL);

    }
    pre_time_up= esp_timer_get_time();
}

static void IRAM_ATTR BTN_DOWN_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    if(esp_timer_get_time() - pre_time_down > 500000)
    {
        xQueueSendFromISR(BTN_DOWNQueue, &pinNumber, NULL);
    }
    pre_time_down = esp_timer_get_time();
}

static void IRAM_ATTR BTN_PWR_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    if(esp_timer_get_time() - pre_time_pwr > 500000){
    xQueueSendFromISR(BTN_PWRQueue, &pinNumber, NULL);
    }
    pre_time_pwr = esp_timer_get_time();
}

static void IRAM_ATTR BTN_OK_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    if(esp_timer_get_time() - pre_time_ok > 500000){
    xQueueSendFromISR(BTN_OKQueue, &pinNumber, NULL);
    intr_time = esp_timer_get_time();
    }
    pre_time_ok = esp_timer_get_time();
}


void app_main(void)
{
    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);

    BTN_PLUSQueue = xQueueCreate(10, sizeof(int));
    BTN_MINUSQueue = xQueueCreate(10, sizeof(int)); 
    BTN_UPQueue = xQueueCreate(10, sizeof(int));
    BTN_DOWNQueue = xQueueCreate(10, sizeof(int));
    BTN_PWRQueue = xQueueCreate(10, sizeof(int));
    BTN_OKQueue = xQueueCreate(10, sizeof(int));
    CH1Queue = xQueueCreate(10, sizeof(int));
    CH2Queue = xQueueCreate(10, sizeof(int));


    if (BTN_PLUSQueue == NULL || BTN_MINUSQueue == NULL || BTN_UPQueue == NULL || BTN_DOWNQueue == NULL || BTN_PWRQueue == NULL || BTN_OKQueue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create button queues");
        return;
    }

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_PLUS, BTN_PLUS_interrupt_handler, (void *)BTN_PLUS);
    gpio_isr_handler_add(BTN_MINUS, BTN_MINUS_interrupt_handler, (void *)BTN_MINUS);
    gpio_isr_handler_add(BTN_UP, BTN_UP_interrupt_handler, (void *)BTN_UP);
    gpio_isr_handler_add(BTN_DOWN, BTN_DOWN_interrupt_handler, (void *)BTN_DOWN);
    gpio_isr_handler_add(BTN_PWR, BTN_PWR_interrupt_handler, (void *)BTN_PWR);
    gpio_isr_handler_add(BTN_OK, BTN_OK_interrupt_handler, (void *)BTN_OK);
    gpio_isr_handler_add(CH1, CH1_interrupt_handler, (void *)CH1);
    gpio_isr_handler_add(CH2, CH2_interrupt_handler, (void *)CH2);


    xTaskCreate(BTN_PLUSTask, "BTN_PLUSTask", 2048, NULL, 1, NULL);
    xTaskCreate(BTN_MINUSTask, "BTN_MINUSTask", 2048, NULL, 1, NULL);    
    xTaskCreate(BTN_UPTask, "BTN_UPTask", 2048, NULL, 1, NULL);
    xTaskCreate(BTN_DOWNTask, "BTN_DOWNTask", 2048, NULL, 1, NULL);
    xTaskCreate(BTN_PWRTask, "BTN_PWRTask", 2048, NULL, 1, NULL);
    xTaskCreate(BTN_OKTask, "BTN_OKTask", 8000, NULL, 1, NULL);
    xTaskCreate(CH1Task, "CH1Task", 8000, NULL, 1, NULL);
    xTaskCreate(CH2Task, "CH2 Task", 8000, NULL, 1, NULL);
    xTaskCreate(STIMTask, "STIMTask", 8000, NULL, 1, NULL);

    display_logo();
    disp_menu();

}
