#pragma once

#include "core/Types.h"

#include <QAbstractListModel>

namespace purrfind {

class SearchModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1, NameRole, PathRole, ParentPathRole, ExtensionRole,
        TypeRole, SizeRole, ModifiedRole, DirectoryRole, HiddenRole, ScoreRole, FileUrlRole,
        SnippetRole, MatchOriginRole, DocumentTitleRole, DocumentAuthorRole, PageCountRole,
        MatchPageRole, CameraRole, DimensionsRole
    };
    explicit SearchModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setResults(QVector<SearchResult> results);
    const SearchResult *at(int row) const;

private:
    QVector<SearchResult> results_;
};

} // namespace purrfind
