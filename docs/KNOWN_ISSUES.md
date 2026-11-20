# Problemas e riscos conhecidos

## Bloqueios

| # | Bloqueio | Bloqueia | Ver |
|---|---|---|---|
| B6 | Primeira execução não feita | Fases 3-7 | [NEXT_TASK](NEXT_TASK.md) |
| B7 | Build no Windows (MSVC) não testado | jogar no Windows | [findings/14](findings/14-repositorio-build-e-testes.md) s.3 |

## Dívidas temporárias (violam o master prompt de propósito, por enquanto)

| # | Dívida | Por quê | Como sair |
|---|---|---|---|
| T1 | IOP executado pelo `ps2xIOP` (interpretação de R3000A) | caminho mais rápido até o boot (D2) | serviços nativos para os 8 módulos ([findings/10](findings/10-modulos-iop.md)) |
| T2 | VU1/VU0 em modo micro podem rodar por interpretação | primeiro frame (D3) | tradução estática pela `.DVP.ovlytab` ([findings/11](findings/11-microprogramas-vu.md)) |

## Riscos técnicos

| # | Risco | Por que importa | Mitigação |
|---|---|---|---|
| R1 | VU1 só por interpretação no PS2Recomp | quase todo jogo de PS2 desenha por VU1 | tradução estática de microprogramas (abordagem do `vu1lift`/`vurecomp`), com census em tempo de execução para achar todos os programas |
| R2 | paraLLEl-GS trava em drivers AMD no Windows | relatado em GPUs AMD Polaris/GCN pelo BT3-Recomp; outras gerações da AMD não foram testadas | testar cedo; manter o renderizador atrás de uma interface para trocar |
| R3 | FPU do EE não é IEEE | física e colisão podem divergir sem clamp correto | testes de instrução comparados com o PCSX2 |
| R4 | Código carregado em tempo de execução | conflito com "sem interpretador" | medido: nenhum encontrado ([findings/09](findings/09-executavel.md) s.8); falta confirmar no Ghidra |
| R5 | Formato do `game.dir` (motor Quantum3) desconhecido | acesso a arquivos | o runtime lê pelos serviços de arquivo do jogo, sem precisar entender o formato no começo |
| R7 | Tabela de dados de 20 KB dentro do `.text` (`0x001002E0`) | traduzida como código, gera lixo | mitigado: fora de qualquer função, não entra na recompilação ([findings/12](findings/12-analise-ghidra.md) s.6) |
| R9 | 2.016 saltos indiretos não resolvidos estaticamente | um alvo ausente na tabela de despacho para o jogo | conferir em execução ([findings/13](findings/13-primeira-recompilacao.md) s.3) |
| R8 | Áudio por driver de terceiros (`ALASD`, "Argonaut") e `CDARB` sem documentação | serviços nativos de áudio exigem engenharia reversa do protocolo | começar pelas chamadas RPC do lado do EE |
| R10 | Operando denormal em ADD/SUB/MUL.S não vira zero (o runtime do PS2Recomp documenta) | resultados minúsculos onde o PS2 dá 0 | teste `DISABLED_MulsDenormalOperandIsZero` pronto; corrigir no runtime ([findings/14](findings/14-repositorio-build-e-testes.md) s.7) |
| R11 | LQC2/SQC2 não alinham o endereço (o PCSX2 também não) | 1.235 usos no jogo | decidir com evidência de hardware |
| R12 | SQRT.S de -0: PCSX2 dá -0, runtime dá +0 | raro | sem árbitro ainda |
| R13 | ~38 instruções específicas do R5900 usadas pelo jogo ainda sem teste | erros silenciosos como os 4 já achados | continuar os testes ([findings/14](findings/14-repositorio-build-e-testes.md) s.7) |
| R6 | PS2Recomp é experimental | bugs no recompilador viram bugs silenciosos no jogo | testes por instrução, logs de não resolvidos, fork próprio com patches documentados |
