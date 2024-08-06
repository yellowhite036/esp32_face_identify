#include "esp_camera.h"
#include "FS.h"      //sd card esp32
#include "SD_MMC.h"  //sd card esp32
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>


int Trig = 12;  // 發出聲波腳位
int Echo = 14;  // 接收聲波腳位
int R_led = 32;
int G_led = 33;
int B_led = 13;
int PicID = 1;    //檔案序號

char ssid[] = "WIFI";
char password[] = "password";
String Linetoken = "line";

// ------ 以下修改成你MQTT設定 ------
char* MQTTServer = "broker.mqttgo.io";                  //免註冊MQTT伺服器
int MQTTPort = 1883;                             //MQTT Port
char* MQTTUser = "";                             //不須帳密
char* MQTTPassword = "";                         //不須帳密
char* MQTTPubTopic1 = "isu/class/face";

WiFiClient WifiClient;
PubSubClient MQTTClient(WifiClient);

//開始MQTT連線
void MQTTConnecte() {
  MQTTClient.setServer(MQTTServer, MQTTPort);
  //MQTTClient.setCallback(MQTTCallback);
  while (!MQTTClient.connected()) {
    //以亂數為ClientID
    String MQTTClientid = "esp32-" + String(random(1000000, 9999999));
    if (MQTTClient.connect(MQTTClientid.c_str(), MQTTUser, MQTTPassword)) {
      //連結成功，顯示「已連線」。
      Serial.println("MQTT已連線");
      //訂閱SubTopic1主題
      //MQTTClient.subscribe(MQTTSubTopic1);
    } else {
      //若連線不成功，則顯示錯誤訊息，並重新連線
      Serial.print("MQTT連線失敗,狀態碼=");
      Serial.println(MQTTClient.state());
      Serial.println("五秒後重新連線");
      delay(5000);
    }
  }
}

//拍照傳送到MQTT
String SendImageMQTT() {
  camera_fb_t* fb = esp_camera_fb_get();
  size_t fbLen = fb->len;
  int ps = 512;
  //開始傳遞影像檔
  MQTTClient.beginPublish(MQTTPubTopic1, fbLen, false);
  uint8_t* fbBuf = fb->buf;
  for (size_t n = 0; n < fbLen; n = n + 2048) {
    if (n + 2048 < fbLen) {
      MQTTClient.write(fbBuf, 2048);
      fbBuf += 2048;
    } else if (fbLen % 2048 > 0) {
      size_t remainder = fbLen % 2048;
      MQTTClient.write(fbBuf, remainder);
    }
  }
  boolean isPublished = MQTTClient.endPublish();
  esp_camera_fb_return(fb);  //清除緩衝區
  if (isPublished) {
    return "MQTT傳輸成功";
  } else {
    return "MQTT傳輸失敗，請檢查網路設定";
  }
}

//拍照傳送到Line
String SendImageLine(String msg, camera_fb_t* fb) {
  WiFiClientSecure client_tcp;
  if (client_tcp.connect("notify-api.line.me", 443)) {
    Serial.println("連線到Line成功");
    //組成HTTP POST表頭
    String head = "--Cusboundary\r\nContent-Disposition: form-data;";
    head += "name=\"message\"; \r\n\r\n" + msg + "\r\n";
    head += "--Cusboundary\r\nContent-Disposition: form-data; ";
    head += "name=\"imageFile\"; filename=\"esp32-cam.jpg\"";
    head += "\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--Cusboundary--\r\n";
    uint16_t imageLen = fb->len;
    uint16_t extraLen = head.length() + tail.length();
    uint16_t totalLen = imageLen + extraLen;
    //開始POST傳送
    client_tcp.println("POST /api/notify HTTP/1.1");
    client_tcp.println("Connection: close");
    client_tcp.println("Host: notify-api.line.me");
    client_tcp.println("Authorization: Bearer " + Linetoken);
    client_tcp.println("Content-Length: " + String(totalLen));
    client_tcp.println("Content-Type: multipart/form-data; boundary=Cusboundary");
  
    client_tcp.println();
    client_tcp.print(head);
    uint8_t* fbBuf = fb->buf;
    size_t fbLen = fb->len;
    Serial.println("傳送影像檔...");
    for (size_t n = 0; n < fbLen; n = n + 2048) {
      if (n + 2048 < fbLen) {
        client_tcp.write(fbBuf, 2048);
        fbBuf += 2048;
      } else if (fbLen % 2048 > 0) {
        size_t remainder = fbLen % 2048;
        client_tcp.write(fbBuf, remainder);
      }
    }
    client_tcp.print(tail);
    client_tcp.println();
    String payload = "";
    boolean state = false;
    int waitTime = 3000;  //等候時間3秒鐘
    long startTime = millis();
    delay(1000);
    Serial.print("等候回應...");
    while ((startTime + waitTime) > millis()) {
      Serial.print(".");
      delay(100);
      while (client_tcp.available()) {
        //已收到回覆，依序讀取內容
        char c = client_tcp.read();
        payload += c;
      }
    }
    client_tcp.stop();
    return payload;
  } else {
    return "傳送失敗，請檢查網路設定";
  }
}

//拍照存檔SD卡副程式
void SavePictoSD(String filename, camera_fb_t* fb) {
  Serial.print("寫入檔案:" + filename + ",檔案大小=");
  Serial.println(String(fb->len) + "bytes");
  fs::FS& fs = SD_MMC;                        //設定SD卡裝置
  File file = fs.open(filename, FILE_WRITE);  //開啟檔案
  if (!file) {
    Serial.println("存檔失敗，請檢查SD卡");
  } else {
    file.write(fb->buf, fb->len);  //
    Serial.println("存檔成功");
  }
}

float distance() {

  digitalWrite(Trig, LOW);
  delayMicroseconds(5);
  digitalWrite(Trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(Trig, LOW);
  float EchoTime = pulseIn(Echo, HIGH); //傳回時間
  float CMValue = EchoTime / 29.4 / 2; //轉換成距離
  Serial.println(CMValue);
  return CMValue;

}

void setup() {
  Serial.begin(115200);
  //初始化相機結束
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sscb_sda = 26;
  config.pin_sscb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.fb_count = 2;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = 12;
  //設定解析度：FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
  config.frame_size = FRAMESIZE_VGA;  //VGA=640*480(VGA格式較穩定)
  //Line notify don't accept bigger than XGA
  esp_err_t err = esp_camera_init(&config);
  if (err == ESP_OK) {
    Serial.println("鏡頭啟動成功");
    // setup stream ------------------------
    sensor_t* s = esp_camera_sensor_get();
    int res = 0;
    res = s->set_brightness(s, 1);  //亮度:(-2~2)
    res = s->set_contrast(s, 1);    //對比度:(-2~2)
    res = s->set_saturation(s, 1);  //色彩飽和度:(-2~2)
    //res = s->set_special_effect(s, 0);//特殊效果:(0~6)
    //res = s->set_whitebal(s, 1);//啟動白平衡:(0或1)
    //res = s->set_awb_gain(s, 1);//自動白平衡增益:(0或1)
    //res = s->set_wb_mode(s, 0);//白平衡模式:(0~4)
    //res = set_exposure_ctrl(s, 1);;//曝光控制:(0或1)
    //res = set_aec2(s, 0);//自動曝光校正:(0或1)
    //res = set_ae_level(s, 0);//自動曝光校正程度:(-2~2)
    //res = set_aec_value(s, 300);//自動曝光校正值：(0~1200)
    //res = set_gain_ctrl(s, 1);//增益控制:(0或1)
    //res = set_agc_gain(s, 0);//自動增益:(0~30)
    //res = set_gainceiling(s, (gainceiling_t)0); //增益上限:(0~6)
    //res = set_bpc(s, 1);//bpc開啟:(0或1)
    //res = set_wpc(s, 1);//wpc開啟:(0或1)
    //res = set_raw_gma(s, 1);//影像GMA:(0或1)
    //res = s->set_lenc(s, 1);//鏡頭校正:(0或1)
    //res = s->set_hmirror(s, 1);//水平翻轉:(0或1)
    //res = s->set_vflip(s, 1);//垂直翻轉:(0或1)
    //res = set_dcw(s, 1);//dcw開啟:(0或1)
  } else {
    Serial.printf("鏡頭設定失敗，5秒後重新啟動");
    delay(5000);
    ESP.restart();
  }

  //設定SD卡
  if (!SD_MMC.begin()) {
    Serial.println("SD卡讀取失敗，5秒後重新啟動");
    delay(5000);
    ESP.restart();
  } else {
    Serial.println("SD卡偵測成功");
  }

  pinMode(Trig, OUTPUT);
  pinMode(Echo, INPUT);
  pinMode(R_led, OUTPUT);
  pinMode(G_led, OUTPUT);
  pinMode(B_led, OUTPUT);


  //開始網路連線
  Serial.print("連線到WiFi:");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

}


void loop() {
  //如果MQTT連線中斷，則重啟MQTT連線
  if (!MQTTClient.connected()) MQTTConnecte();

  float CMValue = distance();

  if (CMValue > 49 && CMValue < 55) {
  
    
    digitalWrite(R_led, LOW);
    digitalWrite(G_led, HIGH);
    digitalWrite(B_led, LOW);


    String result = SendImageMQTT();
    Serial.println(result);

    camera_fb_t * fb = esp_camera_fb_get(); //擷取影像
    if (!fb) {
      Serial.println("拍照失敗，請檢查");
    } else {
      //儲存到SD卡:SavePictoSD(檔名,影像);
      SavePictoSD("/pic" + String(PicID ++) + ".jpg", fb);
      //傳送到Line:SendImageLine(訊息,影像);
      String payload = SendImageLine("簽到成功", fb);
      Serial.println(payload);
      esp_camera_fb_return(fb);//清除影像緩衝區
    }
  }
  Serial.println("無人");

  digitalWrite(R_led, HIGH);
  digitalWrite(G_led, LOW);
  digitalWrite(B_led, LOW);
  delay(500);
}
