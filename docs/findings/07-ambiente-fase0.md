# 07: Fase 0, ambiente de desenvolvimento

## 1. A pergunta

O que existe na máquina de desenvolvimento e o que falta instalar?

## 2. Como foi verificado

A pasta do projeto foi listada: só existe a pasta `Estudo/`. **Nenhum
comando foi executado na máquina** nesta sessão (não havia terminal
disponível), então versões de compiladores e SDKs não foram verificadas.
Este finding é a lista do que conferir.

## 3. Situação

| Item | Situação |
|---|---|
| Sistema | Windows (pasta em `C:\Users\...`) |
| Arquivos do jogo | ISO americano presente (s.7) |
| BIOS | ausente (só necessária para o PCSX2 de referência, não para o port) |
| Repositório git | não existe ainda |
| Git | 2.56.0 (ok) |
| CMake | 4.4.4 (ok, acima do mínimo 3.21) |
| Ninja | 1.13.2 (ok) |
| Python | 3.12.10 (ok) |
| Vulkan SDK | 1.4.363.0 em `C:\VulkanSDK` (ok) |
| Java (JDK) | JDK 21 instalado pelo dono (não verificado com `java -version`) |
| MSVC (`cl`) | **instalado**: "Ferramentas de Build do Visual Studio 2022" com o componente C++ x64 (`vswhere`). Usar pelo "Developer PowerShell for VS 2022" |
| Espaço em disco | suficiente (o build pede ~10 GB) |
| SDL3, GoogleTest | virão pelo CMake (FetchContent), não precisam de instalação |
| Ghidra 12.1.4 + ghidra-emotionengine-reloaded 12.1.4 | em `Estudo\tools\ghidra_12.1.4_PUBLIC` (extensão ainda só como `.zip`; o `tools/ghidra_analisar.ps1` instala) |
| PCSX2 | não verificado |

## 4. O que é necessário

| Ferramenta | Versão mínima | Para quê |
|---|---|---|
| Visual Studio 2022 (MSVC, "Desenvolvimento para desktop com C++") | 17.x | compilador principal do PS2Recomp |
| CMake | 3.21+ (PS2Recomp pede 3.20) | build |
| Ninja | qualquer recente | builds rápidos |
| Git | qualquer recente | versão + submódulos |
| Python | 3.11+ | scripts de ferramenta |
| Vulkan SDK (LunarG) | 1.3+ | backend gráfico, validation layers |
| SDL3 | 3.2+ (pode vir pelo CMake) | janela, entrada, áudio |
| GoogleTest | pelo CMake (FetchContent) | testes |
| Ghidra | 12.1.x | análise do ELF |
| JDK | o exigido pela versão do Ghidra (o Gitaroo cita JDK 25 com Ghidra 12.1) | rodar o Ghidra |
| ghidra-emotionengine-reloaded | a release compatível com o Ghidra instalado | suporte a R5900/MMI/VU0 no Ghidra |
| PCSX2 (versão estável) | 2.x | referência de comportamento e capturas |
| 7-Zip | qualquer | extrair o ISO |

Memória: o BT3-Recomp pede ~16 GB de RAM e ~10 GB livres para o build;
arquivos gerados grandes consomem muita RAM ao compilar (o MoR recomenda
limitar jobs paralelos).

## 5. Comandos para o relatório de ambiente

Rodar no PowerShell e colar a saída em `NEXT_TASK.md` ou numa conversa:

```powershell
git --version
cmake --version
ninja --version
python --version
java -version
where cl
$env:VULKAN_SDK
Get-PSDrive C | Select-Object Used,Free
```

## 6. Em aberto

- Instalar um JDK compatível com a versão do Ghidra escolhida (o Ghidra 11
  em diante pede JDK 21 ou mais novo; conferir no README da versão baixada).
- Ghidra 12.1.x + ghidra-emotionengine-reloaded.

## 7. Disco recebido

- Arquivo: `Estudo\ISO\Ben 10 - Protector of Earth (USA).iso`,
  3.400.499.200 bytes.
- Ainda não lido: tem 3,4 GB e não pode ser copiado para o ambiente do
  agente de uma vez (limite de 400 MB por arquivo). O script
  `tools/coletar_disco.ps1` extrai só o necessário para a Fase 1 (SYSTEM.CNF,
  ELF de boot, IRX, manifesto, SHA-1) em `Estudo\ISO\extraido\`.
