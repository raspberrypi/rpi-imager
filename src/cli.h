#ifndef CLI_H
#define CLI_H

#include <QObject>
#include <QVariant>

class ImageWriter;
class QCoreApplication;
class QJsonObject;

class Cli : public QObject
{
    Q_OBJECT
public:
    explicit Cli(int &argc, char *argv[]);
    virtual ~Cli();
    int run();

protected:
    QCoreApplication *_app;
    ImageWriter *_imageWriter;
    int _lastPercent;
    QByteArray _lastMsg;
    bool _quiet;
    bool _jsonOutput;

    void _printProgress(const QByteArray &msg, QVariant now, QVariant total);
    void _clearLine();
    void _sendJson(const QString &type, const QJsonObject &data);

protected slots:
    void onSuccess();
    void onError(QVariant msg);
    void onDownloadProgress(QVariant dlnow, QVariant dltotal);
    void onVerifyProgress(QVariant now, QVariant total);
    void onPreparationStatusUpdate(QVariant msg);

signals:

};

#endif // CLI_H
