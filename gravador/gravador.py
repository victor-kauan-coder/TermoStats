# -*- coding: utf-8 -*-
"""
gravador.py - quem grava o que o broker nao guarda
Laboratorio 03 · 30/09/2026 · Topicos em Sistemas Computacionais I (IoT/AIoT) · UFPI 2026.2

Um ASSINANTE MQTT: conecta ao broker, assina os topicos da equipe e grava
em dados/mensagens.csv cada mensagem que chega.

    pip install paho-mqtt
    python gravador.py --host SEU_CLUSTER.s1.eu.hivemq.cloud --usuario eq03
    python gravador.py --host broker.hivemq.com --equipe eq01     (broker publico, sem login)

Com --usuario, a senha e pedida no terminal. Para parar: Ctrl+C.
"""
import argparse, csv, getpass, os, ssl, time
from datetime import datetime
import paho.mqtt.client as mqtt

AQUI = os.path.dirname(os.path.abspath(__file__))

p = argparse.ArgumentParser()
p.add_argument("--host", required=True, help="o MQTT_HOST do SIGAA, sem https://")
p.add_argument("--usuario", help="usuario da equipe: eq01, eq02... (sem ele, conecta sem login)")
p.add_argument("--equipe", help="arvore a assinar (padrao: o usuario; o docente usa +)")
p.add_argument("--sessao-limpa", action="store_true", help="o broker NAO guarda nada enquanto voce esta fora")
a = p.parse_args()
equipe = a.equipe or a.usuario
if not equipe:
    p.error("sem --usuario, informe a arvore com --equipe (ex.: --equipe eq01)")
senha = None
if a.usuario:
    senha = os.environ.get("MQTT_SENHA") or getpass.getpass(f"senha de {a.usuario}: ")

# ----------------------------------------------------------------- o arquivo
os.makedirs(os.path.join(AQUI, "dados"), exist_ok=True)
caminho = os.path.join(AQUI, "dados", "mensagens.csv")
novo = not os.path.exists(caminho)
arquivo = open(caminho, "a", newline="", encoding="utf-8")
csvw = csv.writer(arquivo)
if novo:
    csvw.writerow(["t_chegada", "chegada", "topico", "retido", "payload"])


# ----------------------------------------------------------------- 1. ao conectar: assinar
def ao_conectar(cliente, userdata, flags, rc, props):
    if rc != 0:
        print(f"[mqtt] conexao recusada: {rc}")
        return
    guardava = "GUARDAVA" if flags.session_present else "NAO tinha"
    print(f"[mqtt] conectado · o broker {guardava} a minha sessao")
    cliente.subscribe(f"ufpi-iot26/{equipe}/+/telemetria", qos=1)
    cliente.subscribe(f"ufpi-iot26/{equipe}/+/status", qos=1)
    print(f"[mqtt] assinando ufpi-iot26/{equipe}/+/telemetria e .../+/status (QoS 1)")


# ----------------------------------------------------------------- 2. a cada mensagem: gravar
def ao_receber(cliente, userdata, msg):
    agora = time.time()
    texto = msg.payload.decode("utf-8", errors="replace")
    hora = datetime.fromtimestamp(agora).strftime("%H:%M:%S")
    csvw.writerow([f"{agora:.3f}", hora, msg.topic, int(msg.retain), texto])
    arquivo.flush()                                   # vai para o disco agora
    print(f"{hora}  {msg.topic}  {texto}{'   (retida)' if msg.retain else ''}")


# ----------------------------------------------------------------- 3. conectar e esperar
cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                      client_id=f"ufpi-iot26-{a.usuario or equipe}-gravador",   # fixo: o broker me reconhece
                      clean_session=a.sessao_limpa)                  # False = sessao persistente
ca = None                                             # None = os certificados do sistema
if a.usuario:                                         # broker da turma: login e a raiz dele
    cliente.username_pw_set(a.usuario, senha)
    if os.path.exists(os.path.join(AQUI, "ca_raiz.pem")):
        ca = os.path.join(AQUI, "ca_raiz.pem")
cliente.tls_set(ca_certs=ca, cert_reqs=ssl.CERT_REQUIRED)
cliente.on_connect = ao_conectar
cliente.on_message = ao_receber

print(f"[gravador] gravando em {caminho}  (Ctrl+C para parar)")
cliente.connect(a.host, 8883, keepalive=30)
try:
    cliente.loop_forever()                            # recebe mensagens e reconecta sozinho
except KeyboardInterrupt:
    print("\n[gravador] parado.")
finally:
    arquivo.close()
