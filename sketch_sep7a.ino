#include <WiFi.h>
#include <FirebaseESP32.h>
#include <DHT.h>
#include <ESP32Servo.h>

// ================= PENGATURAN WI-FI & FIREBASE =================
const char* ssid = "Kjiwonn";         
const char* password = "geewonii28"; 

#define FIREBASE_HOST "rc-telementri-default-rtdb.asia-southeast1.firebasedatabase.app" 
#define FIREBASE_AUTH "y4KdPtOZOokvMWl8qdZuSUpeOla0NAXgQsOW5a02"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Deklarasi Objek JSON untuk Firebase
FirebaseJson sensorJson;

// ================= DEKLARASI PIN & SENSOR =================
#define DHTPIN 15
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define TRIG_PIN 5
#define ECHO_PIN 18
#define SERVO_PIN 19
#define BUZZER_PIN 21
#define MQ2_PIN 36 

// Pin Motor L298N
#define IN1 25
#define IN2 26
#define IN3 27
#define IN4 14

Servo radarServo;

// ================= VARIABEL GLOBAL =================
float humidity = 0.0;
float temperature = 0.0;
int gasValue = 0;
long distance = 0;
bool isAlarm = false;

unsigned long previousMillis = 0;
unsigned long buzzerMillis = 0;
bool buzzerState = LOW;

const int GAS_THRESHOLD = 2000;
const float HUMIDITY_THRESHOLD = 80.0;
const int OBSTACLE_DISTANCE = 25; // Jarak minimal rintangan (cm)

// ================= FUNGSI KONTROL MOTOR L298N =================
void maju() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}
void mundur() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
}
void belokKiri() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  // Roda kanan maju
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); // Roda kiri mundur
}
void belokKanan() {
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH); // Roda kanan mundur
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  // Roda kiri maju
}
void berhenti() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}

// ================= FUNGSI BACA ULTRASONIK =================
long readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 10000); // Timeout 10ms agar tidak hang
  long dist = duration * 0.034 / 2;
  return (dist == 0 || dist > 400) ? 400 : dist; // Filter error bacaan
}

// ================= FUNGSI MENCARI JALAN =================
void hindariRintangan() {
  berhenti(); // Berhenti seketika
  delay(300);
  
  mundur(); // Mundur sedikit untuk mengambil jarak
  delay(500); 
  berhenti();
  
  // Menoleh ke Kiri
  radarServo.write(180); 
  delay(600);
  long jarakKiri = readUltrasonic();
  
  // Menoleh ke Kanan
  radarServo.write(0); 
  delay(800); // Waktu lebih lama karena putaran dari 180 ke 0
  long jarakKanan = readUltrasonic();
  
  // Kembali ke Tengah
  radarServo.write(90); 
  delay(400);

  // Keputusan Arah
  if (jarakKiri > jarakKanan && jarakKiri > OBSTACLE_DISTANCE) {
    belokKiri();
    delay(500); // Sesuaikan durasi ini agar beloknya pas 90 derajat
  } else if (jarakKanan > jarakKiri && jarakKanan > OBSTACLE_DISTANCE) {
    belokKanan();
    delay(500);
  } else {
    // Jika Kiri dan Kanan sama-sama buntu, Putar Balik
    mundur();
    delay(600);
    belokKanan();
    delay(800); 
  }
  
  berhenti();
  delay(200);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);

  dht.begin();
  radarServo.setPeriodHertz(50);
  radarServo.attach(SERVO_PIN, 500, 2400); 
  radarServo.write(90); // Posisikan servo lurus ke depan di awal

  Serial.print("Menghubungkan ke Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println("\nWi-Fi Terhubung!");

  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// ================= LOOP UTAMA =================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Logika Otonom (Maju & Menghindar)
  distance = readUltrasonic();
  
  if (distance < OBSTACLE_DISTANCE) {
    hindariRintangan();
  } else {
    maju();
    radarServo.write(90); // Pastikan selalu menatap lurus saat maju
  }

  // 2. Timer Khusus Baca DHT22 (Setiap 2 Detik)
  // Sensor DHT22 lambat (maksimal update 2 detik sekali), jangan dibaca terlalu cepat
  static unsigned long dhtMillis = 0;
  if (currentMillis - dhtMillis >= 2000) {
    dhtMillis = currentMillis;
    
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    
    // Pastikan hasil bukan NaN (Not a Number) agar tidak error di JSON
    if (!isnan(h)) humidity = h;
    if (!isnan(t)) temperature = t;
  }

  // 3. Timer MQ-2, Evaluasi Alarm & Kirim JSON ke Firebase (Setiap 500ms)
  if (currentMillis - previousMillis >= 500) {
    previousMillis = currentMillis;
    
    gasValue = analogRead(MQ2_PIN);

    // Evaluasi Alarm
    if (gasValue > GAS_THRESHOLD || humidity > HUMIDITY_THRESHOLD) {
      isAlarm = true;
    } else {
      isAlarm = false;
    }

    // --- PENGEMASAN DATA JSON ---
    sensorJson.clear(); // Bersihkan JSON sebelumnya
    sensorJson.set("suhu", temperature);
    sensorJson.set("kelembapan", humidity);
    sensorJson.set("gas", gasValue);
    sensorJson.set("jarak", distance);

    // Kirim 1 paket data JSON ke node "/monitoring"
    // Gunakan updateNode alih-alih setJSON agar efisien mengirim pembaruan
    if (Firebase.updateNode(fbdo, "/monitoring", sensorJson)) {
      Serial.println("Data JSON berhasil dikirim");
    } else {
      Serial.println("Gagal mengirim data: " + fbdo.errorReason());
    }
  }

  // 4. Logika Buzzer Alarm
  if (isAlarm) {
    if (currentMillis - buzzerMillis >= 300) {
      buzzerMillis = currentMillis;
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = LOW;
  }
}