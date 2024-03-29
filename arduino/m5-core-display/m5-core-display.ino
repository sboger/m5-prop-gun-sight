#include <Arduino.h>
#include "driver/uart.h"

#include "M5Stack.h"

typedef enum {
  WAIT_FRAME_1 = 0x00,
  WAIT_FRAME_2,
  WAIT_FRAME_3,
  GET_NUM,
  GET_LENGTH,
  GET_MSG,
  RECV_FINISH,
  RECV_ERROR,
} uart_rev_state_t;

typedef struct {
  uint8_t idle;
  uint32_t length;
  uint8_t *buf;
} jpeg_data_t;

/* Define ------------------------------------------------------------ */


/* Global Var ----------------------------------------------------------- */
uart_rev_state_t uart_rev_state;
static const int RX_BUF_SIZE = 1024*40;
static const uint8_t frame_data_begin[3] = { 0xFF, 0xD8, 0xEA };

jpeg_data_t jpeg_data_1;
jpeg_data_t jpeg_data_2;

/* Static fun ------------------------------------------------------------ */
static void uart_init();
static void uart_msg_task(void *pvParameters);

void setup() {
  delay(500);
  
  M5.begin(true, false, true);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setBrightness(255);
  uart_init();
  jpeg_data_1.buf = (uint8_t *) malloc(sizeof(uint8_t) * 1024 * 37);
  if(jpeg_data_1.buf == NULL) {
    Serial.println("malloc jpeg buffer 1 error");
  }

  jpeg_data_2.buf = (uint8_t *) malloc(sizeof(uint8_t) * 1024 * 37);
  if(jpeg_data_2.buf == NULL) {
    Serial.println("malloc jpeg buffer 2 error");
  }

  xTaskCreatePinnedToCore(uart_msg_task, "uart_task", 3 * 1024, NULL, 1, NULL, 0);


}

void loop() {
    uint32_t data_len = 0;
    uint8_t rx_buffer[21] = { '\0' };

    //Ugly hack. maybe I can draw the lines in the buffer first. need more research
    M5.Lcd.drawLine(159, 15, 159, 224,TFT_RED);
    M5.Lcd.drawLine(1, 119, 318, 119,TFT_RED);
    for (int i=9; i<310; i+=10)
      M5.Lcd.drawLine(i, 117, i, 121,TFT_RED);
    for (int i=19; i<220; i+=10)
      M5.Lcd.drawLine(157, i, 161, i,TFT_RED);
    
    delay(1);
    
    if(jpeg_data_1.idle == 1) {
      jpeg_data_1.idle = 2;
      M5.lcd.drawJpg(jpeg_data_1.buf, jpeg_data_1.length, 0, 0);
      memset(jpeg_data_1.buf,0,jpeg_data_1.length);
      jpeg_data_1.idle = 0;
    }
    
    if(jpeg_data_2.idle == 1) {
      jpeg_data_2.idle = 2;  
      M5.lcd.drawJpg(jpeg_data_2.buf, jpeg_data_2.length, 0, 0);
      memset(jpeg_data_2.buf,0,jpeg_data_1.length);
      jpeg_data_2.idle = 0;
    }

    M5.update();
    if(M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
      M5.Lcd.fillScreen(BLACK);
    }

}

static void uart_init() {
    const uart_config_t uart_config = {
        .baud_rate = 921600,
        //.baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &uart_config);
    // We change io in here, now use Serial2
    uart_set_pin(UART_NUM_1, 32, 22, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE, 0, 0, NULL, 0);
}



static void uart_msg_task(void *pvParameters) {
  uint32_t data_len = 0;
  uint32_t length = 0;
  uint8_t rx_buffer[10];
  uint8_t buffer_use = 0;
  while(true) {
    // delay(1);
    uart_get_buffered_data_len(UART_NUM_1, &data_len);
    if(data_len > 0) {
      switch(uart_rev_state) {
        case WAIT_FRAME_1:
          uart_read_bytes(UART_NUM_1, (uint8_t *)&rx_buffer, 1, 10);
          if(rx_buffer[0] == frame_data_begin[0]) {
            uart_rev_state = WAIT_FRAME_2; 
          }
          else {
            break ;
          }
          
        case WAIT_FRAME_2:
          uart_read_bytes(UART_NUM_1, (uint8_t *)&rx_buffer, 1, 10);
          if(rx_buffer[0] == frame_data_begin[1]){
            uart_rev_state = WAIT_FRAME_3; 
          } else {
            uart_rev_state = WAIT_FRAME_1;
            break ;
          }

        case WAIT_FRAME_3:
          uart_read_bytes(UART_NUM_1, (uint8_t *)&rx_buffer, 1, 10);
          if(rx_buffer[0] == frame_data_begin[2]){
            uart_rev_state = GET_NUM; 
          } else {
            uart_rev_state = WAIT_FRAME_1;
            break ;
          }

        case GET_NUM:
          uart_read_bytes(UART_NUM_1, (uint8_t *)&rx_buffer, 1, 10);
          printf("get number cam buf %d\t", rx_buffer[0]);
          uart_rev_state = GET_LENGTH;

        case GET_LENGTH:
          uart_read_bytes(UART_NUM_1, (uint8_t *)&rx_buffer, 3, 10);
          data_len =(uint32_t)(rx_buffer[0] << 16) | (rx_buffer[1] << 8) | rx_buffer[2];
          printf("data length %d\r\n", data_len);

        case GET_MSG:
          if(buffer_use == 0) {
            buffer_use = 1;
            if(jpeg_data_1.idle == 0) {
              if(uart_read_bytes(UART_NUM_1, jpeg_data_1.buf, data_len, 10) == -1) {
                uart_rev_state = RECV_ERROR;
                break ;
              }
              jpeg_data_1.length = data_len;
              jpeg_data_1.idle = 1;
            } else {
                uart_flush_input(UART_NUM_1);
            }
          } else {
            buffer_use = 0;
            if(jpeg_data_2.idle == 0) {
              if(uart_read_bytes(UART_NUM_1, jpeg_data_2.buf, data_len, 10) == -1) {
                uart_rev_state = RECV_ERROR;
                break ;
              }
              jpeg_data_2.length = data_len;
              jpeg_data_2.idle = 1;
            } else {
                uart_flush_input(UART_NUM_1);
            }
          }
        //  printf("get image %d buffer", buffer_use);
          uart_rev_state = RECV_FINISH;

        case RECV_FINISH:
        //   printf("get image finish...\r\n");
          uart_rev_state = WAIT_FRAME_1;          
          break ;

        case RECV_ERROR:
          printf("get image error\r\n");
          uart_rev_state = WAIT_FRAME_1;
          break ;
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }

  }
  vTaskDelete(NULL);
}
