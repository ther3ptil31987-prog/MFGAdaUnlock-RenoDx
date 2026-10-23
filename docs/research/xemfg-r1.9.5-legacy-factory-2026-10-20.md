# XeMFG R1.9.5 — criação DXGI antiga no Control Resonant

20 de outubro de 2026. Candidata local na branch `feature/1.4.5-xemfg-compat-ui-r1.9`, a partir de `a003223a16c9336fe8ddf8746c16578a719eb145` (R1.9.4 e seu registro de validação no Pragmata). A release pública 1.4.5 e a árvore antiga modificada permanecem separadas.

## Diagnóstico

O usuário informou que o Control Resonant continuava aguardando, sem interpolar. Na inspeção inicial, o addon instalado tinha SHA-256 `66eb4e89bb6b695d476a42bc5431a33801fd627d68cee7834e0eca188e02af81`, correspondente à R1.9.4. O ReShade registrou a versão 1.4.5.23. Os runtimes Intel presentes eram o par admitido XeFG 1.3.1.78 / XeLL 1.3.0.5.

O log XeMFG do PID 20488 mostra inicialização dos runtimes, isolamento da apresentação NVIDIA em `slInit` e pedidos de FG habilitado na viewport 1, com opções versão 5. **Não há criação de contexto Intel nem inferência nessa sessão.** O log ReShade identifica duas chamadas de `IDXGIFactory::CreateSwapChain`, com flags `0x802`. A ponte até a R1.9.4 interceptava somente `IDXGIFactory2::CreateSwapChainForHwnd`; portanto, não criava sua saída Intel nessa rota. Isso distingue o caso do Control da recusa de profundidade e da aposentadoria da saída investigadas no Pragmata.

O addon deixou de estar presente na pasta durante o trabalho; o usuário confirmou sua remoção manual. O inventário inicial e os logs anteriores foram preservados. Essa remoção não é apresentada como causa da falha já observada no PID 20488.

## Implementação

A R1.9.5 instala também o hook de `IDXGIFactory::CreateSwapChain`, vtable slot 10, conservando o hook moderno no slot 15. Só anuncia instalação completa quando ambos estão disponíveis. A rota antiga aceita a fila direta D3D12 pelo contrato público e obtém `IDXGIFactory2` por QueryInterface.

São transferidos tamanho, formato, amostragem, uso, quantidade de buffers, efeito de swap e flags para `DXGI_SWAP_CHAIN_DESC1`. Refresh, scanline ordering, scaling e estado Windowed seguem no descritor de fullscreen. A representação moderna usa scaling stretch e alpha ignore, sem inventar stereo. O descritor do chamador não é alterado. A política existente de Force Borderless continua responsável pelas solicitações exclusivas.

A criação entra no mesmo `CreateOutput` das integrações já funcionais: valida arquitetura/runtime, ownership da apresentação, fila/dispositivo, formato e o par Intel; cria XeLL e XeFG; devolve o proxy de aplicação que controla input, Present, resize e teardown. Não há novo modelo, algoritmo de pacing ou relaxamento de lifetime.

Chamadas internas do SDK permanecem protegidas contra recursão e conservam o ReShade na saída final. Se a integração não é suportada, o entrypoint original recebe os argumentos originais. A marcação de janela interna é retirada antes desse fallback, para não atribuir ao Intel um output nativo criado por um encaminhamento entre as APIs antigas e modernas.

## Testes

O smoke isolado ganhou uma seleção `--legacy-factory` que efetivamente chama a API antiga. Ele verifica descritor do chamador intacto, fallback FP16 sem contexto Intel ou overlay manual, geração Tensor 4x, rotas Reflex → XeLL e Reflex → Reflex, resize, recriação na mesma janela e encerramento. A variante com Streamline real repete quatro ciclos de inferência/criação/liberação pela API antiga em cada rota de latência.

O mesmo executável novo, com a R1.9.4 selada, falha com `native output/Streamline/XeLL not ready`. O fallback FP16 continua funcionando nessa referência. Essa é uma reprodução negativa da falta do hook; não é um benchmark do jogo.

Na primeira passagem dos novos testes, os filhos completaram inferência e encerramento, mas duas verificações Python exigiam exatamente todos os eventos de criação. O logger assíncrono descarta mensagens ao disputar sua trava, por projeto. A aceitação foi corrigida para usar as verificações diretas de API/SDK/lifetime feitas pelos filhos; a quantidade de eventos permanece no recibo como evidência auxiliar. Não foi alterado o logger nem bloqueado o produtor para satisfazer o teste.

Os resultados finais da suíte, a matriz adicional de runtimes e o SHA-256 da candidata são registrados no manifesto e em `evidence/validation-summary.json` do pacote. A suíte inclui os quatro novos cenários e preserva as verificações das rotas modernas.

A suíte completa concluiu **98 testes aprovados, três opcionais ignorados e nenhuma falha**, em 250,98 s. Os opcionais são `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`. Passaram também as rodadas adicionais com XeLL 1.3.2.10 nas duas saídas, o layout previamente carregado com Reflex e as DLLs Streamline copiadas do próprio Control nas saídas XeLL e Reflex. Estas últimas repetiram a inferência e o teardown em quatro recriações pela API antiga; continuam sendo processos isolados com inputs sintéticos, não gameplay do Control.

Emissão 1.4.5.24, SHA-256 do addon `bce5e0cef035e546ef02783a7efa282d3eee71615f08f94d6430924ae5f38ba3`.

## Evidência e limites

Na árvore principal, `out/xemfg-r1.9.5-evidence/control-baseline/` conserva logs/INIs e seus hashes. `control-baseline.json` registra o PID e as identidades iniciais; `legacy-negative.json` conserva a falha do teste com a R1.9.4. Os diretórios de cada teste isolado ficam na worktree, em `build/tests/native-checks/`.

A correção fecha uma lacuna demonstrada na criação da saída. No momento em que o pacote foi selado, a interpolação em cena ainda não havia sido validada. A confirmação posterior está registrada abaixo; o pacote e seu manifesto permanecem imutáveis. Não houve captura PresentMon nem medição de ganho de FPS, cadência exibida ou latência física nesta correção.

A R1.9.4 já interpolou no Pragmata em uma sessão separada com Steam overlay desativado. A compatibilidade com o overlay Steam ativo continua aberta e não é resolvida por este hook. Essa limitação do Pragmata não é automaticamente atribuída ao Control.

Instalação somente com o jogo fechado, mantendo ReShade, RenoDX, INIs e o par Intel admitido. O instalador salva o estado real encontrado, inclusive a ausência de addon após a remoção manual. Para desfazer a instalação, usar `Restore-MFGBackup.ps1` com esse snapshot; isso pode voltar a deixar o jogo sem addon MFG. A R1.9.4 anterior fica arquivada no pacote e a 1.4.5 oficial fica em `rollback/`. Não há publicação automática nem dependência de OptiScaler/ASI.

Contrato consultado: [Microsoft, IDXGIFactory::CreateSwapChain](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-createswapchain), que admite uma fila direta D3D12 como `pDevice`.

## Validação posterior em cena — PID 13708

O usuário informou que o jogo carregou e parecia gerar quadros. A cópia do log do PID 13708 cobre 129,344 s e confirma execução na R1.9.5 de hash indicado acima, com o mesmo par Intel admitido:

| Observação | Resultado |
|---|---|
| Criação pela API antiga | Duas saídas substituídas pelo Intel, ambas com resultado zero; formatos R10G10B10A2, flags `0x842` e `0x802` |
| Inputs | Amostra de profundidade, movimento e cor com `resources=7`, `markers=31`, `constants=1`, `valid=1`, `failed=0` na viewport 1 |
| Interpolação 4x | 14 amostras de SDK com `enabled=1`, `frames_presented=4` e resultado zero |
| Interpolação 6x | Uma amostra com `enabled=1`, `frames_presented=6` e resultado zero, após troca solicitada pelo usuário |
| CUDA/Tensor | Rota ativa em compute capability 8.9; contador chegou a 184.720 lançamentos |
| XeLL | Dois vínculos de contexto aceitos, resultado zero |
| Flip metering | Configurações 4x aceitas com `status=0`, `feedback=0`, `metered=true`; amostras de deadline indicaram metering ativo em 4x e 6x |

A amostra explícita de admissão completa antecede a ativação de FG; não é uma auditoria de cada frame posterior. A confirmação de interpolação vem dos resultados do SDK e dos lançamentos efetivos. Não houve resultado negativo de interpolação nas amostras habilitadas. Uma reativação de FG registrou o aviso 3 (`XEFG_SWAPCHAIN_RESULT_WARNING_TOO_FEW_FRAMES`); a amostra seguinte, 1.000 ms depois, já mostrou saída 4x com resultado zero. Esse intervalo entre amostras não mede a duração exata do aviso.

A primeira saída de boot foi aposentada com `destroy_result=0` e `remaining_refs=0`. **O log copiado não contém recibo de aposentadoria do contexto final nem de shutdown Streamline**, portanto não se anuncia encerramento completo validado nesta sessão. Não há `startup_rejected`, `native_swapchain_init_failed` ou `native_retirement_pending` no trecho. Há avisos de referência de factory no log ReShade; a ativação bem-sucedida não os transforma em prova de lifetime geral correto.

UI Composition permaneceu desligado no evento registrado. O usuário alternou FG e multiplicador e deixou 6x na configuração; essa preferência foi preservada. A sessão tem dois registros `pacing_hitch`, sem captura PresentMon ou latência física. Interpolação funcional e aceitação do driver não demonstram cadência perfeita nem ganho de desempenho.

Logs, configurações, resumo e manifesto de hashes estão em `out/xemfg-r1.9.5-evidence/control-gameplay-13708/`, relativo à árvore principal. Esta validação confirma a ativação no Control nesta configuração; não generaliza suporte a outras integrações nem modifica a limitação conhecida do overlay Steam no Pragmata.
