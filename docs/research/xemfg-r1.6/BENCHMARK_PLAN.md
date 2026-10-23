# Protocolo A/B — XeMFG R1.6

19/10/2026. Execução controlada de gameplay, scanout e latência física nesta rodada: **NOT TESTED**. Não substituir os campos abaixo por resultados de smoke ou microbenchmark. A execução não dispõe de controle confiável do percurso/input do jogo; este é o procedimento manual exato para fechar a validação.

## Fixar identidade e cenário

Baseline: R1.5, SHA-256 `cd902f48500b8ee110c2cd7095a981694ebfbb371db3e5e7ad1aa78237d18647`; candidata: `renodx-mfgunlock.addon64` do pacote R1.6 e hash do manifesto. Trocar somente addon, com processo fechado; não alterar DLLs Intel, driver, ReShade, RenoDX do jogo ou INIs entre A/B. Guardar configuração efetiva e manifesto de cada versão.

Cyberpunk ManualSave-317, cena da cerca: pan horizontal lento, inversão, parada, mesma posição/velocidade. Segunda rota fixa de carro/asfalto para streaming e giro rápido; documentar ponto inicial e duração. Manter iluminação/horário/clima, resolução, DLSS, RT, HDR, sharpening e HUD iguais. Não suavizar imagem ou reduzir multiplicador para chamar de solução. Repetir em segundo jogo antes de generalizar.

Registrar jogo/build, Windows, driver, HAGS, energia, GPU/CPU, refresh real, monitor, resolução, modo janela, VRR/VSync, limite de fonte/saída e overlays. Hash de todos os módulos mapeados, distinguindo candidatos de provider/output realmente ativos. Fonte na mesma faixa não basta se uma configuração mudou simultaneamente.

## Matriz mínima

| Família | Condições |
|---|---|
| Fonte alta/normal/stress | Aproximadamente 60 / 40 / 25 FPS reais, separadas; ajustar cenário/carga e repetir o mesmo em A/B. Não inferir fonte dividindo FPS exibido por multiplicador |
| Limite de trabalho | GPU-bound e CPU-bound separados; confirmar por execução e filas |
| Geração | FG off, 2x, 3x, 4x; 5x/6x só se aceitos/estáveis, rotulados experimentais NVIDIA |
| Sincronização | VRR on/off; VSync on/off; refresh fixado em cada célula. Sem empilhar limiters independentes |
| Transições | Alt-tab, menu/pause, carregar save, hitch, resize, FG off/on e 2x↔4x↔6x; registrar instante; não misturar com steady state |
| Janela/modo | Borderless e modo efetivo registrado; se PresentMode mudar, divulgar efeito combinado e procurar par controlado |

Aquecer a rota, completar JIT/cache e reiniciar após a primeira compilação antes da medida. Não limpar cache entre passagens já aquecidas. Cold start é outra investigação. Primeiro testar célula 4x/40 FPS habitual; ampliar matriz depois. Para cada intervenção isolada, no mínimo três pares de 30 segundos: **A/B, B/A, A/B**. Reiniciar para carregar cada binário. Aumentar amostra se dispersão não separar efeito. Rodadas de pelo menos cinco minutos ajudam p99.9; não juntar tudo em uma população que esconda variação entre passagens.

## Captura sem profiler

Deixar `Diagnostics > Collect CPU timings` **off**, fechar overlay e retirar ferramentas de profiling. O logger de produção permanece como parte de cada build; suas perdas precisam ser registradas. PresentMon roda como coletor leve com versão fixada. A partir da raiz do worktree:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File src/addons/mfgdiagnostics/Capture-Performance.ps1 -Label r16-A1-4x -DurationSeconds 30 -ProcessName Cyberpunk2077 -TimingPath xemfg -RuntimeIdentity "FG1.3.1.78+XeLL1.3.0.5; hashes do inventario" -BuildManifest "C:\caminho\pacote-r1.5\manifest.json" -ConfigurationNotes "ManualSave-317; rota e configuracao fixadas"
```

Repetir B1/A2/B2/A3/B3, alterando label e manifesto conforme binário instalado. O script verifica ligação hash da captura ao asset do manifesto nativo. Diretório padrão: `Documents/MFGBenchmarks`. A presença do módulo no inventário não comprova feature selecionada; copiar diagnóstico de ativação antes/depois em rodada separada se necessário.

Fixar threshold de gap depois de observar ruído do controle e **antes de avaliar candidata**. Reanalisar todos os CSVs com o mesmo `-DisplayedStutterThresholdMs` ou passar esse argumento a todas as capturas seguintes. Zero mantém heurística por passagem: contagens com thresholds diferentes não são comparação causal de stutter. Exemplo com valor ilustrativo, a substituir pelo threshold pré-registrado:

```powershell
powershell -NoProfile -File src/addons/mfgdiagnostics/Analyze-PresentMon.ps1 -Path "C:\captura\presentmon.csv" -TimingPath xemfg -DisplayedStutterThresholdMs 12
```

Conferir nomes de parâmetros no script se usar versão anterior; a R1.6 fornece schema/coluna/domínio. Para XeMFG, classificação NVIDIA `AnimationTime` é desativada; FPS de fonte, generated fraction e multiplicador inferido ficam indisponíveis. Fonte precisa corroborador do motor/ID, não divisão de saída nem hook CPU sem qualificação. Display intervals requerem campo/evento válido; verificar população, swapchain e modo dominante. Modo minoritário excluído pode esconder transições: inspecionar CSV bruto.

## Perfis separados

1. **CPU:** habilitar ring por trecho curto; Clear, reproduzir, copiar relatório. Correlacionar ID/output/epoch e drops. Coleta é bounded; não perder cauda por interpretar 2.048 amostras como sessão inteira. WPR inclui CSwitch/ReadyThread para distinguir tempo executando, bloqueado e pronto.
2. **GPU:** smoke do profiler precisa mostrar execução do PID/queues do jogo e FG. GPUView/ETW ou GPU trace; mapear produtor, recurso/slot, fence, dispatch, cópias, JIT e residência. Ausência de trabalho não é prova de idle se coleta não cobre aquela fila/engine.
3. **Display:** PresentMon/ETW com eventos de exibição; cruzar output e modo. Perfiler de CPU/GPU não substitui esse domínio. Em refresh fixo 120 Hz, 8,33 ms é referência de refresh, não obrigação universal sob VRR/sync distintos.
4. **Latência:** input conhecido→mudança física no monitor, câmera rápida/LDAT validado; registrar posição de scanout, trigger e repetição. XeLL sleep/markers são proxies de segmentos, não input-to-photon.
5. **Qualidade:** vídeos em movimento A/B ocultos, mesmos settings, cerca/asfalto/oclusões/retorno/cortes; avaliar fios, espessura, desaparecimento, trails e HUD. Novos recursos de imagem não foram implementados nesta revisão.

Nunca aceitar remoção de fence/espera sem demonstrar validade até último consumidor. SleepMode exige idle GPU documentado: testar alteração de cap com trabalho em múltiplas filas e coordenação do host; um fence na fila errada não basta. Resize concorrente deve incluir message pump reentrante para não aprovar deadlock escondido por execução sequencial.

## Relatório por passagem e aceitação

| Métrica e ferramenta | A | B | Diferença |
|---|---|---|---|
| FPS exibido médio — display events | NOT TESTED | NOT TESTED | — |
| Fonte corroborada — motor/ID | NOT TESTED | NOT TESTED | — |
| p95/p99/p99.9 exibidos — mesma coluna/domínio | NOT TESTED | NOT TESTED | — |
| Dispersão/gaps/stutters — threshold comum | NOT TESTED | NOT TESTED | — |
| Input-to-photon — físico | NOT TESTED | NOT TESTED | — |
| CPU global/callbacks — perfil separado | NOT TESTED | NOT TESTED | — |
| Tempo GPU/caminho crítico/residência — perfil separado | NOT TESTED | NOT TESTED | — |

Aceitar alegação de ganho somente com direção consistente nos pares e efeito maior que variação do controle; se estimada faixa de incerteza 95% entre pares, ela deve excluir ausência de ganho. Fixar limites de regressão após baseline e antes de candidata. p95/p99, cadência, fonte, input age, imagem e estabilidade não podem piorar sistematicamente. Resultado inconclusivo continua inconclusivo; ampliar amostra. p99.9 precisa população/contagem de tail publicada.

Exercitar 30 ciclos FG/resize e sessão longa; diferenciar cache estável de crescimento contínuo. Testar erro de SDK/query, inputs ausentes, mudança de viewport, device loss e duas tentativas de criação concorrentes. Capacidade fixa não pode expulsar um frame vivo; logger bloqueado/cheio não pode bloquear frame nem criar JSON truncado. Não promover suporte multiadapter/multioutput pelo sucesso da cena de um jogo.

## Rollback

Fechar processo, guardar candidata/hash e repor `rollback/renodx-mfgunlock-native-r1.5.addon64` como `renodx-mfgunlock.addon64`, mantendo um só MFG addon. Reiniciar. Para reverter toda instalação, usar `Restore-MFGBackup.ps1` com o backup criado pelo instalador. Não hot reload. DLLs Intel não são trocadas automaticamente; teste de XeLL 1.3.2.10 é célula própria, com backup/hash e reinício.
