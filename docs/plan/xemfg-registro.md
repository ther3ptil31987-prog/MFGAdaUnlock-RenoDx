# XeMFG universal — registro de execução (2026-10-22)

Branch `feature/1.5.0-xemfg-universal`, a partir da tag `rc5-baseline`. Plano: `docs/plan/xemfg-plano.md`, com o detalhe técnico de cada item no log de execução. Relatório de origem: `docs/research/xemfg-universalidade-2026-10-21.md`.

Addon final: `build/xemfg-final/renodx-mfgunlock.addon64` (SHA-256 no `.sha256` ao lado). Gate final: `ctest -C Release` com 169 testes, todos aprovados, backlog vazio; replay com 9 sessões de campo.

## O que foi feito

### Fase 1: diagnóstico e infraestrutura de teste
- Corpus de replay de sessões de campo e analisador `tools/xemfg_triage.py` (rota, FG pedido, interpolação, bloqueio e causa por sessão).
- Novos eventos de diagnóstico:
  - cabeçalho de sessão e inventário de módulos;
  - versões do Streamline;
  - erros e log do XeLL;
  - structs recusadas;
  - consultas de suporte;
  - amostra de constantes;
  - histograma de tags;
  - relatório de compatibilidade gravado em disco.
- Matriz de conformidade por fixture (`xemfg_conformance_*`) e builds do Streamline_Sample oficial nas versões 2.7.32, 2.9.0, 2.11.1 e 2.14.16.

### Fase 2: robustez de protocolo
- **2.0 (Plague Tale passou a interpolar):** o XeLL entra em modo estrito após 16 esperas aceitas. O Plague Tale chama `slReflexSleep` com token 0 em todo frame. A ponte só despacha ids mais novos que o último.
- Leitura por prefixo de structs mais novas (ReflexOptions v2/`next`, Constants v3, DLSSGOptions, DLSSGState, ResourceTag).
- Nenhum hook em interposer SL 1.x, onde antes o processo caía.
- Interposer carregado tarde, via `LdrRegisterDllNotification`.
- RTX 20/30 sem DLSS-G nativo coberto pelo XeMFG (validado só por fixture).

### Fase 3: universalidade sem código por jogo
- `profiles.json` embutido mais override opcional na pasta do jogo.
- Convenções de profundidade/jitter aprendidas por executável.
- `native_game_defaults.hpp` removido: os casos de TLOU2, AW2 e DS2 viraram perfis com evidência.

### Fase 4: correções de dados
- **4.2, MV com jitter (DS2):** compute pass que remove o jitter do MV. O XeFG roda com o programa sem jitter, que existe no conjunto portado. Teste numérico em WARP com erro < 1e-3 px.
- **4.3:** backbuffer com subretângulo.
- **4.4:** swapchain FP16/scRGB. A proxy Intel roda em HDR10 e um pass converte scRGB para PQ BT.2020 no Present. Teste numérico com erro ≤ 1 código de 10 bits.

### Fase 5: adaptadores de entrada
- **5.1:** interface neutra `FramePacket`; o adaptador Streamline só traduz.
- **5.2:** o XeFG do próprio jogo usa o multiplicador do usuário (máximo elevado na init, contagem substituída).
- **5.3:** adaptador FidelityFX FG (FSR 3.1, `ffx_api`). Markers e espera XeLL são sintetizados em torno do Present, por decisão do usuário. Desliga com `[Adapters] FidelityFX=0`.
- **5.4:** adaptador DLSS-SR experimental via NGX, desligado por padrão (`[Adapters] DlssSr=1`).
- **Backlog `menu-viewport-first`:** a seleção de viewport agora segue o viewport com entradas completas, por decisão do usuário.

### Correções vindas dos testes de campo (F.1)
- **DS2:**
  - o SDK cria a swapchain do jogo noutra thread, e um guard `thread_local` deixava a ponte empilhar uma saída Intel sob o XeFG do jogo (o jogo caía em seguida);
  - o DS2 tagueia depth/MV duas vezes por frame, e o 2º tag derrubava todos os frames.
- **Mafia:** o primeiro guard novo bloqueava jogos que deixam um contexto XeFG "sonda" vivo sem inicializar. Agora só contam os contextos do jogo inicializados e as inits em andamento.
- Cada uma dessas correções tem caso de conformidade próprio: `native-game-xefg` (thread de trabalho), `duplicate-tags` e `game-xefg-probe`.

## Instalação e testes de campo (F.1)

Addon instalado, com backup do anterior em `MFGAdaUnlock-profiles/backup-1.5.0-final-<data>/` de cada jogo (o primeiro backup de cada pasta tem o addon original). Os testes foram automáticos: o jogo é aberto, espera 120–180 s sem nenhuma entrada, e o log é triado. Mafia, DS2 e Plague Tale foram retestados com o build final (`8fab1468…`); os demais rodaram com o build anterior às correções de sonda/thread, cujo caminho de saída do Streamline é o mesmo.

| Jogo | Resultado com o addon final |
|---|---|
| Resonance: A Plague Tale Legacy | **Interpolando** (3 frames por Present, resultado 0) |
| Death Stranding 2 | **Interpolando** (dejitter ativo, 4 frames por Present); sessão no corpus de replay |
| Alan Wake 2 | **Interpolando** |
| CONTROL Resonant | **Interpolando** |
| Dragon's Dogma 2 | **Interpolando** (6x) |
| Onimusha: Way of the Sword | **Interpolando** |
| PRAGMATA | **Interpolando** |
| Resident Evil Requiem | **Interpolando** |
| The Last of Us Part II | **Interpolando** |
| Mafia: The Old Country | **Interpolando** depois da correção da sonda (o 1º teste de campo revelou a regressão) |
| AC Black Flag Resynced | **Interpolando (6x)** depois da regra de perfil `ac-black-flag-no-output-retention`; ver abaixo |
| Cyberpunk 2077 | **Recusado na partida**: uma atualização do Steam (22/10, 13:46) trocou os runtimes do jogo por `libxess_fg` 1.1.0.19 / `libxell` 1.1.0.6, um par não revisado |
| Monster Hunter Wilds | Não testado: o jogo não está mais instalado (sobrou só o addon) |

Não alterados: Expedition 33, Silent Hill 2 e The Great Circle usam builds antigas e menores do addon (de antes do XeMFG nativo), e The Great Circle é Vulkan. Substituí-las mudaria o produto nesses jogos.

### AC Black Flag (isolado pelo usuário em 2026-10-22)
O usuário isolou o problema: com as DLLs do XeFG trocadas pelo par revisado, o jogo dá erro de DX12 ao iniciar. É o par que permite ao XeMFG rodar; o erro vem do que acontece depois. Com o overlay do Steam, a ponte retém a swapchain nativa e a reutiliza quando o jogo a recria, e nesse jogo o 1º Present da saída reutilizada perde o device (`DXGI_ERROR_DEVICE_REMOVED`, motivo `0x887A002B`). Isso aconteceu nas 6 sessões que reutilizaram, e a única que interpolou (21/10) não reutilizou. Com `RetainNativeOutput=0` o jogo rodou e interpolou a 6x. Correção: regra de perfil `ac-black-flag-no-output-retention` (só o executável `ACBlackFlag.exe`), com teste unitário; revalidada no jogo (6 frames por Present, nenhuma perda de device).

## O que não foi feito ou ficou pendente
1. **4.5, projeto de teste UE5:** bloqueado, porque o Unreal Engine 5 não está instalado. Pulado por decisão do usuário.
2. **Cyberpunk 2077:** precisa do par de runtimes revisado de volta. O botão de instalação do XeSS no painel faz isso; também dá para copiar as cópias verificadas de `build/xemfg-offline-reference/` (com backup dos arquivos do Steam). A cópia automática foi bloqueada pelo sistema de permissões e ficou para o usuário decidir.
3. **4.4:** HUD-less FP16 ainda é filtrado em jogos scRGB, e o XeFG roda sem ele. Falta converter também o HUD-less, inclusive a cópia volátil.
4. **5.3 FidelityFX:**
   - não testado em jogo FSR real;
   - a UI registrada via `REGISTERUIRESOURCE` é ignorada;
   - interpolação manual (lista/textura do jogo) é recusada;
   - o texto de status ainda diz "Streamline inputs".
5. **5.4 DLSS-SR:**
   - projeção assumida (o NGX não tem câmera);
   - não cobre jogos que usam DLSS-SR pelo Streamline;
   - não testado com o `_nvngx.dll` real do driver.
6. **4.2 dejitter:** a convenção de sinal (a mesma do FSR2) foi validada pelo teste numérico e pelo DS2 interpolando. Uma comparação visual (PSNR entre frame gerado e real) não foi feita.
7. **Seleção de viewport:** o ramo "selecionado 30 frames sem entradas" não tem cenário próprio.
8. **Causa-raiz no AC Black Flag:** a reutilização da swapchain nativa retida (`native_output_reused`) perde o device no 1º Present nesse jogo, mas funciona em CONTROL, DD2, Onimusha, PRAGMATA e RE Requiem. Por que só no AC ainda não foi entendido; a regra de perfil contorna o problema.
9. **RTX 20/30:** a cobertura sem DLSS-G nativo foi validada só por fixture (a máquina tem RTX 4070 SUPER).

## Notas para as próximas etapas
- O gate depende de `XEMFG_OFFLINE_FG/LL/RESHADE` no cache do CMake. Eles agora apontam para `build/xemfg-offline-reference/`, cópias com hash verificado do par 2.x e do ReShade, e não mais para a pasta do Cyberpunk (a atualização do Steam apagou os DLLs no meio da execução).
- Cenários que precisam de um processo sem Streamline usam executável próprio (`xemfg_ngx_smoke`), porque o smoke principal carrega o fixture SL logo no início.
- Fixtures com várias funções de corpo idêntico precisam de `/OPT:NOICF`; sem isso o linker as funde e os hooks se confundem (aconteceu no fixture FFX).
- Os testes automáticos de campo não interagem com o jogo. Um jogo que só pede FG no gameplay aparece como "FG não pedido".
