#pragma once

// DHT22 no GPIO4 (mesma ligação da AP1).
void sensorInicia();

// Devolve false se o sensor não respondeu (t e u ficam NAN).
// Respeita o intervalo mínimo de 2 s: dentro dele, repete a última leitura.
bool sensorLe(float& temperatura_c, float& umidade_pct);
