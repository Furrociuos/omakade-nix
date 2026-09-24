#include "library/ReviewAvailability.h"

#include <QChar>
#include <QDir>
#include <QFileInfo>

#include <utility>

namespace {
void appendReason(ReviewAvailability::Result& result, const QString& key, const QString& label,
                  const QString& detail) {
  if (result.reasons.contains(key))
    return;
  result.reasons.append(key);
  result.reasonDetails.append(QVariantMap{{QStringLiteral("key"), key},
                                          {QStringLiteral("label"), label},
                                          {QStringLiteral("detail"), detail}});
}

QString runtimeCacheKey(const ReviewAvailability::Entry& entry) {
  return entry.route + QChar::Null + entry.mode + QChar::Null + entry.core + QChar::Null +
         (entry.flatpak ? QStringLiteral("1") : QStringLiteral("0")) + QChar::Null + entry.system;
}

struct MissingEntry {
  int index = -1;
  QString path;
  QString ancestor;
};
} // namespace

QVector<ReviewAvailability::Result> ReviewAvailability::evaluate(
    const QVector<Entry>& entries, const ContentAvailable& contentAvailable,
    const MissingAncestor& missingAncestor, const PlanResolver& resolvePlan) {
  QVector<Result> results;
  results.reserve(entries.size());
  QVector<MissingEntry> missingEntries;
  QHash<QString, int> missingAncestors;
  QHash<QString, Plan> runtimeResults;
  missingEntries.reserve(entries.size());

  for (const Entry& entry : entries) {
    Result result;
    result.key = entry.key;
    if (!entry.sourceError.trimmed().isEmpty())
      appendReason(result, QStringLiteral("source-error"),
                   entry.source + QStringLiteral(" scan failed"), entry.sourceError);

    if (entry.emulator || entry.manual) {
      result.pathChecked = true;
      result.contentAvailable = !entry.path.isEmpty() && contentAvailable &&
                                contentAvailable(entry.path);
      result.launchable = result.contentAvailable;
      if (!result.contentAvailable) {
        const QString ancestor = missingAncestor ? missingAncestor(entry.path) : QString{};
        if (!ancestor.isEmpty())
          ++missingAncestors[ancestor];
        missingEntries.append({static_cast<int>(results.size()), entry.path, ancestor});
      } else if (entry.emulator && resolvePlan) {
        const QString cacheKey = runtimeCacheKey(entry);
        Plan plan;
        if (runtimeResults.contains(cacheKey)) {
          plan = runtimeResults.value(cacheKey);
        } else {
          plan = resolvePlan(entry);
          if (plan.errorCategory != QStringLiteral("content") &&
              plan.errorCategory != QStringLiteral("sandbox"))
            runtimeResults.insert(cacheKey, plan);
        }
        if (!plan.error.isEmpty() &&
            QStringList{QStringLiteral("runtime"), QStringLiteral("config"),
                        QStringLiteral("sandbox")}
                .contains(plan.errorCategory)) {
          result.launchable = false;
          appendReason(result, QStringLiteral("runtime"),
                       QStringLiteral("Emulator or core unavailable"), plan.error);
        }
      }
    } else {
      result.launchable = !entry.installedKnown || entry.installed;
      if (entry.source != QStringLiteral("Steam") && entry.installedKnown && !entry.installed) {
        const QString ancestor = missingAncestor ? missingAncestor(entry.path) : QString{};
        if (!ancestor.isEmpty())
          ++missingAncestors[ancestor];
        missingEntries.append({static_cast<int>(results.size()), entry.path, ancestor});
      }
    }
    results.append(std::move(result));
  }

  for (const MissingEntry& missing : missingEntries) {
    const bool storage = !missing.ancestor.isEmpty() && missingAncestors.value(missing.ancestor) >= 2;
    appendReason(results[missing.index], storage ? QStringLiteral("missing-storage")
                                                 : QStringLiteral("missing-file"),
                 storage ? QStringLiteral("Drive or folder disconnected")
                         : QStringLiteral("Game file moved or missing"),
                 storage ? missing.ancestor : missing.path);
  }
  return results;
}

QString ReviewAvailability::firstMissingAncestor(const QString& path) {
  const QFileInfo file(path);
  if (path.isEmpty() || !file.isAbsolute())
    return {};
  QString directory = file.absolutePath();
  while (!directory.isEmpty()) {
    const QFileInfo candidate(directory);
    if (!candidate.exists() || !candidate.isDir())
      return directory;
    const QString parent = candidate.dir().absolutePath();
    if (parent == directory)
      break;
    directory = parent;
  }
  return {};
}

bool ReviewAvailability::matchesFilter(const QString& filter, const QStringList& reasons) {
  if (filter.isEmpty())
    return true;
  if (filter == QStringLiteral("either"))
    return reasons.contains(QStringLiteral("identification")) ||
           reasons.contains(QStringLiteral("artwork"));
  if (filter == QStringLiteral("unavailable"))
    return reasons.contains(QStringLiteral("missing-file")) ||
           reasons.contains(QStringLiteral("missing-storage")) ||
           reasons.contains(QStringLiteral("runtime")) ||
           reasons.contains(QStringLiteral("source-error"));
  return reasons.contains(filter);
}
