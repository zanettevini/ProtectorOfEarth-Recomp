# 02: ports de PS2 já feitos e o que aprender com eles

## 1. A pergunta

Alguém já levou o PS2Recomp até um jogo jogável? Que arquitetura deu certo?

## 2. Como foi estudado

Leitura dos READMEs públicos. Nenhum foi compilado. Os números abaixo são o
que cada projeto afirma, não medições nossas.

## 3. Gitaroo Man Native (SLUS-20294)

[erinxocon/gitaroo-man-native](https://github.com/erinxocon/gitaroo-man-native)

- Fork do PS2Recomp como submódulo (`external/PS2Recomp`, branch própria).
- Funções achadas pelo Ghidra; nomes em `config/symbols/` (`sdk.txt`,
  `manual.txt`, `agent.txt`); config em `config/gitaroo.toml`.
- **VU1**: o renderizador de malhas é C++ nativo; o resto do microprograma é
  recompilado para C++ por uma ferramenta própria (`tools/vu1lift`), baseada
  no `vurecomp` do port de Ace Combat 5, usando um *census* em tempo de
  execução e capturas do PCSX2. Afirma resultado idêntico bit a bit ao
  interpretador em mais de 10 milhões de execuções.
- **GS**: OpenGL, mais um rasterizador em CPU como referência; compara todo
  frame GPU contra CPU.
- **IOP emulado**; kernel do EE em HLE.
- **Código EE gerado em tempo de execução cai num interpretador de EE**
  (pode ser desligado). Isso é proibido pelo nosso master prompt.
- Estado: chega ao tutorial e à primeira fase, 60 fps, áudio, widescreen
  opcional.
- Ferramentas úteis: entrada roteirizada (`PS2X_INPUT_SCRIPT`), dump de
  frames, traces por subsistema (`PS2X_TRACE_GS`, `_VIF`, `_VU1`, `_RPC`,
  `_PAD`, `_SPU2`...), cliente PINE para o PCSX2.
- Só Linux testado.

## 4. BT2 Recomp, Dragon Ball Z Budokai Tenkaichi 2 (SLUS-21441)

[oric212/BT2-Recomp](https://github.com/oric212/BT2-Recomp)

- **Windows x64, C++20, MSVC + CMake**: a mesma combinação que queremos.
- Runtime próprio: `runtime/ee/` (memória, kernel, threads, timers, DMA, IPU,
  VU1), `runtime/services/`, `runtime/gs/` (GS, GIF, VIF1), `runtime/host/`.
- **IOP não é emulado**: uma camada de serviços nativos responde aos pedidos
  que o jogo faz ao IOP (disco, arquivos, controle, memory card, som,
  middleware CRI). **Nenhum código de IOP roda.** É o modelo compatível com
  o master prompt.
- **Fronteiras checadas**: comportamento não suportado para a execução em vez
  de ser adivinhado.
- Afirma 11.139 funções do EE mais 494 de um overlay, todas compiladas
  nativamente, nada interpretado.
- **GS em Vulkan via paraLLEl-GS** (LGPL-3.0). Ver [05](05-renderizador-vulkan.md).
- Verificação contra PCSX2: estados de inicialização iguais às capturas,
  lógica/desenho/IA idênticos em batalha quadro a quadro, saves idênticos
  byte a byte.
- Docs: `docs/guides/`, `docs/PROG.md`, `docs/progress/`, `docs/project/`
  (plano e notas legais), `docs/reference/`.
- Marca trabalho feito por IA como não verificado até passar nos testes.

## 5. BT3-Recomp (SLUS-21678)

[z3xox/BT3-Recomp](https://github.com/z3xox/BT3-Recomp)

- Também sobre PS2Recomp; Windows, Linux e macOS (arm64).
- Renderizador OpenGL por padrão, paraLLEl-GS experimental.
- **Aviso relevante**: no Windows o paraLLEl-GS roda sobre o *lavapipe* (Vulkan
  por software) porque drivers Vulkan de alguns fabricantes, em especial o
  driver proprietário da AMD em GPUs Polaris/GCN, travam compilando shaders.
  Ver [KNOWN_ISSUES](../KNOWN_ISSUES.md).
- Requer ~16 GB de RAM e ~10 GB de disco para o build.

## 6. O que adotar

| De onde | O quê |
|---|---|
| BT2 | IOP por serviços nativos HLE, sem rodar IRX; Windows/MSVC; paraLLEl-GS; "parar em vez de adivinhar" |
| Gitaroo | estrutura `config/` + `config/symbols/`; lifter de VU1 por census + capturas; variáveis de trace por subsistema; entrada roteirizada |
| Gitaroo / BT2 | verificar cada substituição contra uma referência (PCSX2, ou o código recompilado original) |
| MoR | AGENTS.md, findings numerados, comentários pesados, regras legais |

## 7. O que não adotar

- O interpretador de EE para código gerado em tempo de execução (Gitaroo).
- O IOP emulado (Gitaroo, e o `ps2xIOP` padrão).
- Interpretador de VU1 no executável final. Ver [06](06-conflitos-com-o-master-prompt.md).

## 8. Em aberto

- Ler o código do runtime do BT2 (`runtime/services/`) para ver quantos
  serviços de IOP um jogo real precisa.
- Ler o `vurecomp` do port de Ace Combat 5 (link citado pelo Gitaroo Man
  Native: `sal063/Ace-Combat-5-Static-recompilation`).
- Conferir se o Ben 10 usa middleware que o BT2 já trata (CRI, por exemplo).
  Depende do disco: [03](03-o-jogo.md).
