#include "worker/worker-application.h"
#include <QCoreApplication>
#include <QCommandLineParser>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);QCoreApplication::setApplicationName(QStringLiteral("Flux Encoder Worker"));QCommandLineParser parser;parser.addHelpOption();QCommandLineOption jobOption({QStringLiteral("j"),QStringLiteral("job")},QStringLiteral("Path to a serialized queue job"),QStringLiteral("file"));parser.addOption(jobOption);parser.process(app);if(!parser.isSet(jobOption))parser.showHelp(1);flux::WorkerApplication worker;return worker.run(parser.value(jobOption));
}
