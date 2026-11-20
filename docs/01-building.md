# Compilar

Do seu `.iso` até o executável `ben10_poe`, mais os testes. São quatro
etapas: tirar o executável do disco, analisá-lo no Ghidra, traduzir para C++
e compilar.

| Etapa | Linux | Windows |
|---|---|---|
| Tirar os arquivos do disco | `bsdtar`/`7z` (s.3) | `tools\coletar_disco.ps1` |
| Ghidra headless | `tools/ghidra_analisar.sh` (**testado**) | `tools\ghidra_analisar.ps1` (não testado) |
| Configurar, traduzir, compilar | presets `linux-*` (**testado**, GCC 13) | presets `windows-*` (não testado) |
| Testes | `ctest --preset linux-tests` (**testado**: 33/33) | `ctest --preset windows-tests` (não testado) |

## 1. O que instalar

| Ferramenta | Versão | Para quê |
|---|---|---|
| Git | qualquer recente | o repositório e o submódulo; o CMake usa para aplicar os patches |
| CMake | 3.21+ | o build |
| Ninja | qualquer recente | o gerador do build |
| Python | 3.10+ | ferramentas e geração dos testes |
| Compilador C++20 | Windows: Visual Studio 2022 (ou só as "Ferramentas de Build"), com "Desenvolvimento para desktop com C++"; Linux: GCC 13+ ou Clang 17+ | |
| JDK | 21 (Eclipse Temurin, por exemplo) | o Ghidra |
| Ghidra | 12.1.x | a lista de funções |
| ghidra-emotionengine-reloaded | a mesma versão do Ghidra | suporte ao processador do PS2 no Ghidra |

Linux (Ubuntu/Debian), bibliotecas que o runtime do PS2Recomp pede para a
janela (raylib):

```bash
sudo apt install build-essential cmake ninja-build git python3 unzip \
    libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxi-dev libasound2-dev libwayland-dev libxkbcommon-dev
```

Memória e disco: o C++ gerado tem ~160 MB em ~20 mil arquivos. O build
`-O0` usou pouca memória com 2 tarefas em paralelo; com mais tarefas,
acompanhe a RAM. Reserve ~10 GB livres.

## 2. Clonar

```bash
git clone --recursive <url do repositório> ProtectorOfEarth-Recomp
cd ProtectorOfEarth-Recomp
```

Já tem a pasta mas falta o submódulo (`thirdparty/PS2Recomp` vazio)?

```bash
git submodule update --init
```

Não aplique patches à mão: o configure do CMake aplica os de
`patches/ps2recomp/` sozinho (docs/DECISIONS.md D10).

## 3. Os arquivos do disco

Só a versão americana: **SLUS-21661** (`SYSTEM.CNF` com `VER = 1.01`). O
que importa é `game/SLUS_216.61`.

**Windows**: ponha o `.iso` numa pasta `iso\` na raiz do repositório e rode
num PowerShell, na raiz:

```powershell
powershell -ExecutionPolicy Bypass -File tools\coletar_disco.ps1
```

Ele monta o `.iso` só para leitura, copia o ELF e os IRX para `game\`, e
grava SHA-1, lista de arquivos e um relatório do ambiente em `out\disco\`.

**Linux**:

```bash
mkdir -p game
bsdtar -xf /caminho/jogo.iso -C game SYSTEM.CNF SLUS_216.61 MODULES
```

(ou `7z x /caminho/jogo.iso -ogame SYSTEM.CNF SLUS_216.61 MODULES`)

## 4. A lista de funções (Ghidra)

O recompilador precisa saber onde começa e termina cada função; o
executável não tem símbolos. O Ghidra descobre, e um script nosso dá nome às
1.586 funções da API de script do jogo (docs/findings/12).

Ponha o `.zip` da extensão ghidra-emotionengine-reloaded em
`<Ghidra>/Extensions/Ghidra/` (o script instala a partir dali).

**Linux**:

```bash
GHIDRA_DIR=/caminho/ghidra_12.1.4_PUBLIC tools/ghidra_analisar.sh
```

**Windows** (PowerShell, na raiz):

```powershell
powershell -ExecutionPolicy Bypass -File tools\ghidra_analisar.ps1 -GhidraDir "C:\caminho\ghidra_12.1.4_PUBLIC"
```

Leva alguns minutos e escreve `generated/ghidra/functions.csv`. O log deve
mostrar `criadas=845 renomeadas=1586 ... falhas=0` e `Exported 4420
functions`.

## 5. Traduzir e compilar

**Windows**: abra o **"x64 Native Tools Command Prompt for VS 2022"** (menu
Iniciar). Ele põe o compilador da Microsoft de 64 bits no PATH; num prompt
comum o `cl` não existe. Na raiz do repositório:

```bat
cmake --preset windows-debug
cmake --build --preset windows-debug --target ben10_codegen
cmake --preset windows-debug
cmake --build --preset windows-debug --target ben10_poe
```

**Linux**:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --target ben10_codegen
cmake --preset linux-debug
cmake --build --preset linux-debug --target ben10_poe
```

Por que configurar duas vezes: o executável é feito a partir dos arquivos de
`generated/default/`, que só existem depois do `ben10_codegen`. O segundo
configure os encontra (a mensagem diz `ben10_poe: 20235 arquivos gerados`).

O que esperar:

- configure: `PS2Recomp: aplicado 0001-...` até `0005-...` (só da primeira vez);
- `ben10_codegen`: o relatório termina com `Unhandled instructions: 0` e
  `errors: 0`;
- `ben10_poe`: no Linux, ~5 minutos com 2 núcleos; o executável fica em
  `out/build/<preset>/ben10_poe`.

**Ainda não dá para jogar**: o executável compila e liga, mas a primeira
execução não foi feita (docs/NEXT_TASK.md).

## 6. Testes

Não precisam do jogo.

```bash
cmake --preset linux-tests          # Windows: windows-tests
cmake --build --preset linux-tests
ctest --preset linux-tests
```

Resultado esperado: todos passam e 1 aparece como desligado
(`MulsDenormalOperandIsZero`, diferença conhecida: KNOWN_ISSUES R10).

## 7. Problemas conhecidos do build

| Mensagem | Causa | O que fazer |
|---|---|---|
| `include could not find requested file: .../ps2xRuntime/cmake/ReleaseMode.cmake` | o patch 0001 não foi aplicado | rode o configure de novo; se persistir, `git -C thirdparty/PS2Recomp status` e veja se o submódulo está no commit `e563698` |
| `Patch 0002-... nao aplicou` | o submódulo está noutro commit ou foi editado à mão | `git -C thirdparty/PS2Recomp checkout . && git submodule update` |
| `inlining failed ... target specific option mismatch` (GCC/Clang) | faltam as flags de SSE4 | o `CMakeLists.txt` já põe `-march=x86-64-v2`; confira se `BEN10_ARCH_FLAGS` não foi apagado do cache |
| `ben10_poe: generated/default/ vazio` | o codegen ainda não rodou | passo 5, `ben10_codegen`, depois configure de novo |
