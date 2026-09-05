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
    int run();

    // What the source argument turns out to be. Split out of run() so the
    // refusals can be checked without a QCoreApplication, elevated
    // privileges, or a device to write to -- these are the errors a script
    // hits, and the message is all it gets back.
    enum class SourceKind {
        Remote,        // http: or https:, fetched
        LocalFile,     // a regular file on disk
        Missing,       // nothing at that path
        NotRegular,    // a directory, a socket, something unusable
    };
    static SourceKind classifySource(const QString &src);

    // Empty when the key is usable, otherwise the reason it is not.
    static QString validateSecureBootKey(const QString &path);

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
