# Decisões de arquitetura

Cada decisão: o que, por quê, alternativas, e situação
(**aceita** / **proposta** / **rejeitada**).

## D1. Base do recompilador: fork do PS2Recomp

- **Situação**: proposta (forte).
- **Por quê**: cobre ELF, decodificação R5900, MMI, VU0 macro, geração de
  C++, syscalls e overrides; já levou três jogos a estado jogável ou quase.
- **Alternativa**: recompilador próprio do zero. Rejeitada por custo.
- **Consequência**: o projeto fica GPL-3.0.
- Ver [findings/01](findings/01-ps2recomp.md).

## D2. IOP: `ps2xIOP` agora, serviços nativos depois

- **Situação**: aceita como **temporária**. O dono pediu "o mais fácil por
  enquanto".
- **Agora**: usar o `ps2xIOP` do PS2Recomp como ele vem (executa os IRX do
  disco e o `IOPRP300.IMG`). É o caminho que leva mais rápido ao boot.
- **Depois (antes da Fase 8)**: trocar por serviços nativos, como manda o
  master prompt s.3. A troca é pequena: só 8 módulos são carregados, 5
  deles padrão da Sony ([findings/10](findings/10-modulos-iop.md)).
- **Dívida registrada**: T1 em [KNOWN_ISSUES](KNOWN_ISSUES.md).
- Ver [findings/06](findings/06-conflitos-com-o-master-prompt.md) s.2.

## D3. Interpretadores

- **Situação**: aceita como **temporária** ("o mais fácil por enquanto").
- **EE**: não precisa de interpretador. Todo o código do EE é estático
  ([findings/09](findings/09-executavel.md) s.8); chamada não resolvida
  para a execução com erro.
- **VU**: para o primeiro frame, usar a implementação de VU0/VU1 que já
  existe no runtime do PS2Recomp, por interpretação
  ([findings/13](findings/13-primeira-recompilacao.md) s.5). A tradução estática vem
  depois, e já tem a lista fechada de microprogramas
  ([findings/11](findings/11-microprogramas-vu.md)). Dívida T2.
- **Regra que não muda**: todo uso de interpretador escreve um aviso no log
  na inicialização, para nunca passar despercebido (master prompt: "não
  cair em interpretação em silêncio").
- Ver [findings/06](findings/06-conflitos-com-o-master-prompt.md) s.4-5.

## D4. GS em Vulkan via paraLLEl-GS

- **Situação**: proposta.
- **Por quê**: Vulkan, LGPL-3.0, já integrado num port Windows.
- **Risco**: drivers AMD. Ver [KNOWN_ISSUES](KNOWN_ISSUES.md).
- Ver [findings/05](findings/05-renderizador-vulkan.md).

## D5. Versão alvo: SLUS-21661 (NTSC-U), revisão 1.01

- **Situação**: aceita (é o disco disponível; confirmado pelo `SYSTEM.CNF`).
- Ver [findings/03](findings/03-o-jogo.md).

## D6. Plataforma inicial: Windows x64, MSVC, CMake, SDL3, GoogleTest

- **Situação**: aceita (master prompt s.5).
- Linux depois; nada só-Windows fora de `platform/windows/`.

## D8. Fixar o PS2Recomp no commit `e563698`

- **Situação**: aceita.
- **Por quê**: o projeto evolui rápido; os resultados de
  [findings/13](findings/13-primeira-recompilacao.md) valem para esse commit.
  Atualizar é uma tarefa própria, com nova recompilação e comparação.

## D9. Organização do repositório como a do Mind over Recomp

- **Situação**: aceita (pedido do dono do projeto).
- **O quê**: `ben10_poe.toml` (config do recompilador, comentada),
  `src/` (código do port), `generated/` (local), `patches/ps2recomp/`,
  `tools/`, `docs/findings/`, `tools/native_progress.py` + painel no README.
- **Diferença do master prompt s.5**: a estrutura sugerida lá
  (`recomp/`, `runtime/ee/`...) supõe um recompilador próprio. Com o
  PS2Recomp como submódulo, recompilador e runtime ficam em
  `thirdparty/PS2Recomp`, e o que é nosso vai para `src/` (por assunto,
  quando existir) e `patches/`. Os arquivos de acompanhamento exigidos pelo
  master prompt s.9 continuam em `docs/`.

## D10. Patches do PS2Recomp aplicados pelo CMake

- **Situação**: aceita.
- **Por quê**: o submódulo fica no commit fixado (D8); as correções ficam
  em `patches/ps2recomp/NNNN-*.patch`, cada uma com cabeçalho; o configure
  aplica em ordem só as que faltam ([findings/14](findings/14-repositorio-build-e-testes.md) s.2).
  Quem compila não aplica nada à mão.

## D7. Documentação no formato do Mind over Recomp, em pt-BR

- **Situação**: aceita (pedido do dono).
- Código, identificadores e logs em inglês.
