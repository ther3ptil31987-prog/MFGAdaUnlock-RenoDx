# XeMFG R1.5 — composição de UI opcional e revisão de pacing

Data: 19/10/2026. Base: R1.4, commit `43b25b3e3edb3d4db93594f56b29d4412327bae8`, addon SHA-256 `d6204480a38682c04a7e4c5fcafec6c0e087517dffc30c39d0a5738efba2e324`. Trabalho na branch isolada `feature/1.4.5-xemfg-reshade`; a árvore antiga e a release oficial permanecem separadas.

## Resultado

A R1.4 habilitava `xefgSwapChainSetUiCompositionState(...ENABLED)` incondicionalmente ao criar o swapchain e ignorava o retorno. Isso diferia do padrão Intel e da preferência inicial do OptiScaler. A R1.5 remove essa imposição e oferece **Quality → UI Composition**, desmarcada por padrão. Não implementa outro compositor nem reaproveita código GPL.

O pacing já contém os mecanismos de estimativa de cadência, recuperação de hitches, compensação da latência da apresentação e metering presentes na referência MIT examinada. A revisão de fonte/logs não demonstrou uma fórmula ausente que justifique alterar deadlines ou remover waits. Isso não certifica regularidade de exibição nem paridade de latência física com OptiScaler.

## Composição de UI

O [contrato público Intel](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/inc/xess_fg/xefg_swapchain.h) define composição desligada como padrão. `uiMode=AUTO` seleciona o método **quando a composição está habilitada**; não significa habilitá-la automaticamente. O [guia de UI](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xess_fg_developer_guide_english.md#ui-handling) recomenda começar com interpolação, fornecendo inputs opcionais quando disponíveis.

- **Desmarcada:** o modelo interpola a UI junto da imagem. Os inputs HUD-less/UI válidos continuam sendo encaminhados; desligar composição não exige descartá-los.
- **Marcada:** solicita composição AUTO usando o input HUD-less da mesma origem/frame. O HUD passa a atualizar na cadência dos frames reais; marcadores móveis podem parecer menos suaves. O objetivo é oferecer alternativa a artefatos de interpolação da UI, sem prometer benefício para todos os jogos.
- A checkbox só pode ser ativada quando foi observado HUD-less válido no frame de origem; uma preferência já marcada sempre pode ser desmarcada. Se faltar esse input num frame, a composição é desligada para ele e a escolha permanece salva.
- A preferência `[XeFG] UIComposition=0/1` tem fallback **0** para chave ausente. Ela é consumida antes do Present de origem, sob a trava de bookkeeping; o callback de desenho do painel não chama o setter do SDK.
- O painel distingue preferência, estado conhecido/aplicado e espera por inputs. Falha do setter não publica sucesso: a geração daquele lote é desativada, preservando o Present nativo, e a aplicação é tentada novamente. Estado idêntico confirmado não é reaplicado por frame. Erros repetidos iguais não produzem uma linha de log por frame.
- Resize invalida a confirmação, para reaplicar o estado na próxima fronteira. O controle nativo de preferências/status usa versão **2** e recusa a versão anterior, evitando interpretar padding antigo como a nova opção.

O OptiScaler fixado em `c2e44a8bc911010d7ed3e4fa63843185fbb55d03` foi consultado apenas como referência de comportamento: default falso, checkbox e dependência de HUD-less. Sua implementação e seu workaround de suprimir dez frames não foram copiados. A ponte independente usa a API Intel, que permite alternar composição em execução. Os testes de integração exercitam a mudança; outros jogos ainda precisam de validação visual.

## Revisão de pacing

### Código e responsabilidades

Referência MIT: dashdogy `a4cf2c4ab632e84c72254932793bb9e64b33d76e`. O header `sdk_pacing_dlssg.h` coincide após normalizar finais de linha. O diff de `sdk_pacing.cpp` contém nosso pedido de toggle na fronteira do lote e snapshot de estado; o núcleo matemático examinado coincide.

1. **XeLL:** recebe sleep e marcadores dos frames reais. A correção R1.3 mantém a trava liberada durante sleep, permite marcadores tardios de outro frame e evita setters de sleep idênticos por frame.
2. **XeFG:** produz os outputs, mantém os recursos/fences e seu proxy de apresentação. Em hardware não Intel o SDK possui uma thread de pacing e fila Direct próprias, conforme o guia.
3. **Estimador importado:** seis deltas de início de lote. Picos acima de 1,5× a mediana ficam fora da média usada, mas permanecem na janela para que uma redução sustentada de desempenho seja assimilada. Pequenos gaps preservam histórico; gaps maiores reiniciam a estimativa.
4. **Deadline sem metering:** usa o intervalo produzido dividido pelos outputs, subtrai a latência de apresentação que o SDK fornece e limita a espera futura a um intervalo. Não substitui os fences de consumo.
5. **Metering NVIDIA:** `SetFlipConfig` V2 usa a fila Direct do SDK. Habilita após amostras suficientes, considera feedback de independent flip e limita crescimento do tempo de lote. O lead padrão é 2.500 µs, limitado a meia distância entre outputs; isso é antecipação de submissão, não medição de latência física.
6. **Fallback:** ausência/recusa do driver, histórico insuficiente, modo de Present diferente ou layout não validado preservam o caminho do SDK. Não foi removido nenhum desses gates. Não foi acrescentado segundo pacer ou consumidor de waitable.

Corrigida também uma afirmação imprecisa de diagnóstico: o texto genérico da ponte dizia “metered output” sempre que admitia FG, mesmo se o driver estivesse em fallback. Agora o estado do metering é informado separadamente. O algoritmo de apresentação desta revisão não mudou.

### Log salvo: escopo e achados

O log é acumulado: nove blocos iniciados por `ready` pertencem a execuções anteriores. A análise usa **somente o último bloco**, linhas 1.950–2.210, ticks 71.550.000–71.674.453, 261 registros. Os timestamps de pacing são de aproximadamente 18:11–18:12. O addon instalado foi conferido como R1.4; o log antigo não carrega por si o hash do addon, portanto esse hash não autentica os outros oito blocos.

| Evidência do último bloco | Leitura permitida |
|---|---|
| 65 registros de deadline, todos com quatro outputs; 49 marcados como metered | O caminho de metering foi executado; registros são amostrados, não contagem de todos os frames |
| 14 registros de configuração com retorno NVAPI 0; um feedback `NO_IFLIP`, os demais zero | Há aceitação e fallback registrado; não calcular porcentagem de tempo ativo a partir de logs limitados |
| Hitch de 212,2975 ms: média bruta 58,3174 ms, estimativa usada 22,7804 ms, dois deltas excluídos | O filtro evitou alimentar o lote seguinte com a média contaminada nesse evento; não demonstra recuperação visual perfeita |
| Dois resets por gaps de 128 e 19 IDs | O histórico foi reiniciado após descontinuidade; sem anotação de gameplay não atribuir esses gaps a um defeito do pacer |
| Skips por histórico insuficiente e modo de Present diferente | Fallback conservador está presente. Faltam sync/flags e timeline correlacionados para explicar cada transição |

Nos registros metered examinados, o deadline calculado já estava no passado em relação à CPU quando consultado. Esse caminho libera a submissão para o driver; **não são 12 ms de atraso físico demonstrado**, nem prova de que frames foram exibidos juntos. Também não converter os intervalos estimados em FPS exibido. Fences, fila, apresentação real e scanout não foram medidos nessa revisão.

Artefatos locais preservados em `out/xemfg-r1.5-pacing-review/`, fora da fonte versionada. Log SHA-256 `aee3ec38693f5ac8c05153ba47e38a8d5092b31ab07650ae2d38cc148193cb02`; INI SHA-256 `0532834a46e6d199a0410c1af9e50f96e8c10371f0056347125a15bd525099b2`. O `review.json` registra seleção do bloco, contagens, amostras e limites. Os dados essenciais estão nesta tabela para o relatório não depender de arquivos locais.

### O que falta para julgar a experiência de pacing

Comparação na mesma cena/rota, FPS de base, multiplicador, refresh, modo de janela, VRR/VSync, limiter e energia. Separar a mudança de composição de UI de um teste de metering. Usar pelo menos três pares alternados de 30 s, PresentMon com métricas de exibição válidas e versões/colunas registradas; comparar p50/p95/p99 por passagem. Perfis de CPU/GPU ficam em rodadas separadas. Incluir 2x/3x/4x/6x, hitches de streaming, pause/retorno e giros de câmera. Medir input-to-photon separadamente se a alegação for latência.

Somente depois de demonstrar uma irregularidade exibida, correlacionar os argumentos do Present, fences e execução GPU. Um skip de validação não autoriza relaxar o gate, e uma espera longa pode proteger recursos. Não foi encontrada evidência para mudar o lead, a média ou a prioridade de thread nesta entrega.

## Validação e entrega

Foram adicionados contratos numéricos de pacing (cadência estável, lead, compensação da latência do SDK, hitch, gaps e mudança de multiplicador) e testes de default/opt-in/off, inputs ausentes, cache, falha/retry e reset da composição. A suíte nativa real 2x/3x/4x/6x agora alterna composição, verifica a persistência e mantém os testes de inferência CUDA/XeLL, tags voláteis e marcadores tardios. As janelas sintéticas/ocultas não validam scanout nem qualidade em Cyberpunk.

`cmake --build build/tests --config Release -j 2` passou. `ctest --test-dir build/tests -C Release --output-on-failure --output-log build/ctest-native-r1.5.log` concluiu **59 testes aprovados, três opcionais ignorados e nenhuma falha**, em 48,20 s. Os opcionais são `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`, dependentes de artefatos históricos. O teste real confirmou quatro mudanças de composição solicitadas no cenário, sem reaplicar o setter nos outros frames; no início, desligada por padrão, nenhuma chamada sobrescreveu o estado de interpolação do SDK. Os quatro cenários adicionais de layout R1.4 também passaram, e a nova aba Quality foi renderizada/inspecionada com a checkbox desmarcada.

Binário final: versão de arquivo `1.4.5.10`, produto `1.4.5-XeMFG-R1.5`, SHA-256 `cd902f48500b8ee110c2cd7095a981694ebfbb371db3e5e7ad1aa78237d18647`. O manifesto do pacote identifica o commit e hashes dos inputs. A aprovação perceptual R1.3 não foi herdada como validação de gameplay desta revisão.

A build R1.5 inclui manifesto e rollback R1.4/1.4.5 oficial. Sem publicação automática de release. A correção oferece a escolha solicitada; nenhum ganho de FPS ou de latência é prometido.

Fontes adicionais: [pacing MIT fixado](https://github.com/dashdogy/MFG-Unlocker/blob/a4cf2c4ab632e84c72254932793bb9e64b33d76e/source/native/xess/sdk_pacing.cpp), [contrato de multithreading Intel](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xess_fg_developer_guide_english.md#multithreading), [opção do OptiScaler, apenas referência](https://github.com/optiscaler/OptiScaler/blob/c2e44a8bc911010d7ed3e4fa63843185fbb55d03/OptiScaler/Config.h).
