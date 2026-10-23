# XeMFG R1.6 — alterações e entrega

19/10/2026. Candidata local, base R1.5/1.4.5 preservada; nenhuma release publicada. Branch `feature/1.4.5-xemfg-pacing-r1.6`. ProductVersion `1.4.5-XeMFG-R1.6`, FileVersion `1.4.5.11`. O manifesto do pacote registra commit, estado Git, hashes das fontes, compiler inputs, payloads e evidências.

## Integração e estabilidade

- **C01 — Device nativo:** contextos XeLL/XeFG agora recebem o device D3D12 já unwrapped. Queue/factory continuam no fluxo necessário ao ReShade. Motivação: compatibilidade de proxy e recomendação oficial; não é alegação de crash reproduzido ou ganho de FPS.
- **C02 — Marcadores:** rejeita END sem START correspondente do mesmo frame. Deduplica mantendo overlap e END tardio reais durante sleep do próximo frame. Não fabrica markers para satisfazer o SDK. Testes de concorrência/lifetime e inferência real passaram.
- **C03 — Aposentadoria:** o output destruído limpa seleção global de viewport/IDs somente se ainda for owner. Teste real libera chain e aceita opção de viewport diferente. Não implementa genericamente multi-output/device-loss recovery.

## CPU e diagnóstico

- **P01 — Log assíncrono:** callbacks colocam registros em fila fixa de 512 slots; worker normal grava batches. Produtor não espera disco, capacidade ou lock ocupado; contabiliza drops/falhas. Registros grandes são descartados inteiros. PID ajuda separar processos. Worker inicia após descoberta/publicação, fora de loader callout; startup rejeitado também pode ser drenado. Heap/process lifetime evita join em DllMain. Teste de sink bloqueado passou. Formatação ainda custa CPU; saída forçada pode perder tail. Impacto de stutter em jogo **NOT TESTED**.
- **P02 — FrameStore:** substitui mapa de nós por 16 entradas fixas. Mantém retenção de dois frames para ENDs tardios, capacidade e recusa sem expulsar frame vivo/futuro. Prune usa idade modular; isso não é solução completa do wrap de todos os IDs da ponte. Microbenchmark com Frame completo foi favorável; não mede FPS do jogo.
- **D01 — Captura CPU opcional:** Diagnostics → Performance capture (advanced), fechada e desligada por padrão. Ring de 2.048 amostras por ID/output/epoch; separa espera de serialização, BeforePresent, chamada SDK, pós-processamento e XeLL sleep do frame. Copy timing summary calcula p50/p95/p99/p99.9; Clear invalida probes antigos. Query SDK indisponível não vira sucesso zero. GPU/display/latência física ficam explicitamente não medidos. Sem lista de contadores crescentes no painel.
- **D02 — Ferramentas de benchmark:** `TimingPath=xemfg`, inventário Intel/CUDA/NVAPI, ligação a manifestos de pacote nativo, p99.9 e threshold de stutter comum opcional. Desativa heurística NVIDIA de source/generated por AnimationTime para XeFG; campos sem corroborador ficam indisponíveis. Probe NVIDIA MFG não roda como se descrevesse o XeMFG ativo. Fixtures de domínio/schema passaram.

## Comportamento preservado

DLSS-G, shaders/modelos, Tensor/CUDA, pacing/deadlines, waits GPU, multiplicadores e tratamento MV/depth não foram alterados. UI Composition continua checkbox opt-in desmarcado por padrão, com preferência/aplicação/fallback distintos. XeLL segue a rota Reflex/Streamline→XeLL; sem segundo controlador. Não há novo busy-wait, prioridade realtime, blur, buffer GPU ou inferência de scanout por contador. O pacote continua um addon ReShade independente de OptiScaler/ASI.

## Validação

| Verificação | Resultado | Limite |
|---|---|---|
| Build Release x64, MSVC/VS2022 | Passou | Não valida gameplay |
| CTest completo | **62 passaram, três opcionais skipped, zero falhas**, 49,55 s | Entradas históricas opcionais não foram inventadas |
| SDK real/ReShade, XeLL instalada 1.3.0.5 | 2x/3x/4x/6x passaram | Sintético/ocluído; sem display cadence |
| XeLL alternativa 1.3.2.10 | 4x passou em diretório isolado | DLL do jogo não foi substituída; restante da matriz nessa versão pendente |
| Log bloqueado/cheio, FrameStore, markers, ring CPU | Passaram | Não provam ausência de toda corrida em jogo |
| Widgets reais/status sintético | Abas, fontes, routing, popup, tooltip, captura expandida conferidos | Não é screenshot de estado de gameplay |
| FPS, p95/p99/p99.9 exibidos e latência física A/B | **NOT TESTED** | Procedimento em BENCHMARK_PLAN.md |

Skips: `provider_identity`, `engineering_manifest`, `release_1_4_5_manifest`. Falhas intermediárias e suas correções estão registradas em ENGINEERING_AUDIT.md e logs incluídos; o resultado acima é da suíte final.

## Pendências de engenharia

**Não está comprovado que SleepMode muda somente com GPU ociosa**, como requer XeLL. Também permanecem criação concorrente, resize reentrante/device loss e quiescência universal de recursos. Não foi adicionada espera genérica que poderia bloquear a thread da janela ou aumentar input age. Essas lacunas estão priorizadas, com evidência e experimentos no audit. Microstutter não foi declarado resolvido.

## Artefatos e reprodução

Addon empacotado: `out/native-reshade-xemfg-r1.6/renodx-mfgunlock.addon64`, relativo à raiz histórica. SHA-256 **`025c6996412f638a18ab53f601b1106aed16ebd7af3f907082f1a93646fd9c3c`**, 3.912.192 bytes. ZIP: `out/MFGAdaUnlock-Native-ReShade-XeMFG-R1.6.zip`. A candidata não foi instalada nem publicada nesta rodada, preservando baseline de comparação. Pacote contém rollback R1.5 e oficial 1.4.5, manifesto, licenças e evidências.

Na raiz do worktree já configurado:

```powershell
cmake --build build/tests --config Release -j 2
ctest --test-dir build/tests -C Release --output-on-failure --output-log build/ctest-native-r1.6.log
build/tests/xemfg/Release/xemfg_frame_store_tests.exe build/tests/frame-store-benchmark-final.json
```

Configuração CMake: gerador VS2022 x64, `-DMFGUNLOCK_BUILD_XEMFG=ON`, `RENODX_SOURCE_DIR`, `XEMFG_SDK_ROOT`, `XEMFG_INPUTS` e `XEMFG_GENERATED` apontam às dependências/payloads locais verificados. Tests offline usam `XEMFG_OFFLINE_FG`, `XEMFG_OFFLINE_LL`, `XEMFG_OFFLINE_RESHADE`, `XEMFG_OFFLINE_HOST`; hashes acompanhando resultados. Clone limpo requer preparar os inputs locais conforme PROVENANCE; o build não baixa modelos ou DLLs silenciosamente. Saída de compilação original: `build/tests/xemfg/Release/renodx-mfgunlock-native.addon64`.

Fechar o jogo para instalar/trocar/voltar. Para voltar só à R1.5, repor o asset de rollback como `renodx-mfgunlock.addon64`, mantendo um único addon MFG. Para restaurar setup completo usar o backup do instalador com `Restore-MFGBackup.ps1`. Sem hot reload ou troca automática das DLLs Intel.
