#pragma once

#include "core/ContentExtractor.h"

#include <QImage>
#include <QSize>

namespace purrfind {

struct PreviewRequest {
    FileRecord file;
    QSize targetSize{480, 320};
    QString query;
    QString snippet;
    int page{0};
    int pageCount{0};
};

struct PreviewResult {
    QString provider;
    QString title;
    QString text;
    QString details;
    QString error;
    QImage image;
    int page{0};
    int pageCount{0};
    bool cacheHit{false};
    qint64 generationMilliseconds{0};
};

class PreviewProvider {
public:
    virtual ~PreviewProvider() = default;
    virtual QString id() const = 0;
    virtual bool supports(const FileRecord &file) const = 0;
    virtual PreviewResult generate(const PreviewRequest &request,
                                   const CancellationToken &cancel) const = 0;
};

} // namespace purrfind
