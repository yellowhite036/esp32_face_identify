#include <WiFi.h>
#include <PubSubClient.h> //請先安裝PubSubClient程式庫
#include <LiquidCrystal_I2C.h>
#include <WiFiClientSecure.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); // 初始化 LCD
int buzzer = 16;
// ------ 以下修改成你自己的WiFi帳號密碼 ------
char ssid[] = "WIFI";
char password[] = "password";

//------ 以下修改成你腳位 -----

// ------ 以下修改成你MQTT設定 ------

char* MQTTServer = "mqttgo.io";//免註冊MQTT伺服器
int MQTTPort = 1883;//MQTT Port
char* MQTTUser = "";//不須帳密
char* MQTTPassword = "";//不須帳密
String Linetoken = "line"

//訂閱主題1:改變LED燈號(記得改Topic)

char* MQTTSubTopic1 = "isu/class/number";

WiFiClient WifiClient;
PubSubClient MQTTClient(WifiClient);

int playing = -1;                           

void noTone(byte pin = playing) {
  if (pin == playing) {
    ledcWriteTone(0, 0);   
    ledcDetachPin(pin);     
    playing = -1;           
  }
}

String SendTextLine(String msg) {
  WiFiClientSecure client_tcp;
  if (client_tcp.connect("notify-api.line.me", 443)) {
    Serial.println("連線到Line成功");

    // 組成HTTP POST表頭
    String payload = "message=" + msg;

    // 開始POST傳送
    client_tcp.println("POST /api/notify HTTP/1.1");
    client_tcp.println("Connection: close");
    client_tcp.println("Host: notify-api.line.me");
    client_tcp.println("Authorization: Bearer " + Linetoken);
    client_tcp.println("Content-Length: " + String(payload.length()));
    client_tcp.println("Content-Type: application/x-www-form-urlencoded");
    client_tcp.println();
    client_tcp.print(payload);

    String response = "";
    boolean state = false;
    int waitTime = 3000;  // 等候時間3秒鐘
    long startTime = millis();
    delay(1000);
    Serial.print("等候回應...");
    while ((startTime + waitTime) > millis()) {
      Serial.print(".");
      delay(100);
      while (client_tcp.available()) {
        // 已收到回覆，依序讀取內容
        char c = client_tcp.read();
        response += c;
      }
    }
    client_tcp.stop();
    return response;
  } else {
    return "傳送失敗，請檢查網路設定";
  }
}

void tone(byte pin, int freq, unsigned long duration = 0) {
  ledcSetup(0, 2000, 8);   
  ledcAttachPin(pin, 0);   
  ledcWriteTone(0, freq);  
  playing = pin;            
  if (duration > 0) {
    delay(duration);    
    noTone(pin);            
  }
}



void setup() {
  Serial.begin(115200);
  pinMode(buzzer, OUTPUT);
  lcd.init(); // 初始化 LCD
  lcd.backlight(); // 开启背光

  //開始WiFi連線
  WifiConnecte();

  //開始MQTT連線
  MQTTConnecte();
}

void loop() {
  //如果WiFi連線中斷，則重啟WiFi連線
  if (WiFi.status() != WL_CONNECTED) WifiConnecte();

  //如果MQTT連線中斷，則重啟MQTT連線
  if (!MQTTClient.connected())  MQTTConnecte();

  MQTTClient.loop();//更新訂閱狀態
  delay(50);
}

//開始WiFi連線
void WifiConnecte() {
  //開始WiFi連線
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi連線成功");
  Serial.print("IP Address:");
  Serial.println(WiFi.localIP());
}

//開始MQTT連線
void MQTTConnecte() {
  MQTTClient.setServer(MQTTServer, MQTTPort);
  MQTTClient.setCallback(MQTTCallback);
  while (!MQTTClient.connected()) {
    
    //以亂數為ClietID
    String  MQTTClientid = "esp32-" + String(random(1000000, 9999999));
    if (MQTTClient.connect(MQTTClientid.c_str(), MQTTUser, MQTTPassword)) {
      //連結成功，顯示「已連線」。
      Serial.println("MQTT已連線");
      //訂閱SubTopic1主題
      MQTTClient.subscribe(MQTTSubTopic1);
    } else {
      //若連線不成功，則顯示錯誤訊息，並重新連線
      Serial.print("MQTT連線失敗,狀態碼=");
      Serial.println(MQTTClient.state());
      Serial.println("五秒後重新連線");
      delay(5000);
    }
  }
}

//接收到訂閱時
void MQTTCallback(char* topic, byte * payload, unsigned int length) {
  Serial.print(topic); Serial.print("訂閱通知:");
  String payloadString;//將接收的payload轉成字串
  
  //顯示訂閱內容
  for (int i = 0; i < length; i++) {
    payloadString = payloadString + (char)payload[i];
  }
  Serial.println(payloadString);
  
  //比對主題是否為訂閱主題1
  if (strcmp(topic, MQTTSubTopic1) == 0) {
    Serial.println("改變燈號：" + payloadString);
    if (payloadString == "none") {
      
      
      tone(buzzer, 2000, 100);
      delay(100);
      tone(buzzer, 2000, 100);
      delay(100);
      tone(buzzer, 2000, 100);
      lcd.setCursor(0, 0);
      lcd.print("Recognition failed");
      lcd.setCursor(0, 1);
      lcd.print("Please try again");
      delay(8000); 
      lcd.clear(); 
      
           
    }
    else {
      
      
      tone(buzzer, 2000, 100);
      lcd.setCursor(0, 0);
      lcd.print(payloadString + " Sign-in");
      lcd.setCursor(0, 1);
      lcd.print("Successful");
      delay(8000); 
      lcd.clear();
      
    
    }

  }
}
