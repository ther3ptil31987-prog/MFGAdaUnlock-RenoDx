# XeMFG — auditoria de universalidade

21/10/2026. Base: worktree `build/xemfg-reshade/source`, branch `feature/1.5.0-xemfg-capability-adapters`, árvore RC5 local (não commitada). Somente análise: nenhum código foi alterado para este relatório. Caminhos relativos a `src/addons/mfgunlock/`.

Evidência de campo usada: logs RC4 de Resonance: A Plague Tale Legacy (Xbox) e Death Stranding 2 (Steam), sessões de 21/10.

## Resumo

O XeMFG **não substitui `sl.dlss_g.dll` nem atua no nível NGX**. Ele faz detour nos exports do `sl.interposer.dll` e nas funções que o próprio Streamline devolve por `slGetFeatureFunction`. Também desliga o plugin DLSS-G com `slSetFeatureLoaded(false)` e troca a swapchain no vtable da factory DXGI pela proxy do Intel XeFG. Essa camada é a certa para jogos com Streamline 2.x: o contrato público é estável e versionado. Ela não basta para "qualquer jogo", por quatro motivos, em ordem de impacto:

1. **A convenção de depth/jitter é escolhida pelo nome do executável antes do primeiro frame**, e uma divergência desliga o FG (`xemfg/native_game_defaults.hpp:7-16`, `xemfg/native_bridge.cpp:381-383`, `xemfg/native_constants.hpp:20-21`).
2. **A única fonte de entrada é o Streamline DLSS-G.** O jogo precisa expor o DLSS-G, ou seja, passar na checagem de suporte da NVIDIA, e chamar Reflex/PCL pelo Streamline. Sem isso não há frame, e não existe outro adaptador (DLSS-SR, FSR-FG ou XeFG nativo com multiplicador próprio).
3. **Structs e versões são validadas por igualdade exata**, sem leitura de prefixo conhecido. Uma versão futura do SL que suba `ReflexOptions`, `Constants` ou `ResourceTag` derruba o caminho. No caso do Reflex, devolve erro ao jogo.
4. **O runtime Intel é fixado por hash, com offsets binários** (`xemfg/backend.cpp:420-422`, `xemfg/vendor/sdk_pacing.cpp:26-27`, `xemfg/native_reflex_profile.hpp:23-30`). Um jogo que carregue outro `libxess_fg.dll` não passa na admissão.

## 1. Camada de interceptação

| Hook | Onde é instalado | Mecanismo | Função |
|---|---|---|---|
| `slInit`, `slSetD3DDevice`, `slShutdown`, `slIsFeatureLoaded`, `slSetFeatureLoaded`, `slGetFeatureFunction` | `xemfg/native_streamline.cpp:560-578` (`CaptureStreamlineStartup`) | MinHook no export de `sl.interposer.dll`, que é fixado (pin) na memória | Após `slInit`/`slSetD3DDevice`, desliga o DLSS-G físico e mantém o estado lógico que o jogo vê (`:39-52`, `:79-97`) |
| `slSetTag`, `slSetTagForFrame`, `slSetConstants`, `slEvaluateFeature` | `xemfg/native_streamline.cpp:588-592` (`DiscoverStreamline`) | MinHook no export | Captura de recursos, constantes e markers legados via Evaluate |
| `slDLSSGSetOptions`, `slDLSSGGetState` | `:532-556` (`ResolveHook`) e `:594-600` | O resolver devolve **o wrapper ao jogo**; o corpo original não recebe detour (`:495-502`). O Steam overlay (`gameoverlayrenderer64.dll`) recebe o original (`:542-547`) | Força `eOff` no NVIDIA (`:324-325`), guarda o pedido do jogo e reescreve o estado (`:336-357`) |
| `slReflexSetOptions`, `slReflexSleep`, `slPCLSetMarker`, `slReflexSetMarker` | `:503-528` (`Feature`) | MinHook no corpo resolvido; `slReflexSetMarker` legado tem original separado (`:25-29`) | Tradução Reflex → XeLL |
| `IDXGIFactory::CreateSwapChain` (vtable 10), `IDXGIFactory2::CreateSwapChainForHwnd` (vtable 15) | `xemfg/native_bridge.cpp:345-363` (`Install`) | MinHook no vtable da factory real | Cria a proxy XeFG (`CreateOutput`, `:129-257`) ou cai para o original |
| `xefgSwapChainD3D12CreateContext/InitFromSwapChain/InitFromSwapChainDesc/Destroy/SetEnabled/SetNumInterpolatedFrames/GetLastPresentStatus` | `xemfg/backend.cpp:452-470` | MinHook no `libxess_fg.dll` | Publica o modelo MFG portado em contextos do host **e do jogo** (XeFG nativo, como o DS2) |
| Present / ResizeBuffers / SetFullscreenState | `xemfg/native_swapchain.cpp:60-134` | Wrapper COM próprio sobre a proxy Intel | Admissão por frame (`BeforePresent`) e invalidação por epoch |

O backend DLSS-G (`addon.cpp`) é outra camada: patch binário do `nvngx_dlssg.dll` carregado (NGX/provider) mais `slGetFeatureFunction`. Ele não participa do XeMFG.

## 2. Código específico por jogo, identidade ou build

| # | Item | Arquivo:linha | Comportamento genérico que compensa | Universalidade |
|---|---|---|---|---|
| G1 | Depth não invertido para `tlou-ii.exe`, `tlou-ii-l.exe`, `AlanWake2.exe` | `xemfg/native_game_defaults.hpp:7-10` | O XeFG fixa `INVERTED_DEPTH` no init, mas o jogo só informa `depthInverted` nas constantes, que chegam depois | **Crítico**: jogo novo com depth normal fica sem FG (`ConstantDepth`) |
| G2 | MV com jitter para `DS2.exe` | `:11-16`, recusa em `xemfg/native_bridge.cpp:144-151` | Idem para `JITTERED_MV`; somado ao shader jittered ausente no conjunto portado | **Crítico**: qualquer jogo com MV jitterizado fica sem FG |
| G3 | Overrides `[XeFG] InvertedDepth/JitteredMotion` no INI | `xemfg/native_bridge.cpp:382-383` | Válvula manual para G1/G2 | Bom mecanismo, mas é manual e por pasta |
| G4 | Steam overlay identificado pelo módulo chamador | `xemfg/native_streamline.cpp:542-547` | Um consumidor externo de `slDLSSGSetOptions` não pode desligar o FG do jogo | Genérico na intenção, porém só reconhece um observador conhecido |
| G5 | Retenção da swapchain nativa ligada à presença do Steam overlay | `xemfg/native_continuity.cpp:38` | O overlay mantém referências à swapchain antiga durante a recriação | A política deveria depender de "existem referências externas", não do nome do módulo |
| G6 | REFramework: hash com defeito conhecido e `dinput8.dll`/`reframework*` | `xemfg/native_reframework.cpp:52`, `:78` | Monitor de hooks do REFramework reinstala detours do XeFG | Específico dos jogos Capcom |
| G7 | Recusa com `MFGUnlocker.asi`, companion `.asi` ou OptiScaler presentes | `xemfg/backend.cpp:382`, `:385`, `:386-390` | Duas pontes no mesmo runtime Intel | Necessário; pode virar detecção de conflito por capacidade |
| G8 | Par Intel por SHA-256 exato (`libxess_fg` 1.3.1.78, `libxell` 1.3.0.5/1.3.2.10) | `xemfg/backend.cpp:420-422` | Port do modelo e offsets dependem do layout exato | **Alto**: jogo com XeFG diferente no mesmo processo é recusado |
| G9 | RVAs fixos do pacing extra (família `fg78`) | `xemfg/vendor/sdk_pacing.cpp:26-27`, `:800` | Pacing NVIDIA dentro do SDK Intel | Mesma dependência de build de G8 |
| G10 | Offsets e seletor mágico do perfil Reflex externo | `xemfg/native_reflex_profile.hpp:23-30` | Saída Reflex nativa via SDK Intel | Idem |
| G11 | Modelo MFG só em vendor `0x10DE` | `xemfg/backend.cpp:173` | O port CUDA/Tensor é para NVIDIA | Escopo declarado, não é por jogo |
| G12 | Formatos de swapchain RGBA8/BGRA8/RGB10A2 | `xemfg/native_bridge.cpp:138-142`, `xemfg/native_swapchain.cpp:20-21` | Ausência de conversão FP16/scRGB | **Alto**: jogos HDR scRGB ficam sem FG |
| G13 | Fullscreen exclusivo recusado | `xemfg/native_bridge.cpp:134-137` | Limite do SDK Intel | Contrato do SDK |
| G14 | (DLSS-G) `Outlaws.exe`, `MonsterHunterWilds.exe` | `addon.cpp:8003-8005` | Fora do XeMFG; listado por completude | Mover para o mesmo arquivo de perfis |

## 3. Detecção

- **Carregamento tardio:** não há `LdrRegisterDllNotification`, hook de `LoadLibrary*` nem de `LdrLoadDll` no XeMFG. O `loadhook.hpp` atende só ao backend DLSS-G. A descoberta usa:
  - o `Attach` (`xemfg/native_bridge.cpp:377`), útil só se o interposer já estiver mapeado;
  - um worker com 80 × 250 ms, cerca de 20 s (`:371-374`);
  - o evento `init_device` do ReShade (`:364-366`);
  - a criação de swapchain (`:288`, `:331`).

  Se o interposer for carregado depois de 20 s e o `slInit` acontecer antes do próximo device ou swapchain, o `IsolatePresentation` não ocorre. A saída então fica em `presentation_not_owned` (`:167-170`).
- **Versão do SL:** não há detecção em runtime. O `InitHook` recebe `sdkVersion` (`xemfg/native_streamline.cpp:53`) e não o registra. O relatório traz só o hash do interposer (`xemfg/native_compatibility.cpp:43`). A presença de `slGetFeatureFunction` é exigida para o status (`:594-606`), o que exclui o SL 1.x na prática. Os detours em `slSetTag`/`slSetConstants` (`:590-591`) são instalados **sem conferir a ABI**. Se um interposer 1.x exportar `slSetTag` com a assinatura 1.x, o wrapper lê argumentos com o tipo errado. Isso não foi verificado; o tratamento seguro é não instalar hooks fora do 2.x.
- **Structs:** são validados `structType` e `structVersion`, mas por igualdade ou faixa fechada, e a cadeia `next` não é percorrida:
  - `ResourceTag`: exatamente v1 (`:175`); `Resource` v0/v1 (`xemfg/native_input_policy.hpp:10-15`).
  - `Constants`: v1-2, e v3+ vira `ConstantMetadata` (`xemfg/native_constants.hpp:16`).
  - `DLSSGOptions`: v1-5 (`:314`). O wrapper encaminha uma v1 própria com `eOff` (`:324`), o que é correto.
  - `DLSSGState`: v1-4, com escrita de campos por versão (`:338`, `:353-355`), também correto.
  - **`ReflexOptions`: exatamente v1 e `next==nullptr`. Fora disso retorna `eErrorUnsupportedInterface` ao jogo** (`:359-360`). O Reflex do jogo deixa de ser configurado e o `reflexOptionsReady` nunca fica verdadeiro (`:370`, `:621`).

  Não há leitura de sizeof fixo além do prefixo v1. O problema é a política "versão desconhecida ⇒ recusa", não o layout.

## 4. Habilitação do FG pelo jogo

- **`slIsFeatureSupported` / `slGetFeatureRequirements`:** não há hook. A opção de FG do jogo depende da checagem real da NVIDIA (DLSS-G suportado na GPU/driver e `nvngx_dlssg.dll` presente). Em GPU sem DLSS-G (RTX 30, AMD) o jogo esconde o FG e o XeMFG não recebe nada. A ordem `slInit` → `slSetFeatureLoaded(false)` (`:57`) acontece antes das consultas do jogo. O `slIsFeatureLoaded` é falsificado para o valor lógico (`:91-97`), mas não se sabe se o `slIsFeatureSupported` de todas as versões ignora o plugin descarregado. Hoje nada é registrado sobre isso.
- **`slDLSSGGetState`** (`:336-357`): remove os bits de falha causados pelo NVIDIA desligado (`eFailReflexNotDetectedAtRuntime`, `eFailGetCurrentBackBufferIndexNotCalled`), anuncia `numFramesToGenerateMax=5` e `bIsDynamicMFGSupported=false`. Outros bits de falha passam ao jogo, que pode desligar a própria opção. O que mais pesa é `eFailResolutionTooLow` e o requisito de VRAM.
- **Viewport:** o primeiro viewport com modo diferente de `eOff` vence; os outros recebem `eErrorFeatureNotSupported` (`:321`, `:99-104`). Um jogo que habilite primeiro num viewport de menu fixa o viewport errado até a saída ser aposentada.
- **Reflex:** a admissão exige `slReflexSetOptions` aceito e Reflex do jogo desligado com confirmação (`:621`), além dos markers SimStart/SimEnd/RenderSubmitStart/PresentStart (`:625-626`, `:644`). Reflex implementado só via NVAPI direto não é reconhecido. Isso foi descartado de propósito na RC3; a RC5 cobre só sleep ausente ou mal posicionado.
- **NVAPI:** usado para flip metering (`xemfg/backend.cpp:190-230`). Não é spoofado nem influencia a decisão do jogo.
- **Vendor ID DXGI:** não é alterado. Só é lido para o port do modelo (`xemfg/backend.cpp:146-153`, `:173`).

## 5. Dados

| Campo | Tratamento | Onde |
|---|---|---|
| `mvecScale` | Lido e multiplicado pelo extent do MV | `xemfg/native_constants.hpp:32`, `xemfg/native_streamline.cpp:647` |
| `jitterOffset` | Repassado; com `JITTERED_MV` o shader falta | `xemfg/native_constants.hpp:31` |
| `depthInverted`, `motionVectorsJittered` | **Comparados com a flag fixada no init**; divergência ⇒ FG off | `:20-21` |
| `cameraMotionIncluded` | Exigido `eTrue`, senão recusa | `:18` |
| `motionVectors3D` | Exigido `eFalse` | `:19` |
| `reset` | Respeitado, e reset forçado em salto de ID | `:22`, `:33`; `xemfg/native_streamline.cpp:648` |
| `motionVectorsDilated`, `orthographicProjection`, `cameraNear/Far/FOV`, `cameraPinholeOffset`, `prevClipToClip`, `clipToPrevClip` | **Não lidos**. A view é reconstruída de `cameraRight/Up/Fwd/Pos` e a projeção copiada de `cameraViewToClip` | `:23-30` |
| `frameRenderTime` | Fixado em 0 | `:33` |
| Modo off/on/auto/dynamic | `eOff` desliga; qualquer outro liga (`eAuto`/`eDynamic` = ligado); `numFramesToGenerate` do jogo é ignorado em favor do INI | `xemfg/native_streamline.cpp:328`, `xemfg/native_bridge.cpp:380` |
| Lifecycle | `eValidUntilPresent` ⇒ `UNTIL_NEXT_PRESENT`. `eOnlyValidNow`/`eValidUntilEvaluate` ⇒ `ONLY_NOW`, com cópia na command list do produtor, exigida e validada (tipo diferente de COPY, mesmo device). Sem command list o tag é recusado | `:215-230` |
| HUDLess/UI opcionais | Recusa isolada se o formato for diferente do output ou o tamanho diferente da tela; não derruba o frame salvo em conflito | `:141`, `:199-213`; `xemfg/hud_inputs.hpp:18-27` |
| Extents | Respeitados para inputs (`resourceBase/Size`) | `:195-198` |
| **Backbuffer** | Sempre o output inteiro; `kBufferTypeBackbuffer` (subretângulo, letterbox) não é mapeado | `:649-650`, `:120-128` |
| Profundidade linear / outros buffers | Só `kBufferTypeDepth`, `MotionVectors`, `HUDLessColor`, `UIColorAndAlpha` | `:120-128` |
| Múltiplos viewports | Um único viewport/output por processo | `xemfg/native_bridge.cpp:166` |
| `slSetTag` sem token | Associado ao último `RenderSubmitStart` | `xemfg/native_streamline.cpp:270-278` |

## 6. Present e swapchain

- **Criação:** a proxy Intel é criada **na criação da swapchain do jogo** e exige o Streamline já isolado (`xemfg/native_bridge.cpp:167-170`). `CreateSwapChainForCoreWindow`/`ForComposition` não são cobertos. A fila tem de ser DIRECT (`:187`). Só um output (`:166`).
- **Resize:** `ResizeBuffers`/`ResizeBuffers1` invalidam epoch e frames e desligam o FG até novos inputs (`xemfg/native_swapchain.cpp:10-25`). Uma troca de formato para FP16 só marca `compatible=false` (passthrough sob a proxy Intel).
- **Recriação:** a aposentadoria e reutilização da swapchain nativa, com referências externas, fica em `xemfg/native_continuity.cpp`. O caso Black Flag (`0x887A002B` antes da geração) segue aberto (RC4).
- **HDR:** HDR10 RGB10A2 com `SetColorSpace1` repassado (`:131`). scRGB FP16 não é suportado (G12).
- **Vulkan vs DX12:** XeMFG é só D3D12 (`xemfg/native_bridge.cpp:364-366`).
- **Filas e fences:** a sincronização é do SDK Intel. Inputs voláteis são copiados na command list do produtor. Não há espera própria em Present.
- **Overlays:** o ReShade roda manualmente no output final (`xemfg/native_overlay.cpp`). O `QueryInterface` da proxy devolve `E_NOINTERFACE` para qualquer IID fora de `IDXGISwapChain*` (`xemfg/native_swapchain.cpp:43-52`). Overlays que pedem interfaces privadas recebem recusa. É intencional (protege ownership), mas não é registrado.

## 7. Diagnóstico

O que já existe e é bom: o log assíncrono JSON com eventos limitados, o bloqueio por requisito (`native_frame_admission.blocker`), os recibos de recursos, o relatório de compatibilidade v1 e, na RC5, `native_sleep_rejected` e `native_simulation_boundary_skipped`. Com isso foi possível diagnosticar Plague Tale e DS2 sem ter os jogos ao vivo.

O que falta para um jogo que você não tem:

1. **Cabeçalho de sessão:** horário real, executável, GPU/driver, versão do ReShade e lista de módulos relevantes (overlays, loaders, `sl.*` com versão de arquivo). Hoje há só `tick` em ms e várias sessões no mesmo arquivo.
2. **`sdkVersion` do `slInit`** e versão de arquivo de `sl.interposer`/`sl.dlss_g`/`sl.reflex`/`sl.pcl`.
3. **Estruturas recusadas por versão:** log de `structVersion` e `next` em `ReflexOptions`, `Constants` e `DLSSGOptions`. Hoje a recusa de `ReflexOptions` é silenciosa.
4. **Consultas de suporte do jogo:** resultado de `slIsFeatureSupported`/`slGetFeatureRequirements` para DLSS-G, Reflex e PCL, para separar "o jogo escondeu o FG" de "o FG chegou e foi recusado".
5. **Amostra de constantes:** uma amostra limitada de `mvecScale`, jitter, near/far e flags na primeira admissão, não só quando a máscara de problemas muda.
6. **Tags e viewports:** histograma de tipos de buffer recebidos (inclusive os não mapeados, como Backbuffer e linear depth) e de viewports vistos.
7. **Exportação automática:** gravar o relatório de compatibilidade no disco ao fechar a saída. Hoje ele só vai para a área de transferência, por botão.

## Proposta de refatoração em etapas

### Etapa 0 — Diagnóstico de capacidade (sem mudar comportamento)
Implementar os itens 1-7 acima. Critério: um log de um jogo desconhecido responde onde a cadeia parou (descoberta, suporte, opções, inputs, latência, output) e por quê.

### Etapa 1 — Convenções aprendidas, não tabeladas (resolve G1/G3, prepara G2)
- Adiar o init da proxy XeFG até as primeiras constantes válidas do viewport selecionado. Alternativa: manter o init atual e, na primeira divergência `ConstantDepth`/`ConstantJitterMode`, agendar uma recriação da saída na próxima fronteira segura, reaproveitando o caminho de resize e recriação que já existe e é testado ("same-window output recreation").
- Salvar a convenção aprendida em `MFGAdaUnlock.ini` (`[XeFG.Learned]`), identificada pelo hash do executável, para que a sessão seguinte já inicie certa.
- Remover `native_game_defaults.hpp`. Os overrides manuais continuam com prioridade.

### Etapa 2 — Tolerância de ABI por prefixo
- Para structs com versão maior que a conhecida e mesmo `structType`, ler o prefixo conhecido, encaminhar o original sem alteração e registrar o fato. Vale para `ReflexOptions` v2+ e `Constants` v3+. Recusar só quando o campo exigido não existir.
- `ReflexOptions` com `next`: encaminhar a cadeia intacta, copiando só o nó base para zerar `mode`/`frameLimitUs`.
- Detectar a versão do SL pelo `sdkVersion` e pela versão de arquivo do interposer; **não instalar** detours de entrada fora de 2.x.
- Fechar a lacuna de carregamento tardio com `LdrRegisterDllNotification` para `sl.interposer.dll`. A instalação dos hooks fica fora do callback do loader, numa fila consumida pelo worker.

### Etapa 3 — Camada de adaptadores de entrada
Interface única `FramePacket` (depth, MV, HUD-less/UI opcionais, constantes normalizadas, ID e viewport, lifecycle, command list do produtor) com estes adaptadores, cada um com o próprio contrato e fixture:
1. **Streamline DLSS-G**: o atual, extraído de `native_streamline.cpp`.
2. **XeFG nativo do jogo**: hoje o contexto do jogo recebe o modelo, mas fica no multiplicador do jogo (`xemfg/backend.cpp:325-336`). Aplicar o multiplicador do usuário e expor o mesmo painel.
3. **Entradas do upscaler (DLSS-SR via NGX evaluate, depois XeSS-SR e FSR)**: maior cobertura, porque quase todo jogo DX12 recente tem DLSS mesmo sem FG. Exige uma fonte própria de markers ou espera, e a HUD vai interpolada por padrão.
4. **FidelityFX FG (ffx_api)**: jogos com FSR3/FSR4 FG.

Seleção por capacidade: o primeiro adaptador com contrato completo para o frame vence; a presença de DLL nunca basta.

### Etapa 4 — Correções de convenção nos dados (resolve G2 e parte de G12)
- **MV com jitter:** um compute pass que subtrai a diferença de jitter numa textura própria e a marca como sem jitter. Também vale implementar ou obter o programa jittered. Validar com sequência conhecida (sample com câmera parada e jitter ativo ⇒ MV ≈ 0).
- **`cameraMotionIncluded=false`:** reprojeção da câmera somada ao MV de objeto (mesma infraestrutura).
- **FP16/scRGB:** conversão para RGB10A2 PQ na entrada da proxy, ou suporte direto quando o SDK permitir.
- **Backbuffer com subretângulo:** mapear `kBufferTypeBackbuffer` para `resourceBase/Size` do backbuffer.

### Etapa 5 — Perfis declarativos
Mover G4-G7 e G14 (e qualquer exceção futura) para `profiles.json` embutido como recurso, sobrescrevível por `MFGAdaUnlock.profiles.json` na pasta do jogo. Exemplo de entrada:

```json
{"match":{"module":"gameoverlayrenderer64.dll"},"policy":{"retain_native_output":true,"observer":"original_dlssg_functions"}}
{"match":{"exe_sha256":"…"},"conventions":{"depth_inverted":false},"evidence":"log 2026-10-20"}
```

Regras:
- o perfil só ajusta política, nunca habilita um caminho sem contrato;
- cada entrada guarda a evidência que a justificou;
- o log registra qual perfil foi aplicado.

O par Intel (G8-G10) continua pinado, mas como **catálogo de builds** (`binary_profiles.json` já existe no backend DLSS-G), com recusa explícita e o instalador da RC5 oferecendo o par certo.

## Testes sem os jogos

1. **Streamline SDK Sample** (repositório `NVIDIA-RTX/Streamline_Sample`, DX12): compilar contra várias tags do SL 2.x. Uma tag antiga, uma intermediária, a usada pelo Plague Tale (2.11.1) e a mais recente, mais as que trazem `ReflexOptions`/`Constants` novos. O sample tem DLSS-G, Reflex/PCL e controles de UI. Matriz por versão:
   - modo off/on/auto;
   - troca de resolução e modo de janela;
   - lifecycle `eOnlyValidNow` vs `eValidUntilPresent`;
   - HUD-less ligado e desligado;
   - `depthInverted` e `motionVectorsJittered` alternados (o sample permite editar as constantes);
   - Reflex off/on/boost.

   Critério: `native_frame_admission.blocker=="ready"` e inferência nos casos com contrato completo; bloqueio nomeado nos outros.
2. **Projeto UE5 com o plugin NVIDIA DLSS/Streamline** (UE 5.3, 5.4 e 5.5, um plugin de cada): cena simples com HUD UMG.
   - Variar `r.Streamline.DLSSG.Enable`, resolução dinâmica (extents variáveis), `r.Streamline.TagUIColorAlpha`, `r.Streamline.Reflex.*`, split-screen (dois viewports) e PIE no editor (viewport extra).
   - Empacotar como Shipping, para o layout `Binaries\Win64` exercitar também o instalador.
   - O UE dá cobertura real de MV com jitter, depth invertido e HUD separado.
3. **Fixture sintético existente** (`tests/xemfg_streamline_fixture.cpp`): ampliar para uma matriz de conformidade de ABI:
   - `ReflexOptions` v2 com `next`;
   - `Constants` v3;
   - `ResourceTag` com `next`;
   - backbuffer com subretângulo;
   - viewport de menu antes do principal;
   - `slInit` depois de 20 s (carregamento tardio);
   - interposer 1.x falso para confirmar que nenhum detour de entrada é instalado.

   Testes de controle negativo, rodados contra a RC selada, devem falhar onde a etapa correspondente ainda não existe.
4. **Intel XeSS SDK samples** (XeFG nativo) para o adaptador 3.2, e **AMD FidelityFX SDK samples** (FSR3 FG) para o 3.4.
5. **Conversão de MV com jitter:** teste numérico com uma câmera parada e jitter de Halton conhecido, em que o MV convertido tem de ser ≈ 0. Depois, a mesma imagem com XeFG em modo sem jitter, comparando PSNR entre frame gerado e real.

Nenhuma alegação de universalidade deve ser feita antes das etapas 1-3 passarem nessa matriz e em pelo menos um jogo real de cada adaptador.
