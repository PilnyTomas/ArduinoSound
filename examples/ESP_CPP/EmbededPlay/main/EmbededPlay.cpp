/*
 Example for Espressif generic ESP32
 In this example we embed short WAV sound file into code.
 This enables you to play short sound without any external storage.
 To keep things minimal we are using integrated Digital-to-Analog Converter (DAC).
 To propperly drive the speaker you will need any audio amplifier.

 Setup:
   Connect your audio amplifier to ESP.
     ESP32 has DAC on GPIO pins 25 and 26.
     ESP32-S2 has DAC on GPIO pins 17 and 18.
   Connect speaker to amplifier.
   Connect ESP to computer with USB and load.
   If you want to trigger the playback with button press:
     Connect your button to any GPIO (in this sketch 12) and GND
     Uncommet line #define USE_BUTTON located below this comment block
   If you want to load your own sound file use name "sound_file.wav"
     and copy it over default file in folder main.
     If you want to port this functionality you should read more here
     https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#embedding-binary-data

 Flashing:
   If upload doesnt start automatically press and hold Boot button and shortly press RST button.
   Flashing should start shortly after releasing both buttons.
   After successfull flashing press shortly RST button

 Usage:
   After flashing and restarting you should hear sound you uploaded with the code.
   If you chose to use button press it and the sound should be played.

 created 13 November 2020
 by Tomas Pilny
 */
#define USE_BUTTON

#include <ArduinoSound.h>

// symbol names of file's start and end
extern const uint8_t sound_file_wav_start[] asm("_binary_sound_file_wav_start");
extern const uint8_t sound_file_wav_end[]   asm("_binary_sound_file_wav_end");

const int button_pin = GPIO_NUM_12;

struct SubChunkHeader {
  uint32_t id;
  uint32_t size;
};

// based on: http://soundfile.sapp.org/doc/WaveFormat/
struct WaveFileHeader {
  uint32_t chunkId;
  uint32_t chunkSize;
  uint32_t format;
  struct {
    struct SubChunkHeader header;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
  } subChunk1;
  struct SubChunkHeader subChunk2Header;
} __attribute__((packed));

// Function to check ebeded wav file
bool read_embeded_wav_header(const uint8_t *file_start, const uint8_t *file_end, WaveFileHeader *header, uint32_t *data_offset);

// Play embedded wav file
// parameters: file_start   symbol name of file's start
//             file_end     symbol name of file's end
// returns: true on success
//          false on failure
// details: Function attempts to read embedded file and its wav header.
//          Based on file header then initializes I2S.
//          After successfull init the function then plays given file.
//          For demostration this function also prints its progress and file header
bool play_embedded_wav_file(const uint8_t *file_start, const uint8_t *file_end){
  WaveFileHeader header;
  uint32_t data_offset;
  Serial.println("play_embedded_wav_file()");
  Serial.print("file_start = "); Serial.println((int)file_start);
  Serial.print("file_end = "); Serial.println((int)file_end);
  Serial.print("file size = "); Serial.println((int)file_end - (int)file_start);
  bool file_ok = read_embeded_wav_header(file_start, file_end, &header, &data_offset);

  if (!file_ok) {
    Serial.println("wave file is invalid!");
    return false;
  }

  // Print out wave file header
  Serial.print("Bits per sample = ");
  Serial.println(header.subChunk1.bitsPerSample);

  long channels = header.subChunk1.numChannels;
  Serial.print("Channels = ");
  Serial.println(channels);

  long sampleRate = header.subChunk1.sampleRate;
  Serial.print("Sample rate = ");
  Serial.print(sampleRate);
  Serial.println(" Hz");
  long bitsPerSample = header.subChunk1.bitsPerSample;
  bool transform_data = false;
  if(bitsPerSample == 8){ // I2S needs at least 16 bits we will reduce sample rate
    bitsPerSample = 16;
    sampleRate = sampleRate /2;
  }
  AudioOutI2S.outBegin(sampleRate, bitsPerSample, 5, 25, 35, true);

  // Adjust the playback volume
  Serial.println("set volume...");
  AudioOutI2S.volume(100.0); // Set maximum volume (100%)

  // start playback
  Serial.println("Starting playback");
  size_t n = 1024;
  uint8_t data[n];
  size_t bytes_written;
  uint8_t *file_pointer;
  file_pointer = (uint8_t *)data_offset;
  Serial.print("I2S Start play from memory address "); Serial.println((uint32_t)file_pointer);
  Serial.print("File relative address "); Serial.println((uint32_t)file_pointer-(uint32_t)file_start);
  while(true){
    if((uint32_t)file_pointer >= (uint32_t)file_end){
      Serial.println("Playback stopped");
      AudioOutI2S.stop();
      return true;
    }else{
      if(n > ((uint32_t)file_end-(uint32_t)file_pointer)){ // last chunk
        Serial.print("last chunk = "); Serial.println(n);
        n = (size_t)((uint32_t)file_end-(uint32_t)file_pointer);
      }
      memcpy(data, (void*)file_pointer, n);
      i2s_write((i2s_port_t) AudioOutI2S.get_esp32_i2s_port_number(), data, n, &bytes_written, 100);

      file_pointer += bytes_written;
    } // if not end of file
  } // playback loop
} // play_wav_file()

void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  #ifdef USE_BUTTON
    pinMode(button_pin, INPUT_PULLUP);
  #endif
  Serial.println("Initialization done.");
}

void loop() {
  #ifdef USE_BUTTON
    static bool button_current_state, button_previous_state;
    button_previous_state = true;
    while(true){
      button_current_state = digitalRead(button_pin);
      if(button_previous_state == true && button_current_state == false){
        Serial.println("Button pressed - play");
        play_embedded_wav_file(sound_file_wav_start, sound_file_wav_end);
      }
      button_previous_state = button_current_state;
    } // infinite loop
  #else
    play_embedded_wav_file(sound_file_wav_start, sound_file_wav_end);
    Serial.println("Back in main loop");
    delay(5000);
  #endif
}

bool read_embeded_wav_header(const uint8_t *file_start, const uint8_t *file_end, WaveFileHeader *header, uint32_t *data_offset)
{
  uint32_t file_size = (uint32_t)file_end - (uint32_t)file_start;
  uint32_t file_pointer = (uint32_t)file_start;

  if (file_size < sizeof(struct WaveFileHeader)) {
    return false; // ERR
  }

  int headerSize;
  //int subChunk2Offset = 0;
  struct SubChunkHeader sch;

  headerSize = sizeof(struct WaveFileHeader) - sizeof(header->subChunk2Header);
  memcpy(header, file_start, headerSize);

  header->chunkId = __REV(header->chunkId);
  header->format = __REV(header->format);
  header->subChunk1.header.id = __REV(header->subChunk1.header.id);

  if (header->chunkId != 0x52494646) { // "RIFF"
    return false; // ERR
  }

  if ((file_size - 8) != header->chunkSize) {
    return false; // ERR
  }

  if (header->format != 0x57415645) { // "WAVE"
    return false; // ERR
  }

  if (header->subChunk1.header.id != 0x666d7420) { // "fmt "
    return false; // ERR
  }

  // header.size==16 for PCM
  // audioFormat==1 is PCM == Linear quantization; other than 1 indicate some form of compression
  if (header->subChunk1.header.size != 16 || header->subChunk1.audioFormat != 1) {
    return false; // ERR
  }

  file_pointer = (uint32_t)file_start+36;
  while (file_pointer+sizeof(sch) <= (uint32_t)file_end) {
    memcpy((void *)&sch, (void *)file_pointer, sizeof(sch));
    sch.id = __REV(sch.id);

    if (sch.id == 0x64617461) {
      // found the data section
      header->subChunk2Header.id = sch.id;
      header->subChunk2Header.size = sch.size;
      *data_offset = file_pointer+8;
      break;
    }
    // skip this header section
    file_pointer += sch.size + sizeof(sch);
  }

  if (header->subChunk2Header.id != 0x64617461) { // "data"
    // no data section found
    return false; // ERR
  }

  return true; // OK
}