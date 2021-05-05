#ifndef CLI_H
#define CLI_H

#include <QObject>
#include <QVariant>

class ImageWriter;
class QCoreApplication;

class Cli : public QObject
{
    Q_OBJECT
public:
    explicit Cli(int &argc, char *argv[]);
    virtual ~Cli();
    int main();

protected:
    QCoreApplication *_app;
    ImageWriter *_imageWriter;
    int _lastPercent;
    QByteArray _lastMsg;
    bool _quiet;

    void _printProgress(const QByteArray &msg, QVariant now, QVariant total);
    void _clearLine();

protected slots:
    void onSuccess();
    void onError(QVariant msg);
    void onDownloadProgress(QVariant dlnow, QVariant dltotal);
    void onVerifyProgress(QVariant now, QVariant total);
    void onPreparationStatusUpdate(QVariant msg);

signals:

};

#endif // CLI_H
