#pragma once

#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

// Each source owns its direct cache files. All sources use the same total budget,
// but live references are protected even when that makes the budget a soft limit.
// This avoids deleting another model's files without notifying it or repeatedly
// downloading and immediately evicting the same visible cover.
namespace CoverCachePolicy {
inline qint64 prune(const QString& sharedRoot, const QString& ownedRoot, qint64 limit,
                    const QSet<QString>& referenced) {
  qint64 total = 0;
  QList<QFileInfo> candidates;
  const QString owner = QDir(ownedRoot).absolutePath();
  QDirIterator files(sharedRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (files.hasNext()) {
    const QFileInfo file(files.next());
    total += file.size();
    if (file.absolutePath() == owner && !referenced.contains(file.absoluteFilePath()))
      candidates.append(file);
  }
  std::sort(candidates.begin(), candidates.end(), [](const QFileInfo& a, const QFileInfo& b) {
    return a.lastModified() != b.lastModified() ? a.lastModified() < b.lastModified()
                                                : a.absoluteFilePath() < b.absoluteFilePath();
  });
  for (const auto& file : candidates) {
    if (total <= qMax<qint64>(0, limit))
      break;
    if (QFile::remove(file.absoluteFilePath()))
      total -= file.size();
  }
  return total;
}
} // namespace CoverCachePolicy
