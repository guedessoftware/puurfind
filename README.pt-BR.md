<div align="center">

<img src="resources/icons/logo_fundo_claro.png" alt="PurrFind" width="420">

**Busca rápida, privada e nativa de arquivos para Linux**

[![CI](https://github.com/guedessoftware/puurfind/actions/workflows/ci.yml/badge.svg)](https://github.com/guedessoftware/puurfind/actions/workflows/ci.yml)
[![Licença](https://img.shields.io/badge/licença-GPL--3.0--or--later-blue)](LICENSE)

[English](README.md)

</div>

O PurrFind cria um índice local privado e encontra arquivos, pastas, metadados
e conteúdo de documentos enquanto você digita. Ele é escrito em C++20/Qt 6,
funciona em X11 e Wayland e não exige conta, telemetria ou conexão de rede.

## Veja em funcionamento

A interface oferece tema escuro e tema claro completos. Os resultados são
organizados por categoria e o painel de pré-visualização abre conteúdos
suportados sem sair do aplicativo.

<p align="center">
  <img src="docs/screenshots/search-dark-empty.png" alt="Tema escuro, busca vazia" width="49%">
  <img src="docs/screenshots/search-light-empty.png" alt="Tema claro, busca vazia" width="49%">
</p>
<p align="center">
  <img src="docs/screenshots/search-dark-result.png" alt="Tema escuro com prévia de documento" width="49%">
  <img src="docs/screenshots/search-light-result.png" alt="Tema claro com prévia de documento" width="49%">
</p>

## Funções

- Busca instantânea por nome e caminho, usando índices trigramas SQLite FTS5.
- Busca no conteúdo com trechos, frases, campos específicos e filtros de categoria.
- Indexação persistente de baixa prioridade, atualizada por inotify e com recuperação de falhas.
- OCR local para PDFs escaneados e híbridos; OCR opcional para PNG, JPEG, TIFF e WebP.
- Pré-visualização de imagens, PDFs, texto, Markdown, documentos Office/ODF,
  pastas e reprodução local de vídeos MP4/WebM.
- Prévia de pastas com o conteúdo imediato e ícones por tipo de arquivo.
- Busca por metadados EXIF (câmera, dimensões e autor) quando o Exiv2 está disponível.
- Indicador na bandeja do sistema, ações abrir/sair e atalho global `Super+F`.
- Temas claro, escuro e sistema, configuráveis em Configurações.
- Ranking opcional por uso, limites de recursos e controles independentes para OCR e conteúdo.
- Operação somente local: sem nuvem, telemetria, upload ou chamadas de rede em segundo plano.

## Download da versão beta

A beta pública atual é a **0.5.0-rc2**. Os pacotes e o código-fonte estão na
[release do GitHub](https://github.com/guedessoftware/puurfind/releases/tag/v0.5.0-rc2).

| Plataforma | Download | Exemplo de instalação |
| --- | --- | --- |
| Debian/Ubuntu | [`.deb`](https://github.com/guedessoftware/puurfind/releases/download/v0.5.0-rc2/purrfind-0.5.0-rc2-x86_64.deb) | `sudo apt install ./purrfind-0.5.0-rc2-x86_64.deb` |
| Fedora/RHEL | [`.rpm`](https://github.com/guedessoftware/puurfind/releases/download/v0.5.0-rc2/purrfind-0.5.0-rc2-x86_64.rpm) | `sudo dnf install ./purrfind-0.5.0-rc2-x86_64.rpm` |
| Arch Linux | [`.pkg.tar.zst`](https://github.com/guedessoftware/puurfind/releases/download/v0.5.0-rc2/purrfind-0.5.0rc2-25-x86_64.pkg.tar.zst) | `sudo pacman -U purrfind-0.5.0rc2-25-x86_64.pkg.tar.zst` |
| Código-fonte | [`tar.xz`](https://github.com/guedessoftware/puurfind/releases/download/v0.5.0-rc2/PurrFind-0.5.0-rc2-source.tar.xz) | Veja [Compilar](#compilar-do-código-fonte) |

Confira os downloads usando [`SHA256SUMS`](https://github.com/guedessoftware/puurfind/releases/download/v0.5.0-rc2/SHA256SUMS).

No Debian/Ubuntu, prefira `apt install ./purrfind-*.deb`: ele instala
automaticamente as dependências de execução do Qt, QML e multimídia. Se o
pacote foi instalado anteriormente com `dpkg -i`, corrija as dependências com
`sudo apt -f install`.

> Flatpak não está incluído nesta beta. O indexador persistente e as raízes de
> arquivos escolhidas pelo usuário precisam de um desenho de portal/sandbox
> que mantenha a funcionalidade sem permissões excessivas.

## Sintaxe de busca

Os termos procuram em nomes e conteúdo indexado. Os campos podem ser combinados:

```text
contrato type:pdf
content:"rede neutra" path:Documentos
camera:Canon width:>3000 type:image
pages:>20 author:João
source:ocr FIRENETWORK
modified:7d size:>10MB
```

Use `kind:file` e `kind:folder`, ou clique nas abas de categoria. As raízes e
pastas excluídas podem ser alteradas em Configurações; arquivos ocultos
continuam pesquisáveis, salvo exclusão explícita.

## Compilar do código-fonte

Requisitos: compilador C++20, CMake 3.24+, Ninja, Qt 6.4+ (Core, DBus, Gui,
Multimedia, Qml, Quick e Quick Controls), SQLite com FTS5, Poppler-Qt6,
libzip, libxml2, Exiv2, Tesseract/Leptonica e dados de idiomas do Tesseract.
Famílias opcionais podem ser desativadas com `-DPURRFIND_WITH_PDF=OFF`,
`-DPURRFIND_WITH_OFFICE=OFF`, `-DPURRFIND_WITH_EXIV2=OFF` ou
`-DPURRFIND_WITH_OCR=OFF`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/purrfind
```

Instale e ative o indexador do usuário:

```sh
cmake --install build
systemctl --user daemon-reload
systemctl --user enable --now purrfind-indexer.service
```

O listener da bandeja/atalho é registrado pelo autostart XDG e inicia no
próximo login gráfico. Para ativá-lo imediatamente após a instalação, execute
`/usr/bin/purrfind --background` uma vez; ele permanecerá ativo na bandeja.

## Atalhos e bandeja

`Super+F` abre ou focaliza o PurrFind. Fechar a janela mantém o indexador e a
bandeja ativos, preservando o atalho. O menu da bandeja mostra o estado da
indexação e do atalho e oferece Abrir e Sair. No X11 o atalho é registrado
diretamente; no Wayland é usado o portal XDG Global Shortcuts quando disponível.

## Documentação

- [Arquitetura](docs/architecture.md) · [Indexação](docs/indexing.md) · [Busca](docs/search.md)
- [Conteúdo](docs/content-indexing.md) · [Extractors](docs/extractors.md)
- [Pré-visualizações](docs/previews.md) · [Metadados](docs/metadata.md) · [OCR](docs/ocr.md)
- [Segurança](docs/security.md) · [Desempenho](docs/performance.md)
- [Status dos pacotes](docs/packaging.md) · [Desenvolvimento](docs/development.md)
- [Relatório da Fase 5 e pendências](docs/phase5-report.md)
- [Solução de problemas](docs/troubleshooting.md)

## Status do projeto

Esta é uma candidata a beta pública, não uma versão estável final. O CI
automatizado está verde em Ubuntu, Debian, Fedora e Arch, incluindo sanitizers,
variantes de recursos e gates de desempenho. A validação em desktops reais
(KDE/GNOME Wayland, HiDPI, múltiplos monitores, suspend/resume, volumes
removíveis e uso prolongado) continua registrada no [relatório da Fase 5](docs/phase5-report.md).

## Licença

O PurrFind é distribuído sob a licença [GPL-3.0-or-later](LICENSE).
