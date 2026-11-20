// ApplyScriptApiNames.java: cria e nomeia as funções da API de script do motor.
//
// Para que serve
//   O executável de Ben 10: Protector of Earth (SLUS-21661) tem uma tabela de
//   pares {ponteiro para nome, ponteiro para função} que o motor usa para
//   ligar nomes de script ("ass_...") a funções nativas (findings/12).
//   Essas funções só são chamadas por ponteiro, então a análise automática
//   do Ghidra não acha muitas delas. Sem elas, o PS2Recomp gera chamadas não
//   resolvidas.
//
// O que faz
//   1. Procura no bloco de dados carregado a sequência mais longa de pares
//      (ponteiro para string que começa com "ass", ponteiro para bloco
//      executável). Exige pelo menos MIN_RUN pares, para não pegar lixo.
//   2. Para cada par: se não houver função no alvo, desmonta e cria uma.
//   3. Dá à função o nome da string (fonte USER_DEFINED só se o nome atual
//      for automático, tipo FUN_xxxxxxxx, para nunca apagar um nome dado à
//      mão).
//   4. Escreve um resumo no log: pares, funções criadas, renomeadas.
//
// Os nomes vêm do executável do próprio usuário: nada do jogo fica
// guardado neste script.
//
// @category Ben10Recomp

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class ApplyScriptApiNames extends GhidraScript {

    // Menos que isso não é a tabela de script (a tabela real tem ~1.600 pares).
    private static final int MIN_RUN = 100;

    @Override
    protected void run() throws Exception {
        Memory mem = currentProgram.getMemory();

        long bestStart = -1;
        int bestLen = 0;

        // Varre os blocos inicializados que não são executáveis (.data/.rodata).
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized() || blk.isExecute()) {
                continue;
            }
            long start = blk.getStart().getOffset();
            long end = blk.getEnd().getOffset();
            // Alinhado a 4 bytes: a tabela é de palavras de 32 bits.
            for (long a = (start + 3) & ~3L; a + 8 <= end; a += 4) {
                int len = 0;
                long p = a;
                while (p + 8 <= end && isPair(p)) {
                    len++;
                    p += 8;
                }
                if (len > bestLen) {
                    bestLen = len;
                    bestStart = a;
                }
                if (len > 0) {
                    a = p - 4; // pula o trecho já medido
                }
            }
        }

        if (bestLen < MIN_RUN) {
            printerr("Tabela da API de script nao encontrada (maior sequencia: " + bestLen + ")");
            return;
        }
        println(String.format("Tabela da API de script em 0x%08X, %d pares", bestStart, bestLen));

        int created = 0, renamed = 0, kept = 0, failed = 0;
        for (int i = 0; i < bestLen; i++) {
            Address pairAddr = toAddr(bestStart + 8L * i);
            Address nameAddr = toAddr(mem.getInt(pairAddr) & 0xFFFFFFFFL);
            Address target = toAddr(mem.getInt(pairAddr.add(4)) & 0xFFFFFFFFL);
            String name = readCString(nameAddr);

            Function f = getFunctionAt(target);
            if (f == null) {
                // Pode haver uma função que "engoliu" este endereço por engano;
                // não mexemos nela aqui, só registramos.
                Function containing = getFunctionContaining(target);
                if (containing != null) {
                    println(String.format("aviso: 0x%08X (%s) esta dentro de %s",
                            target.getOffset(), name, containing.getName()));
                }
                if (getInstructionAt(target) == null) {
                    new DisassembleCommand(target, null, true).applyTo(currentProgram, monitor);
                }
                f = createFunction(target, null);
                if (f == null) {
                    failed++;
                    printerr(String.format("falhou criar funcao em 0x%08X (%s)", target.getOffset(), name));
                    continue;
                }
                created++;
            }
            SourceType src = f.getSymbol().getSource();
            if (src == SourceType.DEFAULT || src == SourceType.ANALYSIS) {
                f.setName(name, SourceType.USER_DEFINED);
                renamed++;
            } else {
                kept++;
            }
        }
        println(String.format("criadas=%d renomeadas=%d mantidas=%d falhas=%d",
                created, renamed, kept, failed));
    }

    // Um par válido: +0 aponta para string que começa com "ass",
    // +4 aponta para um bloco executável.
    private boolean isPair(long a) {
        try {
            Memory mem = currentProgram.getMemory();
            long namePtr = mem.getInt(toAddr(a)) & 0xFFFFFFFFL;
            long codePtr = mem.getInt(toAddr(a + 4)) & 0xFFFFFFFFL;
            MemoryBlock cb = mem.getBlock(toAddr(codePtr));
            if (cb == null || !cb.isExecute() || (codePtr & 3) != 0) {
                return false;
            }
            byte[] b = new byte[3];
            mem.getBytes(toAddr(namePtr), b);
            return b[0] == 'a' && b[1] == 's' && b[2] == 's';
        } catch (Exception e) {
            return false;
        }
    }

    private String readCString(Address a) throws Exception {
        StringBuilder sb = new StringBuilder();
        Memory mem = currentProgram.getMemory();
        for (int i = 0; i < 256; i++) {
            byte c = mem.getByte(a.add(i));
            if (c == 0) {
                break;
            }
            sb.append((char) c);
        }
        return sb.toString();
    }
}
