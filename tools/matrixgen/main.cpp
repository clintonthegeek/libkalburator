// EEE Phase 6 — regenerates the committed convergence matrix.
//
//   cmake --build build --target matrixgen
//   ./build/tools/matrixgen/matrixgen docs/CONVERGENCE_MATRIX.md
//
// The committed copy is byte-enforced by tst_gm_pipeline_convergence.

#include <kalburator/calendar/calendarstockshapes.h>
#include <kalburator/contacts/contactsstockshapes.h>
#include <kalburator/shape/convergencematrix.h>
#include <kalburator/todo/todostockshapes.h>

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

using Kalburator::Shape::ConvergenceMatrix;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString path = QStringLiteral(
        "docs/CONVERGENCE_MATRIX.md");

    const Kalburator::Calendar::CalendarStockShapes calendar;
    const Kalburator::Contacts::ContactsStockShapes contacts;
    const Kalburator::Todo::TodoStockShapes todo;

    const QString md = ConvergenceMatrix::generate(
        { { QStringLiteral("calendar"), &calendar },
          { QStringLiteral("contacts"), &contacts },
          { QStringLiteral("todo"), &todo } });

    if (app.arguments().size() > 1) {
        QFile f(app.arguments().at(1));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCritical("cannot open %s for writing",
                      qPrintable(app.arguments().at(1)));
            return 1;
        }
        f.write(md.toUtf8());
        return 0;
    }
    QTextStream out(stdout);
    out << md;
    out.flush();
    return 0;
}
