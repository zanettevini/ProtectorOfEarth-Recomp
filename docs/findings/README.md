# Findings: o que se sabe até agora

Notas de engenharia reversa sobre o jogo e o port, um arquivo por
investigação, na ordem em que aconteceram. Como quase todo o projeto,
foram escritas com um assistente de IA; cada conclusão foi conferida com
ferramentas (readelf, Ghidra, o próprio recompilador, testes), e os
caminhos errados ficam no texto porque fazem parte de como a resposta foi
achada. Correções são bem-vindas.

Os findings 01-13 foram escritos na fase de estudo, antes do repositório:
caminhos como `Estudo\ISO\extraido\` equivalem hoje a `game\` e
`out\disco\` (ver docs/01-building.md).

Cada arquivo é uma investigação. Procure aqui antes de investigar algo de
novo. Formato de cada finding: ver [AGENTS.md](../../AGENTS.md) s.3.

| # | Assunto | Situação |
|---|---|---|
| [01](01-ps2recomp.md) | PS2Recomp: o que faz, o que é stub, licença, encaixe com o master prompt | estudo (sem código rodado) |
| [02](02-projetos-de-referencia.md) | Ports PS2 já feitos com PS2Recomp (Gitaroo Man, DBZ BT2, DBZ BT3) e o que copiar deles | estudo |
| [03](03-o-jogo.md) | O jogo: versões, seriais, motor Quantum3, `game.dir`, opções de debug | estudo + disco confirmado |
| [04](04-hardware-ps2.md) | Mapa do hardware do PS2 e o que cada parte exige do runtime | estudo |
| [05](05-renderizador-vulkan.md) | Opções de renderizador Vulkan para o GS (paraLLEl-GS e alternativas) | estudo |
| [06](06-conflitos-com-o-master-prompt.md) | Onde o master prompt conflita com a prática dos outros ports | decidido (temporário) |
| [07](07-ambiente-fase0.md) | Fase 0: ambiente de desenvolvimento e o que falta instalar | medido (falta JDK) |
| [08](08-ferramentas.md) | Ferramentas: Ghidra + extensão EE, PCSX2 como referência, extração do ISO | estudo |
| [09](09-executavel.md) | O ELF `SLUS_216.61`: layout, seções, distribuição de instruções, hardware, strings, API `ass_*` | medido |
| [10](10-modulos-iop.md) | Os 15 IRX do disco, os 8 que o jogo carrega, `IOPRP300.IMG` | medido |
| [11](11-microprogramas-vu.md) | Microprogramas de VU no `.vutext` e a tabela `.DVP.ovlytab` | medido |
| [12](12-analise-ghidra.md) | Ghidra headless: 4.420 funções, tabela da API de script (1.586), região de dados de 20 KB | medido |
| [13](13-primeira-recompilacao.md) | Primeira recompilação com o PS2Recomp: 0 instruções não suportadas, C++ compila (amostra) | medido |
| [14](14-repositorio-build-e-testes.md) | Repositório, build completo (liga), testes por instrução; 4 erros do tradutor corrigidos | medido |
