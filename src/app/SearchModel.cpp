#include "app/SearchModel.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>

namespace purrfind {

int SearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : results_.size();
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= results_.size()) return {};
    const auto &result = results_.at(index.row());
    const auto &file = result.file;
    switch (role) {
    case IdRole: return file.id;
    case NameRole: return file.name;
    case PathRole: return file.path;
    case ParentPathRole: return file.parentPath;
    case ExtensionRole: return file.extension.isEmpty() ? (file.directory ? "Folder" : "File") : file.extension.toUpper();
    case TypeRole: return file.type;
    case SizeRole: return file.directory ? QStringLiteral("—") : QLocale().formattedDataSize(file.size);
    case ModifiedRole: return QLocale().toString(QDateTime::fromSecsSinceEpoch(file.mtime), QLocale::ShortFormat);
    case DirectoryRole: return file.directory;
    case HiddenRole: return file.hidden;
    case ScoreRole: return result.score;
    case FileUrlRole: return QUrl::fromLocalFile(file.path);
    case SnippetRole: return result.snippet;
    case MatchOriginRole: return result.matchOrigin;
    case DocumentTitleRole: return result.documentTitle;
    case DocumentAuthorRole: return result.documentAuthor;
    case PageCountRole: return result.pageCount;
    case MatchPageRole: return result.matchPage;
    case CameraRole: return (result.cameraMake + ' ' + result.cameraModel).simplified();
    case DimensionsRole: return result.imageWidth > 0
        ? QString("%1 × %2").arg(result.imageWidth).arg(result.imageHeight) : QString();
    default: return {};
    }
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    return {{IdRole,"fileId"}, {NameRole,"name"}, {PathRole,"path"},
        {ParentPathRole,"parentPath"}, {ExtensionRole,"extension"}, {TypeRole,"mimeType"},
        {SizeRole,"sizeText"}, {ModifiedRole,"modifiedText"}, {DirectoryRole,"directory"},
        {HiddenRole,"hidden"}, {ScoreRole,"score"}, {FileUrlRole,"fileUrl"},
        {SnippetRole,"snippet"}, {MatchOriginRole,"matchOrigin"},
        {DocumentTitleRole,"documentTitle"}, {DocumentAuthorRole,"documentAuthor"},
        {PageCountRole,"pageCount"}, {MatchPageRole,"matchPage"},
        {CameraRole,"camera"}, {DimensionsRole,"dimensions"}};
}

void SearchModel::setResults(QVector<SearchResult> results)
{
    beginResetModel();
    results_ = std::move(results);
    endResetModel();
}

const SearchResult *SearchModel::at(int row) const
{
    return row >= 0 && row < results_.size() ? &results_.at(row) : nullptr;
}

} // namespace purrfind
