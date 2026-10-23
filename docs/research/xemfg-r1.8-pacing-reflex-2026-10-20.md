# XeMFG R1.8 — pacing e saída Reflex

20 de outubro de 2026. Branch `feature/1.4.5-xemfg-pacing-reflex-r1.8`, derivada de R1.7.5 (`d3588a20bc950c0f13fcba627fd4d2c6d40c8088`). A base DLSS-G continua sendo a 1.4.5 oficial. O problema de entrada do ReShade aparentemente desapareceu após o reboot relatado pelo usuário; esta candidata não atribui sua resolução a uma alteração de UI.

## Mudanças

- **Reflex → Reflex com XeMFG:** a opção de saída passa a aceitar Reflex. As opções V1 conhecidas são encaminhadas integralmente ao setter original, na thread do jogo. O sleep original é chamado uma vez por frame admitido. O limitador XeLL fica desligado e seu intervalo mínimo é zero. A escolha de saída é fixa durante o processo: o painel salva a preferência e exige reinício, sem reaplicar opções do jogo pela thread da UI. Auto mantém XeLL.
- **Ligação Intel:** no primeiro teste, o SDK recusou FG com um contexto XeLL em modo neutro. O disassembly de XeFG 1.3.1.78 revelou um seletor explícito de latência externa, reconhecido antes de tratar o argumento como ponteiro. A ponte usa esse caminho interno somente com o provider já admitido por SHA-256 e duas funções carregadas conferidas integralmente. Não modifica instruções ou campos de objetos para essa ligação. Ela depende de uma interface privada, restrita à versão examinada; não é um contrato público Intel. O contexto XeLL separado conserva markers diagnósticos, sem ser o limitador ou o contexto de latência ligado ao XeFG nessa rota. Reflex deve estar habilitado no jogo.
- **Lotes aceitos pelo driver:** após `SetFlipConfig` aceitar metering, um lote validado e recente pode enfileirar seus outputs sem repetir o horário de exibição como espera de CPU. A mudança substitui somente o deadline aritmético por `now`; o `Pace` original continua executando. No provider examinado, a espera de fence do primeiro output ocorre antes da chamada que calcula esse deadline. As chamadas de Present, recursos, filas e sincronizações do SDK permanecem com o runtime. Isso permite avanço do produtor quando as dependências do SDK permitirem; não demonstra overlap GPU ou scanout por si só.
- **Fallback:** Present com sync interval diferente de zero não autoriza esse caminho. Recusa do driver, perfil incompatível, lote antigo, contagem diferente, FG desligado ou timing original Intel mantêm o caminho anterior. `MeteredQueueAhead=0` permite comparar o pacing anterior sem trocar a build. A opção está em General → Advanced pacing comparison, ligada por padrão; seu efeito é condicionado ao metering aceito. Não há novo limitador de FPS.
- **CUDA:** port seletivo da correção MIT pública de reconhecimento de PSO através de camadas intermediárias, com SHA exato, dispositivo e lista do provider ainda obrigatórios. Tamanhos incompatíveis são recusados antes de calcular o hash. Nenhum novo fatbin ou peso foi incorporado.

UI Composition continua opt-in, desligada por padrão. O addon continua independente de OptiScaler e ASI. Configuração e DLLs instaladas não precisam ser trocadas para esta candidata.

## Origem e limites

A referência pública foi fixada em [29504cdd](https://github.com/dashdogy/MFG-Unlocker/commit/29504cdd5d209a25e3b91e809a7e0d6f9db8cbdc), v1.6.1. A beta fornecida é 1.6.2.1022, SHA-256 `f34abd14975a5328ae3baf613f681b75d3075a4ff2a80da20cfefbaa0e9fab7b`; foi examinada por parsing estático de PE, recursos e strings. Seu código de pacing não estava publicado nas referências consultadas. A implementação de fila e a integração Reflex são adaptações próprias; não anunciamos equivalência completa com a beta. AL2, Dynamic XeMFG, o pacing de VSync e os novos payloads CUDA da beta não integram esta alteração.

O [header Intel XeLL](https://github.com/intel/xess/blob/main/inc/xell/xell.h) distingue low-latency e intervalo mínimo. O [guia XeSS-FG](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md) continua sendo a referência dos recursos/filas; a seleção interna de latência externa foi determinada por RE do binário local, não por essa documentação.

## Verificação e interpretação

O pacote inclui `validation-summary.json`, o log CTest e os resultados por execução, vinculados ao hash do addon. A matriz exercita 2x/3x/4x/6x em XeLL e Reflex com D3D12, ReShade e Intel XeFG/XeLL reais. O lado do jogo usa fixture ABI Streamline; isso verifica encaminhamento e contagem de chamadas Reflex, sem medir uma resposta física do driver NVIDIA. Geração real, lançamentos Tensor, composição opcional, bypass por inputs inválidos, resize e aposentadoria de contexto são conferidos. A matriz suplementar repete 4x com XeLL 1.3.2.10.

Os testes de perfil recusam todas as 342 mutações de um byte do corpo do setter externo. O teste de latência cobre deduplicação, erro nativo propagado, modo neutro XeLL, markers tardios e epoch. Os testes de fila cobrem contagens 2x–6x, recusa, mudança de lote, intervalo inválido e estado antigo. A comparação numérica CUDA usa os nove programas disponíveis: cinco substituições autorais e quatro programas de flow originais. Oito programas originais não estão disponíveis nesse conjunto; não há alegação de equivalência numérica para eles. O teste upstream sem a opção de cobertura local continua exigindo o conjunto integral.

As janelas sintéticas podem estar ocluídas e não provar aceitação de flip metering; os logs distinguem essa ausência de observação de ativação. Nenhum teste offline prova melhora de FPS, p95/p99 exibido, latência física ou fluidez no Cyberpunk. A aceitação da candidata precisa de PresentMon no mesmo save/rota, FPS de base comparável e pelo menos três pares alternados, com overlay e timings CPU opcionais desligados. Comparar XeLL versus Reflex separadamente de queue-ahead ligado/desligado. `Use Intel SDK timing (test)` deve ficar desligado na comparação do metering.

## Uso e rollback

Com XeMFG selecionado, mantenha FG habilitado no jogo. Para saída Reflex: Latency → Advanced latency routing → Latency output → Reflex; habilite Reflex no jogo e reinicie o processo. O painel deve indicar **Reflex - active**. Para voltar, selecione XeLL e reinicie. Mudança de backend também continua exigindo reinício.

Para comparar a nova fila com o pacing anterior, altere somente **Queue metered frames early** e espere o próximo lote gerado. Copie Diagnostics para verificar a rota efetiva; checkbox solicitado não prova que o driver a aceitou.

Feche o jogo antes de trocar o addon. O pacote contém rollback R1.7.5 e 1.4.5 oficial; restaure somente um addon MFG por vez. Não se publica release nesta entrega.
