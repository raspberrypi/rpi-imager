/****************************************************************************
** Meta object code from reading C++ file 'downloadthread.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.8)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "downloadthread.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'downloadthread.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_DownloadThread_t {
    QByteArrayData data[9];
    char stringdata0[93];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_DownloadThread_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_DownloadThread_t qt_meta_stringdata_DownloadThread = {
    {
QT_MOC_LITERAL(0, 0, 14), // "DownloadThread"
QT_MOC_LITERAL(1, 15, 7), // "success"
QT_MOC_LITERAL(2, 23, 0), // ""
QT_MOC_LITERAL(3, 24, 5), // "error"
QT_MOC_LITERAL(4, 30, 3), // "msg"
QT_MOC_LITERAL(5, 34, 16), // "cacheFileUpdated"
QT_MOC_LITERAL(6, 51, 6), // "sha256"
QT_MOC_LITERAL(7, 58, 10), // "finalizing"
QT_MOC_LITERAL(8, 69, 23) // "preparationStatusUpdate"

    },
    "DownloadThread\0success\0\0error\0msg\0"
    "cacheFileUpdated\0sha256\0finalizing\0"
    "preparationStatusUpdate"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_DownloadThread[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   39,    2, 0x06 /* Public */,
       3,    1,   40,    2, 0x06 /* Public */,
       5,    1,   43,    2, 0x06 /* Public */,
       7,    0,   46,    2, 0x06 /* Public */,
       8,    1,   47,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    4,
    QMetaType::Void, QMetaType::QByteArray,    6,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    4,

       0        // eod
};

void DownloadThread::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DownloadThread *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->success(); break;
        case 1: _t->error((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 2: _t->cacheFileUpdated((*reinterpret_cast< QByteArray(*)>(_a[1]))); break;
        case 3: _t->finalizing(); break;
        case 4: _t->preparationStatusUpdate((*reinterpret_cast< QString(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (DownloadThread::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DownloadThread::success)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (DownloadThread::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DownloadThread::error)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (DownloadThread::*)(QByteArray );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DownloadThread::cacheFileUpdated)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (DownloadThread::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DownloadThread::finalizing)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (DownloadThread::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DownloadThread::preparationStatusUpdate)) {
                *result = 4;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject DownloadThread::staticMetaObject = { {
    &QThread::staticMetaObject,
    qt_meta_stringdata_DownloadThread.data,
    qt_meta_data_DownloadThread,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *DownloadThread::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DownloadThread::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_DownloadThread.stringdata0))
        return static_cast<void*>(this);
    return QThread::qt_metacast(_clname);
}

int DownloadThread::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QThread::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void DownloadThread::success()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void DownloadThread::error(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void DownloadThread::cacheFileUpdated(QByteArray _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void DownloadThread::finalizing()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void DownloadThread::preparationStatusUpdate(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
