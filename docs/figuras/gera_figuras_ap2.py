"""Gera as figuras da AP2 (arquitetura em camadas e caminho do dado) em PDF e PNG."""
import math
import sys

import cairo

FONT = "Liberation Sans"
INK = (0.13, 0.15, 0.18)
MUTED = (0.38, 0.41, 0.45)
LINE = (0.55, 0.58, 0.62)
CAMP = (0.85, 0.92, 0.98)      # campanha (o que rodou)
CAMP_B = (0.16, 0.42, 0.66)
PROD = (0.96, 0.94, 0.88)      # produto (proposta)
PROD_B = (0.62, 0.50, 0.22)
LAYER = (0.95, 0.95, 0.96)


def font(cr, size, bold=False):
    cr.select_font_face(FONT, cairo.FONT_SLANT_NORMAL,
                        cairo.FONT_WEIGHT_BOLD if bold else cairo.FONT_WEIGHT_NORMAL)
    cr.set_font_size(size)


def text_c(cr, x, y, s, size, bold=False, color=INK):
    font(cr, size, bold)
    e = cr.text_extents(s)
    cr.set_source_rgb(*color)
    cr.move_to(x - e.x_advance / 2, y)
    cr.show_text(s)


def text_l(cr, x, y, s, size, bold=False, color=INK):
    font(cr, size, bold)
    cr.set_source_rgb(*color)
    cr.move_to(x, y)
    cr.show_text(s)


def rbox(cr, x, y, w, h, fill, stroke, r=6, lw=1.2, dash=None):
    cr.new_sub_path()
    cr.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    cr.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    cr.close_path()
    cr.set_source_rgb(*fill)
    cr.fill_preserve()
    cr.set_source_rgb(*stroke)
    cr.set_line_width(lw)
    cr.set_dash(dash or [])
    cr.stroke()
    cr.set_dash([])


def box(cr, x, y, w, h, title, lines, fill, stroke, dash=None, tsize=10.5, lsize=8.6):
    rbox(cr, x, y, w, h, fill, stroke, dash=dash)
    text_c(cr, x + w / 2, y + 17, title, tsize, True)
    for i, s in enumerate(lines):
        text_c(cr, x + w / 2, y + 33 + i * 12.5, s, lsize, False, MUTED if i else INK)


def arrow(cr, x1, y1, x2, y2, color=INK, lw=1.3, head=7, dash=None):
    cr.set_source_rgb(*color)
    cr.set_line_width(lw)
    cr.set_dash(dash or [])
    ang = math.atan2(y2 - y1, x2 - x1)
    xe, ye = x2 - head * 0.8 * math.cos(ang), y2 - head * 0.8 * math.sin(ang)
    cr.move_to(x1, y1)
    cr.line_to(xe, ye)
    cr.stroke()
    cr.set_dash([])
    cr.move_to(x2, y2)
    cr.line_to(x2 - head * math.cos(ang - 0.4), y2 - head * math.sin(ang - 0.4))
    cr.line_to(x2 - head * math.cos(ang + 0.4), y2 - head * math.sin(ang + 0.4))
    cr.close_path()
    cr.fill()


def label(cr, x, y, s, size=7.8, color=MUTED, bold=False):
    text_c(cr, x, y, s, size, bold, color)


# ---------------------------------------------------------------------------
def arquitetura(cr, W, H):
    cr.set_source_rgb(1, 1, 1)
    cr.paint()
    cols = ["Percepção", "Rede", "Processamento e armazenamento", "Aplicação"]
    x0, gap = 20, 10
    cw = (W - 2 * x0 - 3 * gap) / 4
    top = 16
    for i, c in enumerate(cols):
        x = x0 + i * (cw + gap)
        rbox(cr, x, top, cw, H - top - 34, LAYER, (0.85, 0.86, 0.88), r=8, lw=0.8)
        text_c(cr, x + cw / 2, top + 20, c, 11, True)
    cx = [x0 + i * (cw + gap) for i in range(4)]
    bw, pad = cw - 16, 8

    # faixa da campanha
    y1 = 60
    text_l(cr, x0 + 6, y1 - 6, "Campanha da AP2 (o que rodou em 24/10)", 9.5, True, CAMP_B)
    bh = 104
    box(cr, cx[0] + pad, y1, bw, bh, "Nó eq01-no02",
        ["ESP32 + DHT22 (GPIO4)", "leitura única a cada 5 s", "USB, sempre conectado (DP-6 b)",
         "sem alarme nem fila local", "firmware do Lab 03 (lab3)"], CAMP, CAMP_B)
    box(cr, cx[1] + pad, y1, bw, bh, "WiFi → broker",
        ["roteador no mesmo cômodo", "RSSI −15 a −29 dBm", "MQTT/TLS 8883, QoS 1",
         "broker.hivemq.com (público)", "testamento + keepalive 30 s"], CAMP, CAMP_B)
    box(cr, cx[2] + pad, y1, bw, bh, "Gravador (DP-7)",
        ["gravador.py no notebook", "sessão persistente, QoS 1", "client ID fixo",
         "→ dados/mensagens.csv", "flush a cada linha"], CAMP, CAMP_B)
    box(cr, cx[3] + pad, y1, bw, bh, "Leitura dos dados",
        ["terminal do gravador", "MQTT Explorer (ao vivo)", "qualidade.py: completude",
         "grafico.py: série com lacunas", "nenhum aviso automático"], CAMP, CAMP_B)
    for i in range(3):
        arrow(cr, cx[i] + pad + bw, y1 + bh / 2, cx[i + 1] + pad, y1 + bh / 2, CAMP_B)

    # faixa do produto
    y2 = y1 + bh + 46
    text_l(cr, x0 + 6, y2 - 6, "Produto (proposta da equipe; ainda não integrado)", 9.5, True, PROD_B)
    box(cr, cx[0] + pad, y2, bw, bh, "Nó de borda",
        ["ESP32 + DHT22, um por sala", "mediana + EMA (DP-3)", "alarme local (DP-5)",
         "deep sleep, 600 s (DP-4)", "bateria 18650"], PROD, PROD_B, dash=[4, 3])
    box(cr, cx[1] + pad, y2, bw, bh, "LoRa → nó central",
        ["915 MHz; módulo a caminho", "nó central na tomada (DP-6 c)", "WiFi do roteador",
         "HTTPS, envio em lote", "chave de API própria"], PROD, PROD_B, dash=[4, 3])
    box(cr, cx[2] + pad, y2, bw, bh, "Servidor central",
        ["API FastAPI", "PostgreSQL", "leituras e alertas", "imutáveis (gatilhos)",
         "1213 leituras da campanha (CT-16)"], PROD, PROD_B, dash=[4, 3])
    box(cr, cx[3] + pad, y2, bw, bh, "Painel web",
        ["um card por sala", "situação atual", "histórico para auditoria",
         "(relatórios: fora do ciclo)", ""], PROD, PROD_B, dash=[4, 3])
    for i in range(3):
        arrow(cr, cx[i] + pad + bw, y2 + bh / 2, cx[i + 1] + pad, y2 + bh / 2, PROD_B, dash=[4, 3])

    # nota de borda
    text_l(cr, x0, H - 14,
           "Decisão de alarme na borda nas duas versões; contínua = rodou na campanha; tracejada = proposta para o produto.",
           8.2, False, MUTED)


# ---------------------------------------------------------------------------
def caminho(cr, W, H):
    cr.set_source_rgb(1, 1, 1)
    cr.paint()
    bw, bh = 112, 58
    y = 40
    xs = [14 + i * 182 for i in range(5)]
    names = [("DHT22", ["sensor, 1 fio"]),
             ("Nó ESP32", ["eq01-no02, USB"]),
             ("Roteador", ["WiFi 2,4 GHz", "mesmo cômodo"]),
             ("Broker", ["broker.hivemq.com", "público, sem login"]),
             ("gravador.py", ["notebook, tomada", "sessão persistente"])]
    for x, (t, ls) in zip(xs, names):
        rbox(cr, x, y, bw, bh, CAMP, CAMP_B)
        text_c(cr, x + bw / 2, y + 20, t, 10.5, True)
        for i, s in enumerate(ls):
            text_c(cr, x + bw / 2, y + 35 + i * 11.5, s, 8.2, False, MUTED)
    labels = [("GPIO4", "a cada 5 s"), ("WiFi", "até −29 dBm"),
              ("MQTT/TLS 8883", "QoS 1"), ("MQTT/TLS 8883", "QoS 1")]
    for i, (a, b) in enumerate(labels):
        x1, x2 = xs[i] + bw, xs[i + 1]
        arrow(cr, x1 + 2, y + bh / 2, x2 - 2, y + bh / 2)
        label(cr, (x1 + x2) / 2, y + bh / 2 - 8, a, 7.6, INK, True)
        label(cr, (x1 + x2) / 2, y + bh / 2 + 13, b, 7.2)

    # do gravador para baixo: CSV e qualidade
    y2 = y + bh + 52
    gx = xs[4]
    rbox(cr, gx, y2, bw, bh, (1, 1, 1), CAMP_B)
    text_c(cr, gx + bw / 2, y2 + 20, "mensagens.csv", 10, True)
    text_c(cr, gx + bw / 2, y2 + 35, "dados/campanha_24-10-26", 7.6, False, MUTED)
    text_c(cr, gx + bw / 2, y2 + 47, "bruto, 1 linha/mensagem", 7.6, False, MUTED)
    arrow(cr, gx + bw / 2, y + bh + 2, gx + bw / 2, y2 - 2)
    label(cr, gx + bw / 2 + 38, y + bh + 30, "flush por linha", 7.4)

    qx = xs[3]
    rbox(cr, qx, y2, bw, bh, (1, 1, 1), CAMP_B)
    text_c(cr, qx + bw / 2, y2 + 20, "qualidade.py", 10, True)
    text_c(cr, qx + bw / 2, y2 + 35, "completude, lacunas", 7.6, False, MUTED)
    text_c(cr, qx + bw / 2, y2 + 47, "grafico.py: série", 7.6, False, MUTED)
    arrow(cr, gx - 2, y2 + bh / 2, qx + bw + 2, y2 + bh / 2)

    # retornos: status e config
    bx = xs[3] + bw / 2
    nx = xs[1] + bw / 2
    yb = y - 14
    cr.set_source_rgb(*MUTED)
    cr.set_line_width(1.0)
    cr.set_dash([3, 3])
    cr.move_to(bx, y - 2)
    cr.line_to(bx, yb)
    cr.line_to(nx, yb)
    cr.stroke()
    cr.set_dash([])
    arrow(cr, nx, yb, nx, y - 2, MUTED, 1.0, 6)
    label(cr, (bx + nx) / 2, yb - 5, ".../config (retida) → nó;  nó → .../status: online, ou offline pelo testamento após ≈ 45 s",
          7.4, MUTED)

    # legenda inferior
    text_l(cr, 14, H - 30, "O broker transporta; quem guarda é quem assina: sem fila no nó, o que ele mede sem broker se perde;",
           8.2, False, MUTED)
    text_l(cr, 14, H - 17, "com o gravador parado, o broker guarda a sessão (QoS 1) e entrega na volta.",
           8.2, False, MUTED)


def render(fn, W, H, out):
    for ext in ("pdf", "png"):
        path = f"{out}.{ext}"
        if ext == "pdf":
            surf = cairo.PDFSurface(path, W, H)
            cr = cairo.Context(surf)
        else:
            s = 2.5
            surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, int(W * s), int(H * s))
            cr = cairo.Context(surf)
            cr.scale(s, s)
        fn(cr, W, H)
        surf.finish() if ext == "pdf" else surf.write_to_png(path)


if __name__ == "__main__":
    pdf_dir, png_dir = sys.argv[1], sys.argv[2]
    render(arquitetura, 760, 360, f"{pdf_dir}/arquitetura-ap2")
    render(caminho, 870, 230, f"{pdf_dir}/caminho-do-dado")
