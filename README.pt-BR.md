# PurrFind

PurrFind é um buscador nativo e rápido de arquivos para Linux. Ele mantém um
índice SQLite inteiramente local e busca nomes, caminhos e conteúdo enquanto
você digita, sem telemetria ou envio de dados.

A versão 0.5.0-rc2 estabiliza o aplicativo para o primeiro beta público, com
recuperação do índice, hardening, CI em múltiplas distribuições, empacotamento e
limites de regressão. Ela preserva a pesquisa documental em TXT, Markdown, PDF, DOCX, XLSX,
PPTX, ODT, ODS e ODP, previews ricos e pesquisa por EXIF. A camada de metadados do filesystem permanece imediata; a extração de
conteúdo acontece em uma fila persistente de baixa prioridade e pode ser
pausada ou reindexada separadamente.

OCR local encontra texto em PDFs escaneados e, opcionalmente, em JPEG, PNG,
TIFF e WebP. O processamento usa Tesseract e os pacotes de idioma instalados no
sistema, acontece inteiramente no computador e nunca bloqueia a busca por nome.
PDFs são habilitados por padrão; OCR de imagens é uma opção nas configurações.

Exemplos: `contrato FIRENETWORK`, `content:"rede neutra"`, `name:proposta`,
`path:Documentos`, `camera:Canon width:>3000 type:image` e
`cliente type:pdf pages:>20 author:João`, além de
`source:ocr FIRENETWORK` para restringir a busca ao texto reconhecido.

Consulte [OCR](docs/ocr.md), [agendamento](docs/ocr-scheduling.md),
[segurança](docs/ocr-security.md) e [desempenho](docs/ocr-performance.md).

Consulte o [README principal](README.md) para compilação, uso, dependências e
documentação técnica.
