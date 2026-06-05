// Unit test for the LastWriteWins tie-break comparator (src/engine/lastwritewins.h).
//
// Pins the semantics the SyncEngine LWW resolution path relies on (PlanStan bug
// doc sync-conflicts-lastwritewins-tie-bias.md, fix B): modify-delete gives the
// modifier the win, and a true modify-modify tie resolves to the TARGET (not the
// source), matching ConflictManager.

#include <QtTest/QtTest>
#include <QDateTime>

#include "lastwritewins.h"

using Kalburator::Sync::lastWriteWinsPrefersSource;

class TstLastWriteWins : public QObject
{
    Q_OBJECT
private slots:
    void sourceNewer_sourceWins();
    void targetNewer_targetWins();
    void trueTie_targetWins();
    void modifyDelete_modifierWins();
    void deleteModify_modifierWins();
};

void TstLastWriteWins::sourceNewer_sourceWins()
{
    const QDateTime t = QDateTime::fromSecsSinceEpoch(1000);
    QVERIFY(lastWriteWinsPrefersSource(t.addSecs(1), t));   // source later → source wins
}

void TstLastWriteWins::targetNewer_targetWins()
{
    const QDateTime t = QDateTime::fromSecsSinceEpoch(1000);
    QVERIFY(!lastWriteWinsPrefersSource(t, t.addSecs(1)));  // target later → target wins
}

void TstLastWriteWins::trueTie_targetWins()
{
    // The crux of the bug: a real modify-modify tie must NOT default to source.
    const QDateTime t = QDateTime::fromSecsSinceEpoch(1000);
    QVERIFY(!lastWriteWinsPrefersSource(t, t));
}

void TstLastWriteWins::modifyDelete_modifierWins()
{
    // Source modified (valid), target deleted (invalid) → modifier (source) wins.
    const QDateTime valid = QDateTime::fromSecsSinceEpoch(1000);
    const QDateTime invalid;  // null
    QVERIFY(lastWriteWinsPrefersSource(valid, invalid));
}

void TstLastWriteWins::deleteModify_modifierWins()
{
    // Source deleted (invalid), target modified (valid) → modifier (target) wins.
    const QDateTime valid = QDateTime::fromSecsSinceEpoch(1000);
    const QDateTime invalid;  // null
    QVERIFY(!lastWriteWinsPrefersSource(invalid, valid));
}

QTEST_GUILESS_MAIN(TstLastWriteWins)
#include "tst_lastwritewins.moc"
