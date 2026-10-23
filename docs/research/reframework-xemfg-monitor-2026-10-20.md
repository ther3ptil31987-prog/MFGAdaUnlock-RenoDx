# Requiem: REFramework e atividade de apresentação XeFG

20 de outubro de 2026. Adendo à investigação R1.11. Não houve alteração do binário MFG nesta comparação nem publicação de release.

## Resultado observado

O usuário instalou o fork onehoon do REFramework e relatou aproximadamente cinco minutos sem as travadas periódicas. Os logs preservados corroboram uma mudança concreta no comportamento dos hooks:

| Item | Antes | Depois |
|---|---|---|
| Commit identificado pelo log do REFramework | `d052e8fd3fd41d0a1a33394fcf6e6c19be39b1ee` | `59f21de41b9aebe51ff4c9100d275da987cdfa6f` |
| Intervalo do log, horário local | 19:12:54.811–19:17:19.383 | 19:25:22.289–19:30:16.826 |
| `Sending rehook request for D3D` | 22 | 0 |
| Mediana entre pedidos | 11,008 s | Não aplicável |
| Linhas `Unhooking D3D12` | 23, incluindo lifecycle | 0 |
| Addon MFG | R1.11, mesmo SHA-256 | R1.11, mesmo SHA-256 |

O arquivo `MFGAdaUnlock.ini` permaneceu byte a byte igual ao da captura instrumentada durante a rodada posterior. O novo processo, PID 8068, registrou 20 amostras de status com quatro saídas reportadas pelo SDK, execução Tensor e nenhuma recusa de inicialização ou resultado negativo do SDK no parser. Isso confirma que a ausência de rehook ocorreu com XeMFG ativo. Não equivale a medir todos os frames exibidos.

O DLL anterior tinha SHA-256 `70a2a3c24030d8bc3695554e7f929fd1b0b4b757fb5ba2c9d34f42d7155005ac`; o instalado pelo usuário tem `e07b8dafa2e10d5a91ab567f7ba617048fb3510050a71e4d8707cd3f9652148a`. O addon permaneceu `1db596cf140ede231b35026be4341832eead893cb2d1935108d7e5c533c13374` na pasta principal e em `_storage_`.

## Evidência no código do fork

O commit [f000aa75](https://github.com/onehoon/REFramework/commit/f000aa75e1c30009b0db26f8bb290cc477b11032) adiciona `note_present_activity()` ao caminho de Presents aninhados de uma fonte XeFG. Assim, esses Presents podem manter o monitor informado de atividade, em vez de deixá-lo interpretar um caminho aninhado como ausência de apresentação.

Outras correções específicas presentes no histórico consultado:

- [e4a1a1d1](https://github.com/onehoon/REFramework/commit/e4a1a1d1faf06f4e25923dacf765c00213c86ada): libera a trava do monitor durante chamadas downstream de criação de swapchain XeFG; controla transições concorrentes e posterga o rehook até a última saída.
- [79b29c07](https://github.com/onehoon/REFramework/commit/79b29c073eae08e818b5f220f01f25eb6382f086): encaminha callbacks tardios pela vtable restaurada quando a instância XeFG anterior já foi destacada.
- [4bf45b37](https://github.com/onehoon/REFramework/commit/4bf45b370e602f7f6a3ca54f308daa4e353aab8a): exporta um handoff versionado antes da retirada da última referência pública do proxy. Não foi adicionada chamada a esse export pelo nosso addon nesta etapa.

Os diffs foram obtidos da API pública do GitHub e preservados por SHA. Nenhum código externo foi copiado para o addon. A [release 14](https://github.com/onehoon/REFramework/releases/tag/fork-release-14-59f21de41b9aebe51ff4c9100d275da987cdfa6f) identifica o commit da instalação observada e descreve seu escopo Capcom.

## Interpretação e limites

**Confirmado:** havia um ciclo periódico de remoção/reinstalação de hooks na instalação anterior; ele não aparece nos quase cinco minutos do novo log, com o mesmo addon e XeMFG ativo. O código do fork contém correções diretamente relacionadas a esse mecanismo.

**Hipótese fortalecida:** incompatibilidade entre o monitor de hooks do REFramework anterior e a apresentação XeFG contribuía para a instabilidade relatada. A melhora visual é um relato do usuário, corroborado pela eliminação do ciclo de rehook, mas não é um A/B pareado de pacing exibido.

O rastro anterior de ETW mostrou uma cadeia de dependências do jogo e espera no driver NVIDIA durante trabalho de recursos. Isso localizou um atraso específico; não identificou sozinho seu disparador. O ciclo de rehook e cada pico de criação de recurso não foram ligados individualmente por identidade causal. Não concluir que toda alocação ou todo engasgo era causado pelo REFramework, nem generalizar para outros jogos ou forks.

A comparação posterior não incluiu nova captura PresentMon pareada. Não se atribui ganho de FPS, latência física ou um percentual de melhora de pacing. O conflito de boot com Steam e a inferência de TLOU2/Alan Wake 2 continuam verificações separadas.

## Preservação

Evidência local ignorada pelo Git: `out/xemfg-r1.11-evidence/reframework-onehoon/`. Contém os diffs por commit e, em `runtime-comparison/`, os logs anterior/posterior do REFramework, o log MFG, configurações, hashes, `comparison.json` e `native-summary.json`.

Depois de preservar os arquivos, os dois diagnósticos temporários da rodada anterior foram retirados pela restauração do INI de captura, com jogo fechado e verificação de hash. O REFramework escolhido pelo usuário e o addon R1.11 foram mantidos. Os pacotes selados anteriores não foram alterados.
