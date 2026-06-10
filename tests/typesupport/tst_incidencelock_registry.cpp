#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QObject>

#include "../../src/typesupport/incidencelock_registry.h"

using Kalburator::Sync::IncidenceLockRegistry;

class TstIncidenceLockRegistry : public QObject
{
    Q_OBJECT
private slots:
    void tryLock_acquiresLock();
    void tryLock_reentrant_sameOwner_returnsTrue();
    void tryLock_conflict_returnsFalseAndEmitsSignal();
    void unlock_releasesLock();
    void unlock_ownerMismatch_returnsFalse();
    void autoRelease_onOwnerDestroyed();
    void autoRelease_unlockSignalEmitted();
    void tryLockAll_allOrNothing();
    void unlockAll_releasesAllForOwner();
    void lockedUids_onlyReturnsValidLocks();
};

void TstIncidenceLockRegistry::tryLock_acquiresLock()
{
    IncidenceLockRegistry reg;
    QObject owner;

    QVERIFY(!reg.isLocked(QStringLiteral("uid-1")));
    QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &owner, QStringLiteral("Test editor")));
    QVERIFY(reg.isLocked(QStringLiteral("uid-1")));
    QVERIFY(reg.isLockedBy(QStringLiteral("uid-1"), &owner));
    QCOMPARE(reg.lockCount(), 1);

    auto info = reg.lockInfo(QStringLiteral("uid-1"));
    QVERIFY(info.isValid());
    QCOMPARE(info.owner.data(), &owner);
    QCOMPARE(info.ownerDescription, QStringLiteral("Test editor"));
}

void TstIncidenceLockRegistry::tryLock_reentrant_sameOwner_returnsTrue()
{
    IncidenceLockRegistry reg;
    QObject owner;

    QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &owner));
    // Re-entrant: same owner, same uid → still true, count stays at 1
    QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &owner));
    QCOMPARE(reg.lockCount(), 1);
}

void TstIncidenceLockRegistry::tryLock_conflict_returnsFalseAndEmitsSignal()
{
    IncidenceLockRegistry reg;
    QObject owner1;
    QObject owner2;
    QSignalSpy conflictSpy(&reg, &IncidenceLockRegistry::lockConflict);

    QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &owner1, QStringLiteral("Owner 1")));
    QVERIFY(!reg.tryLock(QStringLiteral("uid-1"), &owner2, QStringLiteral("Owner 2")));
    QCOMPARE(conflictSpy.count(), 1);
    QCOMPARE(conflictSpy.at(0).at(0).toString(), QStringLiteral("uid-1"));
    QCOMPARE(conflictSpy.at(0).at(1).value<QObject *>(), &owner2);
    QCOMPARE(conflictSpy.at(0).at(2).value<QObject *>(), &owner1);

    // Still locked by owner1
    QVERIFY(reg.isLockedBy(QStringLiteral("uid-1"), &owner1));
    QVERIFY(!reg.isLockedBy(QStringLiteral("uid-1"), &owner2));
}

void TstIncidenceLockRegistry::unlock_releasesLock()
{
    IncidenceLockRegistry reg;
    QObject owner;
    QSignalSpy unlockedSpy(&reg, &IncidenceLockRegistry::itemUnlocked);

    QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &owner));
    QVERIFY(reg.unlock(QStringLiteral("uid-1"), &owner));
    QVERIFY(!reg.isLocked(QStringLiteral("uid-1")));
    QCOMPARE(reg.lockCount(), 0);
    QCOMPARE(unlockedSpy.count(), 1);
    QCOMPARE(unlockedSpy.at(0).at(0).toString(), QStringLiteral("uid-1"));
}

void TstIncidenceLockRegistry::unlock_ownerMismatch_returnsFalse()
{
    IncidenceLockRegistry reg;
    QObject owner1;
    QObject owner2;

    QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &owner1));
    QVERIFY(!reg.unlock(QStringLiteral("uid-1"), &owner2));
    // Still locked by owner1
    QVERIFY(reg.isLocked(QStringLiteral("uid-1")));
}

void TstIncidenceLockRegistry::autoRelease_onOwnerDestroyed()
{
    // Qt6 nullifies QPointers before emitting destroyed(), so onOwnerDestroyed →
    // unlockAll cannot match by QPointer comparison. The locks become isValid()==false
    // immediately (QPointer is null), but are not eagerly removed from m_locks until
    // the next cleanupInvalidLocks() call. The observable guarantee is: isLocked()
    // returns false and a new owner can re-acquire the lock.
    IncidenceLockRegistry reg;

    {
        QObject owner;
        QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &owner));
        QVERIFY(reg.tryLock(QStringLiteral("uid-2"), &owner));
        QCOMPARE(reg.lockCount(), 2);
        // owner goes out of scope here → destroyed → QPointers nullified
    }

    // isLocked() checks isValid() (QPointer null check) → false for stale entries
    QVERIFY(!reg.isLocked(QStringLiteral("uid-1")));
    QVERIFY(!reg.isLocked(QStringLiteral("uid-2")));

    // A new owner can re-acquire: cleanupInvalidLocks() runs inside tryLock
    QObject newOwner;
    QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &newOwner));
}

void TstIncidenceLockRegistry::autoRelease_unlockSignalEmitted()
{
    // The itemUnlocked signals for auto-released locks are emitted lazily by
    // cleanupInvalidLocks(), which runs inside tryLock/unlock. Trigger it by
    // attempting a new lock after the owner is gone.
    IncidenceLockRegistry reg;
    QSignalSpy unlockedSpy(&reg, &IncidenceLockRegistry::itemUnlocked);

    {
        QObject owner;
        QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &owner));
        QVERIFY(reg.tryLock(QStringLiteral("uid-2"), &owner));
        QCOMPARE(unlockedSpy.count(), 0);
    }

    // Trigger cleanupInvalidLocks by acquiring a new lock
    QObject trigger;
    reg.tryLock(QStringLiteral("uid-trigger"), &trigger);

    // Both stale locks cleaned up → two itemUnlocked signals
    QCOMPARE(unlockedSpy.count(), 2);
}

void TstIncidenceLockRegistry::tryLockAll_allOrNothing()
{
    IncidenceLockRegistry reg;
    QObject ownerA;
    QObject ownerB;

    // Pre-lock uid-2 with ownerB
    QVERIFY(reg.tryLock(QStringLiteral("uid-2"), &ownerB));

    // tryLockAll on [uid-1, uid-2] with ownerA must fail (uid-2 is taken)
    QVERIFY(!reg.tryLockAll({QStringLiteral("uid-1"), QStringLiteral("uid-2")}, &ownerA));

    // uid-1 must NOT be locked (all-or-nothing rollback)
    QVERIFY(!reg.isLocked(QStringLiteral("uid-1")));
    // uid-2 still locked by ownerB
    QVERIFY(reg.isLockedBy(QStringLiteral("uid-2"), &ownerB));
}

void TstIncidenceLockRegistry::unlockAll_releasesAllForOwner()
{
    IncidenceLockRegistry reg;
    QObject ownerA;
    QObject ownerB;

    QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &ownerA));
    QVERIFY(reg.tryLock(QStringLiteral("uid-2"), &ownerA));
    QVERIFY(reg.tryLock(QStringLiteral("uid-3"), &ownerB));

    reg.unlockAll(&ownerA);

    QVERIFY(!reg.isLocked(QStringLiteral("uid-1")));
    QVERIFY(!reg.isLocked(QStringLiteral("uid-2")));
    QVERIFY(reg.isLockedBy(QStringLiteral("uid-3"), &ownerB));
    QCOMPARE(reg.lockCount(), 1);
}

void TstIncidenceLockRegistry::lockedUids_onlyReturnsValidLocks()
{
    IncidenceLockRegistry reg;
    QObject ownerA;

    QVERIFY(reg.tryLock(QStringLiteral("uid-1"), &ownerA));
    QVERIFY(reg.tryLock(QStringLiteral("uid-2"), &ownerA));

    {
        QObject ownerB;
        QVERIFY(reg.tryLock(QStringLiteral("uid-3"), &ownerB));
        // ownerB destroyed here → uid-3 auto-released
    }

    const QStringList uids = reg.lockedUids();
    QCOMPARE(uids.size(), 2);
    QVERIFY(uids.contains(QStringLiteral("uid-1")));
    QVERIFY(uids.contains(QStringLiteral("uid-2")));
    QVERIFY(!uids.contains(QStringLiteral("uid-3")));
}

QTEST_GUILESS_MAIN(TstIncidenceLockRegistry)
#include "tst_incidencelock_registry.moc"
