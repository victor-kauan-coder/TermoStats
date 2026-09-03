
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
Adafruit_SSD1306 display(128, 64, &Wire, -1);

const int PINO_LED = 26;

const uint8_t MPU = 0x68;
float ax = 0, ay = 0, az = 0, tchip = 0;
uint32_t n = 0;

// Escreve um byte numa gaveta do dispositivo.
void escreveReg(uint8_t dev, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(dev);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// Le qtd bytes a partir de uma gaveta do dispositivo.
void leRegs(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t qtd) {
  Wire.beginTransmission(dev);
  Wire.write(reg); // "aponte para esta gaveta"
  Wire.endTransmission(false); // false = nao solte o barramento agora
  Wire.requestFrom(dev, qtd); // "me de qtd bytes a partir dali"
  for (uint8_t i = 0; i < qtd; i++) buf[i] = Wire.read();
}

void iniciaMPU() {
  escreveReg(MPU, 0x6B, 0x00); // gaveta 0x6B = energia; 0 = acorde
  escreveReg(MPU, 0x1C, 0x00); // gaveta 0x1C = escala; 0 = +-2 g
}

void leMPU() {
  uint8_t b[14];
  leRegs(MPU, 0x3B, b, 14); // 14 bytes seguidos: 3 eixos, temperatura, 3 giros
  ax = (int16_t)((b[0] << 8) | b[1]) / 16384.0f; // 16384 contagens = 1 g
  ay = (int16_t)((b[2] << 8) | b[3]) / 16384.0f;
  az = (int16_t)((b[4] << 8) | b[5]) / 16384.0f;
  tchip = (int16_t)((b[6] << 8) | b[7]) / 340.0f + 36.53f;
}

void iniciaOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED nao respondeu em 0x3C");
    while (true) {}
  }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
  }

void desenhaPainel() {
  display.clearDisplay(); // limpa a copia na memoria do ESP32
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("LAB 02");
  display.setCursor(0, 16); display.printf("ax %+.2f ay %+.2f", ax, ay);
  display.setCursor(0, 26); display.printf("az %+.2f g", az);
  display.setCursor(0, 40); display.printf("chip %.1f C", tchip);
  display.setCursor(0, 54); display.printf("amostras: %lu", n);
  display.display(); // <<< AQUI a imagem inteira vai pelo fio
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); // SDA, SCL
  Wire.setClock(400000); //esse comando define a velocidade do barramento para 400khz
  delay(200);
  Serial.println("Scanner I2C - varrendo 0x08 a 0x77");
  int achados = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr); // "alguem ai atende por este numero?"
    if (Wire.endTransmission() == 0) { // 0 significa: alguem respondeu
      Serial.printf(" 0x%02X\n", addr);
      achados++;
    }
  }

  Serial.printf("total: %d dispositivo(s)\n", achados);
  // o nome nao veio do scanner: veio de dentro do chip
  uint8_t v;
  Wire.beginTransmission(0x68); Wire.write(0x75); Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)0x68, (uint8_t)1); v = Wire.read();
  Serial.printf("MPU6050 WHO_AM_I = 0x%02X\n", v);
  pinMode(PINO_LED, OUTPUT);
  delay(300);
  Serial.println();
  Serial.println("Lab 02 - pronto");
  iniciaMPU();
  iniciaOLED();
}

void loop() {
  leMPU();
  uint32_t t0 = micros();
  desenhaPainel();
  uint32_t custoOLED = micros() - t0;
  
  //Serial.printf("ax=%+.2f ay=%+.2f az=%+.2f g | t_chip=%.1f C\n",
  //ax, ay, az, tchip);
  Serial.printf("display() levou %lu us | az=%+.2f\n", custoOLED, az);
  digitalWrite(PINO_LED, (millis() / 250) % 2); // pisca sem travar o programa
  delay(500);
}

