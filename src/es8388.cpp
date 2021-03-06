/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifdef ESP_PLATFORM

#include <string.h>
#include "es8388.h"

static const char *ES_TAG = "ES8388_DRIVER";

#define ES_ASSERT(a, format, b, ...) \
    if ((a) != 0) { \
        ESP_LOGE(ES_TAG, format, ##__VA_ARGS__); \
        return b;\
    }

ES8388::ES8388(int PA_ENABLE_GPIO, TwoWire wire, int bit_clock_pin, int word_select_pin, int codec_data_in_pin, int codec_data_out_pin) :
  _PA_ENABLE_GPIO(PA_ENABLE_GPIO),
  _i2c_initialized(false),
  _bit_clock_pin(bit_clock_pin),
  _word_select_pin(word_select_pin),
  _codec_data_in_pin(codec_data_in_pin),
  _codec_data_out_pin(codec_data_out_pin),
  _wire(wire),
  _codec_initialized(false),
  _esp32_i2s_port_number(-1)
{
  // Enable MCLK on GPIO 0
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
  WRITE_PERI_REG(PIN_CTRL, 0xFFF0);
}

ES8388::~ES8388()
{
  es8388_deinit();
  end();
}

/* Audio In */
  #ifdef CONFIG_IDF_TARGET_ESP32
    int ES8388::inBegin(long sampleRate, int bitsPerSample, bool use_external_mic/*=false*/, int esp32_i2s_port_number/*=0*/)
  #elif CONFIG_IDF_TARGET_ESP32S2
    int ES8388::inBegin(long sampleRate/*=44100*/, int bitsPerSample/*=16*/, bool use_external_mic/*=false*/)
  #endif
{
  if(_codec_initialized){ end(); }

  #ifdef CONFIG_IDF_TARGET_ESP32
    if(!AudioInI2SClass::begin(sampleRate, bitsPerSample, _bit_clock_pin, _word_select_pin, _codec_data_out_pin, esp32_i2s_port_number)){
  #elif CONFIG_IDF_TARGET_ESP32S2
    if(!AudioInI2SClass::begin(sampleRate, bitsPerSample, _bit_clock_pin, _word_select_pin, _codec_data_out_pin)){
  #endif
      return 0; // ERR - begin I2S
    }
  audio_hal_iface_samples_t samples;  /*!< I2S interface samples per second */
    if(sampleRate <= 8000) samples = AUDIO_HAL_08K_SAMPLES;   /*!< set to  8k samples per second */
    if(sampleRate > 8000 && sampleRate <= 11025) samples = AUDIO_HAL_11K_SAMPLES;   /*!< set to 11.025k samples per second */
    if(sampleRate > 11025 && sampleRate <= 16000) samples = AUDIO_HAL_16K_SAMPLES;   /*!< set to 16k samples in per second */
    if(sampleRate > 16000 && sampleRate <= 22050) samples = AUDIO_HAL_22K_SAMPLES;   /*!< set to 22.050k samples per second */
    if(sampleRate > 22050 && sampleRate <= 24000) samples = AUDIO_HAL_24K_SAMPLES;   /*!< set to 24k samples in per second */
    if(sampleRate > 24000 && sampleRate <= 32000) samples = AUDIO_HAL_32K_SAMPLES;   /*!< set to 32k samples in per second */
    if(sampleRate > 32000 && sampleRate <= 44100) samples = AUDIO_HAL_44K_SAMPLES;   /*!< set to 44.1k samples per second */
    if(sampleRate > 44100) samples = AUDIO_HAL_48K_SAMPLES;   /*!< set to 48k samples per second */

  audio_hal_iface_bits_t bits;        /*!< i2s interface number of bits per sample */
    if(bitsPerSample < 16) { return 0; } // ERR - ES8388 does not support less than 16 bits per sample
    if(bitsPerSample <=16) bits = AUDIO_HAL_BIT_LENGTH_16BITS;
    if(bitsPerSample > 16 && bitsPerSample <= 24) bits = AUDIO_HAL_BIT_LENGTH_24BITS;
    if(bitsPerSample > 24) bits = AUDIO_HAL_BIT_LENGTH_32BITS;

  audio_hal_codec_i2s_iface_t i2s_iface = {
    .mode = AUDIO_HAL_MODE_SLAVE, /*!< set slave mode */
    .fmt = AUDIO_HAL_I2S_NORMAL,  /*!< set normal I2S format */
    .samples = samples,
    .bits = bits};

  if(use_external_mic){
    _cfg.adc_input = AUDIO_HAL_ADC_INPUT_LINE2; // AUX_IN for external source
  }else{
    _cfg.adc_input = AUDIO_HAL_ADC_INPUT_LINE1; // Lyrat onboard microphones
  }

  _cfg.codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE;
  _cfg.i2s_iface = i2s_iface;  /*!< set I2S interface configuration */

  if (ESP_OK != es8388_init(&_cfg)){
    return 0; // ERR
  }
  if (ESP_OK != es8388_config_i2s(_cfg.codec_mode, &i2s_iface)){
    return 0; // ERR
  }
  if (ESP_OK != es8388_start(ES_MODULE_ADC)){ // Start ES8388 codec chip in A/D converter mode
    return 0; // ERR
  }
  _codec_initialized = true;
  return 1; // OK
}

/* Audio Out */

  #ifdef CONFIG_IDF_TARGET_ESP32
    int ES8388::outBegin(long sampleRate/*=44100*/, int bitsPerSample/*=16*/, int esp32_i2s_port_number/*=0*/)
  #elif CONFIG_IDF_TARGET_ESP32S2
     int ES8388::outBegin(long sampleRate/*=44100*/, int bitsPerSample/*=16*/)
  #endif
{
  if(_codec_initialized){ end(); }
  int ret;
  #ifdef CONFIG_IDF_TARGET_ESP32
    ret = AudioOutI2SClass::outBegin(sampleRate, bitsPerSample, _bit_clock_pin, _word_select_pin, _codec_data_in_pin, esp32_i2s_port_number);
  #elif CONFIG_IDF_TARGET_ESP32S2
    ret = AudioOutI2SClass::outBegin(sampleRate, bitsPerSample, _bit_clock_pin, _word_select_pin, _codec_data_in_pin);
  #endif

    if(ret == 0){
      return 0; // ERR
    }

    audio_hal_iface_samples_t samples;  /*!< I2S interface samples per second */
      if(sampleRate <= 8000) samples = AUDIO_HAL_08K_SAMPLES;   /*!< set to  8k samples per second */
      if(sampleRate > 8000 && sampleRate <= 11025) samples = AUDIO_HAL_11K_SAMPLES;   /*!< set to 11.025k samples per second */
      if(sampleRate > 11025 && sampleRate <= 16000) samples = AUDIO_HAL_16K_SAMPLES;   /*!< set to 16k samples in per second */
      if(sampleRate > 16000 && sampleRate <= 22050) samples = AUDIO_HAL_22K_SAMPLES;   /*!< set to 22.050k samples per second */
      if(sampleRate > 22050 && sampleRate <= 24000) samples = AUDIO_HAL_24K_SAMPLES;   /*!< set to 24k samples in per second */
      if(sampleRate > 24000 && sampleRate <= 32000) samples = AUDIO_HAL_32K_SAMPLES;   /*!< set to 32k samples in per second */
      if(sampleRate > 32000 && sampleRate <= 44100) samples = AUDIO_HAL_44K_SAMPLES;   /*!< set to 44.1k samples per second */
      if(sampleRate > 44100) samples = AUDIO_HAL_48K_SAMPLES;   /*!< set to 48k samples per second */

    audio_hal_iface_bits_t bits;        /*!< i2s interface number of bits per sample */
      if(bitsPerSample < 16) { return 0; } // ERR - ES8388 does not support less than 16 bits per sample
      if(bitsPerSample <=16) bits = AUDIO_HAL_BIT_LENGTH_16BITS;
      if(bitsPerSample > 16 && bitsPerSample <= 24) bits = AUDIO_HAL_BIT_LENGTH_24BITS;
      if(bitsPerSample > 24) bits = AUDIO_HAL_BIT_LENGTH_32BITS;

    audio_hal_codec_i2s_iface_t i2s_iface = {
      .mode = AUDIO_HAL_MODE_SLAVE, /*!< set slave mode */
      .fmt = AUDIO_HAL_I2S_NORMAL,  /*!< set normal I2S format */
      .samples = samples,
      .bits = bits};

    _cfg.dac_output = AUDIO_HAL_DAC_OUTPUT_LINE1; // Only line 1 is connected to output
    _cfg.codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE; /*!< select adc */ // DO NOT TOUCH!
    _cfg.i2s_iface = i2s_iface;  /*!< set I2S interface configuration */
    if (ESP_OK != es8388_init(&_cfg)){
      return 0; // ERR
    }
    if (ESP_OK != es8388_config_i2s(_cfg.codec_mode, &i2s_iface)){
      return 0; // ERR
    }
    if (ESP_OK != es8388_config_dac_output((es_dac_output_t)(DAC_OUTPUT_LOUT1 | DAC_OUTPUT_ROUT1))){
      return 0; // ERR
    }
    if (ESP_OK != es8388_start(ES_MODULE_DAC)){ // Start ES8388 codec chip in D/A converter mode
      return 0; // ERR
    }
    _codec_initialized = true;
    return 1; // OK
}

    /* Audio In + Out */

  #ifdef CONFIG_IDF_TARGET_ESP32
    int ES8388::begin(long sampleRate, int bitsPerSample, bool use_external_mic, int esp32_i2s_port_number){
  #elif CONFIG_IDF_TARGET_ESP32S2
    int ES8388::begin(long sampleRate, int bitsPerSample, bool use_external_mic){
  #endif
  if(_codec_initialized){ end(); }
  _esp32_i2s_port_number = esp32_i2s_port_number;

  // init ESP I2S for both INput and OUTput
  if(AudioOutI2SClass::_initialized || AudioInI2SClass::_initialized){
    i2s_driver_uninstall((i2s_port_t) esp32_i2s_port_number); //stop & destroy i2s driver
  }

  // Perform bits-per-sample check
  audio_hal_iface_bits_t codec_bits;        /*!< i2s interface number of bits per sample */
  if(bitsPerSample < 16) { return 0; } // ERR - ES8388 does not support less than 16 bits per sample
  if(bitsPerSample <=16) codec_bits = AUDIO_HAL_BIT_LENGTH_16BITS;
  if(bitsPerSample > 16 && bitsPerSample <= 24) codec_bits = AUDIO_HAL_BIT_LENGTH_24BITS;
  if(bitsPerSample > 24) codec_bits = AUDIO_HAL_BIT_LENGTH_32BITS;

  //////////////////////////
  // ESP32 I2S setup
  //////////////////////////

  AudioInI2SClass::_use_adc = false;
  AudioIn::_esp32_i2s_port_number = esp32_i2s_port_number;
  AudioOut::_esp32_i2s_port_number = esp32_i2s_port_number;

  i2s_bits_per_sample_t i2s_bits;
    if(bitsPerSample <=8)                         i2s_bits = I2S_BITS_PER_SAMPLE_8BIT;
    if(bitsPerSample > 8  && bitsPerSample <= 16) i2s_bits = I2S_BITS_PER_SAMPLE_16BIT;
    if(bitsPerSample > 16 && bitsPerSample <= 24) i2s_bits = I2S_BITS_PER_SAMPLE_24BIT;
    if(bitsPerSample > 24)                        i2s_bits = I2S_BITS_PER_SAMPLE_32BIT;

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
    .sample_rate =  sampleRate,
    .bits_per_sample = i2s_bits,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S | I2S_COMM_FORMAT_STAND_PCM_SHORT),
    .intr_alloc_flags = 0, // default interrupt priority
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
      .bck_io_num = _bit_clock_pin,
      .ws_io_num = _word_select_pin,
      .data_out_num = _codec_data_in_pin,
      .data_in_num = _codec_data_out_pin
  };

  if (ESP_OK != i2s_driver_install((i2s_port_t) esp32_i2s_port_number, &i2s_config, 0, NULL)){
    return 0;
  }

  if (ESP_OK != i2s_set_pin((i2s_port_t) esp32_i2s_port_number, &pin_config)){
    return 0;
  }

  ////////////////////////////
  // codec chip setup
  ////////////////////////////

  audio_hal_iface_samples_t samples;  /*!< I2S interface samples per second */
  if(sampleRate <= 8000) samples = AUDIO_HAL_08K_SAMPLES;   /*!< set to  8k samples per second */
  if(sampleRate > 8000 && sampleRate <= 11025) samples = AUDIO_HAL_11K_SAMPLES;   /*!< set to 11.025k samples per second */
  if(sampleRate > 11025 && sampleRate <= 16000) samples = AUDIO_HAL_16K_SAMPLES;   /*!< set to 16k samples in per second */
  if(sampleRate > 16000 && sampleRate <= 22050) samples = AUDIO_HAL_22K_SAMPLES;   /*!< set to 22.050k samples per second */
  if(sampleRate > 22050 && sampleRate <= 24000) samples = AUDIO_HAL_24K_SAMPLES;   /*!< set to 24k samples in per second */
  if(sampleRate > 24000 && sampleRate <= 32000) samples = AUDIO_HAL_32K_SAMPLES;   /*!< set to 32k samples in per second */
  if(sampleRate > 32000 && sampleRate <= 44100) samples = AUDIO_HAL_44K_SAMPLES;   /*!< set to 44.1k samples per second */
  if(sampleRate > 44100) samples = AUDIO_HAL_48K_SAMPLES;   /*!< set to 48k samples per second */

  audio_hal_codec_i2s_iface_t i2s_iface = {
    .mode = AUDIO_HAL_MODE_SLAVE, /*!< set slave mode */
    //.fmt = AUDIO_HAL_I2S_DSP, /*!< set dsp/pcm format */
    .fmt = AUDIO_HAL_I2S_NORMAL, // sounds ok
    .samples = samples,
    .bits = codec_bits
  };

  if(use_external_mic){
    _cfg.adc_input = AUDIO_HAL_ADC_INPUT_LINE2; // AUX_IN for external source
  }else{
    _cfg.adc_input = AUDIO_HAL_ADC_INPUT_LINE1; // Lyrat onboard microphones
  }
  _cfg.dac_output = AUDIO_HAL_DAC_OUTPUT_LINE1; // Only line 1 is connected to output
  _cfg.codec_mode = AUDIO_HAL_CODEC_MODE_BOTH; /*!< select both adc and dac */
  _cfg.i2s_iface = i2s_iface;  /*!< set I2S interface configuration */
  if (ESP_OK != es8388_init(&_cfg)){
    return 0; // ERR
  }
  if (ESP_OK != es8388_config_i2s(_cfg.codec_mode, &i2s_iface)){
    return 0; // ERR
  }
  if (ESP_OK != es8388_config_dac_output((es_dac_output_t)(DAC_OUTPUT_LOUT1 | DAC_OUTPUT_ROUT1))){
    return 0; // ERR
  }
  if (ESP_OK != es8388_start(ES_MODULE_DAC)){ // Start ES8388 codec chip in D/A converter mode
    return 0; // ERR
  }
  AudioOutI2SClass::_initialized = true;
  AudioInI2SClass::_initialized = true;
  _codec_initialized = true;
  return 1; // OK
}

void ES8388::end()
{
  if(_codec_initialized){
    AudioInI2SClass::end();
    AudioOutI2SClass::stop();
    if(_esp32_i2s_port_number >=0){
      i2s_driver_uninstall((i2s_port_t) _esp32_i2s_port_number);
      _esp32_i2s_port_number = -1;
    }
    es8388_stop(ES_MODULE_ADC); // Stop ES8388 codec chip
    es8388_pa_power(false); // Disable power to codec chip
    _codec_initialized = false;
  }
}

/* original functions */

esp_err_t ES8388::es_write_reg(uint8_t slave_addr, uint8_t reg_addr, uint8_t data)
{
  if(!_i2c_initialized){
    return ESP_ERR_INVALID_STATE;
  }
  _wire.beginTransmission(slave_addr>>1);
  _wire.write(reg_addr);
  _wire.write(data);
  _wire.endTransmission(true);
  return ESP_OK;
}


esp_err_t ES8388::es_read_reg(uint8_t reg_addr, uint8_t *p_data)
{
    if(!_i2c_initialized){
      return ESP_ERR_INVALID_STATE;
    }

    _wire.beginTransmission(ES8388_ADDR>>1);
    _wire.write(reg_addr);
    _wire.endTransmission(true);

    _wire.requestFrom(ES8388_ADDR>>1, 1);
    int read_byte = _wire.read();
    _wire.endTransmission(true);

    if (read_byte == -1){
      return ESP_ERR_INVALID_RESPONSE;
    }
    *p_data = (uint8_t)read_byte;
    return ESP_OK;
}

int ES8388::i2c_init()
{
  return i2c_init(_i2c_scl_pin, _i2c_sda_pin);
}

int ES8388::i2c_init(int i2c_scl_pin, int i2c_sda_pin)
{
    _wire.begin();
    if(!_wire.begin()){
        return 1; // ERR
    }else{
        _i2c_initialized = true;
    }
    return 0; // OK
}

void ES8388::es8388_read_all()
{
    for (int i = 0; i < 50; i++) {
        uint8_t reg = 0;
        es_read_reg(i, &reg);
        ets_printf("%x: %x\n", i, reg);
    }
}

esp_err_t ES8388::es8388_write_reg(uint8_t reg_addr, uint8_t data)
{
    return es_write_reg(ES8388_ADDR, reg_addr, data);
}

// TODO volume_out
//void ES8388::volume_out(float level)
// TODO volume_in
//void ES8388::volume_in(float level)

void ES8388::volume(float level)
{
  float input_level = level;
  if(level < 0.0){
    input_level = 0.0;
  }
  if(level > 100.0){
      input_level = 100.0;
  }
  es8388_set_voice_volume((int)input_level); // set output volume (int 0~100)
  AudioOut::volume(input_level); // set parent volume
}

/**
 * @brief Configure ES8388 ADC and DAC volume. Basicly you can consider this as ADC and DAC gain
 *
 * @param mode:             set ADC or DAC or all
 * @param volume:           -96 ~ 0              for example Es8388SetAdcDacVolume(ES_MODULE_ADC, 30, 6); means set ADC volume -30.5db
 * @param dot:              whether include 0.5. for example Es8388SetAdcDacVolume(ES_MODULE_ADC, 30, 4); means set ADC volume -30db
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
int ES8388::es8388_set_adc_dac_volume(int mode, int volume, int dot)
{
    int res = 0;
    if ( volume < -96 || volume > 0 ) {
        ESP_LOGW(ES_TAG, "Warning: volume < -96! or > 0!\n");
        if (volume < -96)
            volume = -96;
        else
            volume = 0;
    }
    dot = (dot >= 5 ? 1 : 0);
    volume = (-volume << 1) + dot;
    if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
        res |= es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL8, volume);
        res |= es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL9, volume);  //ADC Right Volume=0db
    }
    if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL5, volume);
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL4, volume);
    }
    return res;
}


/**
 * @brief Power Management
 *
 * @param mode:      if ES_POWER_CHIP, the whole chip including ADC and DAC is enabled
 * @param enable:   false to disable true to enable
 *
 * @return
 *     - (-1)  Error
 *     - (0)   Success
 */
esp_err_t ES8388::es8388_start(es_module_t mode)
{
    esp_err_t res = ESP_OK;
    uint8_t prev_data = 0, data = 0;
    es_read_reg(ES8388_DACCONTROL21, &prev_data);
    if (mode == ES_MODULE_LINE) {
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL16, 0x09); // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2 by pass enable
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL17, 0x50); // left DAC to left mixer enable  and  LIN signal to left mixer enable 0db  : bupass enable
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL20, 0x50); // right DAC to right mixer enable  and  LIN signal to right mixer enable 0db : bupass enable
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL21, 0xC0); //enable adc
    } else {
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL21, 0x80);   //enable dac
    }
    es_read_reg(ES8388_DACCONTROL21, &data);
    if (prev_data != data) {
        res |= es_write_reg(ES8388_ADDR, ES8388_CHIPPOWER, 0xF0);   //start state machine
        // res |= es_write_reg(ES8388_ADDR, ES8388_CONTROL1, 0x16);
        // res |= es_write_reg(ES8388_ADDR, ES8388_CONTROL2, 0x50);
        res |= es_write_reg(ES8388_ADDR, ES8388_CHIPPOWER, 0x00);   //start state machine
    }
    if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC || mode == ES_MODULE_LINE) {
        res |= es_write_reg(ES8388_ADDR, ES8388_ADCPOWER, 0x00);   //power up adc and line in
    }
    if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC || mode == ES_MODULE_LINE) {
        res |= es_write_reg(ES8388_ADDR, ES8388_DACPOWER, 0x3c);   //power up dac and line out
        res |= es8388_set_voice_mute(false);
        ESP_LOGD(ES_TAG, "es8388_start default is mode:%d", mode);
    }

    return res;
}

/**
 * @brief Power Management
 *
 * @param mod:      if ES_POWER_CHIP, the whole chip including ADC and DAC is enabled
 * @param enable:   false to disable true to enable
 *
 * @return
 *     - (-1)  Error
 *     - (0)   Success
 */
esp_err_t ES8388::es8388_stop(es_module_t mode)
{
    esp_err_t res = ESP_OK;
    if (mode == ES_MODULE_LINE) {
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL21, 0x80); //enable dac
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL16, 0x00); // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL17, 0x90); // only left DAC to left mixer enable 0db
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL20, 0x90); // only right DAC to right mixer enable 0db
        return res;
    }
    if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
        res |= es_write_reg(ES8388_ADDR, ES8388_DACPOWER, 0x00);
        res |= es8388_set_voice_mute(true); //res |= Es8388SetAdcDacVolume(ES_MODULE_DAC, -96, 5);      // 0db
        //res |= es_write_reg(ES8388_ADDR, ES8388_DACPOWER, 0xC0);  //power down dac and line out
    }
    if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
        //res |= Es8388SetAdcDacVolume(ES_MODULE_ADC, -96, 5);      // 0db
        res |= es_write_reg(ES8388_ADDR, ES8388_ADCPOWER, 0xFF);  //power down adc and line in
    }
    if (mode == ES_MODULE_ADC_DAC) {
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL21, 0x9C);  //disable mclk
//        res |= es_write_reg(ES8388_ADDR, ES8388_CONTROL1, 0x00);
//        res |= es_write_reg(ES8388_ADDR, ES8388_CONTROL2, 0x58);
//        res |= es_write_reg(ES8388_ADDR, ES8388_CHIPPOWER, 0xF3);  //stop state machine
    }

    return res;
}


/**
 * @brief Config I2s clock in MSATER mode
 *
 * @param cfg.sclkDiv:      generate SCLK by dividing MCLK in MSATER mode
 * @param cfg.lclkDiv:      generate LCLK by dividing MCLK in MSATER mode
 *
 * @return
 *     - (-1)  Error
 *     - (0)   Success
 */
esp_err_t ES8388::es8388_i2s_config_clock(es_i2s_clock_t cfg)
{
    esp_err_t res = ESP_OK;
    res |= es_write_reg(ES8388_ADDR, ES8388_MASTERMODE, cfg.sclk_div);
    res |= es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL5, cfg.lclk_div);  //ADCFsMode,singel SPEED,RATIO=256
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL2, cfg.lclk_div);  //ADCFsMode,singel SPEED,RATIO=256
    return res;
}

esp_err_t ES8388::es8388_deinit(void)
{
    int res = 0;
    res = es_write_reg(ES8388_ADDR, ES8388_CHIPPOWER, 0xFF);  //reset and stop es8388
    //i2c_bus_delete(i2c_handle);
/*
    #ifdef CONFIG_ESP_LYRAT_V4_3_BOARD
    headphone_detect_deinit();
    #endif
*/
    return res;
}

/**
 * @return
 *     - (-1)  Error
 *     - (0)   Success
 */
esp_err_t ES8388::es8388_init(audio_hal_codec_config_t *cfg)
{
    int res = 0;
/*
#ifdef CONFIG_ESP_LYRAT_V4_3_BOARD
    headphone_detect_init(get_headphone_detect_gpio());
#endif
*/

    res = i2c_init(); // ESP32 in master mode

    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL3, 0x04);  // 0x04 mute/0x00 unmute&ramp;DAC unmute and  disabled digital volume control soft ramp
    /* Chip Control and Power Management */
    res |= es_write_reg(ES8388_ADDR, ES8388_CONTROL2, 0x50);
    res |= es_write_reg(ES8388_ADDR, ES8388_CHIPPOWER, 0x00); //normal all and power up all
    res |= es_write_reg(ES8388_ADDR, ES8388_MASTERMODE, cfg->i2s_iface.mode); //CODEC IN I2S SLAVE MODE

    /* dac */
    res |= es_write_reg(ES8388_ADDR, ES8388_DACPOWER, 0xC0);  //disable DAC and disable Lout/Rout/1/2
    res |= es_write_reg(ES8388_ADDR, ES8388_CONTROL1, 0x12);  //Enfr=0,Play&Record Mode,(0x17-both of mic&paly)
//    res |= es_write_reg(ES8388_ADDR, ES8388_CONTROL2, 0);  //LPVrefBuf=0,Pdn_ana=0
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL1, 0x18);  // 0x18>>3 == 0x3 == 16bit sample
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL2, 0x02);  // DACFsMode,SINGLE SPEED; DACFsRatio,256
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL16, 0x00); // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL17, 0x90); // only left DAC to left mixer enable 0db
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL20, 0x90); // only right DAC to right mixer enable 0db
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL21, 0x80); //set internal ADC and DAC use the same LRCK clock, ADC LRCK as internal LRCK
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL23, 0x00);   //vroi=0
    res |= es8388_set_adc_dac_volume(ES_MODULE_DAC, 0, 0);          // 0db
    int tmp = 0;
    if (AUDIO_HAL_DAC_OUTPUT_LINE2 == cfg->dac_output) {
        tmp = DAC_OUTPUT_LOUT1 | DAC_OUTPUT_ROUT1;
    } else if (AUDIO_HAL_DAC_OUTPUT_LINE1 == cfg->dac_output) {
        tmp = DAC_OUTPUT_LOUT2 | DAC_OUTPUT_ROUT2;
    } else {
        tmp = DAC_OUTPUT_LOUT1 | DAC_OUTPUT_LOUT2 | DAC_OUTPUT_ROUT1 | DAC_OUTPUT_ROUT2;
    }
    res |= es_write_reg(ES8388_ADDR, ES8388_DACPOWER, tmp);  //0x3c Enable DAC and Enable Lout/Rout/1/2
    /* adc */
    res |= es_write_reg(ES8388_ADDR, ES8388_ADCPOWER, 0xFF);
    res |= es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL1, 0xbb); // MIC Left and Right channel PGA gain
    tmp = 0;
    if (AUDIO_HAL_ADC_INPUT_LINE1 == cfg->adc_input) {
        tmp = ADC_INPUT_LINPUT1_RINPUT1;
    } else if (AUDIO_HAL_ADC_INPUT_LINE2 == cfg->adc_input) {
        tmp = ADC_INPUT_LINPUT2_RINPUT2;
    } else {
        tmp = ADC_INPUT_DIFFERENCE;
    }
    res |= es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL2, tmp);  //0x00 LINSEL & RINSEL, LIN1/RIN1 as ADC Input; DSSEL,use one DS Reg11; DSR, LINPUT1-RINPUT1
    res |= es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL3, 0x02);
    res |= es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL4, 0x0d); // 0000 1101 = MSb (00) left data = left ADC, right data = right ADC; (0) ???; (011) 16-bit serial audio data word length; (01) Left Justified // Left/Right data, Left/Right justified mode, Bits length, I2S format
    res |= es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL5, 0x02);  //ADCFsMode,singel SPEED,RATIO=256
    //ALC for Microphone
    res |= es8388_set_adc_dac_volume(ES_MODULE_ADC, 0, 0);      // 0db
    res |= es_write_reg(ES8388_ADDR, ES8388_ADCPOWER, 0x09); //Power on ADC, Enable LIN&RIN, Power off MICBIAS, set int1lp to low power mode
    /* enable es8388 PA */
    es8388_pa_power(true);
    ESP_LOGI(ES_TAG, "init,out:%02x, in:%02x", cfg->dac_output, cfg->adc_input);
    return res;
}

/**
 * @brief Configure ES8388 I2S format
 *
 * @param mode:           set ADC or DAC or all
 * @param bitPerSample:   see Es8388I2sFmt
 *
 * @return
 *     - (-1) Error
 *     - (0)  Success
 */
esp_err_t ES8388::es8388_config_fmt(es_module_t mode, audio_hal_iface_format_t fmt)
{
    esp_err_t res = ESP_OK;
    uint8_t reg = 0;
    if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
        res = es_read_reg(ES8388_ADCCONTROL4, &reg);
        reg = reg & 0xfc;
        res |= es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL4, reg | fmt);
    }
    if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
        res = es_read_reg(ES8388_DACCONTROL1, &reg);
        reg = reg & 0xf9;
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL1, reg | (fmt << 1));
    }
    return res;
}

/**
 * @param volume: 0 ~ 100
 *
 * @return
 *     - (-1)  Error
 *     - (0)   Success
 */
esp_err_t ES8388::es8388_set_voice_volume(int volume)
{
    esp_err_t res = ESP_OK;
    if (volume < 0)
        volume = 0;
    else if (volume > 100)
        volume = 100;
    volume /= 3;
    res = es_write_reg(ES8388_ADDR, ES8388_DACCONTROL24, volume);
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL25, volume);
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL26, 0);
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL27, 0);
    return res;
}

/**
 *
 * @return
 *           volume
 */
esp_err_t ES8388::es8388_get_voice_volume(int *volume)
{
    esp_err_t res = ESP_OK;
    uint8_t reg = 0;
    res = es_read_reg(ES8388_DACCONTROL24, &reg);
    if (res == ESP_FAIL) {
        *volume = 0;
    } else {
        *volume = reg;
        *volume *= 3;
        if (*volume == 99)
            *volume = 100;
    }
    return res;
}

/**
 * @brief Configure ES8388 data sample bits
 *
 * @param mode:             set ADC or DAC or all
 * @param bitPerSample:   see BitsLength
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
esp_err_t ES8388::es8388_set_bits_per_sample(es_module_t mode, es_bits_length_t bits_length)
{
    esp_err_t res = ESP_OK;
    uint8_t reg = 0;
    int bits = (int)bits_length;

    if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
        res = es_read_reg(ES8388_ADCCONTROL4, &reg);
        reg = reg & 0xe3;
        res |=  es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL4, reg | (bits << 2));
    }
    if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
        res = es_read_reg(ES8388_DACCONTROL1, &reg);
        reg = reg & 0xc7;
        res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL1, reg | (bits << 3));
    }
    return res;
}

/**
 * @brief Configure ES8388 DAC mute or not. Basically you can use this function to mute the output or unmute
 *
 * @param enable: enable or disable
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
esp_err_t ES8388::es8388_set_voice_mute(bool enable)
{
    esp_err_t res = ESP_OK;
    uint8_t reg = 0;
    res = es_read_reg(ES8388_DACCONTROL3, &reg);
    reg = reg & 0xFB;
    res |= es_write_reg(ES8388_ADDR, ES8388_DACCONTROL3, reg | (((int)enable) << 2));
    return res;
}

esp_err_t ES8388::es8388_get_voice_mute(void)
{
    esp_err_t res = ESP_OK;
    uint8_t reg = 0;
    res = es_read_reg(ES8388_DACCONTROL3, &reg);
    if (res == ESP_OK) {
        reg = (reg & 0x04) >> 2;
    }
    return res == ESP_OK ? reg : res;
}

/**
 * @param gain: Config DAC Output
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
esp_err_t ES8388::es8388_config_dac_output(es_dac_output_t output)
{
    esp_err_t res;
    uint8_t reg = 0;
    res = es_read_reg(ES8388_DACPOWER, &reg);
    reg = reg & 0xc3;
    res |= es_write_reg(ES8388_ADDR, ES8388_DACPOWER, reg | output);
    return res;
}

/**
 * @param gain: Config ADC input
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
esp_err_t ES8388::es8388_config_adc_input(es_adc_input_t input)
{
    esp_err_t res;
    uint8_t reg = 0;
    res = es_read_reg(ES8388_ADCCONTROL2, &reg);
    reg = reg & 0x0f;
    res |= es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL2, reg | input);
    return res;
}

/**
 * @param gain: see es_mic_gain_t
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
esp_err_t ES8388::es8388_set_mic_gain(es_mic_gain_t gain)
{
    esp_err_t res, gain_n;
    gain_n = (int)gain / 3;
    res = es_write_reg(ES8388_ADDR, ES8388_ADCCONTROL1, gain_n); //MIC PGA
    return res;
}

int ES8388::es8388_ctrl_state(audio_hal_codec_mode_t mode, audio_hal_ctrl_t ctrl_state)
{
    int res = 0;
    //int es_mode_t = 0;
    es_module_t es_mode_t = ES_MODULE_MIN;
    switch (mode) {
        case AUDIO_HAL_CODEC_MODE_ENCODE:
            es_mode_t  = ES_MODULE_ADC;
            break;
        case AUDIO_HAL_CODEC_MODE_LINE_IN:
            es_mode_t  = ES_MODULE_LINE;
            break;
        case AUDIO_HAL_CODEC_MODE_DECODE:
            es_mode_t  = ES_MODULE_DAC;
            break;
        case AUDIO_HAL_CODEC_MODE_BOTH:
            es_mode_t  = ES_MODULE_ADC_DAC;
            break;
        default:
            es_mode_t = ES_MODULE_DAC;
            ESP_LOGW(ES_TAG, "Codec mode not support, default is decode mode");
            break;
    }
    if (AUDIO_HAL_CTRL_STOP == ctrl_state) {
        res = es8388_stop(es_mode_t);
    } else {
        res = es8388_start(es_mode_t);
        ESP_LOGD(ES_TAG, "start default is decode mode:%d", es_mode_t);
    }
    return res;
}

esp_err_t ES8388::es8388_config_i2s(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface)
{
    esp_err_t res = ESP_OK;
    int tmp = 0;
    res |= es8388_config_fmt(ES_MODULE_ADC_DAC, iface->fmt);
    if (iface->bits == AUDIO_HAL_BIT_LENGTH_16BITS) {
        tmp = BIT_LENGTH_16BITS;
    } else if (iface->bits == AUDIO_HAL_BIT_LENGTH_24BITS) {
        tmp = BIT_LENGTH_24BITS;
    } else {
        tmp = BIT_LENGTH_32BITS;
    }
    res |= es8388_set_bits_per_sample(ES_MODULE_ADC_DAC, (es_bits_length_t)tmp);
    return res;
}

void ES8388::es8388_pa_power(bool enable)
{
    gpio_config_t  io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    io_conf.intr_type = (gpio_int_type_t)GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = BIT64((gpio_num_t)_PA_ENABLE_GPIO);
    io_conf.pull_down_en = (gpio_pulldown_t)0;
    io_conf.pull_up_en = (gpio_pullup_t)0;
    gpio_config(&io_conf);
    if (enable) {
        gpio_set_level((gpio_num_t)_PA_ENABLE_GPIO, 1);
    } else {
        gpio_set_level((gpio_num_t)_PA_ENABLE_GPIO, 0);
    }
}

int ES8388::read(void* buffer, size_t size){
  return AudioInI2SClass::read(buffer, size);
}

int ES8388::write(void* buffer, size_t size){
  return AudioOutI2SClass::write(buffer, size);
}
#endif // if def ESP_PLATFORM guard