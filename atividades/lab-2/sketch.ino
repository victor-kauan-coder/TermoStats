
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

const int CS_SD = 5;
File arq; 
Adafruit_SSD1306 display(128, 64, &Wire, -1);

const int PINO_LED = 26;

const uint8_t MPU = 0x68;
float ax = 0, ay = 0, az = 0, tchip = 0;
uint32_t n = 0;

uint32_t ultimoMPU = 0, proxRelato = 0;
int32_t intMin = 0x7FFFFFFF, intMax = 0;

void registraIntervalo(uint32_t agora) {
  if (ultimoMPU != 0) {
    int32_t d = (int32_t)(agora - ultimoMPU);
    if (d < intMin) intMin = d;
    if (d > intMax) intMax = d;
  }
  ultimoMPU = agora;
  if ((int32_t)(agora - proxRelato) >= 0) { // relatorio a cada 5 s
  proxRelato = agora + 5000;
  Serial.printf(">>> amostras=%lu intervalo min=%ld ms max=%ld ms (alvo 20 ms)\n",
  n, (long)intMin, (long)intMax);
  intMin = 0x7FFFFFFF; intMax = 0;
  }
}

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

void iniciaSD() {
  if (!SD.begin(CS_SD)) { // usa SCK 18, MISO 19, MOSI 23 automaticamente
    Serial.println("SD.begin falhou - cheque CS, fiacao, alimentacao e formatacao");
    while (true) {}
  }
  bool novo = !SD.exists("/lab02.csv");
  arq = SD.open("/lab02.csv", FILE_APPEND); // APPEND: nao apaga o que ja existe
  if (!arq) { Serial.println("nao consegui abrir /lab02.csv"); while (true) {} }
  if (novo) arq.println("ms,ax,ay,az,t_chip");
  arq.flush();
  Serial.println("SD pronto: /lab02.csv");
}

void gravaLinha(uint32_t ms) {
  arq.printf("%lu,%.3f,%.3f,%.3f,%.2f\n", ms, ax, ay, az, tchip);
}

void dumpCSV() {
  arq.flush();
  File r = SD.open("/lab02.csv");
  if (!r) { Serial.println("nao consegui abrir para leitura"); return; }
  Serial.printf("\n===== /lab02.csv (%u bytes) =====\n", (unsigned)r.size());
  uint8_t buf[256];
  int k;
  while ((k = r.read(buf, sizeof buf)) > 0) Serial.write(buf, k);
  Serial.println("\n===== fim =====");
  r.close();
}

void diagnosticoSD() {
  Serial.println("\n--- diagnostico do SD ---");
  Serial.printf("SD.begin(%d) .... %s\n", CS_SD, SD.begin(CS_SD) ? "ok" : "FALHOU");
  Serial.printf("cardType ........ %d (0=nenhum, 1=MMC, 2=SD, 3=SDHC)\n", SD.cardType());
  Serial.printf("handle 'arq' .... %s\n", arq ? "valido" : "INVALIDO");
  if (arq) Serial.printf("bytes no arquivo %u\n", (unsigned)arq.size());
  File raiz = SD.open("/");
  Serial.println("conteudo da raiz:");
  for (File f = raiz.openNextFile(); f; f = raiz.openNextFile())
  Serial.printf(" %s %u bytes\n", f.name(), (unsigned)f.size());
  raiz.close();
  Serial.println("-------------------------\n");
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
  iniciaSD();
}

//jeito certo 5B
uint32_t proxMPU = 0, proxOLED = 0, proxFLUSH = 0;
const uint32_t P_MPU = 20, P_OLED = 500, P_FLUSH = 1000; // em ms
void loop() {
  uint32_t agora = millis();
  if ((int32_t)(agora - proxMPU) >= 0) {
    proxMPU = agora + P_MPU;
    leMPU();
    gravaLinha(agora); // so buffer: microssegundos
    n++;
    registraIntervalo(agora);
  }
  if ((int32_t)(agora - proxOLED) >= 0) { proxOLED = agora + P_OLED; desenhaPainel(); }
  if ((int32_t)(agora - proxFLUSH) >= 0) { proxFLUSH = agora + P_FLUSH; arq.flush(); }
  digitalWrite(PINO_LED, (agora / 250) % 2); // heartbeat, sem delay nenhum
}


//jeito ingênuo 5A
/*
void loop() {
  leMPU();
  desenhaPainel();
  uint32_t agora = millis();
  gravaLinha(agora);
  arq.flush();
  n++;
  registraIntervalo(agora);
  digitalWrite(PINO_LED, !digitalRead(PINO_LED));
  delay(20); // "para dar 50 amostras por segundo"
}

*/

//loop pre questão 5
/*
void loop() {
  leMPU();
  
  //uint32_t t0 = micros();
  desenhaPainel();
  //uint32_t custoOLED = micros() - t0;
  
  uint32_t t0 = micros();
  gravaLinha(millis()); // so escreve no buffer da RAM
  uint32_t custoPrint = micros() - t0;
  t0 = micros();
  arq.flush(); // aqui vai ao cartao de verdade
  uint32_t custoFlush = micros() - t0;
  n++;
  Serial.printf("printf %lu us | flush %lu us | linhas %lu\n",
  custoPrint, custoFlush, n);

  if (n == 20) {
    dumpCSV();
    Serial.println("Programa travado para copia do CSV.");
    while (true) {
      delay(10); // Fica preso aqui para você copiar os dados
    }
  }

  //Serial.printf("ax=%+.2f ay=%+.2f az=%+.2f g | t_chip=%.1f C\n",
  //ax, ay, az, tchip);
  //Serial.printf("display() levou %lu us | az=%+.2f\n", custoOLED, az);
  digitalWrite(PINO_LED, (millis() / 250) % 2); // pisca sem travar o programa
  delay(500);
}
*/

