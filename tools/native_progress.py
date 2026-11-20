#!/usr/bin/env python3
"""native_progress.py: o painel "Rumo ao nativo" e os marcos do README.

O QUE MEDE
    O código do jogo (o EE, a CPU principal) é traduzido para C++ antes de
    rodar: essa parte já é nativa por construção. O que ainda finge ser um
    PS2 é o resto do console em volta dele: o IOP e seus módulos, as
    unidades vetoriais (VU), o chip gráfico (GS), o som, o controle, o
    memory card. Ir para o nativo = o port responder cada peça com o próprio
    código de PC, sem emular o hardware.

    Cada PEÇA tem itens. Um item conta como FEITO só quando é código do port
    que responde (status "nativo"). Os outros status:
        "emulado"       o PS2Recomp imita o hardware (genérico, em C++)
        "interpretado"  roda por interpretador (dívida temporária: ver
                        docs/KNOWN_ISSUES.md T1/T2)
        "stub"          só finge responder
        "a medir"       ainda não sabemos como a primeira execução trata

    NÃO CONTA nas barras: o kernel do EE (42 syscalls como CreateThread,
    WaitSema...). O runtime já os traduz 1:1 para o PC (findings/12 s.3),
    como o "kernel basics" do Mind over Recomp.

    Mude o status aqui SÓ com evidência (um finding, um teste, um log), e
    rode --readme para atualizar o README.

USO
    python3 tools/native_progress.py            mostra a tabela
    python3 tools/native_progress.py --readme   reescreve o bloco entre
        <!-- native-progress:begin --> e <!-- native-progress:end --> no README.md

Inspirado em tools/native_progress.py do Mind over Recomp (GPL-3.0).
"""
import argparse
import sys
from pathlib import Path

# -----------------------------------------------------------------------------
# Marcos do caminho até jogar (estilo do BT2 Recomp)
# status: "feito" | "andamento" | "pendente"
# -----------------------------------------------------------------------------
MILESTONES = [
    ("Análise do executável", "feito",
     "4.420 funções; tabela da API de script (findings/09-12)"),
    ("Tradução para C++", "feito",
     "0 instruções não suportadas (findings/13)"),
    ("Build completo", "feito",
     "o C++ gerado + runtime compilam e ligam no Linux (findings/14); Windows: não testado"),
    ("Testes por instrução", "andamento",
     "33 testes de ponta a ponta; 4 erros do tradutor achados e corrigidos (findings/14)"),
    ("Boot", "pendente", "o programa inicia e roda o código do jogo"),
    ("Primeiro frame", "pendente", "a primeira imagem do jogo na tela"),
    ("Menu principal", "pendente", ""),
    ("Fase jogável", "pendente", ""),
]

# -----------------------------------------------------------------------------
# As peças do PS2: (título, o que vira, [(item, status)])
# -----------------------------------------------------------------------------
PIECES = [
    ("Módulos do IOP",
     "serviços do port respondendo cada pedido (RPC) do jogo, sem rodar os IRX",
     [("SIO2MAN (barramento de controle e memory card)", "a medir"),
      ("MTAPMAN (multitap)", "a medir"),
      ("PADMAN (controles)", "a medir"),
      ("MCMAN (memory card)", "a medir"),
      ("MCSERV (memory card, servidor)", "a medir"),
      ("LIBSD (chip de som)", "a medir"),
      ("CDARB (acesso ao disco, do motor)", "a medir"),
      ("ALASD (driver de áudio, do motor)", "a medir")]),
    ("Microprogramas de VU",
     "C++ traduzido no build a partir da tabela .DVP.ovlytab (findings/11)",
     [("programa A", "interpretado"), ("programa B", "interpretado"),
      ("programa C", "interpretado"), ("programa D (VU1)", "interpretado"),
      ("programa E", "interpretado"), ("programa F (VU1)", "interpretado"),
      ("programa G", "interpretado"), ("programa H (VU1)", "interpretado"),
      ("programa I (VU1)", "interpretado")]),
    ("Gráficos (GS)",
     "o renderizador Vulkan do port",
     [("saída de vídeo (modo, framebuffer na tela)", "emulado"),
      ("pacotes GIF para desenho", "emulado"),
      ("texturas e paletas", "emulado"),
      ("mistura, profundidade, recorte", "emulado")]),
    ("Som",
     "cada quadro de áudio direto para a saída do PC",
     [("mixagem e saída (SPU2)", "emulado")]),
    ("Controles",
     "SDL3: teclado, mouse e qualquer controle",
     [("leitura do controle", "emulado")]),
    ("Saves",
     "arquivos de save no PC, sem memory card",
     [("memory card", "emulado")]),
    ("Vídeos",
     "decodificador de vídeo do PC (se o jogo tiver vídeos MPEG)",
     [("IPU / MPEG", "a medir")]),
]

NOT_COUNTED = "42 syscalls do kernel do EE (threads, semáforos, interrupções), já traduzidos 1:1 pelo runtime"

BEGIN = "<!-- native-progress:begin -->"
END = "<!-- native-progress:end -->"
ICON = {"feito": "✅", "andamento": "🔧", "pendente": "⏳"}


def bar(done, total, width=10):
    filled = round(width * done / total) if total else 0
    return "▰" * filled + "▱" * (width - filled)


def render():
    lines = [BEGIN,
             "<!-- Escrito por tools/native_progress.py --readme: edite a ferramenta, não este bloco. -->",
             "",
             "| Marco | | Situação |",
             "|---|---|---|"]
    for name, status, note in MILESTONES:
        lines.append(f"| {name} | {ICON[status]} | {note} |")

    total = sum(len(items) for _, _, items in PIECES)
    done = sum(1 for _, _, items in PIECES for _, st in items if st == "nativo")
    pct = round(100 * done / total) if total else 0
    lines += ["",
              f"**Peças do PS2 substituídas por código do port: {done} de {total} ({pct}%)** `{bar(done, total)}`",
              "",
              "| Peça do PS2 | Substituído | Vira |",
              "|---|---|---|"]
    for title, becomes, items in PIECES:
        d = sum(1 for _, st in items if st == "nativo")
        lines.append(f"| {title} | `{bar(d, len(items))}` {d} / {len(items)} | {becomes} |")
    lines += ["",
              f"Não contado: {NOT_COUNTED}.",
              END]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="Painel de progresso do README")
    ap.add_argument("--readme", action="store_true", help="reescreve o bloco no README.md")
    a = ap.parse_args()
    block = render()
    if not a.readme:
        print(block)
        print()
        for title, _, items in PIECES:
            print(title)
            for name, st in items:
                print(f"    [{st}] {name}")
        return 0
    readme = Path(__file__).resolve().parent.parent / "README.md"
    text = readme.read_text(encoding="utf-8")
    if BEGIN not in text or END not in text:
        print("README.md sem os marcadores native-progress", file=sys.stderr)
        return 1
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    readme.write_text(head + block + tail, encoding="utf-8")
    print("README.md atualizado")
    return 0


if __name__ == "__main__":
    sys.exit(main())
