# XeMFG R1.9.3 — liberação da apresentação com chamada pendente

20 de outubro de 2026. Base R1.9.2, commit `96b9038d5c1add9efec9623fd0851feacc832ee3`, na branch isolada derivada da 1.4.5 oficial. Sem nova release automática.

## Resultado negativo no Pragmata

A R1.9.2 **não resolveu o boot**. O usuário informou que o jogo abria e congelava. O PID 25360 completou a desativação física dos callbacks DLSS-G pelo Streamline e criou uma saída Intel, mas não chegou à inferência. Depois registrou 99 falhas de inicialização (-17), com `0x80070005`, durante tentativas de recriar a saída de 2560×1440 para a mesma janela. Isso caracteriza um loop de tentativas de criação; não há dump provando um deadlock de mutex nesse processo.

O REFramework também registrou resets e reinstalações de hooks nessa sequência. A participação desses hooks ainda não foi isolada. Não é correto declarar o REFramework como causa com base apenas nesses logs. A reprodução com Streamline/ReShade/Intel e o addon RenoDX do Pragmata, fora do jogo, completou quatro recriações; ela não reproduz o motor e todos os hooks instalados pelo REFramework.

Os arquivos da tentativa foram preservados em `out/xemfg-r1.9.2-evidence/pragmata-freeze/`: log do addon, ReShade, REFramework e configurações. O processo já estava fechado quando consultado. Foi restaurado o snapshot `PRAGMATA-20261020-130047-7919`, com o addon oficial 1.4.5 e o par Intel original, antes de preparar a nova candidata.

## Defeito de ciclo de vida reproduzido

A saída usa `shared_ptr<Output>` para manter os contextos válidos durante chamadas de latência e diagnóstico. Antes desta correção, a destruição do contexto FG e a liberação da janela dependiam da destruição final de `Output`. Assim, liberar a swapchain da aplicação não bastava quando uma dessas chamadas ainda retinha uma referência. Uma saída seguinte podia encontrar a apresentação antiga viva.

O novo teste suspende a chamada Reflex original em uma fixture, conservando a execução real do Intel SDK e ReShade. Com a chamada pendente, libera a swapchain da aplicação e tenta religar uma nova saída à mesma janela. **A emissão selada R1.9.2 falha**: `latency lease retained the old FG presentation context`. A correção passa: o contexto FG anterior é destruído, a janela aceita a nova saída e só depois a chamada antiga é liberada. Seu XeLL continua válido e o frame da geração aposentada não é publicado como atual.

Esse teste demonstra o defeito e a correção do contrato. **Ainda não demonstra que essa foi a referência responsável pelo bloqueio observado no Pragmata.** É necessária nova verificação de boot e dos registros de aposentadoria; referências externas de backbuffer e a interação entre hooks continuam hipóteses distintas.

## Implementação

- A swapchain da aplicação chama `RetirePresentation` depois de liberar sua referência ao proxy Intel, antes de abandonar a seleção global. O método serializa Present e acesso aos inputs, marca a saída aposentada e destrói o contexto FG.
- As referências compartilhadas ainda em voo conservam o XeLL, o dispositivo e a fila. O XeLL só é destruído quando essas referências terminam. Não há liberação de um contexto durante sua chamada `xellSleep`.
- Inputs e consultas de FG não usam um contexto aposentado. A conclusão de uma chamada de uma epoch antiga é recusada. O painel copia o identificador de pacing sob a trava da saída.
- Se o SDK recusar a destruição, suas dependências continuam preservadas. Os novos registros identificam a saída, resultado de desativação/destruição, presença de sleep em voo e referências remanescentes no output final. Esses números são evidência de lifecycle, não métricas de exibição.

O contrato [DXGI de criação por HWND](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd) admite somente uma swapchain flip por janela. O código [D3D12Hook do REFramework](https://github.com/praydog/REFramework/blob/master/src/D3D12Hook.cpp) confirma que o projeto faz reset e reinstala hooks em criação de swapchain; a versão consultada serve para compreender o mecanismo, não para atribuir todos os detalhes ao binário instalado. Nenhum código desse projeto foi importado nesta correção.

Mantidos: 4x e demais multiplicadores suportados, modelo CUDA/DXIL, flip metering, rotas Reflex/XeLL, UI Composition opcional desligado por padrão e aparência compacta. Sem blur, cap adicional, alteração de motion vectors ou remoção de waits de recursos. O SDK continua responsável por encerrar seus workers antes da destruição de suas dependências.

## Verificação e entrega

O pacote exige a suíte atualizada, a regressão com chamada pendente, a matriz de runtime/latência e as quatro combinações de Streamline real. Hashes e recibos acompanham `evidence/validation-summary.json`. Comparações CUDA continuam cobrindo numericamente nove de 17 programas; testes de integração não validam imagem, cadência exibida ou latência física.

Emissão 1.4.5.22, SHA-256 `8e4ffb153b40bed7d54d0c51f0a037d41773190f9c02f24b8bc139fc34f694c2`. A suíte concluiu **92 testes aprovados, três opcionais ignorados e nenhuma falha**, em 140,44 s. Os opcionais são `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`. A regressão negativa da R1.9.2 ficou no diretório local `native-20261020-131934-988724`; a primeira prova positiva, antes da emissão final, em `native-20261020-131951-825675`. Os recibos do pacote são da emissão final, incluindo o registro da destruição do FG com `sleep_in_flight=true` e retorno zero.

Rollback no Pragmata: fechar o processo e restaurar o snapshot do instalador. A R1.9.2 permanece arquivada como emissão que falhou no jogo, não como fallback recomendado. A R1.9.3 continua candidata até confirmar boot e execução da feature no log da cena. Nenhuma alegação de melhoria de FPS ou fluidez decorre da aprovação offline.
