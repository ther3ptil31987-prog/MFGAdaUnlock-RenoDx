# MFG Unlock 1.5.0-rc.2 — teste local de compatibilidade

Candidata derivada da RC1, sem publicação ou instalação automática.

- Corrige a associação da espera em integrações que chamam Reflex Sleep após
  terminar a simulação do token atual. O caso foi identificado no TLOU2; também
  pode explicar a espera permanente observada no Plague Tale.
- Preserva token/thread do Reflex nativo, deduplica chamadas e exige entradas
  válidas do mesmo frame. O primeiro frame sem espera anterior não é gerado.
- Corrige a leitura do estado de fullscreen em transições de apresentação.
- Acrescenta motivos de rejeição de marcadores no diagnóstico opcional e o
  motivo específico do dispositivo quando um Present falha.

Os crashes intermitentes de boot de Capcom e o erro DX12 do Black Flag ainda
não têm correção comprovada. Requiem e Pragmata voltaram a funcionar numa
repetição da RC1 sem troca de build; isso não demonstra que o defeito acabou.
Leia `engineering-report.md` para separar resultados em jogos de testes isolados.

Release e Debug passaram, cada um, em 128 testes, com três opcionais ignorados.
O SDK real também passou no cenário de espera após a simulação em 4x/6x, com
XeLL e Reflex. Com DLLs reais do TLOU2 houve inferência, mas o processo isolado
falhou no encerramento, inclusive no controle da RC1. Essa limitação permanece
registrada; não é uma certificação do jogo.

Para testar, feche o jogo, guarde seu addon atual e substitua somente
`renodx-mfgunlock.addon64` pelo arquivo da raiz deste pacote. Use um único addon
MFG e reinicie o processo. Preserve INIs, Intel DLLs, RenoDX, ReShade,
REFramework e overlays. UI Composition continua opcional e desligada por padrão;
esta candidata não modifica a escolha existente do usuário.

O addon Debug está separado e não deve ser instalado junto com Release.
Para rollback, feche o jogo e restaure o addon anterior ou o arquivo da RC1
em `rollback/`. O pacote também mantém a referência R1.12. Nenhum reset de INI
é necessário. As dependências e respectivas licenças continuam iguais às da RC1;
nenhuma DLL Intel é redistribuída no pacote.

Esta é uma build para validação em jogo. Não promete corrigir todos os títulos,
reduzir latência física, alterar os modelos ou melhorar frame pacing medido.
DLSS-G → XeLL permanece indisponível; DLSS-G mantém Reflex nativo.
