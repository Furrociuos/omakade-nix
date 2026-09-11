#pragma once
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>
#include <QVariantMap>
#include <algorithm>

// Explicit dump tags are evidence, not a guess based on the title or desktop locale.
namespace RegionalMetadata {
inline QVariantMap romTags(const QString& filename) {
  static const QHash<QString, QString> regions{{"usa", "North America"},
                                               {"us", "North America"},
                                               {"na", "North America"},
                                               {"u", "North America"},
                                               {"canada", "North America"},
                                               {"europe", "Europe"},
                                               {"eur", "Europe"},
                                               {"eu", "Europe"},
                                               {"e", "Europe"},
                                               {"japan", "Japan"},
                                               {"jpn", "Japan"},
                                               {"jp", "Japan"},
                                               {"j", "Japan"},
                                               {"world", "Worldwide"},
                                               {"w", "Worldwide"},
                                               {"australia", "Australia"},
                                               {"china", "China"},
                                               {"korea", "Korea"},
                                               {"asia", "Asia"},
                                               {"brazil", "Brazil"},
                                               {"new zealand", "New Zealand"}};
  static const QRegularExpression tags(R"(\(([^()]*)\))");
  static const QRegularExpression language(
      R"(^(En|Ja|Fr|De|Es|It|Nl|Pt|Ko|Zh|Sv|Da|No|Fi|Ru|Pl)$)");
  static const QRegularExpression revision(R"(^Rev(?:ision)?\s+([A-Za-z0-9.]+)$)",
                                           QRegularExpression::CaseInsensitiveOption);
  QStringList foundRegions, languages, revisions;
  auto it = tags.globalMatch(QFileInfo(filename).fileName());
  while (it.hasNext()) {
    const auto tag = it.next().captured(1);
    for (const auto& part : tag.split(',')) {
      const QString token = part.trimmed();
      const QString region = regions.value(token.toLower());
      if (!region.isEmpty())
        foundRegions.append(region);
      else if (language.match(token).hasMatch())
        languages.append(token);
      else if (const auto rev = revision.match(token); rev.hasMatch())
        revisions.append(rev.captured(1));
    }
  }
  foundRegions.removeDuplicates();
  languages.removeDuplicates();
  revisions.removeDuplicates();
  return {{"regions", foundRegions}, {"languages", languages}, {"revisions", revisions}};
}

inline QString regionKey(QString region) {
  region = region.toLower();
  region.remove(QRegularExpression("[^a-z]"));
  return region;
}

// Keep all provider rows. Select only within the known platform, and never pretend that a
// worldwide/earliest-platform fallback is the ROM's regional release date.
inline QVariantMap details(QVariantMap value, const QString& filename,
                           const QList<int>& platforms) {
  const auto tags = romTags(filename);
  value["romTags"] = tags;
  const auto regions = tags.value("regions").toStringList();
  QStringList context;
  if (!regions.isEmpty())
    context.append("ROM region: " + regions.join(", "));
  const auto languages = tags.value("languages").toStringList();
  if (!languages.isEmpty())
    context.append("Languages: " + languages.join(", "));
  const auto revisions = tags.value("revisions").toStringList();
  if (!revisions.isEmpty())
    context.append("Revision: " + revisions.join(", "));
  value["romContext"] = context.join(" · ");
  value["releaseLabel"] = "First catalog release";
  QVariantList releases;
  for (const auto& row : value.value("releaseDates").toList()) {
    const auto release = row.toMap();
    if (platforms.contains(release.value("platform").toInt()) &&
        !release.value("human").toString().trimmed().isEmpty() &&
        release.value("date").toLongLong() > 0)
      releases.append(release);
  }
  std::stable_sort(releases.begin(), releases.end(), [](const QVariant& a, const QVariant& b) {
    return a.toMap().value("date").toLongLong() < b.toMap().value("date").toLongLong();
  });
  QVariantMap chosen;
  if (regions.size() == 1) {
    for (const auto& row : releases) {
      if (regionKey(row.toMap().value("region").toString()) == regionKey(regions.first())) {
        chosen = row.toMap();
        value["releaseLabel"] = regions.first() + " release";
        break;
      }
    }
  }
  if (chosen.isEmpty() && !releases.isEmpty()) {
    chosen = releases.first().toMap();
    value["releaseLabel"] = "First platform release";
  }
  if (!chosen.isEmpty()) {
    value["releaseText"] = chosen.value("human");
    value["year"] =
        chosen.value("year").toInt() > 0
            ? chosen.value("year").toInt()
            : QDateTime::fromSecsSinceEpoch(chosen.value("date").toLongLong(), QTimeZone::UTC)
                  .date()
                  .year();
  }
  QStringList names;
  for (const auto& row : value.value("localizations").toList()) {
    const auto localization = row.toMap();
    const QString name = localization.value("name").toString();
    const QString region = localization.value("region").toString();
    if (!name.isEmpty())
      names.append(name + (region.isEmpty() ? QString() : " (" + region + ")"));
  }
  for (const auto& row : value.value("alternativeNames").toList()) {
    const auto alias = row.toMap();
    const QString name = alias.value("name").toString();
    const QString comment = alias.value("comment").toString();
    // Acronyms and capitalization variants add noise without explaining a regional rename.
    if (comment.compare("Acronym", Qt::CaseInsensitive) == 0 ||
        comment.compare("Alternative spelling", Qt::CaseInsensitive) == 0 ||
        comment.compare("Stylized title", Qt::CaseInsensitive) == 0)
      continue;
    if (!name.isEmpty())
      names.append(name + (comment.isEmpty() ? QString() : " (" + comment + ")"));
  }
  names.removeDuplicates();
  value["titleEvidence"] = names;
  return value;
}
} // namespace RegionalMetadata
