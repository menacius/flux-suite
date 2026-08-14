#include "providers/provider-process.h"
#include "flux-encoder-provider-version.h"
#include <QJsonDocument>
#include <QUuid>
namespace flux {
ProviderProcess::ProviderProcess(QObject *parent):QObject(parent){connect(&process_,&QProcess::readyReadStandardOutput,this,&ProviderProcess::readOutput);connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){emit providerError(process_.errorString());});connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){ready_=false;heartbeatTimer_.stop();emit providerExited(code);});heartbeatTimer_.setInterval(5000);connect(&heartbeatTimer_,&QTimer::timeout,this,&ProviderProcess::heartbeat);}
bool ProviderProcess::start(const QString &executable,const QString &providerId,QString *error){stop();providerId_=providerId;process_.setProgram(executable);process_.setArguments({QStringLiteral("--flux-encoder-provider-stdio")});process_.start();if(!process_.waitForStarted(5000)){if(error)*error=process_.errorString();return false;}ready_=true;heartbeatTimer_.start();sendRequest(QStringLiteral("hello"),{{"hostAbi",int(FLUX_ENCODER_RENDER_PROVIDER_ABI_VERSION)},{"protocolVersion",int(FLUX_ENCODER_RENDER_PROVIDER_PROTOCOL_VERSION)}});return true;}
QString ProviderProcess::sendRequest(const QString &method,const QJsonObject &params){const QString id=QUuid::createUuid().toString(QUuid::WithoutBraces);sendObject({{"protocolVersion",int(FLUX_ENCODER_RENDER_PROVIDER_PROTOCOL_VERSION)},{"requestId",id},{"providerId",providerId_},{"method",method},{"params",params}});return id;}
void ProviderProcess::cancelSession(const QString &sessionId){sendRequest(QStringLiteral("cancel"),{{"sessionId",sessionId}});}
void ProviderProcess::stop(){heartbeatTimer_.stop();ready_=false;if(process_.state()!=QProcess::NotRunning){process_.terminate();if(!process_.waitForFinished(1500)){process_.kill();process_.waitForFinished();}}}
void ProviderProcess::sendObject(const QJsonObject &object){if(process_.state()!=QProcess::Running)return;process_.write(QJsonDocument(object).toJson(QJsonDocument::Compact));process_.write("\n");}
void ProviderProcess::readOutput(){inputBuffer_+=process_.readAllStandardOutput();for(;;){const qsizetype end=inputBuffer_.indexOf('\n');if(end<0)break;const QByteArray line=inputBuffer_.left(end).trimmed();inputBuffer_.remove(0,end+1);QJsonParseError error;const auto doc=QJsonDocument::fromJson(line,&error);if(error.error!=QJsonParseError::NoError||!doc.isObject()){emit providerError(tr("Provider returned invalid JSON: %1").arg(error.errorString()));continue;}const auto object=doc.object();emit responseReceived(object.value("requestId").toString(),object);}}
void ProviderProcess::heartbeat(){if(ready_)sendRequest(QStringLiteral("heartbeat"));}
}
