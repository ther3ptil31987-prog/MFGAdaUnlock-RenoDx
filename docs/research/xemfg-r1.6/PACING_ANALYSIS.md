# Análise de pacing — XeMFG R1.6

19/10/2026. **Sem medição nova de intervalos efetivamente exibidos nesta rodada.** Há leitura de código, log histórico identificado, testes numéricos e inferência real em janela sintética. Foram retirados custos/erros evitáveis da integração e criados instrumentos para separar causas; isso não demonstra microstutter resolvido.

## Relógios e propriedade

1. Jogo chama sleep/markers Reflex; a ponte entrega XeLL com ID traduzido. A posição dessa chamada no motor determina o input age evitável.
2. Render e tags chegam por Streamline. A ponte valida inputs/constantes e associa ID; ausência pode suprimir interpolação naquele frame.
3. Present da origem configura geração. Intervalo CPU nesse hook não é quantidade nem hora de scanout dos interpolados.
4. XeFG gera outputs e controla filas/recursos internos. O port experimental acrescenta política de outputs/metering por perfil admitido.
5. DXGI/driver/DWM negociam modo, fila, refresh e descarte. O monitor mostra os pixels.

Intel atribui pacing ao driver Intel nesse hardware e a thread/fila internas no cross-vendor. O metering NVAPI do mod é extensão experimental NVIDIA. Não é o mesmo mecanismo Intel nem demonstra hardware dedicado Blackwell. [Guia XeFG](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xess_fg_developer_guide_english.md#proxy-swap-chain).

| Observação | Domínio válido | Não demonstra |
|---|---|---|
| QPC em source Present | Intervalo/duração CPU do hook | FPS exibido, tempo de kernel, input-to-photon |
| Tempo em XeLL Sleep | CPU retida na chamada/frame | Profundidade de fila ou latência física |
| Fence/queue dependency | Ordem/conclusão de trabalho identificado | Deadline do monitor ou fluidez |
| Status XeFG | Resultado/outputs reportados pelo SDK | Que todos foram varridos no monitor |
| NVAPI aceito/NOIFLIP | Aceitação/feedback do driver | Cadência regular ou equivalência de hardware |
| PresentMon DisplayedTime | Display events reconstruídos pelo coletor | Pixels, classe autoritativa de todo frame XeFG, latência física |
| Câmera/LDAT validado | Mudança física relativa ao input | Decomposição completa dos estágios |

Uma dependência GPU pode ser enfileirada sem bloquear CPU; substituí-la exige provar produtor, consumidor e lifetime. [D3D12 Queue::Wait](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-wait). Present pode esperar message pump; sync/flags alteram semântica de fila. [DXGI Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present).

## Pacer existente, preservado

`vendor/sdk_pacing.cpp` / `sdk_pacing_dlssg.h` já contêm estimativa do período por batch, deadlines, compensação do Present interno, horizonte futuro e metering. A revisão do núcleo contra a referência MIT fixada não encontrou ganho medido que justifique substituição.

- Janela de seis deltas; filtro acima de 1,5× a mediana e tratamento de gaps/reset/mudança de quantidade.
- Deadline por output/fase, compensado pelo custo estimado do Present; horizonte futuro limitado a um intervalo.
- Metering após três amostras, revogação após oito falhas e crescimento limitado por batch.
- Lead padrão 2,5 ms, limitado a metade do spacing. Antecipar a chamada pode ser correto para o driver distribuir outputs.
- Context generation/owner e transições preservados. Fallback continua no caminho já existente; sem segundo pacer, novos waits ou mudança de prioridade.

Esses mecanismos não provam política ótima para todo VRR/VSync/cap/refresh/base FPS. Também não autorizam extrapolar perfis privados a outra DLL.

## Log histórico reavaliado

Revisão R1.5: `out/xemfg-r1.5-pacing-review/review.json` e log na raiz histórica. Último bloco `ready`, linhas 1950–2210, 261 registros, vinculado na revisão ao addon R1.4 `d6204480…2e324`. Blocos acumulados anteriores foram excluídos por identidade incerta.

Há 65 decisões de deadline, 49 marcadas metered, e 14 configurações NVAPI registradas com status zero; uma tem NOIFLIP. São registros limitados por taxa, **não frequências reais nem percentuais de tempo**. Hitch de 212,2975 ms: média bruta 58,3174 ms, estimativa filtrada 22,7804 ms. Mostra reação do estimador, não intervalo mostrado. Deadline CPU passado em batch metered não prova atraso físico correspondente. Não há A/B R1.6 nesse log.

## Hipóteses e próximos experimentos

| Hipótese | Evidência | Experimento discriminante |
|---|---|---|
| Fonte irregular/JIT/streaming | Hitches registrados; GPU não correlacionada | IDs + scheduling CPU + execução GPU; aquecimento/rota fixa |
| Arquivo de log perturbava callback | I/O síncrono identificado na R1.5 | A/B R1.5/R1.6 e trace separado; sink bloqueado comprova só isolamento |
| Compositor/VRR/cap altera entrega | Configuração e PresentMode influenciam DXGI | Matriz de modo/refresh/sync com display events |
| Interpolação é recusada ocasionalmente | Status/bypass de inputs existem | Correlacionar ID, motivo, SDK status e display |
| GPU starved/fila profunda/fence atrasado | Sem execução GPU correlacionada | GPUView/GPU trace com PID/queue/fence; CPU off-core não é GPU idle |
| Scene detector interrompe giro rápido | Helper vendor de alteração não é instalado nesta rota; já ausente na X1.3 | Ablação separada com cortes verdadeiros/ghosting; não remover resets por suposição |
| Mudança de quantidade perturba estado | Transições e smoke funcional | 2x↔4x↔6x anotados, transiente separado de steady state |
| Scheduler atrasa presenter | Não medido | ReadyThread/CSwitch e deadlines; sem prioridade realtime |

Nenhuma espera redundante removível foi demonstrada. Fences, buffering e cap de fila não foram alterados. XeLL segue único controlador de sleep na rota admitida; limiters externos precisam controle de configuração.

## Diagnóstico novo

`Diagnostics > Performance capture (advanced)` fica fechado e coleta vem desligada. `Collect CPU timings` guarda até 2.048 amostras; `Copy timing summary` calcula sob demanda. Separa source Present, espera do mutex, BeforePresent, chamada SDK, AfterPresent/status e XeLL sleep **do frame correspondente**. Usa QPC, ID, output generation e resize epoch. Exclui pares não consecutivos; conta lacunas/drop/query indisponível. Clear invalida probes antigos em voo.

Percentis nearest rank. p99.9 em janela pequena quase coincide com máximo; não é cauda robusta de sessão longa. PresentMon usa interpolação e domínio distintos. GPU duration, queue depth, fence waits e input-to-photon ficam explicitamente indisponíveis. Sem readback ou arquivo no produtor da coleta.

Log: fila de 512 registros, batches até 64, drop em lock ocupado/lotação/oversize. Relatório copiado expõe perdas/falhas; a UI continua sem lista de contadores crescentes. Crash/kill pode perder tail. Formatação e worker ainda gastam CPU. Teste de sink bloqueado passou; overhead em jogo **NOT TESTED**.

Smoke adicional XeLL 1.3.2.10: 48 amostras e inferência Tensor 4x. Usa cap deliberado e provoca bypass/disable; tempos não são FPS Cyberpunk. O diagnóstico conserva quatro status de falha nas fases de controle, sem afirmar que todo Present interpolou.

## Comparação exigida em jogo

| Métrica | Baseline R1.5 | Candidata R1.6 | Diferença |
|---|---|---|---|
| FPS exibido / fonte corroborada | **NOT TESTED** | **NOT TESTED** | Indisponível |
| p95 / p99 / p99.9 exibidos | **NOT TESTED** | **NOT TESTED** | Indisponível |
| Variabilidade / stutters | **NOT TESTED** | **NOT TESTED** | Indisponível |
| Latência física | **NOT TESTED** | **NOT TESTED** | Indisponível |
| CPU global / caminho crítico GPU | **NOT TESTED** | **NOT TESTED** | Indisponível |

Não há controle automatizado confiável da cena nesta execução. O procedimento manual está em [BENCHMARK_PLAN.md](BENCHMARK_PLAN.md). PresentMon/ETW precisam cobrir display events do output/processo correto. HAGS restringe certas inferências GPU; QueuePacket não prova cobertura de execução. [PresentMon](https://github.com/GameTechDev/PresentMon/blob/main/README.md), [GPUView](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/using-gpuview). [XeSS Inspector](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-xess-inspector.html) é opção futura; não executado nesta rodada.
