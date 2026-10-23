MFGAdaUnlock - Dual Backend X1.3 (base oficial 1.4.5)

Prepara o modelo antes do primeiro contexto admitido do jogo ou do OptiScaler.
Os dois passam a compartilhar o modelo desde a criacao; nao se altera um
contexto nativo ja vivo. Um contexto de componente nao admitido ainda bloqueia
o port global. A rota Tensor Core e ligada ao dispositivo do output do host.
Mantem os redirecionamentos do OptiScaler e a correcao de startup das anteriores.
O modelo, os shaders e os ajustes de imagem nao foram alterados nesta correcao.

DLSSG: base de geracao/qualidade 1.4.5, com ReShade direto.
XeMFG: OptiScaler 10, modelo MFG Intel adaptado, XeLL e pacing dos frames extras.
O port desta candidata e admitido em NVIDIA com os runtimes Intel revisados.
O plugin MFG anterior deve ficar guardado; nao carregue os dois unlockers juntos.

Com o jogo fechado, execute no PowerShell a partir deste pacote:
.\Switch-MFGBackend.ps1 -GameDirectory "<pasta do executavel do jogo>" -Backend XeMFG
Para usar DLSS-G, execute o mesmo comando com -Backend DLSSG.
O script verifica os hashes e faz backup dos arquivos atingidos antes da troca.
O RenoDX do jogo e os ajustes de upscaling/imagem sao preservados.

Para voltar exatamente a uma instalacao salva, com o jogo fechado:
.\Restore-MFGBackup.ps1 -BackupDirectory "<pasta backup indicada pelo seletor>"
O backup inicial do Cyberpunk permanece em MFGAdaUnlock-profiles na pasta x64.
Para voltar ao addon oficial 1.4.5, selecione primeiro DLSSG e substitua o addon
pelo arquivo da pasta rollback, usando o nome renodx-mfgunlock.addon64.

Validacao: 46 testes aprovados; 3 opcionais pulados. Inclui abertura com
OptiScaler real, ciclo de vida XeLL/XeFG e contagens 2x/3x/4x/6x em host
sintetico, caller dos redirects, Present/resize, jogo criando o primeiro
contexto MFG e protecao contra publicacao sobre contexto estrangeiro vivo.
A X1.1 passou do startup e caiu durante Present no XeLL no Cyberpunk.
A X1.2 abriu e carregou a cena no teste do usuario, mas ficou em XeFG nativo.
A X1.3 abriu e carregou a cena no teste do usuario. O log confirmou modelo MFG,
interpolacao 4x/6x com sucesso, rota Tensor em uso e metering aceito pelo driver.
O usuario relatou cerca, asfalto e resposta com XeLL perfeitos nessa cena.
Ha tambem recusas de inputs no log; isso nao e certificacao geral,
benchmark de desempenho, medicao de scanout ou de latencia fisica.
Veja engineering-report.md, manifest.json, evidence/, PROVENANCE.md e notices/.
Este e um pacote local; nenhuma nova release foi publicada.
