#include "vcardmerger.h"

#include "conflictpolicy.h"

#include <KContacts/VCardConverter>
#include <KContacts/Addressee>

using namespace Kalburator::Shape;
using namespace Kalburator::Sync::QSyncCore;

namespace {

KContacts::Addressee parseVCard(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    KContacts::VCardConverter conv;
    const auto list = conv.parseVCards(data);
    return list.isEmpty() ? KContacts::Addressee{} : list.first();
}

QByteArray serializeVCard(const KContacts::Addressee &addr)
{
    if (addr.isEmpty())
        return {};
    KContacts::VCardConverter conv;
    return conv.createVCard(addr, KContacts::VCardConverter::v4_0);
}

bool srcWins(const KContacts::Addressee &src,
             const KContacts::Addressee &tgt,
             AutoResolveStrategy strategy)
{
    switch (strategy) {
    case AutoResolveStrategy::SourceAlwaysWins: return true;
    case AutoResolveStrategy::TargetAlwaysWins: return false;
    case AutoResolveStrategy::NewerWins:
        return src.revision() >= tgt.revision();
    default:
        return true;
    }
}

} // namespace

namespace Kalburator::Contacts {

CanonicalRecord IRecordMergerVCard::merge(
    const CanonicalRecord &source,
    const CanonicalRecord &target,
    const CanonicalRecord &baseline,
    const ConflictPolicy &policy) const
{
    const auto src  = parseVCard(source.data);
    const auto tgt  = parseVCard(target.data);
    const auto base = parseVCard(baseline.data);

    if (src.isEmpty() && tgt.isEmpty())
        return baseline;
    if (src.isEmpty())
        return target;
    if (tgt.isEmpty())
        return source;

    const bool preferSrc = srcWins(src, tgt, policy.autoResolve);

    // Start from source; apply target-only changes.
    KContacts::Addressee merged = src;

    auto pick = [&](bool srcChg, bool tgtChg, auto setFn, auto tgtVal) {
        if (!srcChg && tgtChg) {
            setFn(merged, tgtVal);
        } else if (srcChg && tgtChg && !preferSrc) {
            setFn(merged, tgtVal);
        }
    };

    if (!base.isEmpty()) {
        pick(src.formattedName() != base.formattedName(),
             tgt.formattedName() != base.formattedName(),
             [](KContacts::Addressee &a, const QString &v){ a.setFormattedName(v); },
             tgt.formattedName());

        pick(src.organization() != base.organization(),
             tgt.organization() != base.organization(),
             [](KContacts::Addressee &a, const QString &v){ a.setOrganization(v); },
             tgt.organization());

        pick(src.title() != base.title(),
             tgt.title() != base.title(),
             [](KContacts::Addressee &a, const QString &v){ a.setTitle(v); },
             tgt.title());

        pick(src.role() != base.role(),
             tgt.role() != base.role(),
             [](KContacts::Addressee &a, const QString &v){ a.setRole(v); },
             tgt.role());

        pick(src.note() != base.note(),
             tgt.note() != base.note(),
             [](KContacts::Addressee &a, const QString &v){ a.setNote(v); },
             tgt.note());

        pick(src.nickName() != base.nickName(),
             tgt.nickName() != base.nickName(),
             [](KContacts::Addressee &a, const QString &v){ a.setNickName(v); },
             tgt.nickName());

        pick(src.gender() != base.gender(),
             tgt.gender() != base.gender(),
             [](KContacts::Addressee &a, const KContacts::Gender &v){ a.setGender(v); },
             tgt.gender());

        pick(src.langs() != base.langs(),
             tgt.langs() != base.langs(),
             [](KContacts::Addressee &a, const KContacts::Lang::List &v){ a.setLangs(v); },
             tgt.langs());

        pick(src.anniversary() != base.anniversary(),
             tgt.anniversary() != base.anniversary(),
             [](KContacts::Addressee &a, const QDate &v){ a.setAnniversary(v); },
             tgt.anniversary());

        pick(src.kind() != base.kind(),
             tgt.kind() != base.kind(),
             [](KContacts::Addressee &a, const QString &v){ a.setKind(v); },
             tgt.kind());
    } else if (!preferSrc) {
        merged.setFormattedName(tgt.formattedName());
        merged.setOrganization(tgt.organization());
        merged.setTitle(tgt.title());
        merged.setRole(tgt.role());
        merged.setNote(tgt.note());
        merged.setNickName(tgt.nickName());
        merged.setGender(tgt.gender());
        merged.setLangs(tgt.langs());
        merged.setAnniversary(tgt.anniversary());
        merged.setKind(tgt.kind());
    }

    CanonicalRecord result;
    result.shape    = source.shape;
    result.recordId = source.recordId;
    result.data     = serializeVCard(merged);
    return result;
}

} // namespace Kalburator::Contacts
