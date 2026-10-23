# XeMFG 1.5.0-rc.2 — compatibilidade e associação da espera

21 de outubro de 2026. Base local limpa: RC1, commit
`d3053148c1f4eec0a25042cb3277784092168620`, na mesma worktree unificada
`feature/1.4.5-xemfg-reframework-r1.12`. A árvore RD9 antiga e os artefatos
selados da RC1 foram preservados. Esta candidata permanece local: sem push,
tag, release pública ou instalação automática nos jogos.

## Diagnóstico separado por falha

| Jogo / sintoma relatado | Primeira evidência observável nesta rodada | Limite da conclusão |
|---|---|---|
| TLOU2 não interpola | RC1 exata instalada; recursos, constantes e opções chegam. No rastreio PID 18884, as 232 chamadas de sleep amostradas chegam depois de SIMULATION_END do mesmo token. A ponte exigia a espera antes dos marcadores desse token e recusava os frames | Defeito de associação confirmado; validação da RC2 no jogo ainda pendente |
| Resonance: A Plague Tale Legacy não interpola | Executável em `C:\XboxGames\Resonance- A Plague Tale Legacy\Content`. RC1 recebe recursos/constantes, mas apresenta mask=0 e ausência de espera correspondente; as DLLs reais conseguem inferência no processo isolado com ordem convencional | Sintoma compatível com associação de latência; a ordem completa no jogo não foi capturada, portanto a mesma causa ainda é hipótese |
| Requiem crash/congelamento no boot | Tentativa inicial para após primeira apresentação; não há dump ou motivo causal capturado. Em nova execução da mesma RC1, PID 6540, boot, recriação e inferência funcionaram com REFramework e overlays preservados | Intermitente e não resolvido por demonstração. O diagnóstico modifica timing e pode ocultar uma corrida |
| Pragmata crash/congelamento no boot | Mesma RC1 abriu e gerou na repetição, PID 6168; log registra 56.160 Tensor launches e estado ativo. Usa REFramework antigo, sem ABI cooperativa V1 | Intermitente. Inferência nessa sessão não certifica boot repetido nem elimina o problema do monitor de hooks antigo |
| Onimusha | Usuário esclareceu que o sintoma é o mesmo grupo de boot de Requiem/Pragmata. Registro anterior chega à criação da saída, sem dados suficientes para localizar a falha seguinte | Ainda não reproduzido com pilhas nesta rodada; nenhuma declaração de correção |
| AC Black Flag Resynced | Usuário informou `DX12 Error 0x887A0005`; logs conservados não contêm o motivo específico do dispositivo | Perda do dispositivo é o resultado, não a causa; acrescentado diagnóstico no retorno de Present |

No novo registro do Black Flag (PID 22968), houve aposentadoria da primeira
saída, retenção da swapchain nativa e reutilização com resultado zero na
segunda saída. O último status continua FG desligado, sem entradas e com zero
Tensor launches registrados. Portanto, a falha observada precede a ativação
da interpolação; o caminho de criação/reutilização/apresentação precisa ser
examinado antes de atribuir o erro ao modelo. A sequência temporal não prova
que a reutilização causou a perda. Não se alterou essa política sem uma
comparação de runtime que demonstrasse a dependência.

A documentação Microsoft identifica
[`DXGI_ERROR_DEVICE_REMOVED`](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error)
e fornece
[`GetDeviceRemovedReason`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getdeviceremovedreason)
para obter o motivo mais específico. Não foi demonstrado nesta rodada qual
comando, fila, overlay ou componente iniciou a perda no Black Flag.

## Correção da associação da latência

O rastreio de TLOU2 distingue o token de uma simulação já concluída do momento
em que o jogo espera antes da próxima. Os eventos são amostrados por
`id % 64 < 2`; os saltos entre 65 e 128 no arquivo não demonstram saltos reais
entre frames consecutivos. Os pares consecutivos capturados incluem sleep(64)
seguido de input/simulação(65).

A RC2 conserva separadamente os marcadores observados na origem e os aceitos
pelo XeLL. Se a simulação do token N terminou antes da chamada de espera, a
espera é associada ao token N+1 do XeLL. A chamada nativa de Reflex, quando
selecionada, conserva o token N original e a thread original. Não há replay de
marcadores antigos, espera extra, fabricação de START/END ou remoção de fences.

Uma espera anterior à simulação continua associada a N. Espera no meio de uma
simulação, wrap para o ID reservado zero, próxima simulação já iniciada ou
contexto aposentado não autorizam o caminho novo. A resolução do alvo fica
registrada por chamada de origem para deduplicar aliases/repetições.

Somente o token exato seguinte pode consumir a espera: um salto de IDs não é
preenchido por heurística. O primeiro frame sem espera anterior é passado sem
geração. Recursos, viewport, constantes, marcadores, epoch, reconhecimento de
FG/Reflex e sucesso do SDK continuam necessários para admitir os seguintes.
Resize/recriação limpa a associação junto com o FrameStore.

O [guia XeLL fixado](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xell_developer_guide_english.md)
exige a espera antes dos marcadores do frame e admite sobreposição de fases.
A ponte preserva as recusas reais do SDK; não trata uma API disponível como
prova de aceitação dos marcadores ou de exibição no monitor.

## Outras mudanças restritas

- A consulta de fullscreen agora termina antes de ler seu parâmetro de saída.
  As antigas chamadas `Observe(SUCCEEDED(Query(&x)), x)` dependiam da ordem de
  avaliação dos argumentos e podiam observar o valor anterior de x. O teste
  cobre exclusivo, janela e falha da consulta. Isso corrige um defeito de código;
  não demonstra que ele causava os crashes de boot relatados.
- Rejeições de marcadores têm motivo e resultado do SDK, com orçamento limitado
  e somente quando o diagnóstico opcional está ativo.
- Um Present falho registra HRESULT original, motivo do dispositivo, output,
  frame, epoch e estado de FG antes da consulta de status do SDK. Não há retry,
  transformação do erro em sucesso ou consulta de dispositivo em cada Present
  bem-sucedido.

## Preservação e estado informado pelo usuário

| Jogo | Estado informado antes da RC2 | Certificação nova? |
|---|---|---|
| Dragon's Dogma 2 | Funcionando | Não |
| Control Resonant | Funcionando | Não |
| Cyberpunk 2077 | Funcionando | Não |
| The Witcher 3 Remastered | Funcionando | Não |
| Alan Wake 2 | Funcionando | Não |
| Monster Hunter Wilds | Desconhecido | Não |

Os addons instalados não foram substituídos. Steam Overlay, RenoDX e
REFramework não foram desativados ou trocados. Ativou-se temporariamente
`Diagnostics/StallTrace` em TLOU2, Requiem e Pragmata, com backup, e apenas essa
preferência foi restaurada depois da coleta. Mudanças do usuário em outras
opções foram preservadas. Recibos de restauração acompanham a evidência local.

## Evidência automatizada e limites

O teste novo reproduz a espera após a simulação numa thread enquanto render e
Present do frame anterior avançam em outra. Verifica inferência real Intel,
primeiro frame em bypass, recuperação, deduplicação de espera e token original
de Reflex. A RC1 falha nesse cenário; as variantes XeLL/Reflex corrigidas
passaram nos testes focados. O teste puro também recusa espera no meio da
simulação e ID reservado/wrap.

Explorações com DLLs genuínas são guardadas separadamente da suíte registrada.
Com Streamline do TLOU2, houve inferência/recriação, seguida de AV em
`sl.dlss_g.dll+0x1c4d7` no encerramento tanto com RC1 quanto com uma candidata
RC2. Não se converteu a mensagem PASS anterior à saída em sucesso do processo.

Uma exploração inicial de Plague Tale, usando XeLL **1.3.2.10**, com nova thread de sleep por frame
conseguiu inferência, mas a primeira recriação incluiu recusas XeLL -4
(`INVALID_ARGUMENT`) para RenderSubmitEnd/PresentStart; o frame foi recusado
e os seguintes recuperaram. O oracle estrito de warmup falhou. Esse recibo foi
preservado; a opção explícita `--overlap-tail-sleep` mantém o cenário disponível.
O teste serial com DLLs genuínas e o teste concorrente com origem Streamline
sintética são cenários diferentes e não devem ser apresentados como uma única
certificação de todos os caminhos concorrentes dos jogos.

### Resultado final desta rodada

Ambiente: Windows x64, RTX 4070 SUPER, driver 617.42 e MSVC 19.44 / VS 2022.

| Verificação | Resultado efetivo | Escopo |
|---|---|---|
| Build Release + suíte completa | 128 passaram, 3 opcionais ignorados, 0 falhas; 365,48 s | 131 casos registrados; 52 recibos de integração |
| Build Debug + suíte completa | 128 passaram, 3 opcionais ignorados, 0 falhas; 416,07 s | Mesma cobertura, assertions e CRT Debug |
| Controle negativo final, RC1 exata + novo caller | Falha esperada: `tail wait did not reach the following source frame` | Mesmo XeLL 1.3.0.5 dos jogos; prova do defeito reproduzido no protocolo |
| Nova associação em 4x, XeLL e Reflex | Passaram em ambas as configurações | Sobreposição de threads, primeira passagem recusada, recuperação, espera duplicada deduplicada e token original de Reflex verificado |
| Nova associação em 6x, XeLL e Reflex | 2/2 smokes adicionais passaram | SDK real, caller Streamline sintético e XeLL 1.3.0.5 |
| Plague Tale: DLLs reais + XeLL 1.3.0.5 instalado no jogo, espera sobreposta | Passou: inferência, quatro recriações e encerramento, exit 0 | Caller isolado; não é uma sessão do jogo |
| Plague Tale: DLLs reais + XeLL 1.3.2.10, espera serial | Passou: inferência, quatro recriações e encerramento, exit 0 | Não substitui o cenário sobreposto que recusou um frame com esse XeLL |
| TLOU2: DLLs reais + XeLL 1.3.0.5, espera sobreposta | Inferiu nas quatro recriações; processo terminou com AV, exit `0xC0000005` | Não contado como teste bem-sucedido; jogo/encerramento da RC2 ainda não certificado |
| TLOU2: RC1 original + mesmo XeLL 1.3.0.5, ordem convencional | Inferiu nas quatro recriações; mesma falha ao encerrar | A falha não foi introduzida pela nova associação; causa não isolada |
| TLOU2: controle sem addon | Falhou antes, no Present com `0x80004004` | Controle não comparável para atribuir a falha posterior ao addon ou à NVIDIA |
| Controlador de janela | Testes de exclusivo/janela/consulta falha e `/analyze /W4` passaram | Análise estática restrita a `native_window.cpp` |

Os três opcionais são `provider_identity`, `engineering_manifest` e
`release_1_4_5_manifest`, sem os respectivos inputs explícitos históricos/offline.
Nenhuma falha exploratória foi removida da evidência ou convertida em sucesso
por conter uma mensagem PASS anterior à saída do processo.

O XeLL 1.3.0.5 tem SHA-256
`61aeeab7fdcfb6d1742f86c14c68c663d759607d01a6132645c857291eaebb86`;
o 1.3.2.10 tem
`d2030dcd694fda8f2ec7e044b13e6db8f0b56d4ba9113a5efad334e3f3ded8c7`.
Os testes de sobreposição com versões diferentes não foram misturados para
concluir compatibilidade. A candidata preserva as DLLs instaladas e não exige
upgrade para testar essa correção.

Binário Release SHA-256:
`4495326c100fe70972e32376a14bff02600c333c387c898fe07f1e5727c2cc47`.
Debug SHA-256:
`5e70d59ccf688c3d01ed1130ec5bfa58bf3301c62788668d91d096ce5fca1494`.
Versão PE `1.5.0.2`, produto `1.5.0-rc.2`. Commits de implementação:
`f9e39fb` (fullscreen) e `ebe3507` (associação, testes e diagnóstico).

Os resultados finais de compilação e regressão, identidades dos binários,
recibos por configuração e commit final ficam no manifesto do pacote local.
Não houve benchmark pareado, avaliação cega de pixels, medida física de
latência ou nova captura PresentMon para esta candidata. Falhas de boot,
Black Flag e compatibilidade final dos jogos continuam pendentes de evidência
causal/runtime; nenhuma superioridade de FPS ou fluidez é anunciada.

A entrega fica em `out/1.5.0-rc.2/`, na raiz original do workspace, e no ZIP
adjacente. Release está na raiz do pacote e Debug em `debug/`; PDBs, logs,
manifesto, checksums e rollback ficam separados. O commit final de documentação
e empacotamento é registrado no manifesto. A candidata RC2 não foi instalada
em nenhum jogo durante esta rodada.
