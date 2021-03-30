/****************************************************************************
** Meta object code from reading C++ file 'imagewriter.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.8)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "imagewriter.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'imagewriter.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_ImageWriter_t {
    QByteArrayData data[86];
    char stringdata0[1094];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_ImageWriter_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_ImageWriter_t qt_meta_stringdata_ImageWriter = {
    {
QT_MOC_LITERAL(0, 0, 11), // "ImageWriter"
QT_MOC_LITERAL(1, 12, 16), // "downloadProgress"
QT_MOC_LITERAL(2, 29, 0), // ""
QT_MOC_LITERAL(3, 30, 5), // "dlnow"
QT_MOC_LITERAL(4, 36, 7), // "dltotal"
QT_MOC_LITERAL(5, 44, 14), // "verifyProgress"
QT_MOC_LITERAL(6, 59, 3), // "now"
QT_MOC_LITERAL(7, 63, 5), // "total"
QT_MOC_LITERAL(8, 69, 5), // "error"
QT_MOC_LITERAL(9, 75, 3), // "msg"
QT_MOC_LITERAL(10, 79, 7), // "success"
QT_MOC_LITERAL(11, 87, 12), // "fileSelected"
QT_MOC_LITERAL(12, 100, 8), // "filename"
QT_MOC_LITERAL(13, 109, 9), // "cancelled"
QT_MOC_LITERAL(14, 119, 10), // "finalizing"
QT_MOC_LITERAL(15, 130, 13), // "networkOnline"
QT_MOC_LITERAL(16, 144, 23), // "preparationStatusUpdate"
QT_MOC_LITERAL(17, 168, 20), // "startProgressPolling"
QT_MOC_LITERAL(18, 189, 19), // "stopProgressPolling"
QT_MOC_LITERAL(19, 209, 12), // "pollProgress"
QT_MOC_LITERAL(20, 222, 11), // "pollNetwork"
QT_MOC_LITERAL(21, 234, 8), // "syncTime"
QT_MOC_LITERAL(22, 243, 9), // "onSuccess"
QT_MOC_LITERAL(23, 253, 7), // "onError"
QT_MOC_LITERAL(24, 261, 14), // "onFileSelected"
QT_MOC_LITERAL(25, 276, 11), // "onCancelled"
QT_MOC_LITERAL(26, 288, 18), // "onCacheFileUpdated"
QT_MOC_LITERAL(27, 307, 6), // "sha256"
QT_MOC_LITERAL(28, 314, 12), // "onFinalizing"
QT_MOC_LITERAL(29, 327, 15), // "onTimeSyncReply"
QT_MOC_LITERAL(30, 343, 14), // "QNetworkReply*"
QT_MOC_LITERAL(31, 358, 5), // "reply"
QT_MOC_LITERAL(32, 364, 25), // "onPreparationStatusUpdate"
QT_MOC_LITERAL(33, 390, 6), // "setSrc"
QT_MOC_LITERAL(34, 397, 3), // "url"
QT_MOC_LITERAL(35, 401, 11), // "downloadLen"
QT_MOC_LITERAL(36, 413, 7), // "extrLen"
QT_MOC_LITERAL(37, 421, 12), // "expectedHash"
QT_MOC_LITERAL(38, 434, 15), // "multifilesinzip"
QT_MOC_LITERAL(39, 450, 14), // "parentcategory"
QT_MOC_LITERAL(40, 465, 6), // "osname"
QT_MOC_LITERAL(41, 472, 6), // "setDst"
QT_MOC_LITERAL(42, 479, 6), // "device"
QT_MOC_LITERAL(43, 486, 10), // "deviceSize"
QT_MOC_LITERAL(44, 497, 16), // "setVerifyEnabled"
QT_MOC_LITERAL(45, 514, 6), // "verify"
QT_MOC_LITERAL(46, 521, 12), // "readyToWrite"
QT_MOC_LITERAL(47, 534, 10), // "startWrite"
QT_MOC_LITERAL(48, 545, 11), // "cancelWrite"
QT_MOC_LITERAL(49, 557, 8), // "isCached"
QT_MOC_LITERAL(50, 566, 21), // "startDriveListPolling"
QT_MOC_LITERAL(51, 588, 20), // "stopDriveListPolling"
QT_MOC_LITERAL(52, 609, 15), // "fileNameFromUrl"
QT_MOC_LITERAL(53, 625, 17), // "constantOsListUrl"
QT_MOC_LITERAL(54, 643, 15), // "constantVersion"
QT_MOC_LITERAL(55, 659, 14), // "isVersionNewer"
QT_MOC_LITERAL(56, 674, 7), // "version"
QT_MOC_LITERAL(57, 682, 18), // "setCustomOsListUrl"
QT_MOC_LITERAL(58, 701, 14), // "openFileDialog"
QT_MOC_LITERAL(59, 716, 11), // "srcFileName"
QT_MOC_LITERAL(60, 728, 8), // "isOnline"
QT_MOC_LITERAL(61, 737, 14), // "isEmbeddedMode"
QT_MOC_LITERAL(62, 752, 19), // "mountUsbSourceMedia"
QT_MOC_LITERAL(63, 772, 18), // "getUsbSourceOSlist"
QT_MOC_LITERAL(64, 791, 16), // "getDefaultPubKey"
QT_MOC_LITERAL(65, 808, 11), // "getTimezone"
QT_MOC_LITERAL(66, 820, 15), // "getTimezoneList"
QT_MOC_LITERAL(67, 836, 14), // "getCountryList"
QT_MOC_LITERAL(68, 851, 7), // "getSSID"
QT_MOC_LITERAL(69, 859, 6), // "getPSK"
QT_MOC_LITERAL(70, 866, 4), // "ssid"
QT_MOC_LITERAL(71, 871, 14), // "getBoolSetting"
QT_MOC_LITERAL(72, 886, 3), // "key"
QT_MOC_LITERAL(73, 890, 10), // "setSetting"
QT_MOC_LITERAL(74, 901, 5), // "value"
QT_MOC_LITERAL(75, 907, 21), // "setImageCustomization"
QT_MOC_LITERAL(76, 929, 6), // "config"
QT_MOC_LITERAL(77, 936, 7), // "cmdline"
QT_MOC_LITERAL(78, 944, 8), // "firstrun"
QT_MOC_LITERAL(79, 953, 29), // "setSavedCustomizationSettings"
QT_MOC_LITERAL(80, 983, 3), // "map"
QT_MOC_LITERAL(81, 987, 29), // "getSavedCustomizationSettings"
QT_MOC_LITERAL(82, 1017, 31), // "clearSavedCustomizationSettings"
QT_MOC_LITERAL(83, 1049, 29), // "hasSavedCustomizationSettings"
QT_MOC_LITERAL(84, 1079, 5), // "crypt"
QT_MOC_LITERAL(85, 1085, 8) // "password"

    },
    "ImageWriter\0downloadProgress\0\0dlnow\0"
    "dltotal\0verifyProgress\0now\0total\0error\0"
    "msg\0success\0fileSelected\0filename\0"
    "cancelled\0finalizing\0networkOnline\0"
    "preparationStatusUpdate\0startProgressPolling\0"
    "stopProgressPolling\0pollProgress\0"
    "pollNetwork\0syncTime\0onSuccess\0onError\0"
    "onFileSelected\0onCancelled\0"
    "onCacheFileUpdated\0sha256\0onFinalizing\0"
    "onTimeSyncReply\0QNetworkReply*\0reply\0"
    "onPreparationStatusUpdate\0setSrc\0url\0"
    "downloadLen\0extrLen\0expectedHash\0"
    "multifilesinzip\0parentcategory\0osname\0"
    "setDst\0device\0deviceSize\0setVerifyEnabled\0"
    "verify\0readyToWrite\0startWrite\0"
    "cancelWrite\0isCached\0startDriveListPolling\0"
    "stopDriveListPolling\0fileNameFromUrl\0"
    "constantOsListUrl\0constantVersion\0"
    "isVersionNewer\0version\0setCustomOsListUrl\0"
    "openFileDialog\0srcFileName\0isOnline\0"
    "isEmbeddedMode\0mountUsbSourceMedia\0"
    "getUsbSourceOSlist\0getDefaultPubKey\0"
    "getTimezone\0getTimezoneList\0getCountryList\0"
    "getSSID\0getPSK\0ssid\0getBoolSetting\0"
    "key\0setSetting\0value\0setImageCustomization\0"
    "config\0cmdline\0firstrun\0"
    "setSavedCustomizationSettings\0map\0"
    "getSavedCustomizationSettings\0"
    "clearSavedCustomizationSettings\0"
    "hasSavedCustomizationSettings\0crypt\0"
    "password"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_ImageWriter[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      63,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       9,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    2,  329,    2, 0x06 /* Public */,
       5,    2,  334,    2, 0x06 /* Public */,
       8,    1,  339,    2, 0x06 /* Public */,
      10,    0,  342,    2, 0x06 /* Public */,
      11,    1,  343,    2, 0x06 /* Public */,
      13,    0,  346,    2, 0x06 /* Public */,
      14,    0,  347,    2, 0x06 /* Public */,
      15,    0,  348,    2, 0x06 /* Public */,
      16,    1,  349,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      17,    0,  352,    2, 0x09 /* Protected */,
      18,    0,  353,    2, 0x09 /* Protected */,
      19,    0,  354,    2, 0x09 /* Protected */,
      20,    0,  355,    2, 0x09 /* Protected */,
      21,    0,  356,    2, 0x09 /* Protected */,
      22,    0,  357,    2, 0x09 /* Protected */,
      23,    1,  358,    2, 0x09 /* Protected */,
      24,    1,  361,    2, 0x09 /* Protected */,
      25,    0,  364,    2, 0x09 /* Protected */,
      26,    1,  365,    2, 0x09 /* Protected */,
      28,    0,  368,    2, 0x09 /* Protected */,
      29,    1,  369,    2, 0x09 /* Protected */,
      32,    1,  372,    2, 0x09 /* Protected */,

 // methods: name, argc, parameters, tag, flags
      33,    7,  375,    2, 0x02 /* Public */,
      33,    6,  390,    2, 0x22 /* Public | MethodCloned */,
      33,    5,  403,    2, 0x22 /* Public | MethodCloned */,
      33,    4,  414,    2, 0x22 /* Public | MethodCloned */,
      33,    3,  423,    2, 0x22 /* Public | MethodCloned */,
      33,    2,  430,    2, 0x22 /* Public | MethodCloned */,
      33,    1,  435,    2, 0x22 /* Public | MethodCloned */,
      41,    2,  438,    2, 0x02 /* Public */,
      41,    1,  443,    2, 0x22 /* Public | MethodCloned */,
      44,    1,  446,    2, 0x02 /* Public */,
      46,    0,  449,    2, 0x02 /* Public */,
      47,    0,  450,    2, 0x02 /* Public */,
      48,    0,  451,    2, 0x02 /* Public */,
      49,    2,  452,    2, 0x02 /* Public */,
      50,    0,  457,    2, 0x02 /* Public */,
      51,    0,  458,    2, 0x02 /* Public */,
      52,    1,  459,    2, 0x02 /* Public */,
      53,    0,  462,    2, 0x02 /* Public */,
      54,    0,  463,    2, 0x02 /* Public */,
      55,    1,  464,    2, 0x02 /* Public */,
      57,    1,  467,    2, 0x02 /* Public */,
      58,    0,  470,    2, 0x02 /* Public */,
      59,    0,  471,    2, 0x02 /* Public */,
      60,    0,  472,    2, 0x02 /* Public */,
      61,    0,  473,    2, 0x02 /* Public */,
      62,    0,  474,    2, 0x02 /* Public */,
      63,    0,  475,    2, 0x02 /* Public */,
      64,    0,  476,    2, 0x02 /* Public */,
      65,    0,  477,    2, 0x02 /* Public */,
      66,    0,  478,    2, 0x02 /* Public */,
      67,    0,  479,    2, 0x02 /* Public */,
      68,    0,  480,    2, 0x02 /* Public */,
      69,    1,  481,    2, 0x02 /* Public */,
      71,    1,  484,    2, 0x02 /* Public */,
      73,    2,  487,    2, 0x02 /* Public */,
      75,    3,  492,    2, 0x02 /* Public */,
      79,    1,  499,    2, 0x02 /* Public */,
      81,    0,  502,    2, 0x02 /* Public */,
      82,    0,  503,    2, 0x02 /* Public */,
      83,    0,  504,    2, 0x02 /* Public */,
      84,    1,  505,    2, 0x02 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QVariant, QMetaType::QVariant,    3,    4,
    QMetaType::Void, QMetaType::QVariant, QMetaType::QVariant,    6,    7,
    QMetaType::Void, QMetaType::QVariant,    9,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QVariant,   12,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QVariant,    9,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    9,
    QMetaType::Void, QMetaType::QString,   12,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QByteArray,   27,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 30,   31,
    QMetaType::Void, QMetaType::QString,    9,

 // methods: parameters
    QMetaType::Void, QMetaType::QUrl, QMetaType::ULongLong, QMetaType::ULongLong, QMetaType::QByteArray, QMetaType::Bool, QMetaType::QString, QMetaType::QString,   34,   35,   36,   37,   38,   39,   40,
    QMetaType::Void, QMetaType::QUrl, QMetaType::ULongLong, QMetaType::ULongLong, QMetaType::QByteArray, QMetaType::Bool, QMetaType::QString,   34,   35,   36,   37,   38,   39,
    QMetaType::Void, QMetaType::QUrl, QMetaType::ULongLong, QMetaType::ULongLong, QMetaType::QByteArray, QMetaType::Bool,   34,   35,   36,   37,   38,
    QMetaType::Void, QMetaType::QUrl, QMetaType::ULongLong, QMetaType::ULongLong, QMetaType::QByteArray,   34,   35,   36,   37,
    QMetaType::Void, QMetaType::QUrl, QMetaType::ULongLong, QMetaType::ULongLong,   34,   35,   36,
    QMetaType::Void, QMetaType::QUrl, QMetaType::ULongLong,   34,   35,
    QMetaType::Void, QMetaType::QUrl,   34,
    QMetaType::Void, QMetaType::QString, QMetaType::ULongLong,   42,   43,
    QMetaType::Void, QMetaType::QString,   42,
    QMetaType::Void, QMetaType::Bool,   45,
    QMetaType::Bool,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Bool, QMetaType::QUrl, QMetaType::QByteArray,   34,   27,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::QString, QMetaType::QUrl,   34,
    QMetaType::QUrl,
    QMetaType::QString,
    QMetaType::Bool, QMetaType::QString,   56,
    QMetaType::Void, QMetaType::QUrl,   34,
    QMetaType::Void,
    QMetaType::QString,
    QMetaType::Bool,
    QMetaType::Bool,
    QMetaType::Bool,
    QMetaType::QByteArray,
    QMetaType::QString,
    QMetaType::QString,
    QMetaType::QStringList,
    QMetaType::QStringList,
    QMetaType::QString,
    QMetaType::QString, QMetaType::QString,   70,
    QMetaType::Bool, QMetaType::QString,   72,
    QMetaType::Void, QMetaType::QString, QMetaType::QVariant,   72,   74,
    QMetaType::Void, QMetaType::QByteArray, QMetaType::QByteArray, QMetaType::QByteArray,   76,   77,   78,
    QMetaType::Void, QMetaType::QVariantMap,   80,
    QMetaType::QVariantMap,
    QMetaType::Void,
    QMetaType::Bool,
    QMetaType::QString, QMetaType::QByteArray,   85,

       0        // eod
};

void ImageWriter::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ImageWriter *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->downloadProgress((*reinterpret_cast< QVariant(*)>(_a[1])),(*reinterpret_cast< QVariant(*)>(_a[2]))); break;
        case 1: _t->verifyProgress((*reinterpret_cast< QVariant(*)>(_a[1])),(*reinterpret_cast< QVariant(*)>(_a[2]))); break;
        case 2: _t->error((*reinterpret_cast< QVariant(*)>(_a[1]))); break;
        case 3: _t->success(); break;
        case 4: _t->fileSelected((*reinterpret_cast< QVariant(*)>(_a[1]))); break;
        case 5: _t->cancelled(); break;
        case 6: _t->finalizing(); break;
        case 7: _t->networkOnline(); break;
        case 8: _t->preparationStatusUpdate((*reinterpret_cast< QVariant(*)>(_a[1]))); break;
        case 9: _t->startProgressPolling(); break;
        case 10: _t->stopProgressPolling(); break;
        case 11: _t->pollProgress(); break;
        case 12: _t->pollNetwork(); break;
        case 13: _t->syncTime(); break;
        case 14: _t->onSuccess(); break;
        case 15: _t->onError((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 16: _t->onFileSelected((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 17: _t->onCancelled(); break;
        case 18: _t->onCacheFileUpdated((*reinterpret_cast< QByteArray(*)>(_a[1]))); break;
        case 19: _t->onFinalizing(); break;
        case 20: _t->onTimeSyncReply((*reinterpret_cast< QNetworkReply*(*)>(_a[1]))); break;
        case 21: _t->onPreparationStatusUpdate((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 22: _t->setSrc((*reinterpret_cast< const QUrl(*)>(_a[1])),(*reinterpret_cast< quint64(*)>(_a[2])),(*reinterpret_cast< quint64(*)>(_a[3])),(*reinterpret_cast< QByteArray(*)>(_a[4])),(*reinterpret_cast< bool(*)>(_a[5])),(*reinterpret_cast< QString(*)>(_a[6])),(*reinterpret_cast< QString(*)>(_a[7]))); break;
        case 23: _t->setSrc((*reinterpret_cast< const QUrl(*)>(_a[1])),(*reinterpret_cast< quint64(*)>(_a[2])),(*reinterpret_cast< quint64(*)>(_a[3])),(*reinterpret_cast< QByteArray(*)>(_a[4])),(*reinterpret_cast< bool(*)>(_a[5])),(*reinterpret_cast< QString(*)>(_a[6]))); break;
        case 24: _t->setSrc((*reinterpret_cast< const QUrl(*)>(_a[1])),(*reinterpret_cast< quint64(*)>(_a[2])),(*reinterpret_cast< quint64(*)>(_a[3])),(*reinterpret_cast< QByteArray(*)>(_a[4])),(*reinterpret_cast< bool(*)>(_a[5]))); break;
        case 25: _t->setSrc((*reinterpret_cast< const QUrl(*)>(_a[1])),(*reinterpret_cast< quint64(*)>(_a[2])),(*reinterpret_cast< quint64(*)>(_a[3])),(*reinterpret_cast< QByteArray(*)>(_a[4]))); break;
        case 26: _t->setSrc((*reinterpret_cast< const QUrl(*)>(_a[1])),(*reinterpret_cast< quint64(*)>(_a[2])),(*reinterpret_cast< quint64(*)>(_a[3]))); break;
        case 27: _t->setSrc((*reinterpret_cast< const QUrl(*)>(_a[1])),(*reinterpret_cast< quint64(*)>(_a[2]))); break;
        case 28: _t->setSrc((*reinterpret_cast< const QUrl(*)>(_a[1]))); break;
        case 29: _t->setDst((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< quint64(*)>(_a[2]))); break;
        case 30: _t->setDst((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 31: _t->setVerifyEnabled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 32: { bool _r = _t->readyToWrite();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 33: _t->startWrite(); break;
        case 34: _t->cancelWrite(); break;
        case 35: { bool _r = _t->isCached((*reinterpret_cast< const QUrl(*)>(_a[1])),(*reinterpret_cast< const QByteArray(*)>(_a[2])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 36: _t->startDriveListPolling(); break;
        case 37: _t->stopDriveListPolling(); break;
        case 38: { QString _r = _t->fileNameFromUrl((*reinterpret_cast< const QUrl(*)>(_a[1])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 39: { QUrl _r = _t->constantOsListUrl();
            if (_a[0]) *reinterpret_cast< QUrl*>(_a[0]) = std::move(_r); }  break;
        case 40: { QString _r = _t->constantVersion();
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 41: { bool _r = _t->isVersionNewer((*reinterpret_cast< const QString(*)>(_a[1])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 42: _t->setCustomOsListUrl((*reinterpret_cast< const QUrl(*)>(_a[1]))); break;
        case 43: _t->openFileDialog(); break;
        case 44: { QString _r = _t->srcFileName();
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 45: { bool _r = _t->isOnline();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 46: { bool _r = _t->isEmbeddedMode();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 47: { bool _r = _t->mountUsbSourceMedia();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 48: { QByteArray _r = _t->getUsbSourceOSlist();
            if (_a[0]) *reinterpret_cast< QByteArray*>(_a[0]) = std::move(_r); }  break;
        case 49: { QString _r = _t->getDefaultPubKey();
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 50: { QString _r = _t->getTimezone();
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 51: { QStringList _r = _t->getTimezoneList();
            if (_a[0]) *reinterpret_cast< QStringList*>(_a[0]) = std::move(_r); }  break;
        case 52: { QStringList _r = _t->getCountryList();
            if (_a[0]) *reinterpret_cast< QStringList*>(_a[0]) = std::move(_r); }  break;
        case 53: { QString _r = _t->getSSID();
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 54: { QString _r = _t->getPSK((*reinterpret_cast< const QString(*)>(_a[1])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 55: { bool _r = _t->getBoolSetting((*reinterpret_cast< const QString(*)>(_a[1])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 56: _t->setSetting((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QVariant(*)>(_a[2]))); break;
        case 57: _t->setImageCustomization((*reinterpret_cast< const QByteArray(*)>(_a[1])),(*reinterpret_cast< const QByteArray(*)>(_a[2])),(*reinterpret_cast< const QByteArray(*)>(_a[3]))); break;
        case 58: _t->setSavedCustomizationSettings((*reinterpret_cast< const QVariantMap(*)>(_a[1]))); break;
        case 59: { QVariantMap _r = _t->getSavedCustomizationSettings();
            if (_a[0]) *reinterpret_cast< QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 60: _t->clearSavedCustomizationSettings(); break;
        case 61: { bool _r = _t->hasSavedCustomizationSettings();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 62: { QString _r = _t->crypt((*reinterpret_cast< const QByteArray(*)>(_a[1])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ImageWriter::*)(QVariant , QVariant );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ImageWriter::downloadProgress)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ImageWriter::*)(QVariant , QVariant );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ImageWriter::verifyProgress)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (ImageWriter::*)(QVariant );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ImageWriter::error)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (ImageWriter::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ImageWriter::success)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (ImageWriter::*)(QVariant );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ImageWriter::fileSelected)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (ImageWriter::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ImageWriter::cancelled)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (ImageWriter::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ImageWriter::finalizing)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (ImageWriter::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ImageWriter::networkOnline)) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (ImageWriter::*)(QVariant );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ImageWriter::preparationStatusUpdate)) {
                *result = 8;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject ImageWriter::staticMetaObject = { {
    &QObject::staticMetaObject,
    qt_meta_stringdata_ImageWriter.data,
    qt_meta_data_ImageWriter,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *ImageWriter::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ImageWriter::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ImageWriter.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int ImageWriter::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 63)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 63;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 63)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 63;
    }
    return _id;
}

// SIGNAL 0
void ImageWriter::downloadProgress(QVariant _t1, QVariant _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ImageWriter::verifyProgress(QVariant _t1, QVariant _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void ImageWriter::error(QVariant _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void ImageWriter::success()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void ImageWriter::fileSelected(QVariant _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void ImageWriter::cancelled()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void ImageWriter::finalizing()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void ImageWriter::networkOnline()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void ImageWriter::preparationStatusUpdate(QVariant _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
