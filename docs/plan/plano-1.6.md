# Plano 1.6 — Latência com Reflex e presets do DLSS

Pedidos:

- **XeMFG:** melhorar a latência usando Reflex ("mouse mais leve").
- **DLSS-G:** trocar o preset do DLSS durante o jogo.

O overlay de estatísticas saiu deste plano (2026-10-26).

Status: `pendente`, `em andamento`, `feito`, `descartado`. **[você]** marca o que precisa de você (jogo, painel, decisão).

---

## Análise

### O que já existe

| Peça | Onde | Serve para |
|---|---|---|
| Rota de saída Reflex no XeMFG (`[Latency] Output=Reflex`) | `native_latency`, `reflex_profile` | o Reflex do jogo controla o ritmo; exige reiniciar |
| Override de modo do Reflex (On / Boost) | DLSS-G (`ReflexModeOverride`) | modelo para o mesmo controle no XeMFG |
| Medição externa com PresentMon (latência de exibição p50/p99) | `mfgdiagnostics/Capture-Performance.ps1` | medir antes e depois de cada mudança de latência |
| Limite por refresh da tela (`[XeFG] DisplayCap`, 1.5.7) | `display_cap.hpp` | limite da fonte, que reduz a fila |
| Wrapper do `slDLSSGSetOptions` pelo `slGetFeatureFunction` | `framecount.hpp` | modelo para envolver o `slDLSSSetOptions` |
| Leitura do preset forçado pelo driver (NVIDIA App) | `nvapi_status.hpp` (`kNgxOverridePreset`) | mostrar se o driver está sobrepondo o preset |
| Fixture do Streamline para o DLSS-G (`dlssg_streamline_fixture`, bloco 6) | `tests/` | testar o wrapper de presets sem jogo |

### Pontos técnicos e riscos

1. **Latência no XeMFG ("mouse mais leve").** Toda geração de frames atrasa a imagem real em cerca de um frame da fonte. As alavancas que sobram são estas, e cada uma só entra se a medição mostrar ganho:
   - a rota **Reflex** no lugar da XeLL, em jogos com Reflex (o Reflex do driver conhece a fila da GPU da NVIDIA);
   - o **Boost** do Reflex (mantém o clock da GPU alto e reduz a fila quando a GPU é o gargalo);
   - **trocar de rota sem reiniciar** (hoje a troca XeLL/Reflex exige reiniciar o jogo);
   - **profundidade da fila** da swapchain da Intel (latência máxima de frames e contagem de buffers);
   - **Reflex injetado pelo NVAPI** em jogos **sem Reflex** (rotas FSR e DLSS-SR), como fazem RTSS e Special K: experimental e opcional.
2. **Presets do DLSS durante o jogo.**
   - **Jogos com Streamline:** o jogo chama `slDLSSSetOptions` com os presets de cada modo (`dlaaPreset`, `qualityPreset`… J, K, L, M). O addon pode envolver essa função como já faz com o `slDLSSGSetOptions`, trocando o preset do modo ativo. Falta **confirmar** se o Streamline recria a feature do DLSS ao trocar o preset no meio do jogo e quanto isso trava (0.2, no Streamline_Sample que já temos compilado).
   - **Jogos só com NGX** (sem Streamline): o preset vai na criação da feature (`NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_*`). A troca vale na próxima recriação, quando o jogo muda resolução ou qualidade, e não na hora.
   - **Override do driver** (NVIDIA App): se estiver ativo, ele vence. O painel precisa mostrar isso, e a leitura já existe.
   - Presets do **DLSS-RR** (ray reconstruction, `sl_dlss_d.h`) seguem o mesmo modelo e ficam como item opcional.
   - O XeMFG com o adaptador DLSS-SR passa pelos mesmos hooks do NGX, então o seletor pode valer lá também (opcional).

---

## Bloco 0 — Verificações antes de codar

| # | Tarefa | Resultado esperado | Est. | Status |
|---|---|---|---|---|
| 0.1 | **[você]** Medição de base da latência no XeMFG: rota XeLL × rota Reflex, em 2 jogos com Reflex (ex.: Cyberpunk, Plague Tale), mesma cena, PresentMon 30 s | latência de exibição p50/p99 e FPS de cada rota | 1 h | feito |
| 0.2 | Protótipo descartável no Streamline_Sample: trocar `qualityPreset` no `slDLSSSetOptions` durante a execução | o Streamline recria a feature? quanto trava? | 1,5 h | feito (pelo código-fonte) |
| 0.3 | Levantar quais jogos instalados usam DLSS-SR via Streamline e quais via NGX direto | lista para os testes de campo do bloco B | 30 min | feito |

---

## Bloco A — Latência do XeMFG com Reflex

Cada item só entra se a medição do 0.1 (ou a sua própria) mostrar ganho. Mudança de apresentação continua opcional: o painel sugere, nunca liga sozinho.

| # | Tarefa | Est. | Status |
|---|---|---|---|
| A.1 | Painel: recomendar a rota Reflex quando o jogo tem Reflex e a XeLL está ativa, com o ganho medido no 0.1 | 1 h | descartado (medição 0.1) |
| A.2 | Modo do Reflex na rota Reflex do XeMFG: On / Boost (como o `ReflexModeOverride` do DLSS-G) | 2 h | descartado (medição 0.1) |
| A.3 | Trocar XeLL ⇄ Reflex sem reiniciar: o contexto da XeLL e a ligação de latência da XeFG mudam na próxima recriação da saída ou num ponto seguro entre frames | 3 h | descartado (medição 0.1) |
| A.4 | Profundidade da fila: revisar a latência máxima de frames da swapchain da Intel e a contagem de buffers; medir com PresentMon | 2 h | descartado (sem fila a cortar) |
| A.5 | *(Experimental, opcional)* Reflex injetado pelo NVAPI (`SetSleepMode`/`Sleep` e markers) em jogos sem Reflex (rotas FSR e DLSS-SR) | 4 h | fora (opcional, não pedido) |
| A.6 | Conformidade dos itens que entrarem | 2 h | não se aplica |
| A.7 | **[você]** Campo: mesmas cenas do 0.1, antes e depois, com PresentMon; e a sensação do mouse | 1 h | não se aplica |

---

## Bloco B — Presets do DLSS durante o jogo (DLSS-G)

| # | Tarefa | Est. | Status |
|---|---|---|---|
| B.1 | Wrapper do `slDLSSSetOptions` pelo `slGetFeatureFunction(kFeatureDLSS)`, no mesmo esquema do `slDLSSGSetOptions`. Troca o preset do modo ativo e não toca nada mais | 2 h | feito |
| B.2 | Painel: seletor "Preset do DLSS" (Padrão do jogo, J, K, L, M), aplicado na hora; mostra o preset ativo, e avisa quando o override do driver está vencendo | 1,5 h | feito |
| B.3 | Jogos só com NGX: dica de preset na criação da feature, com o aviso "vale na próxima troca de resolução ou ao reiniciar" | 2 h | feito |
| B.4 | Proteções: estrutura de opções desconhecida passa intacta; um preset recusado volta ao do jogo (mesma lógica do override de multiplicador) | 1 h | feito |
| B.5 | Fixture: `slDLSSSetOptions` no Streamline falso (preset trocado, modo e resolução intactos, estrutura nova intacta) | 1,5 h | feito |
| B.6 | *(Opcional)* Presets do DLSS-RR | 2 h | feito (pedido no campo) |
| B.7 | *(Opcional)* O mesmo seletor no XeMFG com o adaptador DLSS-SR | 1,5 h | pendente |
| B.8 | **[você]** Campo: um jogo com Streamline (ex.: Cyberpunk) trocando J/K/L/M durante o jogo; um jogo só com NGX | 30 min | feito (KCD2, DD2, Cyberpunk RR) |

---

## Release

Proposta: duas versões, para não segurar o que ficar pronto antes.

| Versão | Conteúdo | Condição |
|---|---|---|
| 1.6.0 | Bloco B (presets) | gate completo, campo do bloco B |
| 1.6.1 | Bloco A (latência), conforme as medições | gate completo, medição antes e depois |

Cada release segue o processo de sempre: versão em todos os pontos (incluindo `addon_overlay.inl` e a versão numérica do recurso), notas, `fork/main` e `develop` adiantados, tag e release com o addon.

**Estimativa total:** cerca de 26 h no meu lado sem os opcionais (0: 2 h, A: 14 h, B: 9,5 h), mais uns 3 h de campo seus.

---

## Decisões em aberto

1. **"Mouse mais leve":** em quais jogos você sente o mouse pesado no XeMFG? E em qual rota (XeLL ou Reflex)? Essa é a base da medição 0.1.
2. **Presets:** só DLSS-SR, ou também DLSS-RR? Expor só J/K/L/M (os presets atuais) ou também os antigos E/F? Salvar por jogo?
3. **Reflex injetado (A.5):** vale o risco de um recurso experimental para jogos sem Reflex?
4. **Releases:** duas versões (1.6.0 e 1.6.1) ou uma só?

## Registro

- **2026-10-26 · análise** Levantamento do código existente e das APIs (XeLL, Streamline `DLSSOptions` com presets J–M, NVAPI). Nenhum código escrito.
- **2026-10-26 · escopo** Overlay de estatísticas retirado do plano a pedido.
- **2026-10-26 · 0.2** Respondido pelo código-fonte do `sl.dlss` nas quatro versões que temos (2.7.32, 2.9.0, 2.11.1, 2.14.16): `slEvaluateFeature` compara modo, tamanho e os cinco presets com os do frame anterior e, se mudaram, recria a feature (libera a antiga 3 frames depois e reinicia o histórico). O comentário do Streamline_Sample ("Changing presets requires a restart of DLSS", com `slFreeResources`) está desatualizado. Consequência: o wrapper do B.1 só troca os campos de preset; a recriação é do Streamline, com um frame de histórico reiniciado.
- **2026-10-26 · 0.3** Streamline: Assassin's Creed Black Flag Resynced (2.11.1), CONTROL Resonant (2.12.130), Cyberpunk 2077 (2.14.1), Dragon's Dogma 2 (2.12.0), Elden Ring (2.14.16), Monster Hunter Wilds (2.14.1, não instalado de fato), Onimusha (2.14.1), Pragmata (2.8.0), RE4 (2.12.0), RE Requiem (2.8.0), The Great Circle (2.7.32), The Witcher 3 (2.14.1), Alan Wake 2 (2.7.0), Plague Tale (2.11.1). NGX direto: Kingdom Come: Deliverance II e Minecraft.
- **2026-10-26 · decisões assumidas** Medição em Cyberpunk e Plague Tale; presets só do DLSS-SR (J/K/L/M e Padrão do jogo), salvos por jogo no `ReShade.ini` de cada um; A.5 (Reflex injetado) fica de fora salvo pedido.
- **2026-10-26 · B.1–B.5** `dlss_preset.hpp` (lógica pura) e o wrapper do `slDLSSSetOptions` no `framecount.hpp`: com um preset escolhido, encaminha uma cópia das opções do jogo com o preset de todos os modos trocado; copia só os bytes da versão que o jogo declara (presets desde a v1), deixa intacta uma versão desconhecida, não mexe com o DLSS desligado e volta às opções do jogo se o Streamline recusar. Painel: "DLSS Super Resolution preset" na aba Quality (Game default, J, K, L, M), aplicado na hora, salvo em `DlssSuperResolutionPreset` no `ReShade.ini` do jogo, com aviso quando o override da NVIDIA App vence. B.3: em jogos que criam o DLSS pelo NGX sem Streamline, o preset vai como dica de criação no `NVSDK_NGX_D3D12_CreateFeature` (vale na próxima recriação); os valores do jogo são guardados e restaurados ao voltar para "Game default". Esse hook tem instalador próprio e só é instalado com um preset escolhido: o `TryInstallHooks` antigo do NGX nunca é chamado (o override de parâmetros dele é código morto). Testes: `dlss_preset_tests` e a fixture DLSS-G (preset K nos seis modos; estrutura do jogo intacta; DLSS desligado; struct v1 encostada numa página sem acesso; versão desconhecida; dica do NGX para o DLSS e nenhuma mudança em outra feature).
- **2026-10-26 · ambiente** A tela estava em 1024x768 a 60 Hz (monitor desligado ou desconectado) durante a matriz: os casos `exclusive-transition` e `exclusive-first` falham nesse estado (Present inválido em fullscreen). Repetir com o monitor ligado.
- **2026-10-26 · bloco A, preparação** Rota Reflex hoje: o `ReflexOptionsHook` repassa as opções do jogo intactas e o `SleepHook` chama o `slReflexSleep` nativo com o token do jogo; na rota XeLL o Reflex nativo fica desligado. O guia da Intel diz que redução de latência que não seja a XeLL "não é suportada e pode causar comportamento inesperado": a rota Reflex funciona pelo seletor externo verificado por perfil (`reflex_profile`), então cada mudança nela precisa de medição. Para o A.4, a XeFG informa os frames na fila de apresentação em `xefgSwapChainGetLastPresentStatus`. Desenho do A.2 (Boost): repassar uma cópia das opções do jogo com o modo trocado e reaplicar as últimas opções do jogo na thread dona do `slReflexSetOptions` (o mesmo cuidado do DLSS-G). O A.3 depende de uma recriação da saída (a ligação XeLL/Reflex é feita antes do init da XeFG), então valeria na próxima troca de resolução ou de modo de tela do jogo. Aguardando a medição 0.1 para decidir os itens do bloco A.
- **2026-10-26 · 0.1** PresentMon, 30 s, mesma cena, XeMFG, build 1.6 (bloco B). Cyberpunk 2077 (4x, ~40 FPS reais, GPU ~17 ms por frame real): XeLL 158,9 FPS mostrados, latência de exibição p50/p95/p99 56,6/68,5/70,8 ms; Reflex 160,4 FPS, 56,4/66,9/68,0 ms (empate na mediana). O jogo não deixa escolher Reflex + Boost com o Frame Generation ligado. Resonance: A Plague Tale Legacy (3x, ~87 FPS reais, limite por refresh desligado nas duas): XeLL 260 FPS, 20,5/26,2/27,8 ms; Reflex 267 FPS, 25,0/29,9/31,8 ms (Reflex 4,5 ms pior na mediana). Conclusão: a XeLL já elimina a fila da GPU; a rota Reflex empata ou piora. A latência que sobra é de cerca de 2 frames reais (render + o frame que a geração segura + exibição), e o que a reduz é mais FPS real. A.1 e A.3 descartados. INIs devolvidos (Plague Tale com o limite por refresh ligado, como estava).
- **2026-10-26 · B.6 e preset de FG** A pedido, presets do Ray Reconstruction: wrapper do `slDLSSDSetOptions` e linha "DLSS Ray Reconstruction preset" (Game default, D, E) na aba Quality, salva em `DlssRayReconstructionPreset`. Os presets do RR só existem na versão 3 do `DLSSDOptions`, e o `sl.dlss_d` só os compara (e recria a feature) para um chamador v3; v1/v2 passam intactos e o painel avisa. **Frame Generation não tem preset em nenhuma API pública** (nem no `DLSSGOptions` do Streamline, nem no SDK do NGX): o "preset de FG" mostrado no painel é o override do driver (NVIDIA App), gravado no perfil do jogo e aplicado ao abrir o jogo. Trocar durante o jogo não é possível por API pública.
- **2026-10-26 · bloco A, fechamento** A.2 (Boost) só existiria na rota Reflex, que empatou ou piorou na medição, e os jogos travam o modo do Reflex com o Frame Generation ligado (Cyberpunk). A.4: a XeFG não expõe a profundidade da fila (`xefgSwapChainGetLastPresentStatus.framesPresented` é o número de frames enviados naquela chamada); e no Cyberpunk, limitado pela GPU, a XeLL empatou com o Reflex, o que indica que não há fila sobrando. O bloco A termina sem mudança de código: a latência restante é a da própria geração (cerca de 2 frames reais), e o que deixa o mouse mais leve é subir o FPS real (preset M em Performance, modo de DLSS mais leve, multiplicador menor) e manter o limite por refresh ligado.
- **2026-10-26 · KCD2 (B.3/B.8)** Primeira tentativa: o KCD2 cria o DLSS uma vez e não recria ao trocar o modo de qualidade. Segunda: o `_nvngx.dll` carrega por uma importação que não passa pelo hook do `LoadLibrary`, então o preset salvo não foi instalado na abertura (`f34a193`). Correção: retentativa no Present a cada 250 ms enquanto houver preset escolhido e o hook faltar (`876b92b`). Build instalado no KCD2 com o preset J salvo; falta abrir o jogo e conferir o indicador da NVIDIA.
- **2026-10-27 · campo B.8** Super Resolution trocado durante o jogo no Dragon's Dogma 2 (Streamline) e aplicado na abertura no Kingdom Come: Deliverance II (NGX, com a retentativa no Present), conferido com o indicador da NVIDIA. Ray Reconstruction E/D/F no Cyberpunk com path tracing. Preset F do RR incluído a pedido (modelo do DLSS 4.5 oferecido pela NVIDIA App).
- **2026-10-27 · release 1.6.0** Bloco B (presets de SR e RR e caminho NGX), bloco 6 (reorganização do DLSS-G, fixture e `LatencyGuardView`) e a medição do bloco A (sem mudança de código). Gate 194/194 (3 skipped de proveniência). Versão 1.6.0 (recurso numérico 1,6,0,0), notas em `docs/releases/1.6.0.md`, chaves novas no README. Fora desta versão: adaptador DLSS-SR para o DLSS-G (protótipo proposto; plano próprio).

