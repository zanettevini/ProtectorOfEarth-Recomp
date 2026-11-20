# 05: renderizador Vulkan para o GS

## 1. A pergunta

Escrever um renderizador do GS do zero em Vulkan, ou reaproveitar um?

## 2. Opções encontradas

| Opção | O que é | Licença | Observação |
|---|---|---|---|
| **paraLLEl-GS** | emula o GS com compute shaders Vulkan, de Hans-Kristian Arntzen (Themaister) / Arntzen Software | LGPL-3.0 (segundo os créditos do BT2 Recomp) | já usado pelo BT2 Recomp; suporta supersampling até 16x; não busca exatidão bit a bit, porque nem o renderizador por software do PCSX2 é exato |
| Renderizador do PCSX2 (GSdx) | renderizador por GPU (Vulkan, D3D, GL) e por software | GPL-3.0 | muito testado, mas grande e acoplado ao emulador |
| Iris | emulador de PS2 com renderizador Vulkan próprio e SDL3 | a verificar | útil como leitura |
| Próprio | escrever do zero | nosso | o master prompt pede para "não assumir que traduzir comandos do GS direto em draw calls basta" |

## 3. Recomendação

Começar com o **paraLLEl-GS** como backend Vulkan:

- Cumpre o requisito de Vulkan.
- Licença LGPL-3.0 é compatível com um projeto GPL-3.0.
- O BT2 Recomp prova que funciona integrado a um port com PS2Recomp no
  Windows.
- Ele trata o GS como hardware (compute), então reproduz comportamentos
  difíceis (reinterpretação de memória, feedback) que um tradutor ingênuo
  para draw calls erra.

Melhorias de PC (resolução, widescreen) entram depois, atrás de opções.

## 4. Riscos

- O BT3-Recomp relata travamentos do paraLLEl-GS na compilação de shaders
  com o driver proprietário da AMD em GPUs Polaris/GCN no Windows. GPUs RDNA
  não são citadas. Testar cedo em GPUs AMD. Ver
  [KNOWN_ISSUES](../KNOWN_ISSUES.md).
- Desempenho: GS em compute é pesado; medir.

## 5. Em aberto

- Confirmar a licença no repositório oficial do paraLLEl-GS (a fonte
  consultada foi um espelho e os créditos do BT2).
- Ler a interface de integração (como o BT2 entrega pacotes do GIF a ele).
