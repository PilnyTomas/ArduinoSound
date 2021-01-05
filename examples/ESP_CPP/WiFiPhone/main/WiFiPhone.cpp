/*
TODO po prazdninach - data samotny sou ok -teda aspon tak vypadaj.
problem je ale v rychlosti, laggovani atd - je velky zpozdeni a cely to zni trhane
zacal sem pridavat implementaci z udp verze ktera pouziva fronty a strukturu pro data
*/


/*
 * WiFi Phone / intercomm / baby monitor
 * This example demonstrates simultaneous use of input and output.
 *
 * Needed hardware:
 *  - At least 2 LyraT boards.
 *  - For each LyraT board you will also need speaker or headphones.
 *  - You can use either onboard microphones or your own external microphone.
 *
 * Setup:
 *  - Build and flash your LyraT boards.
 *    # Each board must have different IP address - simply change BOARD_NUMER for each board
 *  - If you intend to use external microphone set USE_EXTERNAL_MIC true;
 *    # If you don't have any external microphone simply leave USE_EXTERNAL_MIC false
 *
 */

#define I_AM_RX
#define I_AM_TX

#define BOARD_NUMBER 1
#define USE_EXTERNAL_MIC false

#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <ArduinoSound.h>
#include <queue>
// lyrat v4_3 codec chip
ES8388 *codec_chip;

const int sampleRate = 8000;
const int bitsPerSample = 16;
const int buffer_bytes = ESP_NOW_MAX_DATA_LEN;
const int data_bytes = buffer_bytes - 1; // one byte for debug stamp
uint8_t *audio_buffer;


#define NUMSLAVES 20
esp_now_peer_info_t slaves[NUMSLAVES] = {};
int SlaveCnt = 0;
unsigned long lastSentTime = 0;
unsigned long lastRecvTime = 0;
uint64_t pos = 0;

#define CHANNEL_MASTER 1
#define CHANNEL_SLAVE 1
#define PRINTSCANRESULTS 0

const int buffer_size = ESP_NOW_MAX_DATA_LEN;
typedef struct buffer {
  uint8_t data[buffer_size];
  int size;
} buffer_t;

std::queue<buffer_t> q_wifi_in;

void InitESPNow();
void ScanForSlave();
void manageSlave();
void sendData(uint8_t *data, size_t len);
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len);
void configDeviceAP();

void setup() {
  disableCore0WDT();
  disableCore1WDT();
  audio_buffer = (uint8_t*)malloc(buffer_bytes);
  Serial.begin(115200); // setup the serial

  Serial.print("ESP_NOW_MAX_DATA_LEN = "); Serial.println(ESP_NOW_MAX_DATA_LEN);
  Serial.print("buffer_size = "); Serial.println(buffer_size);
  Serial.print("data_bytes = "); Serial.println(data_bytes);
  int Bps = sampleRate * (bitsPerSample/8) * 2;
  Serial.print("Bytes per sec = "); Serial.println(Bps);
  float foo = (float)Bps / data_bytes;
  Serial.print("ms per packet = "); Serial.println((1.0/foo)*1000.0);


  // init codec chip
  Wire.begin(GPIO_NUM_18, GPIO_NUM_23);
  codec_chip = new ES8388(GPIO_NUM_21, Wire);
  // init input
  if (!codec_chip->begin(sampleRate, bitsPerSample, USE_EXTERNAL_MIC, 0)){
    Serial.println("Failed to initialize I2S!");
    while (1); // do nothing
  }
  Serial.println("codec setup OK.");

  //Set device in AP+STA mode to begin with
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_ps(WIFI_PS_NONE); // disable Power Save to speedup communication (default PS is WIFI_PS_MIN_MODEM - station sleeps and wakens up peridiocially)
  // configure device AP mode
  configDeviceAP();
  // This is the mac address of the Master in Station Mode
  Serial.print("STA MAC: "); Serial.println(WiFi.macAddress());
  Serial.print("AP MAC: "); Serial.println(WiFi.softAPmacAddress());
  // Init ESPNow with a fallback logic
  InitESPNow();
  // Once ESPNow is successfully Init, we will register callbacks
  // get the status of Sent packet
  esp_now_register_send_cb(OnDataSent);
  // get the status of Trasnmitted packet
  esp_now_register_recv_cb(OnDataRecv);
  // Scan for slave
  ScanForSlave();

  Serial.println("All set - end of setup()");
}

// TODO delete
void get_debug_data(buffer_t *buf){
  static uint8_t cnt = 0;
  memset(buf->data, cnt++, buffer_size);
  buf->size =buffer_size;
}

// TODO delete
void print_arr(void* audio_buffer, int buffer_bytes){
  for(int i = 0; i < buffer_bytes/2; ++i){
    Serial.print(((uint16_t*)audio_buffer)[i]);Serial.print(" ");
  }
}

void loop(){
  //static uint8_t stamp = 0; // debug

  // If Slave is found, it would be populate in `slave` variable
  // We will check if `slave` is defined and then we proceed further
  if (SlaveCnt > 0) { // check if slave channel is defined
    // `slave` is defined
    // Add slave as peer if it has not been added already
    manageSlave();
    // pair success or already paired
    // Send data to device
    #ifdef I_AM_TX
      int ret = codec_chip->read(audio_buffer, buffer_bytes);
      //int ret = codec_chip->read(audio_buffer, data_bytes); // debug
      //Serial.print("Send stamp "); Serial.println(stamp);
      //audio_buffer[buffer_bytes-1] = stamp++; // debug
      sendData(audio_buffer, buffer_bytes);
    #endif // I_AM_TX

    #ifdef I_AM_RX
      if(q_wifi_in.size()){
          codec_chip->write((void*)q_wifi_in.front().data, q_wifi_in.front().size);
          //Serial.print("Received stamp "); Serial.println(q_wifi_in.front().data[buffer_bytes-1]);
          q_wifi_in.pop();
      }
    #endif // I_AM_RX
  }else{
    // No slave found to process
    // Scan for slave
    ScanForSlave();
  } // if (SlaveCnt > 0)
} // main loop()


void InitESPNow() {
  if (esp_now_init() == ESP_OK) {
    Serial.println("ESPNow Init Success");
  }
  else {
    Serial.println("ESPNow Init Failed");
    ESP.restart();
  }
}

void ScanForSlave() {
  int8_t scanResults = WiFi.scanNetworks();
  //reset slaves
  memset(slaves, 0, sizeof(slaves));
  SlaveCnt = 0;
  Serial.println("");
  if (scanResults == 0) {
    Serial.println("No WiFi devices in AP Mode found");
  } else {
    Serial.print("Found "); Serial.print(scanResults); Serial.println(" devices ");
    for (int i = 0; i < scanResults; ++i) {
      // Print SSID and RSSI for each device found
      String SSID = WiFi.SSID(i);
      int32_t RSSI = WiFi.RSSI(i);
      String BSSIDstr = WiFi.BSSIDstr(i);

      if (PRINTSCANRESULTS) {
        Serial.print(i + 1); Serial.print(": "); Serial.print(SSID); Serial.print(" ["); Serial.print(BSSIDstr); Serial.print("]"); Serial.print(" ("); Serial.print(RSSI); Serial.print(")"); Serial.println("");
      }
      // Check if the current device starts with `Slave`
      if (SSID.indexOf("ESPNOW") == 0) {
        // SSID of interest
        Serial.print(i + 1); Serial.print(": "); Serial.print(SSID); Serial.print(" ["); Serial.print(BSSIDstr); Serial.print("]"); Serial.print(" ("); Serial.print(RSSI); Serial.print(")"); Serial.println("");
        // Get BSSID => Mac Address of the Slave
        int mac[6];

        if ( 6 == sscanf(BSSIDstr.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5] ) ) {
          for (int ii = 0; ii < 6; ++ii ) {
            slaves[SlaveCnt].peer_addr[ii] = (uint8_t) mac[ii];
          }
        }
        slaves[SlaveCnt].channel = CHANNEL_MASTER; // pick a channel
        slaves[SlaveCnt].encrypt = 0; // no encryption
        SlaveCnt++;
      }
    }
  }

  if (SlaveCnt > 0) {
    Serial.print(SlaveCnt); Serial.println(" Slave(s) found, processing...");
  } else {
    Serial.println("No Slave Found, trying again.");
  }

  // clean up ram
  WiFi.scanDelete();
}

void manageSlave() {
  if (SlaveCnt > 0) {
    for (int i = 0; i < SlaveCnt; i++) {
      const esp_now_peer_info_t *peer = &slaves[i];
      const uint8_t *peer_addr = slaves[i].peer_addr;
      // check if the peer exists
      bool exists = esp_now_is_peer_exist(peer_addr);
      if (!exists) {
        // Slave not paired, attempt pair
        esp_err_t addStatus = esp_now_add_peer(peer);
        if (addStatus == ESP_OK) {
          // Pair success
          //Serial.println("Pair success");
        } else if (addStatus == ESP_ERR_ESPNOW_NOT_INIT) {
          // How did we get so far!!
          //Serial.println("ESPNOW Not Init");
        } else if (addStatus == ESP_ERR_ESPNOW_ARG) {
          //Serial.println("Add Peer - Invalid Argument");
        } else if (addStatus == ESP_ERR_ESPNOW_FULL) {
          //Serial.println("Peer list full");
        } else if (addStatus == ESP_ERR_ESPNOW_NO_MEM) {
          //Serial.println("Out of memory");
        } else if (addStatus == ESP_ERR_ESPNOW_EXIST) {
          //Serial.println("Peer Exists");
        } else {
          //Serial.println("Not sure what happened");
        }
      }
    }
  } else {
    // No slave found to process
    //Serial.println("No Slave found to process");
  }
}

void sendData(uint8_t *data, size_t len) {
  for (int i = 0; i < SlaveCnt; i++) {
    const uint8_t *peer_addr = slaves[i].peer_addr;

    esp_err_t result = esp_now_send(peer_addr, data, len);

    //Serial.print("Send Status: ");
    if (result == ESP_OK) {
      //Serial.println("Success");
    } else if (result == ESP_ERR_ESPNOW_NOT_INIT) {
      // How did we get so far!!
      //Serial.println("ESPNOW not Init.");
    } else if (result == ESP_ERR_ESPNOW_ARG) {
      //Serial.println("Invalid Argument");
    } else if (result == ESP_ERR_ESPNOW_INTERNAL) {
      //Serial.println("Internal Error");
    } else if (result == ESP_ERR_ESPNOW_NO_MEM) {
      //Serial.println("ESP_ERR_ESPNOW_NO_MEM");
    } else if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
      //Serial.println("Peer not found.");
    } else {
      //Serial.println("Not sure what happened");
    }
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // NOP
}
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  #ifdef I_AM_RX
    buffer_t buf;
    memcpy(buf.data, data, data_len);
    buf.size = data_len;
    q_wifi_in.push(buf);
  #endif
}

void configDeviceAP() {
  String Prefix = "ESPNOW:";
  String Mac = WiFi.macAddress();
  String SSID = Prefix + Mac;
  String Password = "123456789";
  bool result = WiFi.softAP(SSID.c_str(), Password.c_str(), CHANNEL_SLAVE, 0);
  if (!result) {
    Serial.println("AP Config failed.");
  } else {
    Serial.println("AP Config Success. Broadcasting with AP: " + String(SSID));
  }
}
