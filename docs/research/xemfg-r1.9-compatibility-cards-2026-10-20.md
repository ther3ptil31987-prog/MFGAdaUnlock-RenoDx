# XeMFG R1.9 — compatibilidade e cartões compactos

20 de outubro de 2026. Branch `feature/1.4.5-xemfg-compat-ui-r1.9`, derivada da R1.8 (`8f7a45eedb5a54f4c29af1420facc8216fe8ca77`). DLSS-G permanece na base oficial 1.4.5. O usuário escolheu Pragmata para a primeira validação em jogo.

## Diagnóstico dos registros locais

| Jogo | Evidência anterior à instalação desta candidata | Ação preparada |
|---|---|---|
| Pragmata | XeFG 1.2.0.87 + XeLL 1.2.0.9 recusados por `unreviewed_Intel_runtime_pair`; cópias antigas na raiz e em `_storage_`. A instalação atual do addon era a 1.4.5 oficial, após reinstalação do usuário. | Instalar a ponte nativa R1.9 e o par Intel validado nos locais existentes, preservando os arquivos anteriores. |
| CONTROL Resonant | Ausência de `libxess_fg.dll` e `libxell.dll`; `hash_file_open` no registro XeMFG. | Instalar o par validado e a candidata; conferir a captura da factory e os inputs em uma cena nova. |
| Requiem | `loaded_runtime_path_differs` em uma tentativa XeMFG anterior; o loader utiliza `_storage_`; os hashes Intel atuais são admitidos. Configuração atual selecionava DLSS-G. | Aceitar a identidade validada da instância carregada; preservar a escolha de backend até um teste controlado. |
| Onimusha | Tentativa XeMFG anterior recusada por caminho; `dxgi.dll` atual identifica OptiScaler 10.0.0.1, com ReShade separado. | A ponte independente requer ReShade direto e apenas um proprietário de FG. Preparar a troca com backup em uma rodada própria. |

Esses registros pertencem a tentativas anteriores e configurações diferentes. Explicam bloqueios de inicialização observados; não demonstram que sejam o único problema de cada jogo. A confirmação exige o log do processo novo, após a instalação. Nenhuma compatibilidade geral com todos os jogos Capcom é anunciada.

## Implementação

- **Instância Intel correta:** a ponte seleciona os módulos já mapeados, quando presentes; caso contrário procura a raiz e `_storage_`. Confere SHA-256 dos arquivos efetivamente retornados pelo loader e conserva os perfis das instruções carregadas. Os ponteiros de API, a verificação Reflex e o controle de debug usam os mesmos handles admitidos. A aceitação não depende de o caminho de uma cópia idêntica ser igual ao caminho solicitado inicialmente.
- **Versões desconhecidas continuam recusadas:** a candidata não adapta silenciosamente o par 1.2 antigo. XeFG admitido: 1.3.1.78, SHA-256 `ec5e0c65e075570c6ede72618bb666d0be0c2e10b2ea9762c0fe8cb8e375ab27`. XeLL admitidos: 1.3.0.5 (`61aeeab7fdcfb6d1742f86c14c68c663d759607d01a6132645c857291eaebb86`) ou 1.3.2.10 (`d2030dcd694fda8f2ec7e044b13e6db8f0b56d4ba9113a5efad334e3f3ded8c7`). O novo evento `runtime_selection` registra caminhos e hashes reais.
- **Factory:** o worker limitado de inicialização também tenta instalar o hook de criação do output fora do loader, sem depender exclusivamente de uma futura notificação de criação de device. Isso não reconstrói retroativamente uma swapchain já criada antes do hook. Não foi adicionada varredura contínua em Present.
- **Estado honesto:** falha de inicialização aparece como indisponível, com orientação sobre DLLs ausentes, par incompatível ou outro loader FG. O relatório copiado inclui a causa exata. O painel deixa de apresentar essas falhas como espera normal pela cena.
- **Visual:** cartões compactos restaurados para ambos os backends, usando o mesmo ImGui fornecido pelo ReShade. Os cartões de resumo são somente leitura, sem captura de input ou navegação. Controles, tooltips, alinhamento por tabelas e modo estreito permanecem. Não foram restaurados hooks de recuperação da tecla Home.

Reflex → Reflex, Reflex → XeLL, metering, CUDA e queue-ahead da R1.8 permanecem. UI Composition continua opcional e desmarcada por padrão. Não há novo filtro de imagem, limitador, algoritmo de pacing ou mudança dos modelos nesta candidata.

## Verificações

Build Release concluída com MSVC 19.44. A suíte registra **89 testes: 86 passaram, três verificações históricas opcionais foram ignoradas e nenhuma falhou**, em 122,99 segundos.

Os novos testes exercitam o SDK Intel real e ReShade real com caller Streamline sintético: runtime somente em `_storage_`, cópia previamente carregada de `_storage_` mesmo existindo outra na raiz, recusa de DLL ausente e recusa de um PE carregável com um byte alterado no stub DOS. A recusa exige ausência de publicação dos hooks do modelo; não afirma ausência do hook público de bootstrap. O caminho armazenado também é repetido com saída Reflex. A matriz existente cobre XeLL e Reflex em 2x/3x/4x/6x; uma rodada adicional cobre os dois outputs em 4x com XeLL 1.3.2.10.

As verificações de interface renderizam os widgets de produção, incluindo combo aberto, tooltip, seções avançadas, fonte ampliada, navegação de teclado e Home em ReShade real. As páginas não voltam a exibir contadores acumulados. A comparação numérica CUDA mantém a cobertura explícita de nove dos 17 programas; os oito fixtures originais indisponíveis continuam fora dessa alegação.

Esses testes demonstram inicialização, inferência e contratos nos cenários sintéticos. Não demonstram ativação no Pragmata/Control, preferência visual, ganho de FPS ou latência física. A validação em jogo será registrada separadamente por instalação e PID; o pacote não transforma um teste sintético em benchmark de gameplay.

## Instalação local e rollback

`Install-NativeXeMfgLocal.ps1` exige o jogo fechado, confere os hashes do addon e dos runtimes, salva snapshot dos destinos e atualiza a raiz e as cópias correspondentes já existentes em `_storage_`. Configurações, loader ReShade e REFramework são preservados. Cópias de logs anteriores ficam no backup. Falha de instalação tenta restaurar o snapshot. Não há DLL Intel redistribuída dentro do pacote.

Para retornar à instalação anterior, feche o jogo e execute `Restore-MFGBackup.ps1 -BackupDirectory <backup informado no recibo>`. Isso restaura também o par Intel anterior e remove os sidecars que não existiam. O rollback somente do addon R1.8 e da 1.4.5 oficial também está no pacote. Nenhuma release é publicada automaticamente.
