# Relatório da Fase 5 — 0.5.0-rc2

Atualizado em 2026-09-05. Este relatório separa o que foi verificado localmente
do que depende de outro compositor, hardware, distribuição ou execução remota.

## Estado do produto

O PurrFind está funcional como release candidate para uso local. A arquitetura
é composta pelo aplicativo Qt/QML residente na bandeja, indexador `systemd
--user`/D-Bus, crawler inicial, watcher inotify, SQLite+FTS5+WAL, filas de
conteúdo/metadados/OCR, cache de previews e pacote nativo. A recomendação para
beta pública continua condicionada às validações externas listadas em
**Pendências reais**.

## Auditoria e estabilidade

Foram corrigidos polling agressivo da interface, pesquisas D-Bus obsoletas,
recuperação automática de SQLite, ciclo explícito de shutdown, overflow do
inotify, limites de extração/preview e mensagens de erro de comunicação.
Migrações v1–v5→v6 com rollback transacional, arquivos malformados, caminhos
Unicode, symlinks circulares, filas e cancelamento assíncrono passam nos testes
locais.

## Performance medida

Os números abaixo são do benchmark sintético local, em armazenamento local,
com build Release:

| Registros | Inserção | Consulta média | p95 | p99 | RSS | Banco |
|---:|---:|---:|---:|---:|---:|---:|
| 100.000 | 9,20 s | 1,47 ms | 3,70 ms | 3,85 ms | 88,8 MiB | 78,5 MiB |
| 1.000.000 | 107,66 s | 9,63 ms | 31,79 ms | 32,54 ms | 157,4 MiB | 799,3 MiB |
| 5.000.000 | 596,07 s | 47,75 ms | 164,29 ms | 166,15 ms | 152,3 MiB | 4,21 GiB |

O tier de 5.000.000 foi executado. O p95 acima do alvo de 1M é registrado como
limite conhecido para instalações muito grandes, não como falha silenciosa. A
medição de escrita física pode ser `-1` quando o kernel não expõe essa
contabilidade em `/proc/self/io`.

## CPU, I/O e filas

O indexador ocioso, com filas drenadas, registrou 0 ticks de CPU e 0 bytes de
I/O na amostra local. O OCR é de baixa prioridade, isolado em worker, com
timeout, cancelamento e retry. O loop OCR agora dorme por condição e só acorda
quando recebe trabalho, configuração ou pedido explícito.

## Banco e recuperação

WAL, checkpoint, `quick_check` semanal, migrações transacionais, preservação de
`index.sqlite3`, `-wal` e `-shm`, reconstrução automática e botão de rebuild
manual estão implementados. Os testes de corrupção/retenção e migração
interrompida passam localmente. Testes de queda elétrica real não são
reproduzíveis de forma segura no ambiente de desenvolvimento e permanecem uma
validação de instalação.

## inotify

O teste de integração cobre criação, alteração, renomeação e remoção. O teste
de estresse cria 20.000 arquivos, renomeia 10.000, remove 5.000, verifica
overflow/reconciliação e confirma a contagem final no banco. Ambos passam no
CTest local.

## Interface e desktops

O overlay é assíncrono, tem lista virtualizada, limite de resultados, preview
de PDF/imagem/documento/vídeo e tray residente. O atalho nativo `Super+F` foi
verificado em X11 com o processo oculto. Wayland KDE/GNOME, HiDPI, múltiplos
monitores e suspend/resume precisam ser executados em sessões reais.

## Segurança

Há limites contra ZIP bombs/traversal, XML externo/XXE, imagens gigantes,
cache sem permissões abertas e OCR em processo separado. PDF/alguns parsers
continuam no processo do indexador por desenho; um parser vulnerável pode
derrubar esse processo, embora o `systemd` faça restart e o índice seja
recuperável. Não há telemetria, rede ou upload.

## CI, pacotes e documentação

O workflow declara matrizes Ubuntu, Debian, Fedora e Arch, feature matrix,
sanitizers e gate de performance. CTest, `qmllint`, `desktop-file-validate` e
`appstreamcli validate --no-net` passam localmente. A execução remota da CI e
o teste de instalação/desinstalação de DEB/RPM/Arch ainda precisam de um
runner/distribuição correspondente. Flatpak não é suportado; a limitação do
`systemd --user` e acesso a raízes arbitrárias está documentada.

Artefatos gerados nesta revisão:

- `dist/PurrFind-0.5.0-rc2-source.tar.xz` (tarball limpo);
- `dist/purrfind-0.5.0-rc2-x86_64.deb`;
- `dist/purrfind-0.5.0-rc2-x86_64.rpm`;
- `dist/purrfind-0.5.0rc2-25-x86_64.pkg.tar.zst`;
- `dist/SHA256SUMS` com o source e os três pacotes nativos da revisão.

## Testes locais

Após reconfiguração limpa, 7 testes CTest passam: core, integração,
inotify-stress, recovery, content, preview e OCR. Os builds mínimo e com
sanitizers passam 4/4; as três variantes da feature matrix (OCR desligado,
Exiv2 desligado e todos os extratores opcionais desligados) também compilam e
passam seus testes. O smoke headless da UI foi inspecionado visualmente, o
lint QML, `desktop-file-validate`, AppStream e `scripts/validate-install.sh`
passam. O ciclo X11 inicia oculto e `Super+F` reabre a janela.

O gate de performance local (`scripts/check-performance.sh`) passa com
100.000 registros: p95 de consulta 3,76 ms, p95 de digitação 0,53 ms, p95
escopado 4,49 ms e RSS abaixo de 89 MiB. Esses números não substituem a
medição manual dos tiers de 1M/5M descritos acima.

## Pendências reais antes da publicação

- otimizar a consulta FTS para manter p95 baixo também acima de 1M (melhoria de
  escala, não falha funcional);
- validar Wayland KDE/GNOME, HiDPI, multi-monitor, suspend/resume e volumes
  removíveis em máquinas reais;
- obter execução verde da CI remota nos quatro containers e publicar os logs;
- testar ciclo completo de instalação, atualização, uso e desinstalação dos
  três pacotes na distribuição correspondente;
- executar dogfooding prolongado e profiling/leak check em hardware real;
- decidir a estratégia de distribuição Flatpak: o artefato completo continua
  bloqueado até substituir o `systemd --user` por um ciclo de sandbox/portal e
  definir permissões para raízes escolhidas; não publicar um Flatpak reduzido
  como se tivesse a mesma funcionalidade.

**Recomendação:** distribuir como release candidate para testes controlados;
publicar beta somente após fechar essas pendências de ambiente e release.
