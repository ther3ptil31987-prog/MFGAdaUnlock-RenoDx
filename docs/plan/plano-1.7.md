# Plano 1.7 — DLSS-G no painel, Reflex, adaptador SR→G, RTX 50; XeMFG em mais APIs

Pedidos:

- **DLSS-G:**
  1. tirar o "Force Borderless" do painel;
  2. Reflex **Off** e **On + Boost** parecem não funcionar;
  3. adaptador **DLSS-SR → DLSS-G** (gerar frames em jogos que têm só DLSS-SR);
  4. aplicar as correções de qualidade na **série RTX 50** (pedido de um usuário com 5070 Ti, log anexo).
- **XeMFG:**
  5. reduzir o **flicker da UI** com UI Composition desligada;
  6. suporte a **Vulkan**;
  7. avaliar o **pacing do driver da NVIDIA**.
- **Análise:** o projeto [`dlssg_for_sm86`](https://github.com/sdli1995/dlssg_for_sm86). Ele daria Vulkan? Quais os benefícios? Vale a pena?

Status: `pendente`, `em andamento`, `feito`, `descartado`. **[você]** marca o que precisa de você (jogo, painel, decisão).

---

## Análise

### Log do usuário RTX 5070 Ti (Witcher 3, driver 617.42)

| Achado | Consequência |
|---|---|
| O addon é a **1.4.4** (`Release 1.4.4; DLSS-G 310.9.16 …`) | O usuário está três versões atrás. Tudo o que a 1.5/1.6 trouxe falta nesse log. Pedir um log com a 1.6.0 antes de concluir qualquer coisa |
| `rewrote 2 arch gate(s) (0x1b0 -> 0x190)` numa placa **Blackwell** | A troca de arch gate existe para a Ada passar pelo caminho Blackwell. Numa RTX 50 ela é desnecessária, e talvez até nociva: o provedor passa a tratar a GB20x como AD10x nas comparações |
| `full Blackwell framework kernels applied … in original Ada slots`, `thin-geometry … applied=yes` | As correções trocam os cubins/PTX **sm_89** (`blackwell.hpp`; `thin_geometry.hpp` reescreve `.target sm_120` → `sm_89`). Uma RTX 50 executa os cubins **sm_120**, então o "applied=yes" **não muda nada na imagem** dela. O log engana: hoje as correções de qualidade não chegam à série 50 |
| Segundo provedor no `DriverStore` (`found 0 arch-gate comparisons`, `unsupported provider metadata`) | O override da NVIDIA App carrega outro `nvngx_dlssg.dll`, e o addon não o modifica (falha fechada, correto). Se o jogo usar esse provedor, nenhuma correção vale, nem na Ada |
| `numFramesToGenerate=2 (3x)` e `reports 3 frame(s) actually presented` | A geração 3x funcionou; o pedido é só de qualidade |

**Resumo para a série 50:** para a correção valer, é preciso aplicar **variantes sm_120** nos slots sm_120 do provedor, e a troca de arch gate não deve rodar quando a GPU já é Blackwell. A fonte das correções (o PTX do `BlendCandidatesFused` e do `EstimateIntermMvecsScatter`) **já é sm_120** e hoje é rebaixada para sm_89. O caminho inverso é natural, mas exige montar outra tabela de slots e conferir tudo em hardware RTX 50, que não temos.

### Reflex Off e On + Boost no DLSS-G

O código (`framecount.hpp` `SubmitReflexOptions`, `pacing_policy.hpp` `ResolveReflexMode`):

- **Off** é proposital: o Streamline continua recebendo `eLowLatency` (alguns jogos desligam o DLSS-G com o Reflex em Off) e só a espera do `slReflexSleep` é pulada. O overlay da NVIDIA e o próprio jogo continuam mostrando "Reflex On". **Hipótese 1:** "não funciona" é na verdade "não aparece como Off".
- O override só é aplicado quando o jogo chama `slReflexSetOptions`. Uma troca no painel vai para `RefreshReflexTarget`, que só reenvia **na thread dona da chamada original**. Se o jogo chama uma vez só (na abertura ou no menu), a troca fica pendente até a próxima chamada. **Hipótese 2:** a troca não chega ao driver durante o jogo.
- **On + Boost:** com o DLSS-G ligado, o `sl.dlss_g`/driver pode manter o modo próprio (como no Cyberpunk, que trava o Boost com FG). **Hipótese 3:** o Boost é aceito (`sl::Result 0`) mas ignorado.
- Não existe hoje um **efeito medido** que diferencie os modos. O painel mostra "game → effective" com base no que **enviamos**, não no que o driver aplicou.

### Adaptador DLSS-SR → DLSS-G

Já estava previsto no fechamento da 1.6 ("plano próprio"). O XeMFG já tem um adaptador DLSS-SR (`native_ngx.cpp`) que tira color, depth, MV e jitter da chamada do DLSS-SR. A ideia: usar as mesmas entradas para criar e avaliar o **DLSS-G pelo NGX/Streamline** num jogo que não o integra. Riscos: os markers do Reflex (o DLSS-G exige PCL/Reflex), a criação da swapchain/proxy de apresentação do Streamline em um jogo que não carrega `sl.interposer`, HUD-less e a câmera (matrizes) que o DLSS-SR não recebe. É o item maior do lado DLSS-G e vale um protótipo antes de se comprometer.

### Force Borderless

`dlssg_display.hpp`, chave `[DLSSG] ForceBorderless` no `MFGAdaUnlock.ini`, desligada por padrão desde a 1.5.2. Retirar do painel é simples. Falta decidir se a chave no INI continua valendo (oculta) ou se o código sai junto.

### XeMFG

- **Flicker da UI com UI Composition desligada.** Sem composição, a XeFG interpola a imagem **com a HUD já desenhada**: os textos e os elementos da HUD se deslocam com os vetores de movimento da cena e piscam nos frames gerados. Alavancas: (a) uma máscara de HUD (diferença entre o backbuffer e a cópia HUD-less, quando o jogo entrega a HUD-less) para copiar a HUD do frame real nos gerados; (b) zerar o MV/forçar a confiança nas regiões estáticas da HUD; (c) heurística sem HUD-less (pixels que não mudam entre frames reais e estão acima do conteúdo). `native_hud_copy.cpp` já faz parte disso; o item começa medindo o que ele cobre hoje.
- **Vulkan.** A XeFG e a XeLL da Intel são **só D3D12**. O [MFG-Unlocker do dashdogy](https://github.com/dashdogy/MFG-Unlocker) (MIT, a mesma base de onde o nosso XeMFG veio, commit `a4cf2c4`) resolveu isso na **v1.7.0** (25/10, commit `b97deee`) **sem ponte para o D3D12 de verdade**:
  - **`xvk`**: uma camada mínima de **D3D12 sobre Vulkan** (`source/native/xess/vulkan/xvk_*.cpp`). Ela implementa só o pedaço de D3D12/DXGI que a XeFG usa e falha fechada no resto. A `libxess_fg.dll` e a `libxell.dll` **originais da Intel** rodam sem modificação, mas no `VkDevice` do próprio jogo.
  - **Programas:** cada programa DXIL que a XeFG cria é trocado por um **SPIR-V traduzido e embutido**, casado pelo SHA-256 do DXIL (83 programas, manifesto fixo em `generate_xvk_programs.py`). Os `.spv` **não estão no repositório**; ficam embutidos no `MFGUnlocker.dll` da v1.7.0 e seguem os termos da Intel.
  - **Apresentação:** a swapchain Vulkan do jogo vira uma `VkSwapchainKHR` da camada, cujas imagens são os back buffers da XeFG. Ela precisa de Vulkan 1.3, de filas e extensões extras no `vkCreateDevice` (timeline semaphore, sync2, maintenance4, integer dot product) e opcionalmente do `VK_NV_low_latency2` (Reflex) e do `VK_AMD_anti_lag`.
  - **Entradas:** no modo standalone, só as do **DLSS-SR** (NGX Vulkan); motion vectors em resolução de tela não são suportados. Pelo fork do OptiScaler deles, qualquer entrada.
  - **Tamanho:** cerca de 430 KB de código (o `vk_host.cpp` sozinho tem 140 KB). Ele é **experimental** lá, sem lista de jogos testados além do Arknights: Endfield (pelo OptiScaler).

  **Para nós**, o caminho é **portar** (MIT, como já fizemos com o resto do XeMFG), não inventar uma ponte. As diferenças da nossa arquitetura: somos um addon do ReShade, então falta confirmar se dá para **acrescentar filas e extensões no `vkCreateDevice`** pelo ReShade (a camada Vulkan do ReShade cria o device) ou se precisamos de hook próprio no loader; e falta o lado de **entrada**: o adaptador NGX para **Vulkan** (`NVSDK_NGX_VULKAN_*`) e o DLSS-G do Streamline em Vulkan. Os SPIR-V seguiriam o mesmo esquema dos DXIL de hoje: lidos estaticamente de um binário fornecido pelo usuário, com hash conferido, fora do repositório.
- **Pacing do driver da NVIDIA.** Hoje a XeFG pacea os frames gerados pelo próprio SDK (`sdk_pacing.cpp`). Duas peças da NVIDIA são candidatas: o **Reflex pelo NVAPI** (`NvAPI_D3D_SetSleepMode/Sleep` + markers), que só controla a fila e já foi medido na 1.6 (a XeLL empata com o Reflex); e o **flip metering** em hardware das RTX 50, que só é acessível pelo próprio DLSS-G (sem API pública). Expectativa realista: **pouco ou nenhum ganho** fora da RTX 50, e na RTX 50 sem caminho público. O item é uma investigação curta com resposta "sim/não". A v1.7.0 do dashdogy trouxe uma **flip queue** nova: cada lote de frames da XeMFG é apresentado em cadência uniforme, o primeiro frame do lote assim que fica pronto, e na NVIDIA ela **funciona junto com o flip metering do driver**. Segundo as notas, isso tirou o stutter e a perda de FPS de muitos jogos. O nosso R1.8 tem um caminho próprio de flip metering (`XeFGFlipMetering`, `MeteredQueueAhead`). Antes de qualquer coisa nossa, o G compara o nosso pacing com essa flip queue. Um aviso deles também nos afeta: **com o `dxgi.dll` do ReShade a swapchain da XeFG não cresce, e em 5x/6x alguns frames gerados não chegam à tela**.

---

## Bloco 0 — Verificações antes de codar

| # | Tarefa | Resultado esperado | Est. | Status |
|---|---|---|---|---|
| 0.1 | Partir do log 1.4.4 da 5070 Ti: o código de arch gate e dos slots das correções não mudou até a 1.6.0 (não há checagem de arquitetura da GPU), então as conclusões valem. O log com a 1.6 vem com o tester do C.5, sem bloquear | conclusões do log válidas para a 1.6 | — | feito |
| 0.2 | Ler um provedor 310.9.16: existem slots sm_120 para os três kernels das correções? Com que tamanho? | viabilidade do bloco C | 2 h | feito (só PTX sm_120, sem cubin) |
| 0.3 | Reflex: logar o modo **aplicado** (`slReflexGetState` / `NvAPI_D3D_GetSleepStatus`) depois de cada troca, e se a troca do painel ficou pendente | separa as hipóteses 1, 2 e 3 | 2 h | feito (driver: low latency e sleep do jogo; sem campo de Boost) |
| 0.4 | **[você]** Reflex: PresentMon 30 s em 1 jogo (Witcher 3 ou Cyberpunk) com Jogo / Off / On / On + Boost | diferença real de latência entre os modos | 1 h | feito (Cyberpunk: os quatro empatam) |
| 0.5 | XeFG no Vulkan: o SDK da Intel continua só D3D12; o dashdogy resolveu com uma camada D3D12-sobre-Vulkan (`xvk`, v1.7.0, MIT) | decide o bloco F (portar o `xvk`) | 30 min | feito |
| 0.6 | Medir o flicker atual (captura com UI Composition desligada, 2 jogos) e o que o `native_hud_copy` cobre | base do bloco E | 1,5 h | parcial (análise feita; captura vai com o E.4) |

---

## Bloco A — Painel do DLSS-G

| # | Tarefa | Est. | Status |
|---|---|---|---|
| A.1 | Retirar a linha "Force Borderless" do painel (`dlssg_display.hpp`); o estado do modo de tela continua visível só como leitura | 30 min | feito |
| A.2 | A chave `[DLSSG] ForceBorderless` e o código do hook de janela continuam; quem já tem `1` no INI segue com o comportamento atual (decisão: só sai do painel) | — | descartado (decisão) |
| A.3 | README: a chave passa a ser só do INI; testes do hook continuam valendo | 30 min | feito |
| A.4 | Multiplicador do destaque com o Dynamic MFG: voltar a mostrar "ao vivo / máximo" (ex.: 3x / 6x), perdido no redesenho do painel (`8fe4cc9`) | 30 min | feito |

## Bloco B — Reflex Off / On + Boost (DLSS-G)

Depende do 0.3 e do 0.4.

| # | Tarefa | Est. | Status |
|---|---|---|---|
| B.1 | Painel: explicar que o "Off" mantém o Reflex do Streamline ligado e só tira a espera, e que com o Frame Generation ligado Off e Boost geralmente não mudam a latência (o Boost só ajuda com a CPU como gargalo); mostrar o modo aplicado quando houver leitura | 1,5 h | feito |
| B.2 | Troca no painel sem nova chamada do jogo: reenviar as opções no próximo `slReflexSleep`/Present da thread dona (o mesmo ponto seguro que já existe para o limite de FPS) | 2 h | feito |
| B.3 | Boost e Off com o FG ligado: medidos sem efeito no 0.4; o texto do B.1 cobre. Opcional: On × On + Boost numa cena limitada pela CPU | — | parcial (0.4) |
| B.4 | Fixture: troca do painel aplicada sem nova chamada; modo aplicado lido de volta | 1,5 h | feito |
| B.5 | **[você]** Campo: repetir o 0.4 | 1 h | pendente |

## Bloco C — Correções de qualidade na série RTX 50 (DLSS-G)

Depende do 0.2. Sem uma RTX 50 aqui, cada item precisa de um testador.

| # | Tarefa | Est. | Status |
|---|---|---|---|
| C.1 | Não reescrever arch gates quando a GPU já é Blackwell (NVAPI arch 0x1b0); o log diz que não precisa | 1 h | feito (IDs PCI pelo registro) |
| C.2 | Log honesto: "applied" só quando o slot trocado é o da arquitetura da GPU; na RTX 50, até o C.3 sair, dizer "correções de qualidade só para Ada" | 1 h | feito |
| C.3 | Tabela de slots **sm_120** e montagem das variantes sm_120 (sem o rebaixamento para sm_89) do `BlendCandidatesFused` e do `EstimateIntermMvecsScatter`, com a mesma validação exata de tamanho e de fatbin | 6 h | feito (warp blend; scatter fica Ada) |
| C.4 | Opção no INI (desligada por padrão) para aplicar as correções na RTX 50, enquanto o campo não confirmar | 1 h | feito (`Rtx50QualityFixes`) |
| C.5 | **[você/usuário RTX 50]** Campo: mesma cena com as correções ligadas e desligadas, capturas e PresentMon | — | pendente |

## Bloco D — Adaptador DLSS-SR → DLSS-G (protótipo)

| # | Tarefa | Est. | Status |
|---|---|---|---|
| D.1 | Protótipo descartável no Streamline_Sample com o DLSS-G desligado pelo app: o addon cria e avalia o DLSS-G a partir das entradas do DLSS-SR | 6 h | feito (estudo; sem jogo-alvo instalado) |
| D.2 | Decidir pelo resultado do D.1 (markers do Reflex sintéticos, proxy de apresentação, matrizes da câmera); se der, abrir um plano próprio | 1 h | feito (plano próprio) |

## Bloco E — Flicker da UI sem UI Composition (XeMFG)

Depende do 0.6.

| # | Tarefa | Est. | Status |
|---|---|---|---|
| E.1 | Máscara de HUD quando o jogo entrega a HUD-less: copiar os pixels da HUD do frame real sobre os gerados | 4 h | não se aplica (a XeFG já faz com o UI Composition) |
| E.2 | Sem HUD-less: máscara de pixels estáticos (iguais nos dois frames reais, com MV zero ou desconhecido), opcional | 4 h | feito (`UIStabilization`) |
| E.3 | Opção no painel (Desligada / Máscara pela HUD-less / Heurística), com a padrão decidida pela medição | 1 h | feito (caixa separada) |
| E.4 | **[você]** Campo: mesmos jogos do 0.6 | 1 h | pendente |

## Bloco F — XeMFG em Vulkan

Só depois do A–E. O suporte a DX11 (ponte DX11→DX12) saiu deste plano e fica para o futuro.

| # | Tarefa | Est. | Status |
|---|---|---|---|
| F.1 | Vulkan: ler o `xvk` e o `vk_host.cpp` do dashdogy (v1.7.0) e mapear o que serve para um addon do ReShade: edição do `vkCreateDevice`, troca da swapchain Vulkan, origem dos SPIR-V (recursos do `MFGUnlocker.dll` v1.7.0, lidos sem carregar o código) | 4 h | feito (mapa no registro) |
| F.2 | Vulkan: adaptador de entrada do DLSS-SR pelo NGX Vulkan (o mesmo recorte do standalone deles) | 6 h | feito (SR e RR, cópia no present) |
| F.3 | Vulkan: portar o `xvk` e protótipo num jogo Vulkan com DLSS-SR **[você]** escolhe o jogo | 16 h | feito (Indiana Jones and the Great Circle, 4x gerando) |
| F.4 | Vulkan: present metering da NVIDIA (`VK_NV_present_metering`) na saída do host | 3 h | feito (campo: "parece que melhorou"; PresentMon pendente) |

## Bloco G — Pacing do driver da NVIDIA no XeMFG (investigação)

| # | Tarefa | Est. | Status |
|---|---|---|---|
| G.0 | Comparar o nosso flip metering (R1.8) com a flip queue da v1.7.0 do dashdogy: código e PresentMon (desvio entre frames) no mesmo jogo; ver se o aviso de 5x/6x com o `dxgi.dll` do ReShade vale para nós | 4 h | feito (pacing estável; porte adiado) |
| G.1 | Levantar o que o NVAPI expõe para pacing de apresentação (além do Reflex) e se o flip metering da RTX 50 tem acesso fora do DLSS-G | 2 h | feito (já usado: NvAPI_D3D12_SetFlipConfig) |
| G.2 | Se houver caminho: protótipo opcional, medido com PresentMon (desvio entre frames); senão, encerrar com o registro | 2 h | não se aplica |

## Bloco I — Interpolação própria da HUD (XeMFG)

Pedido no campo (2026-10-29): a HUD stabilization deixa a HUD estável, mas no ritmo dos frames reais, como o UI Composition. A XeFG aceita uma só imagem de UI por frame real e a repete em todos os frames gerados, sem API para interpolar a HUD à parte. A saída é desenhar a HUD nós mesmos em cada frame gerado.

Desenho: a XeFG interpola só a cena limpa (a HUD-less do jogo, com uma UI vazia), então os frames gerados saem sem HUD. Para cada frame real o addon guarda a camada de HUD (cópia do back buffer e HUD-less). No `FinalOutput::Present`, que roda em toda saída da XeFG, o hook do Present nativo do `sdk_pacing` informa se o frame é gerado e de qual lote; o addon desenha a HUD na fase *t* do intervalo como transição suave entre a HUD do frame real anterior e a do atual. Nunca deforma (não usa os vetores da cena) e muda em todo frame. Limite: HUD que muda de lugar vira esmaecimento entre as duas posições, não deslocamento. Exige HUD-less do jogo.

| # | Tarefa | Est. | Status |
|---|---|---|---|
| I.1 | `sdk_pacing`: publicar para a thread do Present nativo se a saída é gerada, o id do lote, o índice e o total | 2 h | feito |
| I.2 | Anel de camadas de HUD por frame real (cópia do back buffer e HUD-less), indexado pelo id | 2 h | feito |
| I.3 | Passe de composição no back buffer nativo (pixel shader com blend pré-multiplicado, por formato) e entradas da XeFG (HUD-less do jogo + UI vazia) | 4 h | feito |
| I.4 | Painel: modo "HUD interpolation (experimental)" ao lado da stabilization | 1 h | feito |
| I.5 | Testes: composição numérica em WARP (fase *t*, HUD só no anterior, só no atual, nos dois) | 2 h | feito |
| I.6 | **[você]** Campo: Cyberpunk e DD2 | 1 h | descartado (−60 FPS gerados) |

## Bloco J — Base do XeMFG atualizada para a v1.7.0 do dashdogy

Decidido em 2026-10-29 (opção 3): antes do Vulkan, trazer o motor do XeMFG (`vendor/`, copiado em 19/10 do commit `a4cf2c4`) para a v1.7.0 do dashdogy (`b97deee`) e medir que o D3D12 não piorou. Dos 27 arquivos copiados, 18 não mudaram; mudaram `sdk_pacing.cpp` (1.468 linhas de delta deles, 123 nossas), `cuda_route.cpp/.h`, `host_unlock.cpp/.h`, `shader_port.h`, `sdk_pacing.h`, `sdk_pacing_dlssg.h` e `xess_mod.h`. Traz a flip queue, o NativeBuffersScope (mais buffers na swapchain da XeFG, contra os frames perdidos em 5x/6x com o ReShade como `dxgi.dll`) e as correções de 19 a 25/10.

| # | Tarefa | Est. | Status |
|---|---|---|---|
| J.1 | Merge de três vias (base `a4cf2c4`, nossa, v1.7.0) dos 9 arquivos; dependências novas (`cadence_stats.h`, `adapter_vendor_kmt.h`; `optiscaler_host.h` como shim, sem OptiScaler) | 6 h | feito |
| J.2 | `backend.cpp`: o que o host D3D12 deles passou a fazer (registrar a flip queue, NativeBuffersScope na criação da swapchain da XeFG) | 3 h | feito (fila desligada por padrão) |
| J.3 | Gate completo e testes do XeMFG | 1 h | feito (197/197) |
| J.4 | **[você]** Campo: Cyberpunk 6x com PresentMon contra a medição do G, e um jogo em 5x/6x para os frames entregues | 1 h | feito (fila desligada por padrão) |

## Bloco H — `dlssg_for_sm86`

| # | Tarefa | Est. | Status |
|---|---|---|---|
| H.1 | README: atualizar a seção das RTX 30 (versão 0.4.1, RTX 20 incluída, só D3D12, sem limite de borderless, motor XeSS-FG próprio) e quando usar o XeMFG ou o `dlssg_for_sm86` | 1 h | feito |
| H.2 | *(Opcional)* **[comunidade]** Comparação A/B numa RTX 30 | — | pendente |

---

## Release

| Versão | Conteúdo | Condição |
|---|---|---|
| 1.7.0 | A, B, C (C.5 pendente do tester), E.2, F (Vulkan, ligado por padrão por decisão do usuário), G, H.1 e J; I descartado; D vira plano próprio | gate completo e campo de cada bloco; o C.5 sai como experimental desligado até o log do tester RTX 50 |

Cada release segue o processo de sempre: versão em todos os pontos (incluindo `addon_overlay.inl` e a versão numérica do recurso), notas, `fork/main` e `develop` adiantados, tag e release com o addon.

**Estimativa:** cerca de 34 h no meu lado para 0, A, B, C, E e H; D, F e G somam mais uns 39 h só de protótipo e investigação.

---

## Decisões

1. **Force Borderless:** sai só do painel; a chave do INI e o código ficam.
2. **RTX 50:** C.3 entra, validado por um testador com RTX 50 (C.5).
3. **Ordem do XeMFG:** flicker da UI (E) primeiro; Vulkan (F) depois.
5. **DX11:** fora deste plano; fica para o futuro.
4. **Branch:** `feat/1.7`, criada a partir da `fork/main` (`5c98fd8`).

### Em aberto

1. **DLSS-SR → DLSS-G:** começar pelo protótipo D.1 nesta versão ou deixar para depois do 1.7.0? Sem resposta, fica para depois do 1.7.0.

## Registro

- **2026-10-29 · análise** Leitura do log RTX 5070 Ti (addon 1.4.4), do código do Reflex (`SubmitReflexOptions`, `ResolveReflexMode`), do Force Borderless (`dlssg_display.hpp`), das correções de qualidade (`blackwell.hpp`, `thin_geometry.hpp`: slots sm_89), dos cabeçalhos do SDK da Intel (XeFG e XeLL só D3D12) e do README do `dlssg_for_sm86` v0.4.1 (só D3D12, GPLv3, RTX 20/30). Nenhum código escrito.
- **2026-10-29 · decisões** Force Borderless só sai do painel (A.2 descartado); variantes sm_120 entram com testador RTX 50; flicker da UI antes de DX11/Vulkan; branch `feat/1.7` a partir da `fork/main`.
- **2026-10-29 · Vulkan** O MFG-Unlocker do dashdogy v1.7.0 (`b97deee`, 25/10, MIT) roda a XeFG/XeLL originais no Vulkan por uma camada D3D12-sobre-Vulkan própria (`xvk`) com 83 programas SPIR-V traduzidos dos DXIL (embutidos no binário, fora do repositório). O 0.5 fecha com isso; o bloco F passa a ser portar o `xvk` (hoje F.1–F.3). A mesma versão trouxe uma flip queue nova para o pacing da XeMFG que funciona com o flip metering da NVIDIA: entra no G como G.0. Não resolve o flicker da UI: sem imagem HUD-less a HUD continua interpolada com a cena (limitação declarada deles).
- **2026-10-29 · DX11** Retirado do plano a pedido (ponte DX11→DX12, F.1/F.2 antigos); fica para o futuro. O bloco F passa a ser só Vulkan (F.1–F.3).
- **2026-10-29 · 0.4** Cyberpunk 2077, DLSS-G 6x, addon 1.6.0 publicado (RTX 4070 SUPER, driver 621.01), PresentMon 30 s na mesma cena, modo gravado no `ReShade.ini` antes de cada abertura (a troca pelo painel não foi testada). Latência de exibição p50/p95/p99 e FPS mostrados: jogo 35,2/46,9/49,8 ms, 222 FPS; Off 34,1/45,4/47,9 ms, 221 FPS; On 35,8/48,2/50,8 ms, 217 FPS; On + Boost 36,2/48,7/51,5 ms, 215 FPS. FPS reais 36–37, GPU ~22 ms por frame real. O log confirma "FG-safe Reflex Off active" e "Reflex mode override applied: On + Boost". Conclusão: os overrides são aplicados e não mudam nada com a GPU no limite; sem a espera do `slReflexSleep` a fila não cresce, então quem segura a fila com o DLSS-G ligado é o pacing do próprio DLSS-G. O bloco B vira explicação honesta no painel (proposta aceita); o B.2 continua para a troca pelo painel.
- **2026-10-29 · A.1, A.3, A.4** Force Borderless fora do painel (chave do INI mantida, README). A.4, pedido no campo: com o Dynamic MFG o destaque mostrava só o multiplicador ao vivo desde o redesenho `8fe4cc9`; volta a mostrar "ao vivo / máximo" (`DynamicMultiplierText`, que estava sem uso). Gate 194/194 (3 skipped de proveniência); sem teste novo (só texto do painel).
- **2026-10-29 · B.1, B.2, B.4, 0.3** Painel do Reflex: o Off explica que o Reflex continua ligado no Streamline e só a espera sai, e que com o FG ligado Off e Boost costumam medir igual (dados do 0.4); nova linha "Driver Reflex" com o que o `NvAPI_D3D_GetSleepStatus` informa (low latency e se o jogo chama o sleep; o driver não expõe o Boost). B.2: uma troca no painel que ficou pendente é reenviada pelo próximo `slReflexSleep` da thread dona das opções do jogo (antes só o Present tentava, e ele roda em outra thread em muitos jogos). Teste em `latency_reflex_tests`: falha sem a mudança (linha 224), passa com ela; o sleep segue em ~2,5 ns.
- **2026-10-29 · 0.2** Provedor 310.9.16: os fatbins do `BlendCandidatesFused` e do `EstimateIntermMvecsScatter` têm PTX sm_120 (comprimido), PTX sm_89 e cubin sm_89, e **nenhum cubin sm_120**. Uma RTX 50 compila o PTX sm_120 na hora (JIT); as correções da Ada trocam cubins/PTX sm_89 que ela nunca usa. Os "Blackwell framework kernels" são o PTX da Blackwell montado para a Ada: na RTX 50 eles já são o caminho nativo.
- **2026-10-29 · C.1, C.2** `gpu_arch.hpp`: a família da GPU vem dos IDs PCI das placas NVIDIA no registro (AD10x 0x2680–0x28FF, GB20x 0x2B80–0x2FFF), lidos pelo `kernelbase.dll` (o patch do provedor roda dentro do loader do Windows, onde inicializar o NVAPI pode travar; sem import estático de `advapi32`, que o `xemfg_addon_imports` barra). Só quando todas as placas NVIDIA são RTX 50 o addon deixa o provedor intacto (sem arch gates, sem kernels Ada) e diz isso no log. ID desconhecido mantém o comportamento anterior.
- **2026-10-29 · C.3, C.4** `Rtx50QualityFixes=1` (desligado por padrão): numa RTX 50, o warp blend validado (V3) é aplicado reescrevendo o PTX sm_120 do próprio provedor, sem rebaixar para sm_89; o driver compila esse PTX. O previous scatter, o intermediate scatter e o silhouette guard continuam só na Ada (dependem dos cubins sm_89) e o log diz que não foram aplicados. `thin_geometry_provider_tests` reconstrói o fatbin real para sm_89 e sm_120; o PTX sm_120 resultante passa no `ptxas` 13.0 (47 registradores, sem spill, mesmo perfil do sm_89). Falta o campo com o tester (C.5).
- **2026-10-29 · E** A API da XeFG já tem o modo que o E.1 previa (interpolar a HUD-less e extrair a UI do back buffer): é o UI Composition existente, para jogos que entregam HUD-less. O flicker que sobra é o de jogos sem HUD-less. E.2: `[XeFG] UIStabilization` (experimental, desligado): a cada frame real, um passe próprio na fila de apresentação copia o back buffer como HUD-less e gera uma textura de UI com os pixels que ficaram iguais por 4 frames reais (tolerância 2/255, 1 pixel de dilatação para as bordas dos textos); a XeFG compõe essa HUD do frame real sobre os gerados. A HUD-less do jogo sempre tem prioridade. Painel: caixa "HUD stabilization (experimental)" e o estado em "HUD handling". Testes: `xemfg_ui_mask_numeric` (WARP: HUD composta depois de 4 frames, cena transparente, HUD-less exata, histórico reinicia ao mudar o tamanho) e `xemfg_ui_composition` (composição sem a preferência do UI Composition, e desliga quando as entradas somem).
- **2026-10-29 · gate** B, C, E e H.1 juntos: 197/197 (3 skipped de proveniência).
- **2026-10-29 · G.0, leitura** A flip queue do dashdogy (`sdk_pacing_meter.h`, v1.6.1–1.7.0) é um "jitter buffer": cada lote é entregue cedo numa fila de cerca de um lote e um relógio próprio libera cada flip, com várias correções de campo (hitch, lock-in, coupled decay). É um sistema grande (o `sdk_pacing.cpp` deles tem 134 KB, o nosso 54 KB); portar fica para o plano próprio do G, com PresentMon antes e depois.
- **2026-10-29 · escopo** A pedido, a 1.7 só sai com o plano inteiro (D, F Vulkan, G pacing) e com o bloco I novo (interpolação própria da HUD): uma atualização maior. Campo da E.2: no Cyberpunk e no DD2 a HUD fica estável, mas no ritmo dos frames reais; versão com cobertura exata pela HUD-less (`04379e1`).
- **2026-10-29 · bloco I, campo e pausa** Evolução no campo (DD2, Cyberpunk): o id do frame da XeFG traz o nosso id nos 32 bits altos (a 1ª versão não compunha nada); transição pura funcionou mas parecia ritmo de frames reais; movimento por blocos de 8x8 (busca ±24 e depois ±64 px, pista pelos vetores do jogo para HUD presa ao mundo) gerou textos voando e mosaico; checagem de consistência e confiança reduziram, sem resolver; versão por elemento (rotulagem de componentes na grade de blocos, um vetor por elemento escolhido pelo custo do elemento inteiro, elementos grandes só com transição, `098eb86`) passa todos os testes WARP mas o resultado no jogo não ficou bom. Decisão: pausar o I, seguir com D, F e G; a opção `[XeFG] UIInterpolation` fica desligada por padrão e, antes do release, decidir se aparece no painel. A HUD stabilization (E.2) segue como está.
- **2026-10-29 · G** PresentMon 30 s, Cyberpunk 2077, 6x, mesma cena, build 1.7 de teste. XeMFG: intervalo mostrado p50/p95/p99 4,52/5,91/6,90 ms, 1 intervalo longo, 217 FPS mostrados, ~35 FPS reais. DLSS-G (0.4, mesma manhã): 4,47/5,69/6,45 ms, 7 intervalos longos, 222 FPS. O pacing do XeMFG está no nível do DLSS-G; portar a flip queue do dashdogy (`sdk_pacing_meter.h`, ~1.470 linhas de delta e dependências novas: `cadence_stats.h`, `optiscaler_host.h`, `adapter_vendor_kmt.h`, `xvk_output_frame.h`) fica adiado. O "pacing do driver da NVIDIA" já é usado: `XeFGFlipMetering` chama o `NvAPI_D3D12_SetFlipConfig`, o mesmo flip metering do DLSS-G. Achado à parte, fora do G: latência de exibição p50 do XeMFG 59,6 ms contra 35,2 ms do DLSS-G no mesmo cenário (FPS real parecido); a investigar depois.
- **2026-10-29 · D** Requisitos do DLSS-G (guia do Streamline 2.14.16, `ProgrammingGuideDLSS_G.md`) para ligá-lo de fora num jogo só com DLSS-SR: (1) interceptar o `slInit` e acrescentar DLSS-G e Reflex aos `featuresToLoad`, com `sl.dlss_g.dll` e `nvngx_dlssg.dll` presentes na pasta do jogo (não podemos distribuí-los); (2) a swapchain precisa ser a proxy do Streamline, e jogos só com DLSS-SR podem nem criá-la por ela; (3) "any existing Reflex SDK integration that does not use Streamline cannot be used with DLSS-G": marcadores PCL e sleep sintetizados pelo Streamline; (4) profundidade e vetores marcados pelo DLSS-SR valem até o evaluate, o DLSS-G os lê no Present: cópias; (5) constantes: já fornecidas. Levantamento das instalações: todos os jogos com Streamline (AC Black Flag, Control, Cyberpunk, DD2, Elden Ring, MH Wilds, Onimusha, Pragmata, RE4, RE Requiem, The Great Circle) já trazem `sl.dlss_g.dll`; os só com NGX (KCD2, Minecraft) não usam Streamline. Sem jogo-alvo, o protótipo não tem onde rodar. Decisão: o adaptador vira plano próprio, aberto com um jogo-alvo; nesses jogos o XeMFG já gera frames a partir do DLSS-SR.
- **2026-10-29 · F.1, mapa do porte Vulkan** Código do dashdogy v1.7.0 (MIT) baixado para análise. Pasta `source/native/xess/vulkan`: ~8.300 linhas. Camada `xvk` (D3D12 sobre Vulkan: `xvk_device` 998, `xvk_dxgi` 1.376, `xvk_commandlist` 437, `xvk_resource` 296, `xvk_queue` 260, `xvk_pipeline`, `xvk_programs`, `xvk_util`, `xvk_internal.h` 823), host `vk_host.cpp` (2.611: tomada da swapchain, entradas do DLSS-SR, XeLL e Reflex no Vulkan) e API para o OptiScaler (`vk_xefg_api.cpp`, `mfg_xefg_vk.h`). Dependências fora da pasta: `overlay_vulkan.cpp` (52 KB: os wrappers de resolução do `vulkan-1`, por onde o host recebe `vkCreateInstance/Device`, swapchain, acquire e present), as thunks de `Evaluate` Vulkan do NGX em `ampere_backend.cpp` (entradas do DLSS-SR), `xemfg_window`, `xemfg_dynamic`, `standalone_policy`, `runtime_search`, `single_module`, `protected_pointer` e o conjunto de 83 programas SPIR-V (recursos RCDATA do `MFGUnlocker.dll` v1.7.0, gerados de DXIL da Intel, fora do repositório deles). Requisitos do device: Vulkan 1.3, filas extras na criação do device, `timelineSemaphore`, `synchronization2`, `maintenance4`, `shaderIntegerDotProduct`; opcional `VK_NV_low_latency2`. Para o nosso addon (ReShade como camada Vulkan): os pontos de entrada precisam ser interceptados antes da camada do ReShade criar o device (o ReShade não deixa um addon acrescentar filas e extensões ao `vkCreateDevice`), então o caminho é um hook próprio no `vulkan-1` como o deles. Jogo-alvo instalado: Indiana Jones and the Great Circle (só Vulkan, Streamline com DLSS-SR e DLSS-G, ReShade e o addon já instalados).
- **2026-10-29 · F.3, programas SPIR-V** `tools/extract_xvk_programs.py` lê, sem carregar, o `MFGUnlocker.dll` v1.7.0 (SHA-256 `e6b6e6b7…` conferido com o `SHA256SUMS.txt` da release): os 83 programas SPIR-V são recursos RCDATA 7400–7482, e a tabela `ProgramAsset` (entradas de 40 bytes: ponteiro da chave DXIL, recurso, ponteiro do hash do SPIR-V, tamanho, bytes de push constant, primeiro binding, quantidade) dá a chave DXIL de cada um. Os bindings vêm da reflexão do próprio SPIR-V (quantidade conferida com a tabela; as push constants seguem a tabela, porque módulos que não leem as constantes não têm bloco de push). Resultado: 83 programas, 484 bindings (storage buffer 207, sampled image 137, storage image 100, uniform 30, sampler 10), todos no set 0. Saída fora do repositório (`build/deps/xvk-programs`), como os DXIL do caminho D3D12. Headers do Vulkan: Khronos `Vulkan-Headers` `vulkan-sdk-1.4.350.1` em `build/deps`.
- **2026-10-29 · I descartado** Último campo do bloco I: a HUD interpolation custava cerca de 60 FPS gerados. Os cinco commits do bloco (`b0bde4c`, `19b8faa`, `a40ac3e`, `d4457e1`, `098eb86`) foram revertidos; a HUD stabilization (E.2) segue. Testes do XeMFG: 145/145.
- **2026-10-29 · escopo, opção 3** O host Vulkan do dashdogy depende de partes novas do `xess_mod` dele (`VulkanHostCallScope`, `SetVulkanHostDevice`, `AdoptVulkanHostPair`, `HostRedirectedExport`, `ActiveSdkPair`), então o Vulkan exige primeiro atualizar a base do XeMFG. Decisão: bloco J (base para a v1.7.0, medida no D3D12) antes de seguir no F.
- **2026-10-29 · J.1, J.2** Merge de três vias (base `a4cf2c4`, nossa, v1.7.0) com `git merge-file`: 9 conflitos, todos resolvidos mantendo as nossas mudanças (traces `RuntimeCall`, controles de flip metering, queue-ahead e deadlines do SDK e o `Snapshot` do painel) e as dele (flip queue, `NativeBuffersScope`, `GuardedLaunch` da rota CUDA contra falha do driver, flip meter da AMD, contadores de saída). `xess_mod.h` segue o nosso adaptador (o dele declara o módulo host inteiro que o nosso `backend.cpp` substitui); o código mesclado só usa `Self`, `WriteLog` e `kGeneratedCapacity`. Novos: `sdk_pacing_meter.h`, `cadence_stats.h`, `adapter_vendor_kmt.h` (sem mudança); `optiscaler_host.h` como shim (`MenuBackBuffers` = 16); `native_xvk_stub.cpp` para os dois ganchos do `xvk` que o pacing chama. Achado no `xess_mod.cpp` dele: o host D3D12 standalone (sem OptiScaler), o caso análogo ao nosso, deixa a flip queue desligada (o relógio da fila trava abaixo do jogo quando o jogo entrega no ritmo das liberações da fila: Subnautica 2, 100–110 FPS contra ~180 do DLSS MFG) e mantém a contagem de buffers do jogo. O componente mesclado nasce com a fila ligada, então o `backend.cpp` passa a configurá-la sempre: `[XeFG] XeFGFlipQueue` (padrão 0), `XeFGFlipQueueBufferPct` (50), `XeFGFlipQueueFirst` (`ready`) e, com a fila ligada, `XeFGNativeBuffers` (10, até 16) via `NativeBuffersScope` no `InitFromSwapChainDesc`. Build limpo.
- **2026-10-29 · J.4** PresentMon 30 s, Cyberpunk 2077 6x, mesma cena. Motor v1.7.0 com a fila desligada contra o motor de 19/10: intervalo p50/p95/p99 4,63/5,90/6,43 contra 4,52/5,91/6,90 ms, 0 contra 1 intervalo longo, 1% low 155 contra 145, latência p50 61,3 contra 59,6 ms (FPS real ~2% menor nessa tomada): sem regressão, pacing um pouco melhor. Fila ligada: o `NativeBuffersScope` do dashdogy recusava a nossa factory (`factory_not_dxgi`: o `FinalOutputFactory` embrulha a do DXGI); o nosso `OutputFactory` passou a criar a swapchain nativa da XeFG com `XeFGNativeBuffers` (log `native_buffers`: jogo 3, pedidos 10, resultado 0). Duas tomadas de cada: fila desligada 4,63/5,69/6,62 e 4,58/5,72/6,66 ms, 2–3 intervalos longos, 211–212 FPS, latência p50 61,2–61,5 ms; fila ligada com 10 buffers 4,85/6,30/6,54 e 4,89/6,14/6,44 ms, 0 intervalos longos, 205–208 FPS, latência p50 63,9–66,4 ms. Decisão: fila desligada por padrão (menos latência e mais FPS; a fila só ganha nos intervalos longos), como o host standalone do dashdogy; `XeFGFlipQueue=1` segue como opção.
- **2026-10-29 · F.2, F.3, porte** Host Vulkan do dashdogy v1.7.0 (`vk_host.cpp` e cabeçalhos, MIT) em `vendor/xvkhost`, ligado ao addon por um adaptador nosso (`native_vulkan.cpp`), no lugar do `overlay_vulkan.cpp` dele: hooks nos exports do `vulkan-1` instalados no `create_device` do ReShade para Vulkan (o ReShade carrega o addon dentro do `vkCreateInstance`, antes do device) e os do NGX Vulkan (`NVSDK_NGX_VULKAN_*` do `_nvngx.dll`) para as entradas do DLSS-SR e do Ray Reconstruction. O host usa o par da Intel já validado pelo backend (`ActiveSdkPair`); no device do xvk a rota CUDA, o flip metering do NVAPI e a flip queue ficam de fora. O shader de cópia das entradas vem com os três SPIR-V que o dashdogy fixa, lidos do mesmo DLL (sem glslang). Painel: "XeMFG · Vulkan", fps base e de saída, motivo da espera.
- **2026-10-29 · F.3, campo no Indiana Jones** (RTX 4070 SUPER, driver 621.01, Streamline com DLSS RR, ReShade como camada Vulkan, RenoDX do jogo). Em ordem: (1) o `create_device` do ReShade escreve a versão do Vulkan como `major<<12 | minor<<8`, não `VK_MAKE_API_VERSION` (a instância saiu "3.0"); (2) o jogo liga `VK_EXT_full_screen_exclusive`, `VK_NV_low_latency2` e `VK_KHR_present_id`, que o host só aceitava sob o OptiScaler: as chamadas na swapchain do host (modo exclusivo, Reflex, present wait) passam a ser respondidas pelo addon e o pedido de modo exclusivo sai da criação; (3) o adaptador de entrada do Streamline para D3D12 lia o `FrameToken` de cada marcador e derrubava o jogo: num jogo Vulkan ele só repassa as chamadas (o DLSS-G continua segurado desligado e o switch do jogo liga e desliga a XeMFG); (4) o `sl.common` (versão OTA) guarda private data na swapchain (`vkSetPrivateData` com `VK_OBJECT_TYPE_SWAPCHAIN_KHR`) direto no driver, que escrevia num handle que nunca criou: private data e nomes de debug da swapchain do host ficam com o addon; (5) a cópia das entradas, gravada no command buffer do DLSS, trocava o estado de compute do jogo (pipeline, push descriptors, push constants) e o jogo não o religa depois do DLSS: GPU travada (device lost). Isolado com chaves de campo (`VulkanInputCopy` 0/2, `VulkanGenerate=0`), a cópia passou a rodar no present, num command buffer do host, junto da ponte; também amostra views próprias de um aspecto (o depth D24S8 do jogo é declarado ao NGX como cor). Resultado: 4x gerando, troca de multiplicador ao vivo, ~39 FPS base → ~155 FPS. Linha de base PresentMon (uma tomada, XeLL + CPU): 129 FPS mostrados, 1% low 26, intervalo p50/p95/p99 6,6/13,0/38,5 ms, 184 intervalos longos em 3.753; a segunda tomada não gravou (o UAC tira o foco e o jogo para de apresentar) e o jogo depois travou por falta de VRAM.
- **2026-10-29 · RenoDX do Indiana Jones** Com o backend XeMFG, o `renodx-indygreatcircle.addon64` caía no `create_pipeline` (com `Vulkan=0` também, então não é o host). Com `[Diagnostics] VulkanFaults=1` a pilha mostrou pipelines do próprio jogo (via Fossilize e ReShade): o `ClonePipelineSubObjects` do RenoDX (`src/utils/pipeline.hpp`) copia os dados de todo estágio de shader sem checar ponteiro nulo. Corrigido no checkout local do RenoDX (mesma checagem do `DestroyPipelineSubobjects`) e o addon do Indy recompilado: os dois rodam juntos. A correção é do RenoDX (fora deste repositório).
- **2026-10-29 · F.4** O driver 621.01 oferece `VK_NV_present_metering` com a feature `presentMetering` na 4070 SUPER: é a forma Vulkan do flip metering do NVAPI que o caminho D3D12 usa. O host pede a extensão no `vkCreateDevice` e o primeiro present de cada frame base leva `VkSetPresentConfigNV` com os frames do lote (4 em 4x; 1 ao desligar; não com V-Sync). `[XeFG] VulkanPresentMetering` (padrão 1). Campo: o driver aceitou (nenhum erro de present, o lote acompanhou o switch do jogo) e "parece que melhorou"; medição PresentMon pendente. Os diagnósticos de campo ficam atrás de `[Diagnostics] VulkanFaults` (padrão 0).
- **2026-10-29 · F.3, campo no Red Dead Redemption 2** (Vulkan, DLSS-SR pelo NGX direto, sem Streamline). (1) O depth D32S8 chega ao NGX com aspecto depth+stencil e o host recusava todas as entradas ("no_dlss_inputs"): com as views próprias do addon ele passa. (2) A GPU caía (TDR) logo depois dos primeiros frames gerados; um vigia de travamento (`[Diagnostics] VulkanFaults=1`: pilha de cada thread quando os presents param por 3 s) mostrou todas as threads esperando a GPU, nenhuma em código nosso. Metering desligado não mudou; geração desligada parou a queda, mas a imagem congelava com o jogo em foco (normal no alt-tab). PresentMon em foco: o host apresentava ~95/s e o Windows só via 30 presents em 20 s, em "Hardware Composed: Independent Flip". Só no modo tela cheia do jogo: em janela e sem bordas funcionava. Causa: o RDR2 liga `VK_EXT_full_screen_exclusive` sem dizer o modo, o driver escolhia exclusivo para a swapchain do jogo e ela ficava com a tela depois de aposentada. Correção: toda swapchain do jogo é criada com o modo `DISALLOWED` explícito (os comandos de modo são respondidos pelo addon) e MAILBOX vira IMMEDIATE. Com isso a geração também passou a rodar sem queda: 4x funcionando em tela cheia.
- **2026-10-29 · F, decisão** `[XeFG] Vulkan` fica ligado por padrão (decisão do usuário), documentado como experimental no README com a saída `Vulkan=0`; a tabela de jogos do XeMFG ganha a coluna API (Indiana Jones: Vulkan; RDR2: DX12 e Vulkan). Avaliação honesta: o caminho Vulkan é mais frágil que o D3D12 (a swapchain do jogo vira um handle que só o addon conhece), oito problemas em dois jogos, todos corrigidos de forma geral.
- **2026-10-29 · release 1.7.0** A pedido, a 1.7 sai com o Vulkan (Indiana Jones e RDR2) e sem esperar o C.5: `Rtx50QualityFixes` segue desligado por padrão e as notas dizem que não foi confirmado numa RTX 50. `fork/main` ganhou um commit do usuário (Phantom Blade Zero na tabela do DLSS-G), trazido por merge. Versão 1.7.0 em todos os pontos; notas em `docs/releases/1.7.0.md`; README com o build do Vulkan.
