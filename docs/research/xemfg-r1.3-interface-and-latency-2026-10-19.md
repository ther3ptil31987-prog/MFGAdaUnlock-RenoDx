# XeMFG R1.3 — interface e comparação da resposta do mouse

## Resultado e limite

A candidata R1.3 reorganiza as interfaces DLSS-G e XeMFG em General, Quality, Latency e Diagnostics. XeMFG recebe cards com controles, descrições curtas e indicação do estado efetivo. Informações de CUDA, códigos de driver e contadores ficam em Diagnostics. DLSS-G mantém os mesmos bindings/configurações, com Quality separada, resumo compacto e controles técnicos recolhidos. O estilo é local ao addon, acompanha a escala da fonte do ReShade e usa uma navegação compacta em janelas estreitas. Não muda o tema ou a fonte dos outros addons.

A comparação encontrou um defeito de concorrência na ponte nativa e uma chamada redundante por frame. Ambos foram tratados. **Ainda não foi demonstrado que explicam integralmente o mouse mais pesado**, nem medido ganho físico de latência. O usuário relata atraso com multiplicador e FPS de base semelhantes; esse relato orienta o próximo A/B, não é descartado por contadores de FPS.

## Bases separadas

| Base | Identificação | Uso |
|---|---|---|
| Addon anterior | R1.2, commit `2ac4bff409e31004fcc6a523151a3df363c2970a`, addon SHA-256 `18bfb21b9d9b1da4006aac06adb77c9a1dc20b5313f5525fe263e1470da6eaf7` | Baseline e rollback desta candidata |
| Nosso transporte anterior com OptiScaler | X1.3, `0d8dc0b6d9aff9aad9afaf57f6939e94fada93c5` | Identificar diferenças introduzidas pela retirada do host |
| dashdogy | Fonte MIT fixada em `a4cf2c4ab632e84c72254932793bb9e64b33d76e`; README público também consultado nesta data | Comparação de componentes e funcionalidades; o README corrente não substitui a versão fixada |
| ASI fornecido | `MFGUnlocker (1).asi`, SHA-256 `161b9e1245a3736dc462ee83f4ad51e6125d8e7e73ce3eedde89d2eed60c69fa` | Identidade do arquivo e dos recursos já extraídos; seu código executável não foi carregado para a investigação |
| OptiScaler | Fonte do host anterior em `024c4a1b79a6484a0ce4c56a8961a0fa03583203`; checkout corrente `c2e44a8bc911010d7ed3e4fa63843185fbb55d03` identificado separadamente | Referência de comportamento/contratos; nenhum código GPL incorporado |
| Intel | SDK fixado em `de0fb9c1c510661c571164e1418ceca8101dab69`, XeFG 1.3.1.78 / XeLL 1.3.0.5 no teste local | Contratos e testes do SDK real |

A configuração OptiScaler ainda no diretório do jogo usa LowLatencyInput/Output e FramerateLimit em Auto, ForceBorderless verdadeiro e contém InterpolationCount=5. Esse arquivo inativo pode ter sido alterado entre sessões; não foi tratado como prova de que o A/B relatado pelo usuário usava outro multiplicador. A imagem anterior fornecida mostra input Reflex e output XeLL.

## Comparação de funcionalidades

| Mecanismo | Referência dashdogy + OptiScaler | Nosso addon | Relevância para a sensação |
|---|---|---|---|
| Modelo MFG e programas portados | Port e catálogo de shaders MIT; recursos do ASI fornecido | Mesmos inputs de port já incorporados e manifestados; rota Tensor validada | Não apareceu um modelo diferente que explique o A/B |
| CUDA/Tensor | Reconhecimento exato dos programas/layers e substituição de dispatch | Presente; launches reais observados | Não faltava aceleração na R1.2 |
| Extra-output pacing / metering | Estimador, deadline, correções de hitch e metering NVIDIA | Presente. Diff de `sdk_pacing.cpp` contra a referência só mostra pedido de toggle na fronteira de lote e leitura de status da R1.2 | Não foi encontrada ausência da fórmula principal de pacing. Aceitação do driver não mede latência física |
| XeLL e marcadores | OptiScaler traduz a entrada e compartilha contexto XeLL com XeFG | Ponte própria recebe Streamline/PCL e fornece XeLL | Ponto de integração diferente; prioridade da revisão |
| Configuração de sleep | Host evita setter quando parâmetros não mudam | R1.2 chamava `xellSetSleepMode` em cada `slReflexSleep`; R1.3 mantém cache e confirma sucesso antes de atualizá-lo | Redundância confirmada; não prova que cada setter reiniciava o estado interno do XeLL |
| Travas durante sleep | Referência MIT usa lease de lifetime e solta a trava antes do sleep | R1.2 mantinha `Output::io` durante sleep e Present; R1.3 usa shared ownership e libera bookkeeping durante as esperas | Um marcador de outro frame/thread podia ficar atrás da espera. Teste sintético reproduz e verifica a correção |
| Boost | Host repassa o bit solicitado | Nosso transporte não o repassa | `inc/xell/xell.h` o marca como não suportado; não foi usado como explicação nem promessa de ganho |
| Input/output alternativos | Host tem adaptadores XeLL/Reflex e outras tecnologias; também existe caminho com outro sleep output e contexto XeLL próprio para FG | Auto/Reflex Streamline -> Auto/XeLL. Outros adaptadores não implementados | Não é paridade completa. A alternativa Reflex-output não explica por si um A/B cujo output anterior era XeLL. Exige outra integração, não apenas um seletor |
| Detecção de cortes em giros rápidos | `ApplySceneDetector`/`SceneDetour` pode desligar o detector angular interno, preservando reset explícito | O helper está importado, mas o backend não o instala. Isso também ocorre no nosso X1.3 anterior | Diferença real para o mod completo do autor, voltada a freeze/jump em giros grandes, não prova de atraso contínuo. Mantida fora desta correção para não mudar rejeição de cortes junto com a latência |
| Correção de MVs inválidos | Referência tem `motion_fill` e adaptações para integrações específicas | Tags/MVs da entrada Streamline são preservados | Não foi demonstrada necessidade disso no Cyberpunk com imagem boa. Não copiar heurística de outro caminho sem dados |
| Dynamic XeMFG / múltiplos fornecedores / inputs | Funcionalidades do host e port com suporte mais amplo | XeMFG fixo 2x–6x, NVIDIA/D3D12/Streamline, um output | Backlog de compatibilidade; não causa demonstrada de peso no mouse em 4x fixo |
| Borderless, VSync, limiters | Host possui overrides e políticas adicionais | Borderless opcional; Present e sincronização do jogo preservados | Podem alterar a latência. Falta um trace pareado para isolar compositor/modo de apresentação; não foram alterados por suposição |

## Correção de concorrência

`SleepSourceFrame` toma a trava apenas para reservar o frame, verificar parâmetros e marcar a chamada em voo. Executa `xellSleep` sem `Output::io`. O `shared_ptr<Output>` mantém o contexto vivo; ao retornar, confere o epoch e procura o frame novamente, sem reusar um ponteiro de map invalidado por resize. Sleep simultâneo no mesmo contexto é recusado; repetição de um sleep já concluído é deduplicada.

`ForwardSourceMarker` admite o timestamp real de um frame anterior enquanto outro está dormindo. Para o mesmo frame, exige sleep concluído antes do marcador, como a Intel documenta. Não inventa timestamps. Resize invalida o epoch e limpa o cache. Um sleep que retorna depois disso não republica um frame antigo.

Present/Present1 continuam serializados entre si por uma trava específica; ela não é a trava dos marcadores. O mod não remove fences, muda deadlines ou altera esperas de recursos do XeFG. As chamadas de setup na fronteira de BeforePresent continuam separadas do sleep liberado; não foi alegado que toda operação interna do SDK se tornou assíncrona.

O cache de parâmetros evita reaplicação idêntica. Falha do setter não é registrada como sucesso; a próxima chamada tenta de novo. Uma mudança efetiva continua sendo entregue na fronteira do sleep de origem. **Limite ainda aberto:** o guia Intel exige GPU quiescente para mudanças de sleep mode; a ponte não demonstrou uma barreira de todas as filas privadas do SDK durante alterações dinâmicas do limiter. O cache reduz chamadas, mas não é uma prova dessa quiescência. Essa lacuna não foi escondida nem convertida em uma recomendação para remover waits.

## Interface

- General: método, multiplicador, pacing do driver e janela. Trocas de backend anunciam reinício; multiplicador XeMFG permanece ao vivo.
- Quality: DLSS-G mantém perfis existentes com nomes mais simples; XeMFG explica a seleção automática e mostra aceleração. Não oferece um perfil DLSS-G que não atua no backend Intel.
- Latency: estado XeLL e fonte do timing em linguagem de jogador; roteamento avançado recolhido. DLSS-G separa controles comuns das estatísticas e do laboratório de pacing.
- Diagnostics: versões, resultados, contadores e cópia do diagnóstico. “Gerando frames” exige observação de submissions válidas no XeMFG; solicitação aceita não vira comprovação de scanout.

`xemfg_panel_preview` compila os mesmos widgets da produção com ImGui real e status sintético, renderiza em CPU para PNG e testa navegação entre quatro abas. Não injeta nem envia teclas ao jogo. Capturas de largura normal e fonte maior foram inspecionadas; a versão estreita usa resumo e seletor de página compactos. Esses PNGs validam layout, não estado do driver. DLSS-G foi compilado com seus bindings existentes; a inspeção visual automatizada usa a tela XeMFG, que é a parte mais alterada.

## Validação e próximo A/B

Uma rodada de integração falhou ao aplicar Borderless em 4x/6x. A espera limitada pelo reconhecimento assíncrono não resolveu; instrumentação revelou erro Win32 **126**, em vez de uma simples demora. O registro `WH_GETMESSAGE` passou a usar `hMod=nullptr` para uma thread do mesmo processo, conforme o contrato Microsoft. O addon continua explicitamente pinado para proteger o callback. Falhas de bind agora conservam o código para Diagnostics. O teste não reenvia pedidos para ocultar falhas: processa mensagens e aguarda no máximo dois segundos, publicando o estado se o controlador não confirmar a aplicação.

`cmake --build build/tests --config Release -j 2` e CTest completos ficam em `build/ctest-native-r1.3.log`. A suíte inclui testes de concorrência com sleep bloqueado esperando marcador de outra thread, cache/retry de configuração, ordenação por frame, invalidação por epoch e os quatro testes com XeFG/XeLL/CUDA reais em 2x/3x/4x/6x. O teste real exige que 48 sleeps não causem 48 configurações idênticas. Os testes de imagem da interface usam dados sintéticos e são rotulados como tal.

Para aprovar a resposta do mouse: comparar R1.2 e R1.3 na mesma cena, multiplicador, faixa de FPS de base, refresh, VRR/VSync, limiter e energia. Primeiro verificar preferência do usuário sem overlays extras. Se persistir diferença, alternar com o transporte X1.3 salvo e correlacionar input sample, sleep, RenderSubmitEnd, Present e exibição. Não atribuir mudança à sensibilidade do mouse sem medir câmera/input; o mod não reescreve input nem sensibilidade. Latência física e desempenho ainda precisam de medição própria.

Resultado final: **57 testes passaram, três opcionais foram ignorados, nenhum falhou**, em 45,89 segundos. Os ignorados dependem de artefatos históricos (`provider_identity`, `engineering_manifest`, `release_1_4_5_manifest`). Depois da correção do hook local, Window Controls, 4x e 6x também passaram em três repetições consecutivas cada; isso não substitui um teste de longa duração em jogo.

## Retorno do usuário — 19/10/2026

Após instalar a R1.3, o usuário respondeu **“Mais próximo do que sentia com OptiScaler”** à comparação do peso/atraso do mouse com a R1.2. O protocolo solicitado foi ManualSave-317, XeMFG 4x, mesmos ajustes e faixa de FPS de base; essas condições não foram medidas independentemente nesta rodada. O addon instalado foi conferido pelo SHA-256 `f20a92b2ced415fbe8462b4cfb82f1f80b526e4a9b839bfbf6417e704bd7dcff`, da build de código `8fe4cc97033e334fd60bdb130d317439e284469f`.

O retorno registra uma melhora subjetiva inicial da resposta do mouse. Não demonstra equivalência completa ao transporte com OptiScaler, redução de latência física ou qual correção causou a diferença. Também não constitui avaliação da nova interface nem validação geral de desempenho e estabilidade. A R1.3 fica preservada como referência desta comparação; nenhum binário, configuração ou pacote selado foi alterado para registrar o relato.

## Fontes primárias

- [MFG-Unlocker, referência fixada](https://github.com/dashdogy/MFG-Unlocker/tree/a4cf2c4ab632e84c72254932793bb9e64b33d76e/source/native/xess): `sdk_pacing`, `LlLease`, `ApplySceneDetector`, modelo/CUDA e controle do host.
- [Guia XeLL](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xell_developer_guide_english.md): sleep antes da simulação/inputs, configuração somente ao mudar parâmetros e requisitos de sincronização.
- [Header XeLL](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/inc/xell/xell.h): bit de Boost documentado sem suporte.
- [OptiScaler no commit do host anterior](https://github.com/optiscaler/OptiScaler/tree/024c4a1b79a6484a0ce4c56a8961a0fa03583203/OptiScaler/low_latency): referência GPL apenas de leitura; implementação nova permanece MIT.
- [SetWindowsHookExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw): parâmetro de módulo para hooks de uma thread do próprio processo.
