# Plano 1.5.6 / 1.5.7: correções, HUD e pacing do XeMFG, startup do DLSS-G

Base: `fork/main` (release 1.5.5, `10b31a7`). Uma branch e um PR por bloco, sempre com base na `fork/main`.

## Como retomar

- A próxima sessão começa pelo **primeiro item `pendente` ou `em andamento`** da tabela.
- Cada item é pequeno (até ~1 h de trabalho) e vira **um commit**. No fim do item, o status muda para `feito (<commit>)` e uma linha entra no [registro](#registro-de-sessões).
- Status possíveis: `pendente`, `em andamento`, `feito (<commit>)`, `bloqueado (<motivo>)`, `descartado (<motivo>)`.
- Itens marcados **[você]** dependem de você (abrir o jogo, clicar no painel). Os outros eu faço sozinho.

## Gate de cada item

1. Build completo (`tools/Build-XeMFG.ps1` / alvo do addon).
2. `ctest -C Release -LE backlog` em `build/tests`, 100% verde. Os opcionais pulados são os de sempre (`provider_identity`, `engineering_manifest`, `release_1_4_5_manifest`).
3. `ctest -C Release -R xemfg_replay`: 8/8.
4. Teste novo do item: falha antes da mudança e passa depois.

Release só sai se todos os testes automáticos e de campo passarem sem regressão.

## Decisões já tomadas (2026-10-24)

- Branches: XeMFG #1 em `fix/xemfg-reserved-resource-state`; depois uma branch por bloco.
- 1.5.6 = blocos 1 a 3. 1.5.7 = blocos 4 e 5. O bloco 6 vem depois.
- DLSS-G: a recusa do runtime à contagem forçada é lembrada até o jogo desligar e religar o FG, o multiplicador mudar ou a swapchain ser recriada.
- XeMFG, rota Reflex: quando a espera do XeLL é recusada, o jogo recebe o resultado do `slReflexSleep` original.
- A recusa do DLSS-G (item 2.4) é validada só por fixture (não há jogo conhecido com plugin limitado).
- UI Composition continua desligado por padrão; as conversões de HUD só rodam com ele ligado.
- Limite pela taxa do monitor (P2): opção **desligada por padrão**, com aviso no painel.
- Pesquisa de qualidade de imagem do DLSS-G: descartada por agora.

## Achados dos logs de campo usados no plano

| Jogo | Achado | Item |
|---|---|---|
| Cyberpunk 2077 | HUD-less recusado por formato: `R8G8B8A8_TYPELESS` (27) | 4.2 |
| Mafia: The Old Country | Textura de UI recusada: `B8G8R8A8_TYPELESS` (90) com backbuffer `R10G10B10A2` (24) | 4.3 |
| Kingdom Come: Deliverance II | Único jogo instalado com saída **scRGB FP16** (`native_scrgb_ready`), mas não manda HUD-less | 4.4 |
| Death Stranding 2 (não instalado) | UI `R8G8B8A8_UNORM_SRGB` (29) contra backbuffer `UNORM` (28) | 4.2 (fixture) |

---

## Release 1.5.6

### Bloco 0: preparação

| # | Tarefa | Teste / critério | Est. | Status |
|---|---|---|---|---|
| 0.1 | Build limpo da 1.5.5 e matriz completa; anotar contagem de testes como referência | matriz verde, contagem no registro | 1 h | feito (sem commit: só build e matriz) |

### Bloco 1: XeMFG, estado reservado do recurso (branch `fix/xemfg-reserved-resource-state`)

| # | Tarefa | Teste / critério | Est. | Status |
|---|---|---|---|---|
| 1.1 | Cenário de conformidade: recurso A tagueado com estado válido, liberado; recurso B no mesmo endereço tagueado com `0x80000000` | caso novo no `backlog`, falhando por herdar o estado de A | 1 h | feito (`8a371fe`) |
| 1.2 | Guardar o último estado válido no próprio recurso (`SetPrivateData` com GUID próprio); remover `taggedStates` | caso 1.1 sai do backlog; a substituição no mesmo recurso (Witcher 3) continua passando | 1 h | feito (`4c06b10`) |
| 1.3 | **[você]** Witcher 3: mudar opções gráficas várias vezes com FG ligado | nenhuma perda de device; `native_tag_reserved_state` no log | 30 min | feito (campo, 2026-10-24) |

### Bloco 2: DLSS-G, crash e custo (branch `fix/dlssg-hook-safety`, empilhada sobre a do bloco 1)

| # | Tarefa | Teste / critério | Est. | Status |
|---|---|---|---|---|
| 2.1 | Extrair o patch da vtable do NGX para um header testável | teste unitário com vtable falsa, comportamento idêntico | 1 h | feito (`8cdae1d`) |
| 2.2 | Patch da vtable instalado uma única vez (trava) e nunca guarda o próprio hook como original | teste com 8 threads simultâneas: um patch, sem recursão | 45 min | feito (`0801e59`) |
| 2.3 | Testar `session` nulo em `TryPatchFlipMeteringInModule` | teste de política/revisão; matriz verde | 20 min | feito (`ba963b6`) |
| 2.4 | Política pura de "recusa lembrada" em `force_policy.hpp` | testes unitários: lembra, reseta em off/on, multiplicador, swapchain | 1 h | feito (`2896244`) |
| 2.5 | Ligar a política no `HookedSetOptions` e nos pontos de reset | teste: com recusa, 1 chamada ao Streamline por frame em vez de 3 | 1 h | feito (`d9466ed`) |

### Bloco 3: XeMFG, Reflex e log (branch `fix/xemfg-reflex-sleep-log`, empilhada sobre a do bloco 2)

| # | Tarefa | Teste / critério | Est. | Status |
|---|---|---|---|---|
| 3.1 | Cenário: rota Reflex com token constante (como o Plague Tale) | caso novo no backlog: `slReflexSleep` original não é chamado | 45 min | feito (`6ecd5b8`) |
| 3.2 | Rota Reflex chama o `slReflexSleep` original em toda recusa e devolve o resultado dele | caso 3.1 sai do backlog; rota XeLL inalterada | 45 min | feito (`1c693f9`) |
| 3.3 | Log do XeFG agrupado por padrão (`PatternBudget`), como o do XeLL | teste: 200 mensagens repetidas + 1 nova; a nova aparece | 45 min | feito (`f6059c4`) |
| 3.4 | **[você]** Plague Tale com saída Reflex | Reflex ativo no painel, interpolando, sem erro | 30 min | feito (campo, 2026-10-25) |

### Release 1.5.6

| # | Tarefa | Est. | Status |
|---|---|---|---|
| R6.1 | Gate completo no build final de release | 1 h | feito (2026-10-25) |
| R6.2 | **[você]** Regressão rápida: um jogo XeMFG (ex.: CONTROL) e um DLSS-G (ex.: Cyberpunk) | 30 min | feito (campo, 2026-10-25) |
| R6.3 | Versão, `docs/releases/1.5.6.md`, merge na `fork/main` e `develop`, tag e release no GitHub | 45 min | feito (2026-10-25) |

---

## Release 1.5.7

### Bloco 4: XeMFG, HUD e pacing (branch `feature/xemfg-hud-pacing`)

| # | Tarefa | Teste / critério | Est. | Status |
|---|---|---|---|---|
| 4.1 | **[você + eu]** Linha de base de pacing com a 1.5.6: PresentMon 30 s em Plague Tale e KCD2, mesma cena | resumo p50/p99 e frames por Present no registro | 1 h | feito (campo, 2026-10-25; junto com o 4.9) |
| 4.2 | HUD-less/UI do mesmo grupo de formato (TYPELESS, `_SRGB`) copiados para o formato do backbuffer | cenário de fixture com 27→28 e 29→28: entrada aceita e bits preservados | 1,5 h | feito (`5e52fcb`) |
| 4.3 | UI em outro formato (BGRA8 → RGB10A2) convertida por shader, com modo de refinamento do Intel | teste numérico em WARP; **avaliar em campo, pode ser descartado** se o alpha de 2 bits piscar | 2 h | descartado (codificação da UI desconhecida em saída HDR10; ver registro) |
| 4.4 | HUD-less FP16 convertido pelo encoder scRGB existente | só fixture: nenhum jogo instalado manda HUD-less em scRGB | 1,5 h | feito (`8bacb9e`) |
| 4.5 | Painel: o que o jogo fornece (HUD-less/UI, convertido ou recusado) e aviso quando o UI Composition está disponível e desligado | teste de UI/preview | 1 h | feito (`fb92008`) |
| 4.6 | Função pura do limite: base máxima = taxa do monitor ÷ multiplicador, com margem de VRR (~3%) | testes unitários (60/120/144/165/240 Hz, 2x–6x, limite do jogo menor que o calculado) | 45 min | feito (`767f5b3`) |
| 4.7 | Opção `[XeFG] DisplayCap` (desligada), painel com aviso quando base × multiplicador passa da taxa, ligação ao `minimumIntervalUs` do XeLL | cenário de conformidade: intervalo aplicado e removido; rota Reflex intocada | 1,5 h | feito (`ddc9f18`) |
| 4.8 | **[você]** Cyberpunk e Mafia com UI Composition ligado | HUD nítido nos frames gerados, sem piscar; log mostra a conversão | 30 min | feito (campo, 2026-10-25; só Cyberpunk, o 4.3 foi descartado) |
| 4.9 | **[você + eu]** Plague Tale e KCD2: P2 ligado e desligado, PresentMon | cadência mais regular ou igual; nenhum frame a mais descartado | 1 h | feito (campo, 2026-10-25) |

### Bloco 5: DLSS-G, startup e detecção (branch `perf/dlssg-startup`)

| # | Tarefa | Teste / critério | Est. | Status |
|---|---|---|---|---|
| 5.1 | Busca de marcador com cache de módulos inspecionados e busca rápida | teste unitário da busca; contagem de varreduras por módulo = 1 | 1 h | feito (`5c125c7`) |
| 5.2 | `OnInitDevice`/`OnInitCommandQueue` só sinalizam o worker | teste: nenhuma varredura na thread chamadora | 1 h | feito com mudança: continua síncrona e passa a ser medida (`dd79354`; ver registro) |
| 5.3 | Sonda: `LdrRegisterDllNotification` dispara antes do DllMain de uma DLL de teste? | resultado registrado; decide o 5.4 | 45 min | feito (`9eae117`) |
| 5.4 | Detecção por notificação (patch dentro do callback, sem varredura), mantendo o hook de `LoadLibrary` como reserva | teste com DLL carregada por `LdrLoadDll` direto | 1,5 h | descartado (crash no Cyberpunk; revertido em `f29f3d1`) |
| 5.5 | **[você]** DLSS-G em 2 jogos (ex.: Cyberpunk, Alan Wake 2): MFG liberado de primeira, startup sem travadas | log mostra o patch antes do primeiro `SetOptions` | 30 min | feito (campo, 2026-10-25) |

### Release 1.5.7

| # | Tarefa | Est. | Status |
|---|---|---|---|
| R7.1 | Gate completo no build final | 1 h | feito (2026-10-26) |
| R7.2 | **[você]** Regressão rápida XeMFG + DLSS-G | 30 min | feito (coberta pelos campos do 4.x e do 5.5) |
| R7.3 | Versão, notas, merge, tag e release | 45 min | feito (2026-10-26) |

---

## Depois: bloco 6, refatoração e fixture do DLSS-G

Um PR por item.

| # | Tarefa | Est. | Status |
|---|---|---|---|
| 6.1 | Fixture com Streamline falso para o DLSS-G (`SetOptions`, `GetState`, Reflex) | 3 h | feito |
| 6.2 | Extrair `provider_patches` (gates, midpoint, thin geometry) do `addon.cpp` | 2 h | feito (`addon_provider.inl`) |
| 6.3 | Extrair `streamline_patches` (teto, flip metering) | 1,5 h | feito (`addon_streamline.inl`) |
| 6.4 | Extrair `discovery` (worker, varreduras) | 1,5 h | feito (`addon_discovery.inl`; as varreduras já estavam em `module_scan.hpp` desde o 5.1) |
| 6.5 | Extrair `latency_guard` e publicar um snapshot único para a UI | 3 h | feito (`addon_latency.inl`, `LatencyGuardView`) |
| 6.6 | Extrair o overlay (UI) | 3 h | feito (`addon_overlay.inl`) |
| 6.7 | Limpezas: detecção de API duplicada, comentário repetido, atribuição redundante, `IsBadReadPtr` | 1 h | feito (`IsBadReadPtr` já tinha saído no 5.1) |

## Fora do plano (registrado para não perder)

- XeMFG: associação de `slSetTag` sem token pelo marker `RenderSubmitStart` (pode cair no frame anterior).
- XeMFG: corrida de dados leve em `s.invert`/`s.jitter`; `Snapshot()` e `flush()` por Present.
- DLSS-G: `DLSSGOptions` acima da v5 desliga o override (ler pelo prefixo, como o XeMFG).
- DLSS-G: pesquisa de qualidade de imagem (cercas, retículo do HUD). Exige instrumentação por estágio antes de qualquer heurística nova.

## Registro de sessões

(Uma linha por item concluído: data, item, commit, resultado do gate e observações.)

- **2026-10-24 · 0.1** Build Release incremental da 1.5.5 na árvore `build/tests` já configurada (sem erros nem avisos). Matriz `-LE backlog`: **175/175**, opcionais de sempre pulados (`provider_identity`, `engineering_manifest`, `release_1_4_5_manifest`), replay incluído, ~16 min.
- **2026-10-24 · 1.1** (`8a371fe`) A correção da 1.5.3 não tinha teste automático; o caso `xemfg_conformance_reserved-state` cobre a substituição no mesmo recurso e a herança num recurso recriado no mesmo endereço. Falhou como esperado: o recurso novo herdou o estado 64 do liberado. **O risco era real**, não só teórico.
- **2026-10-24 · 1.2** (`4c06b10`) Estado guardado como private data no próprio recurso; mapa removido. Ajustes no cenário: quadros 10–13 (o quadro 9 tem a verificação de UI composition) e lotes de recursos candidatos para garantir a reutilização do endereço (o heap entrega blocos livres em ordem aleatória; uma tentativa só falhou 1 vez em 5 na matriz). Com a correção: 25/25. Sem ela: 5/5 falham pelo motivo certo. Matriz **176/176**.
- **2026-10-24 · 1.3** Witcher 3 (5.0.15.65482) com o build `4c06b10`, path tracing ligado e desligado várias vezes. Nenhum crash, nenhum `native_device_failure`/`native_present_failure`. ~4.000 tags com o estado reservado, todas trocadas pelo último estado válido do mesmo recurso (1024 = `COPY_DEST`), inclusive em intervalos com o FG ativo; o FG voltou a interpolar a 4x depois das trocas. A primeira abertura foi recusada por `unreviewed_Intel_runtime_pair` (`libxess_fg.dll` 1.2.2.118 na pasta do jogo), sem relação com esta mudança; a segunda abertura rodou normalmente. **Bloco 1 concluído.**
- **2026-10-25 · ambiente** Desde esta sessão, três casos da rota sintetizada (`xemfg_conformance_ffx-adapter`, `dlss-sr-adapter`, `ffx-with-reflex`) falham sempre com um frame gerado descartado (`presented=3`, esperado 4). Reproduz com o código de antes do 2.1, então não é regressão destes itens. Na máquina estavam abertos o NVIDIA Overlay e a edição do plano de energia (plano ativo: Equilibrado). **Precisa ficar verde antes da release (R6.1).** As branches dos blocos 2 e 3 ficaram empilhadas sobre a do bloco 1, porque os três vão juntos na 1.5.6 e o plano é versionado na primeira; os PRs entram em ordem.
- **2026-10-25 · 2.1** (`8cdae1d`) `ngx_param_vtable.hpp` com `Patch`/`Restore`; teste com vtable falsa numa página somente-leitura. Matriz 176/177 (a falha ambiental acima).
- **2026-10-25 · 2.2** (`0801e59`) Trava SRW, `original` atômico publicado antes do slot e recusa de adotar o próprio hook (`kForeignHook`). Teste de 8 threads × 400 rodadas: sem a trava todas as execuções falham (uma com segmentation fault, de outra thread escrevendo depois que a página voltou a somente-leitura); com a trava, 10/10.
- **2026-10-25 · 2.3–2.5** (`ba963b6`, `2896244`, `d9466ed`) Um gate só para os três. 2.5: depois de uma recusa, a chamada seguinte faz 1 chamada ao Streamline; desligar o FG no jogo, mudar o multiplicador e a transição de swapchain liberam nova tentativa. Sem a ligação, o teste falha (3 chamadas). Matriz 174/177 (as três ambientais).
- **2026-10-25 · 3.1** (`6ecd5b8`) O cenário mostrou **dois** problemas na rota Reflex com token constante: as esperas do jogo param de chegar ao Reflex nativo **e** nenhum frame é admitido, porque a associação pela fronteira da simulação só existia na rota XeLL.
- **2026-10-25 · 3.2** (`1c693f9`) **Escopo maior que o planejado**, para o Plague Tale interpolar na rota Reflex: o Reflex nativo é chamado primeiro, com o token e a thread do jogo, e o jogo recebe o resultado dele; a associação com o XeLL (só ordena markers, sem pacing nessa rota) segue a fronteira da simulação como na rota XeLL. O adaptador FSR na rota Reflex deixa de falhar na associação, mas continua sem gerar frames: a rota Reflex exige que o jogo ligue o Reflex, e um jogo só com FSR não liga (verificado no R6.3: `Waiting for compatible low-latency settings from the game`). Três testes foram ajustados ao contrato novo (espera duplicada chega ao nativo; espera nativa durante a aposentadoria devolve o resultado nativo sem associação; `SleepSourceFrame` sem espera do jogo associa sem chamar o nativo). Matriz 176/179 (as três ambientais).
- **2026-10-25 · 3.3** (`f6059c4`) Log do SDK XeFG agrupado por padrão, com `occurrence`. Matriz como no 3.2.
- **2026-10-25 · 3.4** Build `f6059c4` instalado no Plague Tale (versão Xbox, `C:\XboxGames\Resonance- A Plague Tale Legacy\Content`) com backup (`renodx-mfgunlock.addon64.1.5.5.bak`, `MFGAdaUnlock.ini.1.5.5.bak`); `[Latency] Output=Reflex`. Log com 2.092 linhas antes do teste.
- **2026-10-25 · 3.4** Plague Tale com o build `f6059c4` e saída Reflex: `native_latency_binding` Reflex; em todos os resumos de 10 s, FG ativo (`ready`, 0 frames sem FG, 3 frames por Present no multiplicador 3x). O jogo continua mandando token 0; os frames são associados pela fronteira da simulação (`native_simulation_boundary_sleep`, resultado 0) e o contador `sleeps_rejected` conta as associações recusadas (o Reflex nativo recebe todas as esperas). Nenhuma perda de device, falha de Present ou erro do XeLL. INI devolvido a `Output=Auto`; o build novo continua instalado. **Blocos 1 a 3 concluídos**; falta o gate de release (R6.1), que depende dos três casos ambientais.
- **2026-10-25 · ambiente resolvido** Com o plano de energia Alto desempenho, os três casos da rota sintetizada passam 3/3 cada: no Equilibrado a CPU segura o ritmo e esses testes curtos perdem um frame gerado. O plano voltou ao Equilibrado depois do gate. **Para os próximos gates, rodar a matriz com Alto desempenho.**
- **2026-10-25 · R6.1** Build do `d720c33` (código igual ao `f6059c4`), Alto desempenho: matriz **179/179**, backlog vazio, opcionais de sempre pulados.
- **2026-10-25 · R6.2** Build instalado no CONTROL Resonant (XeMFG) e no Cyberpunk 2077 com `[Backend] Selected=DLSSG` temporário; backups `*.before-1.5.6.bak` do addon e do INI.
- **2026-10-25 · R6.2** CONTROL Resonant (XeMFG): FG a 4x, nenhum frame sem geração enquanto o jogo pedia FG. Cyberpunk 2077 (DLSS-G): gates e kernels no provider 310.9.1, override de 5x para 6x aceito, fechamento normal. Cyberpunk devolvido a `Selected=XeMFG`.
- **2026-10-25 · disco** `native-checks`/`xemfg-checks` tinham 6.800 execuções e 238 GB; disco em 98%. Apagadas as anteriores a hoje e podados os binários das execuções de hoje que passaram (285 GB livres). O host OptiScaler dos testes foi movido de dentro de uma pasta de evidência para `build/xemfg-offline-reference/optiscaler/`. `tests/evidence.py` (`e03b5e1`): execução que passa fica só com logs.
- **2026-10-25 · flutuação dos testes FSR/DLSS-SR** Os casos `ffx-adapter`, `dlss-sr-adapter` e `ffx-with-reflex` exigiam exatamente 4 frames exibidos no último Present; com a janela de teste coberta ou sem foco, o DWM descarta um gerado (`presented=3 generated=3`). Passaram logo depois de reiniciar o PC e falharam uma hora depois com o mesmo binário. Com aprovação do usuário, passaram a aceitar `presented >= multiplicador-1`, como a verificação do Streamline já fazia (`28066e3`). Três testes do OptiScaler que falharam depois do reinício forçado das 15:20 voltaram a passar depois de um reinício normal.
- **2026-10-25 · R6.3** Gate final do build 1.5.6 (Alto desempenho): **179/179**, backlog vazio. Addon `cdbb2686…`. Versão 1.5.6 nos 9 pontos, notas em `docs/releases/1.5.6.md`, publicada como release 1.5.6. **Release 1.5.6 concluída**; próximo: bloco 4 (1.5.7).
- **2026-10-25 · bloco 4** Branch `feature/xemfg-hud-pacing` a partir da `fork/main` (1.5.6). Gates com Alto desempenho.
- **2026-10-25 · 4.2** (`5e52fcb`) `CopyableHudFormat` (TYPELESS/UNORM/UNORM_SRGB da mesma família) e `HudCopy`: tag volátil copiado e tagueado na lista do produtor; tag válido até o Present copiado na lista própria no Present. Casos `hud-srgb` e `hud-typeless`; sem a cópia, os dois falham. Matriz 181/181.
- **2026-10-25 · 4.3 descartado** A UI do Mafia é `B8G8R8A8_TYPELESS` sobre `R10G10B10A2_UNORM`, e o jogo provavelmente roda em HDR10 (o usuário usa shaders HDR do Lilium). Converter exigiria conhecer a codificação da UI (sRGB, brilho de papel branco), que o jogo não informa; o guia da Intel exige UI já em HDR10 no mesmo formato e avisa que o alpha de 2 bits pode piscar. Uma conversão às cegas deixaria o HUD dos frames gerados com cor/brilho errados. Pode ser retomado se houver um jogo SDR de 10 bits com UI em 8 bits.
- **2026-10-25 · 4.6** (`767f5b3`) `display_cap.hpp`: limite = refresh × 0,97 ÷ multiplicador, combinado com o limite do jogo (o mais restrito vence), e o teste do aviso.
- **2026-10-25 · 4.7** (`ddc9f18`) `[XeFG] DisplayCap` (desligado, só saída XeLL), refresh lido a cada 2 s, intervalo entre Presents suavizado, opção e aviso no painel. Caso `display-cap`: intervalo do XeLL igual ao limite (17.183 µs a 240 Hz e 4x) e 0 ao desligar.
- **2026-10-25 · 4.5** (`fb92008`) Painel lista HUD-less e UI separados (recebido, convertido, filtrado, ausente) e sugere o UI Composition quando há HUD-less. Matriz 183/183 (4.5 e 4.7 juntos).
- **2026-10-25 · 4.4** (`8bacb9e`) HUD-less FP16 de jogo scRGB convertido pelo encoder scRGB→HDR10; caso `scrgb-hudless`; sem a conversão, falha. Matriz 184/184.
- **2026-10-25 · campo do bloco 4** Candidato `8bacb9e` instalado no Plague Tale, no KCD2 e no Cyberpunk, com backup `*.before-1.5.7.bak` do addon e do INI. O 4.1 (linha de base) e o 4.9 (limite ligado/desligado) viram uma sessão por jogo com o candidato: o pacing só muda com o limite ligado, então a captura com ele desligado serve de linha de base.
- **2026-10-25 · 4.1/4.9, Plague Tale (3x, 240 Hz)** Limite desligado: jogo 84,7 FPS, 262 FPS exibidos (acima do monitor), p95/p50 1,30, latência p50/p99 19,3/26,8 ms. **Primeira tentativa com o limite ligado revelou um erro**: o XeLL ligado ao XeFG aplica o intervalo mínimo a cada frame exibido (gerados incluídos), e o cálculo dividia pelo multiplicador; o jogo caiu para 25 FPS e a latência subiu para 53 ms. Corrigido (`0caf3dd`): intervalo = 1 / (refresh × 0,97). Com a correção: jogo 75,2 FPS, 233 FPS exibidos, p95/p50 1,17, latência 18,5/23,3 ms. O aviso apareceu antes de ligar; o usuário sentiu o movimento mais suave.
- **2026-10-25 · 4.1/4.9, KCD2 (3x, adaptador DLSS-SR)** Desligado: jogo 80,8 FPS, 251 exibidos, p95/p50 1,45, 21 picos, latência 24,3/32,8 ms. Ligado: jogo 73,4 FPS, 231 exibidos, p95/p50 1,50, 29 picos, latência 22,5/31,0 ms. Regularidade medida igual ou um pouco pior (rota DLSS-SR já irregular; mundo aberto varia entre capturas), latência melhor; o usuário sentiu melhora. O aviso apareceu.
- **2026-10-25 · 4.8, Cyberpunk (4x)** HUD-less `R8G8B8A8_TYPELESS` convertido (`copied: true`, caminho volátil na lista do produtor); UI Composition aplicado com ele (`enabled: 1`, resultado 0); FG a 4x sem erros. O usuário preferiu sem o UI Composition (com ele o HUD fica na taxa real, não interpolado), o que confirma o padrão desligado.
- **2026-10-25 · bloco 4 concluído** Candidato `0caf3dd` instalado no Plague Tale (com `DisplayCap=1`), no KCD2 e no Cyberpunk; backups `*.before-1.5.7.bak`. Gate final do bloco: 184/184 (Alto desempenho). Próximo: bloco 5 (DLSS-G, startup e detecção) e depois a release 1.5.7.
- **2026-10-25 · bloco 5** Branch `perf/dlssg-startup`, empilhada sobre a do bloco 4 (vão juntos na 1.5.7).
- **2026-10-25 · 5.1** (`5c125c7`) `module_scan.hpp`: busca Boyer-Moore-Horspool nas seções legíveis e registro das imagens já buscadas (endereço + timestamp + tamanho). O teto do plugin e o flip metering legado pulam módulos já buscados sem o marcador; o flip metering só perfila módulos com o marcador. Achado no caminho: `ProfileFor` revalida hasheando o código inteiro e as sessões têm teto de 64, então o flip metering legado podia nem chegar ao `sl.dlss_g` em jogos com muitos módulos. Matriz 185/185.
- **2026-10-25 · 5.2 mudou** Mover a manutenção de `OnInitDevice`/`OnInitCommandQueue` para o worker quebraria uma regra do código: o worker só começa no primeiro Present (para não prender instâncias temporárias do ReShade), e essa manutenção é uma das chances de aplicar os gates antes da consulta do NGX. Ficou síncrona e passou a registrar quanto tempo levou nas 6 primeiras vezes (`dd79354`), para o campo confirmar que o 5.1 a deixou barata.
- **2026-10-25 · 5.3** (`9eae117`) `loader_notification_probe`: a notificação dispara para DLL carregada por `LdrLoadDll`, antes do DllMain; patch feito no callback é visto pelo DllMain; relocações já aplicadas; identidade de código (`modulesession::Inspect`) igual no callback e depois da carga.
- **2026-10-25 · 5.4** (`e4c7c06`) `LdrRegisterDllNotification` no loadhook: o provider é tratado ao ser mapeado (só os gates, sob o loader lock; logs enfileirados porque o log do ReShade tem trava própria). Hooks de `kernel32` mantidos; `Uninstall` desregistra. `loader_notification_dlssg_tests`. Matriz 187/187.
- **2026-10-25 · 5.5** Candidato `dd79354` instalado no Cyberpunk e no Alan Wake 2 com `[Backend] Selected=DLSSG` temporário; backups `*.before-5.5.bak`.
- **2026-10-25 · 5.5 e 5.4 descartado** Alan Wake 2 (DLSS-G): funcionou; override 4x → 6x aceito. Cyberpunk (DLSS-G): **crash no início**, `EXCEPTION_ACCESS_VIOLATION` dentro do plugin `sl.dlss_g` OTA (`190_E658703.dll+0x4f765`), no thread principal, logo depois do primeiro `slDLSSGGetState`, lendo um endereço fora de qualquer módulo. O log mostrava os gates aplicados pela notificação, antes do DllMain do `nvngx_dlssg`. Com o 5.4 revertido (`f29f3d1`), o Cyberpunk em DLSS-G funcionou (override 6x → 4x aceito, fechamento normal). Hipótese: o `nvngx_dlssg` confere o próprio código na inicialização e o patch antes do DllMain muda esse resultado; a sonda (DLL simples) não tinha como reproduzir isso. A sonda do 5.3 continua no repositório como conhecimento.
- **2026-10-25 · medição do 5.2** No Cyberpunk, a manutenção na criação do device levou 1.677 ms e algumas criações de fila 332–698 ms (thread do jogo, no carregamento). Não causou o crash; fica para investigar depois (provável custo dos patches e hashes de módulo, não da busca).
- **2026-10-25 · flicker do painel** Vídeo do usuário (Cyberpunk, XeMFG 4x): as linhas *HUD handling* e a dica do UI Composition alternavam a cada quadro com *Waiting for frame status*, porque o painel só tenta a trava da saída e mostrava valores padrão quando ela estava ocupada. Corrigido mostrando o último estado lido da mesma saída (`4a8f03e`). Confirmado no jogo; matriz 186/186.
- **2026-10-26 · release 1.5.7** Blocos 4 e 5 (sem o 4.3 e o 5.4), mais a correção do flicker. Versão 1.5.7, notas em `docs/releases/1.5.7.md`.
- **2026-10-26 · bloco 6** Fixture do DLSS-G (`dlssg_streamline_fixture`) e extração do `addon.cpp` (8.120 → ~1.300 linhas) em `addon_provider.inl`, `addon_streamline.inl`, `addon_discovery.inl`, `addon_latency.inl` e `addon_overlay.inl`, fragmentos incluídos no mesmo lugar e no mesmo namespace (todos usam os ~80 atômicos de configuração). Cada movimentação conferida com `.text`/`.data`/`.pdata` idênticos byte a byte ao build anterior. Mudanças de código só no 6.5 (`LatencyGuardView`, snapshot único para o painel e o relatório) e no 6.7 (limpezas). Gate 193/193 (3 skipped de proveniência).
- **2026-10-26 · campo do bloco 6** DLSS-G em quatro engines: Cyberpunk 2077 (REDengine 4: gates, wrappers, pedidos 6x/4x do jogo aceitos), Alan Wake 2 (Northlight: override 4x → 6x aceito), Resonance: A Plague Tale Legacy (Zouna: teto 6x, override 6x → 3x aceito) e Dragon's Dogma 2 (RE Engine: funcionou; o ReShade desse jogo está com log desligado). Monster Hunter Wilds não está instalado. Os quatro voltaram para XeMFG; o build novo ficou instalado e os anteriores como `*.before-block6.bak`.

