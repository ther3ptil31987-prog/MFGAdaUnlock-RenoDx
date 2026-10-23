# XeMFG R1.12: coordenação opcional com REFramework

20 de outubro de 2026. Candidata local derivada da R1.11, na branch `feature/1.4.5-xemfg-reframework-r1.12`. A release oficial 1.4.5, a árvore antiga e a instalação funcional do usuário são preservadas. Este trabalho não publica release.

## Problema e evidência

A comparação anterior está em [reframework-xemfg-monitor-2026-10-20.md](reframework-xemfg-monitor-2026-10-20.md): o REFramework anterior reinstalava os hooks aproximadamente a cada 11 segundos; o fork onehoon não fez isso durante quase cinco minutos com o mesmo addon XeMFG. O relato de melhora e a eliminação desse ciclo são evidências de compatibilidade, sem provar a causa de cada hitch.

Além do monitor, o fork define uma coordenação de retirada do proxy. Nosso wrapper soltava sua referência pública Intel antes de notificar qualquer outro componente, e `Output::RetirePresentation` mantinha as travas de Present/inputs durante a destruição. Isso deixava a aposentadoria sem o protocolo disponível e podia bloquear um callback que precisasse consultar o estado do addon em outra thread. A falha de coordenação é reproduzida pelo teste sintético contra a R1.11; não foi reproduzido um crash em jogo causado especificamente por esse ramo.

## Implementação própria

`native_reframework.cpp/.hpp` reconhece o ABI exportado [REFramework_XeFG_PreRetireSwapchainV1](https://github.com/onehoon/REFramework/commit/4bf45b370e602f7f6a3ca54f308daa4e353aab8a), sem copiar implementação do REFramework. Não há patches em seus campos privados, heartbeat fabricado, novo Present, alteração de waits ou mudança de configuração de overlays.

Descoberta:

- Enumera somente módulos já carregados, na criação e retirada da saída. Não carrega um REFramework por conta própria.
- Procura o export V1 em qualquer módulo. Para versões sem o export, reconhece DLLs com nome `dinput8.dll` ou prefixo `reframework`/`REFramework` e identidade de produto REFramework; a assinatura antiga conhecida também é reconhecida pelo hash. Um REFramework legado renomeado arbitrariamente e sem identidade reconhecível pode não ser detectado.
- Registra caminho, versão, SHA-256 do arquivo e capacidade. Hash de arquivo é proveniência/aviso, não autorização para escrever no binário nem prova de identidade de toda a imagem em memória.
- Retém uma referência ao módulo durante o uso da função e enquanto a saída estiver viva ou em quarentena, inclusive se uma nova enumeração falhar. Rejeita export encaminhado para outro módulo e múltiplos peers reconhecidos. Não fixa permanentemente DLLs de terceiros no processo.
- A UI lê um snapshot em cache. Não há enumeração, hashing ou consulta externa adicionada ao caminho de cada frame.

Retirada:

1. Aguarda as operações de Present/inputs já admitidas, marca a saída aposentada e avança seu epoch. Chamadas antigas de sleep não podem publicar frames nessa geração.
2. Solta as travas de Present/inputs e retira o controle da janela.
3. Descobre também REFramework carregado tardiamente e chama o ABI com o proxy Intel ainda referenciado, o contexto XeFG e o HWND correspondentes.
4. Somente os retornos documentados `0` (não rastreado) e `1` (destacado) autorizam seguir. A referência pública é liberada antes de `xefgSwapChainDestroy`.
5. Retorno `2`, código desconhecido, exceção ou descoberta ambígua preservam proxy/contexto/dependências. O addon bloqueia outra criação XeMFG até reiniciar o processo. Não repete automaticamente a negociação recusada.

Uma falha de retirada mantém recursos deliberadamente vivos para evitar ponteiros pendentes. O bloqueio global de novas saídas limita a acumulação; isso não é uma recuperação sem reinício. A serialização existente de criação/destruição do SDK continua ativa. A mudança não pretende reparar travas privadas de qualquer outro componente.

Versões sem V1 mantêm o caminho anterior. Ausência do export não é certificação de compatibilidade e não é, sozinha, motivo para recusar um REFramework desconhecido. O hash antigo observado recebe aviso específico; outras versões aparecem como integração legada não verificada. Diagnostics e o relatório copiado incluem capacidade, decisão e identidade, sem novos contadores por frame.

## Validação automatizada

Os fixtures são DLLs sintéticas próprias que implementam somente o contrato do export; não são cópias do REFramework. Os testes de integração usam o ReShade e o SDK Intel reais, com inputs Streamline sintéticos, em diretórios isolados.

| Teste novo | Contrato exercitado |
|---|---|
| `xemfg_reframework_cooperation` | Ausente/legado/V1; códigos 0/1/2/desconhecido; exceção; duplicidade; falha injetada de enumeração; referência do proxy; lease e unload do módulo; aviso restrito ao hash conhecido |
| `xemfg_native_ref_detached` | Retorno 1, inferência e três recriações no mesmo HWND |
| `xemfg_native_ref_not-tracked` | Retorno 0 com o mesmo ciclo de vida |
| `xemfg_native_ref_blocked` | Recusa 2 mantém proxy/contexto; nova criação recusada sem retry |
| `xemfg_native_ref_invalid` | Código não documentado não autoriza liberar recursos |
| `xemfg_native_ref_exception` | Exceção na chamada não autoriza liberar recursos |
| `xemfg_native_ref_late` | Peer carregado depois da criação é descoberto antes da retirada |
| `xemfg_native_ref_legacy` | Sem V1, inferência/resize/recriação continuam pelo caminho legado |
| `xemfg_native_ref_ambiguous` | Segundo peer carregado tardiamente bloqueia retirada, sem chamar o módulo errado |

O fixture V1 também chama `MFGAXeNativeRefresh` em outra thread durante o handoff: o callback deve adquirir a trava dos inputs, observar a saída aposentada e retornar. Falhas e códigos desconhecidos são verificados pelo estado real dos contextos e do runtime final; o log não é o único oráculo.

Os resultados finais da suíte, os recibos por execução e o controle negativo com a R1.11 acompanham o pacote. Não converter esses testes em alegação de compatibilidade de gameplay.

Resultado final: **124 casos, 121 passaram, três opcionais ignorados, zero falhas**, em 333,84 segundos reportados pelo CTest. Os opcionais foram `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`. Os nove casos novos passaram. No controle negativo, a R1.11 selada falhou com `blocked peer released the Intel context`; a R1.12 preservou o contexto e recusou recriação no mesmo cenário. Essa comparação valida o novo contrato de retirada, não uma correção visual ou de stalls em jogo.

Na primeira rodada, executada no sandbox restrito, os cenários com o Streamline real completaram inferência e retirada dos contextos, mas o shutdown do NGX não retornou em 60 segundos. O dump do processo de teste tem candidatos de pilha em `NvTelemetryAPI64`/`_nvngx`; o scan bruto não substitui um unwind simbólico. Sem alterar essas DLLs nem os serviços do sistema, o teste de quatro recriações e encerramento passou para a R1.11 selada e para a candidata quando executado fora do sandbox. A rodada final usa essa condição normal de acesso ao driver/IPC. Não se atribui esse resultado a uma correção de telemetria no addon, nem se identifica qual restrição causou a diferença.

## Limites e verificação em jogo

- A [correção do heartbeat do monitor](https://github.com/onehoon/REFramework/commit/f000aa75e1c30009b0db26f8bb290cc477b11032) continua pertencendo ao REFramework. A R1.12 não corrige essa função privada nas versões antigas.
- A nova negociação ainda precisa ser exercitada em um jogo com o REFramework real, incluindo boot, recriação de swapchain, resize e encerramento. As evidências anteriores com o fork foram coletadas com a R1.11.
- Testar Steam Overlay ativo preservando a opção do usuário; incluir alternância de FG, resolução e alt-tab. TLOU2, Alan Wake 2 e stalls de outros jogos continuam questões separadas.
- Não há ganho demonstrado de FPS, pacing exibido ou latência física nesta alteração. A consulta de identidade adiciona trabalho nas transições; profiling de desempenho deve ser feito separadamente.
- Uma função externa que não retorna ainda pode bloquear seu chamador. Não há timeout que abandone uma thread enquanto ela manipula hooks e referências COM.
- A contenção de exceção falha de forma conservadora; não garante recuperar um processo cuja memória foi corrompida.

## Artefato e rollback

O addon identifica `XeMFG R1.12`, file version `1.4.5.27`. O pacote contém manifesto, hashes, símbolos, evidências automatizadas, avisos de licença e rollback R1.11/1.4.5. Não inclui DLL do REFramework, runtime Intel, OptiScaler ou ASI. Não altera a preferência de UI Composition.

Para testar: fechar o jogo e preservar o addon instalado; substituir somente o addon na instalação ReShade nativa existente, mantendo as DLLs e configurações. Para rollback, fechar o processo e restaurar o backup ou um dos addons de rollback; manter somente um addon MFG instalado.
