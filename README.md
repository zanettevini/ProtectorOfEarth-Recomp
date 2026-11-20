<div align="center">

# Protector of Earth Recomp

**Um port nativo não oficial de *Ben 10: Protector of Earth* para PC**<br>
<sub>PlayStation 2 · 2007 · High Voltage Software: recompilado estaticamente, depois reconstruído peça por peça com código nativo</sub>

<br>

![Plataformas](https://img.shields.io/badge/plataformas-Windows%20%7C%20Linux-blue)
![Situação](https://img.shields.io/badge/situa%C3%A7%C3%A3o-em%20desenvolvimento-red)
![Licença](https://img.shields.io/badge/licen%C3%A7a-GPL--3.0-green)
![Feito com PS2Recomp](https://img.shields.io/badge/feito%20com-PS2Recomp-8A2BE2)

**[Sobre](#-sobre)** · **[Rumo ao nativo](#-rumo-ao-nativo)** · **[Compilar](#%EF%B8%8F-compilar)** · **[Documentação](#-documenta%C3%A7%C3%A3o)** · **[Contribuir](#-contribuir)**

</div>

<br>

> [!WARNING]
> **Ainda não dá para jogar.** O código do jogo já é traduzido para C++ e compila inteiro, mas o programa ainda não foi executado. Veja os marcos abaixo.

> [!IMPORTANT]
> **Nenhum arquivo do jogo está incluído.** É preciso o seu próprio disco de *Ben 10: Protector of Earth* (PS2, **EUA**, SLUS-21661) copiado para um `.iso`. O jogo é montado no seu computador a partir dele.

## 📖 Sobre

*Ben 10: Protector of Earth* saiu no PlayStation 2, PSP e Wii, mas nunca no PC. O **Protector of Earth Recomp** quer preservá-lo como um jogo de PC de verdade.

Não é um emulador: o programa do PS2 é [traduzido para C++ antes de rodar](docs/02-how-the-port-works.md) pelo [PS2Recomp](https://github.com/ran-j/PS2Recomp) e compilado como um programa nativo de Windows ou Linux. As partes que ainda fingem ser um PS2 (o processador de entrada e saída, as unidades vetoriais, o chip gráfico, o som) são trocadas, uma a uma, pelo código do próprio port.

O projeto segue a organização do [Mind over Recomp](https://github.com/Maxilure/MindOver-Recomp): tudo o que se descobre sobre o jogo vira uma nota em [docs/findings/](docs/findings/README.md).

## 🧩 Rumo ao nativo

O código do jogo já é traduzido para C++. O que ainda é emulado é o **PS2 em volta dele**: cada peça que o port responde com o próprio código é um pedaço a menos de PS2 de mentira.

<!-- native-progress:begin -->
<!-- Escrito por tools/native_progress.py --readme: edite a ferramenta, não este bloco. -->

| Marco | | Situação |
|---|---|---|
| Análise do executável | ✅ | 4.420 funções; tabela da API de script (findings/09-12) |
| Tradução para C++ | ✅ | 0 instruções não suportadas (findings/13) |
| Build completo | ✅ | o C++ gerado + runtime compilam e ligam no Linux (findings/14); Windows: não testado |
| Testes por instrução | 🔧 | 33 testes de ponta a ponta; 4 erros do tradutor achados e corrigidos (findings/14) |
| Boot | ⏳ | o programa inicia e roda o código do jogo |
| Primeiro frame | ⏳ | a primeira imagem do jogo na tela |
| Menu principal | ⏳ |  |
| Fase jogável | ⏳ |  |

**Peças do PS2 substituídas por código do port: 0 de 25 (0%)** `▱▱▱▱▱▱▱▱▱▱`

| Peça do PS2 | Substituído | Vira |
|---|---|---|
| Módulos do IOP | `▱▱▱▱▱▱▱▱▱▱` 0 / 8 | serviços do port respondendo cada pedido (RPC) do jogo, sem rodar os IRX |
| Microprogramas de VU | `▱▱▱▱▱▱▱▱▱▱` 0 / 9 | C++ traduzido no build a partir da tabela .DVP.ovlytab (findings/11) |
| Gráficos (GS) | `▱▱▱▱▱▱▱▱▱▱` 0 / 4 | o renderizador Vulkan do port |
| Som | `▱▱▱▱▱▱▱▱▱▱` 0 / 1 | cada quadro de áudio direto para a saída do PC |
| Controles | `▱▱▱▱▱▱▱▱▱▱` 0 / 1 | SDL3: teclado, mouse e qualquer controle |
| Saves | `▱▱▱▱▱▱▱▱▱▱` 0 / 1 | arquivos de save no PC, sem memory card |
| Vídeos | `▱▱▱▱▱▱▱▱▱▱` 0 / 1 | decodificador de vídeo do PC (se o jogo tiver vídeos MPEG) |

Não contado: 42 syscalls do kernel do EE (threads, semáforos, interrupções), já traduzidos 1:1 pelo runtime.
<!-- native-progress:end -->

## 🛠️ Compilar

O passo a passo completo, Windows e Linux, está em **[Compilar](docs/01-building.md)**. É preciso CMake 3.21+, Ninja, Python 3, Git, um compilador C++20 (Visual Studio 2022 ou GCC 13+/Clang) e, para a análise do executável, Ghidra 12.1 com JDK 21.

<details>
<summary><b>Comandos do build</b></summary>
<br>

```bash
git clone --recursive <este repositório>
cd ProtectorOfEarth-Recomp

# 1. o executável do SEU disco em game/ (Windows: tools/coletar_disco.ps1)
# 2. a lista de funções, pelo Ghidra (headless)
tools/ghidra_analisar.sh            # Windows: tools/ghidra_analisar.ps1

# 3. configurar (aplica os patches de patches/ps2recomp sozinho), traduzir, compilar
cmake --preset linux-debug
cmake --build --preset linux-debug --target ben10_codegen
cmake --preset linux-debug          # de novo: agora acha o código gerado
cmake --build --preset linux-debug --target ben10_poe

# testes (não precisam do jogo)
cmake --preset linux-tests && cmake --build --preset linux-tests
ctest --preset linux-tests
```

</details>

<details>
<summary><b>Organização do repositório</b></summary>
<br>

```
ben10_poe.toml           config do recompilador: cada ajuste, comentado (por que + como achamos)
CMakeLists.txt           monta o port (código gerado + runtime do PS2Recomp + src/)
src/                     o código nativo do próprio port (vazio por enquanto)
tests/                   testes automatizados (GoogleTest): instruções do EE de ponta a ponta
tools/                   coleta do disco, análise do executável, Ghidra, painel de progresso
patches/ps2recomp/       nossas correções do PS2Recomp (aplicadas no submódulo pelo CMake)
docs/                    guias, situação do projeto e as findings sobre o jogo
thirdparty/PS2Recomp     o recompilador + runtime (submódulo git, fixado)

# feito na sua máquina, nunca versionado:
*.iso, game/             o seu disco e os arquivos tirados dele
generated/               o C++ gerado e a análise do Ghidra (é o código do jogo)
out/, logs/, user/       builds, logs, os SEUS saves e configurações
```

</details>

## 📚 Documentação

| | |
|---|---|
| 🧠 [Como o port funciona](docs/02-how-the-port-works.md) | recompilação estática, explicada |
| 🔧 [Compilar](docs/01-building.md) | cada passo do build, Windows e Linux |
| 📍 [Situação do projeto](docs/PROJECT_STATUS.md) | o que está feito, testado e não testado |
| ➡️ [Próxima tarefa](docs/NEXT_TASK.md) | o próximo passo concreto |
| 🗺️ [Roteiro](docs/DEVELOPMENT_ROADMAP.md) | as fases do projeto |
| ⚖️ [Decisões](docs/DECISIONS.md) e [Problemas conhecidos](docs/KNOWN_ISSUES.md) | o que foi escolhido e por quê; o que falta |
| 🔬 [Findings](docs/findings/README.md) | tudo o que se aprendeu sobre o jogo e o PS2 |

## 🤝 Contribuir

Contribuições de qualquer tipo são bem-vindas: código, engenharia reversa, testes, relatórios de bug, correções. Antes de um pull request, leia o **[CONTRIBUTING.md](CONTRIBUTING.md)**. Usando um agente de IA? Aponte-o para o **[AGENTS.md](AGENTS.md)**: as regras do projeto para agentes (nenhum dado do jogo, findings para tudo o que se aprende, comentários fartos, como testar).

> [!NOTE]
> **Desenvolvido com muita ajuda de IA** (Claude, pelo Claude Code). O código, as notas de engenharia reversa e a documentação foram escritos em grande parte pela IA; o mantenedor define a direção e revisa os resultados. As findings são notas de trabalho, conferidas com ferramentas e testes, não por especialistas: trate-as como tal e avise de erros.

## 💛 Créditos

- **[PS2Recomp](https://github.com/ran-j/PS2Recomp)** (ran-j e colaboradores): o recompilador e o runtime sobre os quais o port é feito
- **[PCSX2](https://github.com/PCSX2/pcsx2)**: a referência de comportamento do PS2 usada nos testes
- **[Mind over Recomp](https://github.com/Maxilure/MindOver-Recomp)** (Maxilure): o modelo de organização, de regras para agentes e deste painel
- **[Ghidra](https://github.com/NationalSecurityAgency/ghidra)** e **[ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)**: a análise do executável
- **Claude** (Anthropic): assistente de IA que escreveu a maior parte do código e da documentação

A lista completa está em [CREDITS.md](CREDITS.md).

## ⚖️ Licença e aviso

- O código deste repositório é licenciado sob a **[GNU General Public License v3.0](LICENSE)**, a mesma do PS2Recomp, ao qual ele é ligado.
- Os patches em `patches/ps2recomp/` modificam o código do PS2Recomp e ficam sob a licença dele (GPL-3.0).

<sub>Este é um projeto de fãs, não oficial, para preservação de jogos. Não é afiliado, endossado ou patrocinado pela D3 Publisher, High Voltage Software, Cartoon Network ou Sony. *Ben 10* é marca registrada dos seus donos. Este repositório não contém código, arquivos ou dados do jogo: é preciso a sua própria cópia legítima.</sub>
