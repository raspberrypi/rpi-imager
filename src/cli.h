#ifndef CLI_H
#define CLI_H

#include <QObject>
#include <QStringList>
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

    // Whether the destination is one of the removable volumes the system
    // reports. Unless --enable-writing-system-drives is given, a destination
    // that is not on that list is refused -- this is the CLI's equivalent of
    // the storage picker greying out the machine's own disk, and the only
    // thing standing between a mistyped script and somebody's root volume.
    static bool destinationIsRemovable(class DriveListModel &drives,
                                       const QString &destination);

    // "device (description)" for each removable volume, which is what the
    // refusal above prints so the operator can see what they could have
    // written instead.
    static QStringList removableDestinations(class DriveListModel &drives);

    // Empty when the pair is usable, otherwise the reason it is not.
    //
    // --cache-file without --sha256 is the case that matters, and the option's
    // own help text has always said so. Unenforced, both hashes are empty, the
    // cache lookup compares them and matches, and the file named by
    // --cache-file is written in place of the image the script asked for --
    // unverified, with nothing on the console to say it happened.
    static QString validateCacheOptions(const QString &cacheFile, const QString &sha256);

    // Read a customisation file named on the command line. `what` is how the
    // file is described back to the operator. Returns false with `error`
    // filled when the file cannot be used -- unreadable is told apart from
    // absent, because a permissions problem and a typo need different fixes
    // and there is no dialog here to work it out from.
    static bool readCustomisationFile(const QString &path, const QString &what,
                                      QByteArray &contents, QString &error);

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
